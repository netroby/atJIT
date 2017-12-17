#include <easy/runtime/Function.h>
#include <easy/runtime/RuntimePasses.h>
#include <easy/exceptions.h>

#include <llvm/Transforms/IPO/PassManagerBuilder.h> 
#include <llvm/IR/LegacyPassManager.h> 
#include <llvm/Support/Host.h> 
#include <llvm/Target/TargetMachine.h> 
#include <llvm/Support/TargetRegistry.h> 
#include <llvm/Analysis/TargetTransformInfo.h> 
#include <llvm/Analysis/TargetLibraryInfo.h> 
#include <llvm/Support/DynamicLibrary.h>

using namespace easy;

namespace easy {
  DefineEasyException(ExecutionEngineCreateError, "Failed to create execution engine for:");
}

std::unique_ptr<llvm::TargetMachine> Function::GetTargetMachine(llvm::StringRef Triple) {
  std::string TgtErr;
  llvm::Target const *Tgt = llvm::TargetRegistry::lookupTarget(Triple, TgtErr);

  llvm::StringMap<bool> Features;
  (void)llvm::sys::getHostCPUFeatures(Features);

  std::string FeaturesStr;
  for (auto &&KV : Features) {
    if (KV.getValue()) {
      FeaturesStr += '+';
      FeaturesStr += KV.getKey();
      FeaturesStr += ',';
    }
  }

  return std::unique_ptr<llvm::TargetMachine>(
      Tgt->createTargetMachine(Triple, llvm::sys::getHostCPUName(), 
                               FeaturesStr, llvm::TargetOptions(), llvm::None));
}

void Function::Optimize(llvm::Module& M, const char* Name, const Context& C, unsigned OptLevel, unsigned OptSize) {

  auto Triple = llvm::sys::getProcessTriple();

  llvm::PassManagerBuilder Builder;
  Builder.OptLevel = OptLevel;
  Builder.SizeLevel = OptSize;
  Builder.LibraryInfo = new llvm::TargetLibraryInfoImpl(llvm::Triple{Triple});

  std::unique_ptr<llvm::TargetMachine> TM = GetTargetMachine(Triple);

  llvm::legacy::PassManager MPM;
  MPM.add(llvm::createTargetTransformInfoWrapperPass(TM->getTargetIRAnalysis()));
  MPM.add(easy::createContextAnalysisPass(C));
  MPM.add(easy::createInlineParametersPass(Name));
  Builder.populateModulePassManager(MPM);
  MPM.run(M);
}

std::unique_ptr<llvm::ExecutionEngine> Function::GetEngine(std::unique_ptr<llvm::Module> M, const char *Name) {
  llvm::EngineBuilder ebuilder(std::move(M));
  std::string eeError;

  std::unique_ptr<llvm::ExecutionEngine> EE(ebuilder.setErrorStr(&eeError)
          .setMCPU(llvm::sys::getHostCPUName())
          .setEngineKind(llvm::EngineKind::JIT)
          .setOptLevel(llvm::CodeGenOpt::Level::Aggressive)
          .create());

  if(!EE) {
    throw easy::ExecutionEngineCreateError(Name);
  }

  return EE;
}

void Function::MapGlobals(llvm::ExecutionEngine& EE, GlobalMapping* Globals) {
  for(GlobalMapping *GM = Globals; GM->Name; ++GM) {
    EE.addGlobalMapping(GM->Name, (uint64_t)GM->Address);
  }
}

std::unique_ptr<Function> Function::Compile(void *Addr, const Context& C) {

  auto &BT = BitcodeTracker::GetTracker();

  const char* Name;
  GlobalMapping* Globals;
  std::tie(Name, Globals) = BT.getNameAndGlobalMapping(Addr);
  auto Original = BT.getModule(Addr);

  std::unique_ptr<llvm::Module> Clone(llvm::CloneModule(Original));

  unsigned OptLevel;
  unsigned OptSize;
  std::tie(OptLevel, OptSize) = C.getOptLevel();

  Optimize(*Clone, Name, C, OptLevel, OptSize);

  std::unique_ptr<llvm::ExecutionEngine> EE = GetEngine(std::move(Clone), Name);

  MapGlobals(*EE, Globals);

  void *Address = (void*)EE->getFunctionAddress(Name);

  return std::unique_ptr<Function>(new Function{Address, std::move(EE)});
}