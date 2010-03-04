//===--- CodeGenAction.cpp - LLVM Code Generation Frontend Action ---------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "clang/Frontend/CodeGenAction.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclGroup.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Basic/TargetOptions.h"
#include "clang/CodeGen/CodeGenOptions.h"
#include "clang/CodeGen/ModuleBuilder.h"
#include "clang/Frontend/ASTConsumers.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendDiagnostic.h"
#include "llvm/Module.h"
#include "llvm/PassManager.h"
#include "llvm/ADT/OwningPtr.h"
#include "llvm/Assembly/PrintModulePass.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/Analysis/Verifier.h"
#include "llvm/Bitcode/ReaderWriter.h"
#include "llvm/CodeGen/RegAllocRegistry.h"
#include "llvm/CodeGen/SchedulerRegistry.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Support/StandardPasses.h"
#include "llvm/Support/Timer.h"
#include "llvm/Target/SubtargetFeature.h"
#include "llvm/Target/TargetData.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/Target/TargetRegistry.h"
using namespace clang;
using namespace llvm;

namespace {
  enum BackendAction {
    Backend_EmitAssembly,  ///< Emit native assembly files
    Backend_EmitBC,        ///< Emit LLVM bitcode files
    Backend_EmitLL,        ///< Emit human-readable LLVM assembly
    Backend_EmitNothing,   ///< Don't emit anything (benchmarking mode)
    Backend_EmitObj        ///< Emit native object files
  };

  class BackendConsumer : public ASTConsumer {
    Diagnostic &Diags;
    BackendAction Action;
    const CodeGenOptions &CodeGenOpts;
    const LangOptions &LangOpts;
    const TargetOptions &TargetOpts;
    llvm::raw_ostream *AsmOutStream;
    llvm::formatted_raw_ostream FormattedOutStream;
    ASTContext *Context;

    Timer LLVMIRGeneration;
    Timer CodeGenerationTime;

    llvm::OwningPtr<CodeGenerator> Gen;

    llvm::OwningPtr<llvm::Module> TheModule;
    llvm::TargetData *TheTargetData;
    llvm::OwningPtr<llvm::TargetMachine> TheTargetMachine;

    mutable FunctionPassManager *CodeGenPasses;
    mutable PassManager *PerModulePasses;
    mutable FunctionPassManager *PerFunctionPasses;

    FunctionPassManager *getCodeGenPasses() const;
    PassManager *getPerModulePasses() const;
    FunctionPassManager *getPerFunctionPasses() const;

    void CreatePasses();

    /// AddEmitPasses - Add passes necessary to emit assembly or LLVM IR.
    ///
    /// \return True on success.
    bool AddEmitPasses();

    void EmitAssembly();

  public:
    BackendConsumer(BackendAction action, Diagnostic &_Diags,
                    llvm::TargetMachine &machine,
                    const LangOptions &langopts, const CodeGenOptions &compopts,
                    const TargetOptions &targetopts, bool TimePasses,
                    const std::string &infile, llvm::raw_ostream *OS,
                    LLVMContext& C) :
      Diags(_Diags),
      Action(action),
      CodeGenOpts(compopts),
      LangOpts(langopts),
      TargetOpts(targetopts),
      AsmOutStream(OS),
      LLVMIRGeneration("LLVM IR Generation Time"),
      CodeGenerationTime("Code Generation Time"),
      Gen(CreateLLVMCodeGen(Diags, infile, compopts, machine, C)),
      TheTargetData(0), TheTargetMachine(&machine),
      CodeGenPasses(0), PerModulePasses(0), PerFunctionPasses(0) {

      if (AsmOutStream)
        FormattedOutStream.setStream(*AsmOutStream,
                                     formatted_raw_ostream::PRESERVE_STREAM);

      llvm::TimePassesIsEnabled = TimePasses;
    }

