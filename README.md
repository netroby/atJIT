atJIT: A just-in-time autotuning compiler for C++
==========================================

[![Build Status](https://travis-ci.org/kavon/atJIT.svg?branch=master)](https://travis-ci.org/kavon/atJIT)

About
-----

atJIT is an early-phase experiment in online autotuning.

The code was originally based on the Easy::jit project.

Prerequisites
--------

Before you can build atJIT, ensure that your system has these essentials:

- a C++ compiler with sufficient [C++17 support](https://en.cppreference.com/w/cpp/compiler_support#C.2B.2B17_features). This likely means GCC >= 7, or Clang >= 4, but slightly older versions may work.
- cmake >= 3.5, and make
- The test suite (the `check` build target) requires the following:
  - Python 2.7
  - The Python [lit package](https://pypi.org/project/lit/), installable with `pip install lit`
  - [Valgrind](http://valgrind.org/), installable with `sudo apt install valgrind` on Ubuntu.

Then, do the following:

### Step 1

Install a compatible version of [clang](http://clang.llvm.org/) and [LLVM](http://llvm.org/).
You have two options for this:

#### Option 1 — Polly Knobs *(recommended)*

In order for the tuner to make use of powerful loop transformations via [Polly](https://polly.llvm.org/), you'll need to download and build an out-of-tree version of LLVM + Clang + Polly.
We have automated this process with a script, which you can use in the following way:

```bash
mkdir llvm
./get-llvm-with-polly.sh ./llvm
```

The location of this custom-built LLVM will be `./llvm/install`



#### Option 2 — Vanilla

You can also use plain-old LLVM + Clang version 6. To do this on Ubuntu 18.04,
you can install using APT:

```bash
sudo apt update
sudo apt install llvm-6.0-dev llvm-6.0-tools clang-6.0
```

For versions of Debian or Ubuntu that do not have version 6 available in the
default APT repositories, you can first add the appropriate APT repository
[from this list](http://apt.llvm.org/).

In order to use a LLVM + Clang 6 that was built from source, you will need to
configure the build of LLVM with special CMake options (e.g., we require RTTI).
We have collected these options in the `./cmake/LLVM.cmake` file, which can
be added to your usual
invocation of CMake when building LLVM with the `-C` flag like so:

```bash
cmake -C <path-to-atJit-root>/cmake/LLVM.cmake  .. other arguments ..
```

### Step 2
Install [Grand Central Dispatch](https://apple.github.io/swift-corelibs-libdispatch/), which on
Ubuntu amounts to running:

```bash
sudo apt install libdispatch0 libdispatch-dev
```

### Step 3

Obtain and build [XGBoost](https://xgboost.ai/) by running the following command:

```
./xgboost/get.sh
```

Building atJIT
--------

Once you have met the prerequisites, we can build atJIT.
Starting from the root of the project, the general build steps are:

```bash
mkdir build install
cd build
cmake -DCMAKE_INSTALL_PREFIX=../install -DPOLLY_KNOBS=<ON/OFF>..
cmake --build . --target install
```

Where the argument for `POLLY_KNOBS` is set to `OFF` if and only if you are
using the Vanilla, non-Polly LLVM.

By default, `POLLY_KNOBS` is set to `ON`. If you followed the **Polly Knobs** instructions above, jump to the Build Options section below for more information.

Once this completes, you can jump to the usage section. For special builds of
atJIT, see below.



#### Build Options

If you are using a custom-built LLVM that is not installed system-wide, or followed the **Polly Knobs** instructions, you'll need to add `-DCMAKE_PREFIX_PATH=<path-to-where-LLVM-was-installed>` to the first CMake command above.

For example, for **Polly Knobs**, use `-DCMAKE_PREFIX_PATH=../llvm/install`.

To build the examples, install the [opencv](https://opencv.org/) library,
and add the flags ```-DEASY_JIT_EXAMPLE=1``` to the cmake command.

To enable benchmarking, install the [google benchmark](https://github.com/google/benchmark) framework,
and add the flags ```-DEASY_JIT_BENCHMARK=1 -DBENCHMARK_DIR=<path_to_google_benchmark_install>``` to the cmake command.

#### Regression Testing

The test suite (`check` target) can be run after the `install` target has been built:

```bash
cmake --build . --target install
cmake --build . --target check
```

None of the tests should have an unexpected failure/success.

<!--
### Docker

If you want to give only a quick test to the project, everything is provided to use it with docker.
To do this, generate a Dockerfile from the current directory using the scripts in ```<path_to_easy_jit_src>/misc/docker```,
then generate your docker instance.

```bash
python3 <path_to_easy_jit_src>/misc/docker/GenDockerfile.py  <path_to_easy_jit_src>/.travis.yml > Dockerfile
docker build -t easy/test -f Dockerfile
docker run -ti easy/test /bin/bash
```
-->

Basic usage
-----------

Look in your install directory for the `bin/atjitc` executable, which is a
thin wrapper around `clang++` with the correct arguments to run the
clang plugin and dynamically link in the atJIT runtime system.
You can use `atjitc` as if it were `clang++`, as it forwards its arguments to `clang++`.
Here's an example:

```bash
➤ install/bin/atjitc -Wall -O2 tests/simple/int_a.cpp -o int_a
➤ ./int_a
inc(4) is 5
inc(5) is 6
inc(6) is 7
inc(7) is 8
```

### Using atJIT in my project

The C++ library interface to atJIT is quite minimal.
To get started, construct the driver for the autotuner:

```c++
#include <tuner/driver.h>

... {
  tuner::ATDriver AT;
  // ...
}
```

A single driver can handle the tuning of multiple functions, each with their own unique partial argument applications.
The driver only exposes one, do-it-all, variadic method `reoptimize`.
Given a tuner `AT`, `reoptimize` has the following generic usage:

```c++
  /* (1) return type */ F = AT.reoptimize(
                           /* (2) function to reoptimize */
                           /* (3) arguments to the function */
                           /* (4) options for the tuner */
                          );
```

1. The return type of the function is some variant of `easy::FunctionWrapper<> const&`, which is a C++ function object that can be called like an ordinary function. The type depends on (2) and (3), and you can typically just write `auto const&` in its place.

2. The function to be optimized, which can be a template function if the type is specified.

3. A list of arguments that must match the arity of the original function. The following types of values are interpreted as arguments:

  - **A placeholder** (i.e., from `std::placeholders`) representing a standard, unfilled function parameter.
  - **A runtime value**. Providing a runtime value will allow the JIT compiler to specialize based on the actual, possibly dynamic, runtime value given to `reoptimize`.
  - **A tuned parameter**. This is a special value that represents constraints on the allowed arguments to the function, and leaves it up to the tuner to fill in an "optimal" value as a constant before JIT compilation. This can be used for algorithmic selection, among other things.

Here's an example:

```c++
using namespace std::placeholders;
using namespace tuned_param;

float fsub(float a, float b) { return a-b; }
void wait(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
int main () {
  tuner::ATDriver AT;
  // returns a function computing fsub(a, 1.0)
  easy::FunctionWrapper<float(float)> const& decrement = AT.reoptimize(fsub, _1, 1.0);

  // returns a function computing fsub(0.0, b)
  auto const& negate = AT.reoptimize(fsub, 0.0, _1);

  // returns a function with a fixed `wait` period in the range [1, 500]
  auto const& pause = AT.reoptimize(wait, IntRange(1, 500));

  printf("dec(5) == %f\n", decrement(5));
  printf("neg(3) == %f\n", negate(3));
  pause();
  // ...
```

4. The main option for the tuner is what algorithm to use during the search. If no option is specified, the tuner currently will not perform any search.
To use the random search, we would specify `tuner::AT_Random` like so:

```c++
using namespace easy::options;

// returns a function equivalent to fsub(a, b)
auto const& fsubJIT = AT.reoptimize(fsub, _1, _2,
                           tuner_kind(tuner::AT_Random));

printf("fsubJIT(3, 2) == %f\n", fsubJIT(3.0, 2.0));
```

The current list of tuning options (namespaces omitted) are:

- `tuner_kind(x)` — where `x` is one of `AT_None`, `AT_Random`, `AT_Bayes`, `AT_Anneal`.
- `pct_err(x)` — where `x` is a double representing the precentage of tolerated time-measurement error during tuning. If `x < 0` then the first measurement is always accepted. The default is currently `1.0`.

#### Autotuning a Function

To actually *drive* the online autotuning process for some function F, you must repeatedly `reoptimize` F and call the newly returned version F' at least once. Ideally, you would ask the tuner for a reoptimized version of F before every call. For example:

```c++
for (int i = 0; i < 100; ++i) {
    auto const& tunedSub7 =
        AT.reoptimize(fsub, _1, 7.0, tuner_kind(tuner::AT_Random));

    printf("8 - 7 == %f\n", tunedSub7(8));
  }
```

Don't worry about calling `reoptimize` too often. Sometimes the tuner will JIT compile a new version, but often it will return
a ready-to-go version that needs more runtime measurements to determine its quality.

See `doc/readme/simple_at.cpp` for the complete example we have walked through in this section.


<!--

Consider the code below from a software that applies image filters on a video stream.
In the following sections we are going to adapt it to use the atJIT library.
The function to optimize is ```kernel```, which applies a mask on the entire image.

The mask, its dimensions and area do not change often, so specializing the function for
these parameters seems reasonable.
Moreover, the image dimensions and number of channels typically remain constant during
the entire execution; however, it is impossible to know their values as they depend on the stream.

```cpp
static void kernel(const char* mask, unsigned mask_size, unsigned mask_area,
                   const unsigned char* in, unsigned char* out,
                   unsigned rows, unsigned cols, unsigned channels) {
  unsigned mask_middle = (mask_size/2+1);
  unsigned middle = (cols+1)*mask_middle;

  for(unsigned i = 0; i != rows-mask_size; ++i) {
    for(unsigned j = 0; j != cols-mask_size; ++j) {
      for(unsigned ch = 0; ch != channels; ++ch) {

        long out_val = 0;
        for(unsigned ii = 0; ii != mask_size; ++ii) {
          for(unsigned jj = 0; jj != mask_size; ++jj) {
            out_val += mask[ii*mask_size+jj] * in[((i+ii)*cols+j+jj)*channels+ch];
          }
        }
        out[(i*cols+j+middle)*channels+ch] = out_val / mask_area;
      }
    }
  }
}

static void apply_filter(const char *mask, unsigned mask_size, unsigned mask_area, cv::Mat &image, cv::Mat *&out) {
  kernel(mask, mask_size, mask_area, image.ptr(0,0), out->ptr(0,0), image.rows, image.cols, image.channels());
}
```

The main header for the library is ```easy/jit.h```, where the only core function
of the library is exported. This function is called -- guess how? -- ```easy::jit```.
We add the corresponding include directive them in the top of the file.

```cpp
#include <easy/jit.h>
```

With the call to ```easy::jit```, we specialize the function and obtain a new
one taking only two parameters (the input and the output frame).

```cpp
static void apply_filter(const char *mask, unsigned mask_size, unsigned mask_area, cv::Mat &image, cv::Mat *&out) {
  using namespace std::placeholders;

  auto kernel_opt = easy::jit(kernel, mask, mask_size, mask_area, _1, _2, image.rows, image.cols, image.channels());
  kernel_opt(image.ptr(0,0), out->ptr(0,0));
}
```

#### Deducing which functions to expose at runtime

atJIT embeds the [LLVM bitcode](https://llvm.org/docs/LangRef.html)
representation of the functions to specialize at runtime in the binary code.
To perform this, the library requires access to the implementation of these
functions.
atJIT does an effort to deduce which functions are specialized at runtime,
still in many cases this is not possible.

In this case, it's possible to use the ```EASY_JIT_EXPOSE``` macro, as shown in
the following code,

```cpp
void EASY_JIT_EXPOSE kernel() { /* ... */ }
```

or using a regular expression during compilation.
The command bellow exports all functions whose name starts with "^kernel".

```bash
clang++ ... -mllvm -easy-export="^kernel.*"  ...
```

#### Caching

In parallel to the ```easy/jit.h``` header, there is ```easy/code_cache.h``` which
provides a code cache to avoid recompilation of functions that already have been
generated.

Bellow we show the code from previous section, but adapted to use a code cache.

```cpp
#include <easy/code_cache.h>
```

```cpp
static void apply_filter(const char *mask, unsigned mask_size, unsigned mask_area, cv::Mat &image, cv::Mat *&out) {
  using namespace std::placeholders;

  static easy::Cache<> cache;
  auto const &kernel_opt = cache.jit(kernel, mask, mask_size, mask_area, _1, _2, image.rows, image.cols, image.channels());
  kernel_opt(image.ptr(0,0), out->ptr(0,0));
}
```

-->

License
-------

See file `LICENSE` at the top-level directory of this project.

Acknowledgements
------

Special thanks to:

* Hal Finkel & Michael Kruse (Argonne National Laboratory)
* John Reppy (University of Chicago)
* Serge Guelton & Juan Manuel Martinez Caamaño (originally developed [Easy::jit](https://github.com/jmmartinez/easy-just-in-time))