    ~BackendConsumer() {
      delete TheTargetData;
      delete CodeGenPasses;
      delete PerModulePasses;
      delete PerFunctionPasses;
    }

    llvm::Module *takeModule() { return TheModule.take(); }

    virtual void Initialize(ASTContext &Ctx) {
      Context = &Ctx;

      if (llvm::TimePassesIsEnabled)
        LLVMIRGeneration.startTimer();

      Gen->Initialize(Ctx);

      TheModule.reset(Gen->GetModule());
      TheTargetData = new llvm::TargetData(Ctx.Target.getTargetDescription());

      if (llvm::TimePassesIsEnabled)
        LLVMIRGeneration.stopTimer();
    }

    virtual void HandleTopLevelDecl(DeclGroupRef D) {
      PrettyStackTraceDecl CrashInfo(*D.begin(), SourceLocation(),
                                     Context->getSourceManager(),
                                     "LLVM IR generation of declaration");

      if (llvm::TimePassesIsEnabled)
        LLVMIRGeneration.startTimer();

      Gen->HandleTopLevelDecl(D);

      if (llvm::TimePassesIsEnabled)
        LLVMIRGeneration.stopTimer();
    }

    virtual void HandleTranslationUnit(ASTContext &C) {
      {
        PrettyStackTraceString CrashInfo("Per-file LLVM IR generation");
        if (llvm::TimePassesIsEnabled)
          LLVMIRGeneration.startTimer();

        Gen->HandleTranslationUnit(C);

        if (llvm::TimePassesIsEnabled)
          LLVMIRGeneration.stopTimer();
      }

      // EmitAssembly times and registers crash info itself.
      EmitAssembly();

      // Force a flush here in case we never get released.
      if (AsmOutStream)
        FormattedOutStream.flush();
    }

    virtual void HandleTagDeclDefinition(TagDecl *D) {
      PrettyStackTraceDecl CrashInfo(D, SourceLocation(),
                                     Context->getSourceManager(),
                                     "LLVM IR generation of declaration");
      Gen->HandleTagDeclDefinition(D);
    }

    virtual void CompleteTentativeDefinition(VarDecl *D) {
      Gen->CompleteTentativeDefinition(D);
    }
  };
}

FunctionPassManager *BackendConsumer::getCodeGenPasses() const {
  if (!CodeGenPasses) {
    CodeGenPasses = new FunctionPassManager(&*TheModule);
    CodeGenPasses->add(new TargetData(*TheTargetData));
  }

  return CodeGenPasses;
}

PassManager *BackendConsumer::getPerModulePasses() const {
  if (!PerModulePasses) {
    PerModulePasses = new PassManager();
    PerModulePasses->add(new TargetData(*TheTargetData));
  }

  return PerModulePasses;
}

FunctionPassManager *BackendConsumer::getPerFunctionPasses() const {
  if (!PerFunctionPasses) {
    PerFunctionPasses = new FunctionPassManager(&*TheModule);
    PerFunctionPasses->add(new TargetData(*TheTargetData));
  }

  return PerFunctionPasses;
}

bool BackendConsumer::AddEmitPasses() {
  if (Action == Backend_EmitNothing)
    return true;

  if (Action == Backend_EmitBC) {
    getPerModulePasses()->add(createBitcodeWriterPass(FormattedOutStream));
  } else if (Action == Backend_EmitLL) {
    getPerModulePasses()->add(createPrintModulePass(&FormattedOutStream));
  } else {
    bool Fast = CodeGenOpts.OptimizationLevel == 0;

    // FIXME: Expose these capabilities via actual APIs!!!! Aside from just
    // being gross, this is also totally broken if we ever care about
    // concurrency.
    llvm::NoFramePointerElim = CodeGenOpts.DisableFPElim;
    if (CodeGenOpts.FloatABI == "soft")
      llvm::FloatABIType = llvm::FloatABI::Soft;
    else if (CodeGenOpts.FloatABI == "hard")
      llvm::FloatABIType = llvm::FloatABI::Hard;
    else {
      assert(CodeGenOpts.FloatABI.empty() && "Invalid float abi!");
      llvm::FloatABIType = llvm::FloatABI::Default;
    }
    NoZerosInBSS = CodeGenOpts.NoZeroInitializedInBSS;
    llvm::UseSoftFloat = CodeGenOpts.SoftFloat;
    UnwindTablesMandatory = CodeGenOpts.UnwindTables;

    std::vector<const char *> BackendArgs;
    BackendArgs.push_back("clang"); // Fake program name.
    if (!CodeGenOpts.DebugPass.empty()) {
      BackendArgs.push_back("-debug-pass");
      BackendArgs.push_back(CodeGenOpts.DebugPass.c_str());
    }
    if (!CodeGenOpts.LimitFloatPrecision.empty()) {
      BackendArgs.push_back("-limit-float-precision");
      BackendArgs.push_back(CodeGenOpts.LimitFloatPrecision.c_str());
    }
    if (llvm::TimePassesIsEnabled)
      BackendArgs.push_back("-time-passes");
    BackendArgs.push_back(0);
    llvm::cl::ParseCommandLineOptions(BackendArgs.size() - 1,
                                      (char**) &BackendArgs[0]);

    // Set register scheduler & allocation policy.
    RegisterScheduler::setDefault(createDefaultScheduler);
    RegisterRegAlloc::setDefault(Fast ? createLocalRegisterAllocator :
                                 createLinearScanRegisterAllocator);

    // From llvm-gcc:
    // If there are passes we have to run on the entire module, we do codegen
    // as a separate "pass" after that happens.
    // FIXME: This is disabled right now until bugs can be worked out.  Reenable
    // this for fast -O0 compiles!
    FunctionPassManager *PM = getCodeGenPasses();
    CodeGenOpt::Level OptLevel = CodeGenOpt::Default;

    switch (CodeGenOpts.OptimizationLevel) {
    default: break;
    case 0: OptLevel = CodeGenOpt::None; break;
    case 3: OptLevel = CodeGenOpt::Aggressive; break;
    }

    // Request that addPassesToEmitFile run the Verifier after running
    // passes which modify the IR.
#ifndef NDEBUG
    bool DisableVerify = false;
#else
    bool DisableVerify = true;
#endif

    // Normal mode, emit a .s or .o file by running the code generator. Note,
    // this also adds codegenerator level optimization passes.
    TargetMachine::CodeGenFileType CGFT = TargetMachine::CGFT_AssemblyFile;
    if (Action == Backend_EmitObj)
      CGFT = TargetMachine::CGFT_ObjectFile;
    if (TheTargetMachine->addPassesToEmitFile(*PM, FormattedOutStream,
                                              CGFT, OptLevel, DisableVerify)) {
      Diags.Report(diag::err_fe_unable_to_interface_with_target);
      return false;
    }
  }

  return true;
}

void BackendConsumer::CreatePasses() {
  unsigned OptLevel = CodeGenOpts.OptimizationLevel;
  CodeGenOptions::InliningMethod Inlining = CodeGenOpts.Inlining;

  // Handle disabling of LLVM optimization, where we want to preserve the
  // internal module before any optimization.
  if (CodeGenOpts.DisableLLVMOpts) {
    OptLevel = 0;
    Inlining = CodeGenOpts.NoInlining;
  }

  // In -O0 if checking is disabled, we don't even have per-function passes.
  if (CodeGenOpts.VerifyModule)
    getPerFunctionPasses()->add(createVerifierPass());

  // Assume that standard function passes aren't run for -O0.
  if (OptLevel > 0)
    llvm::createStandardFunctionPasses(getPerFunctionPasses(), OptLevel);

  llvm::Pass *InliningPass = 0;
  switch (Inlining) {
  case CodeGenOptions::NoInlining: break;
  case CodeGenOptions::NormalInlining: {
    // Set the inline threshold following llvm-gcc.
    //
    // FIXME: Derive these constants in a principled fashion.
    unsigned Threshold = 225;
    if (CodeGenOpts.OptimizeSize)
      Threshold = 75;
    else if (OptLevel > 2)
      Threshold = 275;
    InliningPass = createFunctionInliningPass(Threshold);
    break;
  }
  case CodeGenOptions::OnlyAlwaysInlining:
    InliningPass = createAlwaysInlinerPass();         // Respect always_inline
    break;
  }

  // For now we always create per module passes.
  PassManager *PM = getPerModulePasses();
  llvm::createStandardModulePasses(PM, OptLevel, CodeGenOpts.OptimizeSize,
                                   CodeGenOpts.UnitAtATime,
                                   CodeGenOpts.UnrollLoops,
                                   /*SimplifyLibCalls=*/!LangOpts.NoBuiltin,
                                   /*HaveExceptions=*/true,
                                   InliningPass);
}

/// EmitAssembly - Handle interaction with LLVM backend to generate
/// actual machine code.
void BackendConsumer::EmitAssembly() {
  // Silently ignore if we weren't initialized for some reason.
  if (!TheModule || !TheTargetData)
    return;

  TimeRegion Region(llvm::TimePassesIsEnabled ? &CodeGenerationTime : 0);

  // Make sure IR generation is happy with the module. This is
  // released by the module provider.
  Module *M = Gen->ReleaseModule();
  if (!M) {
    // The module has been released by IR gen on failures, do not
    // double free.
    TheModule.take();
    return;
  }

  assert(TheModule.get() == M &&
         "Unexpected module change during IR generation");

  CreatePasses();
  if (!AddEmitPasses())
    return;

  // Run passes. For now we do all passes at once, but eventually we
  // would like to have the option of streaming code generation.

  if (PerFunctionPasses) {
    PrettyStackTraceString CrashInfo("Per-function optimization");

    PerFunctionPasses->doInitialization();
    for (Module::iterator I = M->begin(), E = M->end(); I != E; ++I)
      if (!I->isDeclaration())
        PerFunctionPasses->run(*I);
    PerFunctionPasses->doFinalization();
  }

  if (PerModulePasses) {
    PrettyStackTraceString CrashInfo("Per-module optimization passes");
    PerModulePasses->run(*M);
  }

  if (CodeGenPasses) {
    PrettyStackTraceString CrashInfo("Code generation");
    CodeGenPasses->doInitialization();
    for (Module::iterator I = M->begin(), E = M->end(); I != E; ++I)
      if (!I->isDeclaration())
        CodeGenPasses->run(*I);
    CodeGenPasses->doFinalization();
  }
}

//

CodeGenAction::CodeGenAction(unsigned _Act) : Act(_Act) {}

CodeGenAction::~CodeGenAction() {}

void CodeGenAction::EndSourceFileAction() {
  // If the consumer creation failed, do nothing.
  if (!getCompilerInstance().hasASTConsumer())
    return;

  // Steal the module from the consumer.
  BackendConsumer *Consumer = static_cast<BackendConsumer*>(
    &getCompilerInstance().getASTConsumer());

  TheModule.reset(Consumer->takeModule());
}

llvm::Module *CodeGenAction::takeModule() {
  return TheModule.take();
}

static llvm::TargetMachine *CreateTargetMachine(CompilerInstance &CI) {
  const CodeGenOptions &CodeGenOpts = CI.getCodeGenOpts();
  const TargetOptions &TargetOpts = CI.getTargetOpts();

  std::string Error;
  std::string Triple = CI.getTarget().getTriple().getTriple();
  const llvm::Target *TheTarget = TargetRegistry::lookupTarget(Triple, Error);
  if (!TheTarget) {
    CI.getDiagnostics().Report(diag::err_fe_unable_to_create_target) << Error;
    return 0;
  }

  TargetMachine::setAsmVerbosityDefault(CodeGenOpts.AsmVerbose);

  // FIXME: Parse this earlier.
  if (CodeGenOpts.RelocationModel == "static") {
    TargetMachine::setRelocationModel(llvm::Reloc::Static);
  } else if (CodeGenOpts.RelocationModel == "pic") {
    TargetMachine::setRelocationModel(llvm::Reloc::PIC_);
  } else {
    assert(CodeGenOpts.RelocationModel == "dynamic-no-pic" &&
           "Invalid PIC model!");
    TargetMachine::setRelocationModel(llvm::Reloc::DynamicNoPIC);
  }

  // FIXME: Parse this earlier.
  if (CodeGenOpts.CodeModel == "small") {
    TargetMachine::setCodeModel(llvm::CodeModel::Small);
  } else if (CodeGenOpts.CodeModel == "kernel") {
    TargetMachine::setCodeModel(llvm::CodeModel::Kernel);
  } else if (CodeGenOpts.CodeModel == "medium") {
    TargetMachine::setCodeModel(llvm::CodeModel::Medium);
  } else if (CodeGenOpts.CodeModel == "large") {
    TargetMachine::setCodeModel(llvm::CodeModel::Large);
  } else {
    assert(CodeGenOpts.CodeModel.empty() && "Invalid code model!");
    TargetMachine::setCodeModel(llvm::CodeModel::Default);
  }

  std::string FeaturesStr;
  if (TargetOpts.CPU.size() || TargetOpts.Features.size()) {
    SubtargetFeatures Features;
    Features.setCPU(TargetOpts.CPU);
    for (std::vector<std::string>::const_iterator
           it = TargetOpts.Features.begin(),
           ie = TargetOpts.Features.end(); it != ie; ++it)
      Features.AddFeature(*it);
    FeaturesStr = Features.getString();
  }

  return TheTarget->createTargetMachine(Triple, FeaturesStr);
}

ASTConsumer *CodeGenAction::CreateASTConsumer(CompilerInstance &CI,
                                              llvm::StringRef InFile) {
  llvm::OwningPtr<llvm::TargetMachine> TM(CreateTargetMachine(CI));
  if (!TM)
    return 0;

  BackendAction BA = static_cast<BackendAction>(Act);
  llvm::OwningPtr<llvm::raw_ostream> OS;
  switch (BA) {
  case Backend_EmitAssembly:
    OS.reset(CI.createDefaultOutputFile(false, InFile, "s"));
    break;
  case Backend_EmitLL:
    OS.reset(CI.createDefaultOutputFile(false, InFile, "ll"));
    break;
  case Backend_EmitBC:
    OS.reset(CI.createDefaultOutputFile(true, InFile, "bc"));
    break;
  case Backend_EmitNothing:
    break;
  case Backend_EmitObj:
    OS.reset(CI.createDefaultOutputFile(true, InFile, "o"));
    break;
  }
  if (BA != Backend_EmitNothing && !OS)
    return 0;

  return new BackendConsumer(BA,
                             CI.getDiagnostics(),
                             *TM.take(),
                             CI.getLangOpts(),
                             CI.getCodeGenOpts(),
                             CI.getTargetOpts(),
                             CI.getFrontendOpts().ShowTimers,
                             InFile,
                             OS.take(),
                             CI.getLLVMContext());
}

EmitAssemblyAction::EmitAssemblyAction()
  : CodeGenAction(Backend_EmitAssembly) {}

EmitBCAction::EmitBCAction() : CodeGenAction(Backend_EmitBC) {}

EmitLLVMAction::EmitLLVMAction() : CodeGenAction(Backend_EmitLL) {}

EmitLLVMOnlyAction::EmitLLVMOnlyAction() : CodeGenAction(Backend_EmitNothing) {}

EmitObjAction::EmitObjAction() : CodeGenAction(Backend_EmitObj) {}
