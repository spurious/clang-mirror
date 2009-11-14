//===--- clang.cpp - C-Language Front-end ---------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This utility may be invoked in the following manner:
//   clang --help                - Output help info.
//   clang [options]             - Read from stdin.
//   clang [options] file        - Read from "file".
//   clang [options] file1 file2 - Read these files.
//
//===----------------------------------------------------------------------===//

#include "Options.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/Analysis/PathDiagnostic.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Basic/Version.h"
#include "clang/CodeGen/ModuleBuilder.h"
#include "clang/Frontend/ASTConsumers.h"
#include "clang/Frontend/ASTUnit.h"
#include "clang/Frontend/AnalysisConsumer.h"
#include "clang/Frontend/CommandLineSourceLoc.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/CompilerInvocation.h"
#include "clang/Frontend/DependencyOutputOptions.h"
#include "clang/Frontend/FixItRewriter.h"
#include "clang/Frontend/FrontendDiagnostic.h"
#include "clang/Frontend/PathDiagnosticClients.h"
#include "clang/Frontend/PreprocessorOptions.h"
#include "clang/Frontend/PreprocessorOutputOptions.h"
#include "clang/Frontend/Utils.h"
#include "clang/Frontend/VerifyDiagnosticsClient.h"
#include "clang/Lex/HeaderSearch.h"
#include "clang/Lex/LexDiagnostic.h"
#include "clang/Parse/Parser.h"
#include "clang/Sema/CodeCompleteConsumer.h"
#include "clang/Sema/ParseAST.h"
#include "clang/Sema/SemaDiagnostic.h"
#include "llvm/LLVMContext.h"
#include "llvm/ADT/OwningPtr.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Config/config.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/PluginLoader.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/Timer.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/System/Host.h"
#include "llvm/System/Path.h"
#include "llvm/System/Signals.h"
#include "llvm/Target/TargetSelect.h"
#include <cstdlib>
#if HAVE_SYS_TYPES_H
#  include <sys/types.h>
#endif

using namespace clang;

//===----------------------------------------------------------------------===//
// Frontend Actions
//===----------------------------------------------------------------------===//

enum ProgActions {
  RewriteObjC,                  // ObjC->C Rewriter.
  RewriteBlocks,                // ObjC->C Rewriter for Blocks.
  RewriteMacros,                // Expand macros but not #includes.
  RewriteTest,                  // Rewriter playground
  HTMLTest,                     // HTML displayer testing stuff.
  EmitAssembly,                 // Emit a .s file.
  EmitLLVM,                     // Emit a .ll file.
  EmitBC,                       // Emit a .bc file.
  EmitLLVMOnly,                 // Generate LLVM IR, but do not
  EmitHTML,                     // Translate input source into HTML.
  ASTPrint,                     // Parse ASTs and print them.
  ASTPrintXML,                  // Parse ASTs and print them in XML.
  ASTDump,                      // Parse ASTs and dump them.
  ASTView,                      // Parse ASTs and view them in Graphviz.
  PrintDeclContext,             // Print DeclContext and their Decls.
  DumpRecordLayouts,            // Dump record layout information.
  ParsePrintCallbacks,          // Parse and print each callback.
  ParseSyntaxOnly,              // Parse and perform semantic analysis.
  FixIt,                        // Parse and apply any fixits to the source.
  ParseNoop,                    // Parse with noop callbacks.
  RunPreprocessorOnly,          // Just lex, no output.
  PrintPreprocessedInput,       // -E mode.
  DumpTokens,                   // Dump out preprocessed tokens.
  DumpRawTokens,                // Dump out raw tokens.
  RunAnalysis,                  // Run one or more source code analyses.
  GeneratePTH,                  // Generate pre-tokenized header.
  GeneratePCH,                  // Generate pre-compiled header.
  InheritanceView               // View C++ inheritance for a specified class.
};

static llvm::cl::opt<ProgActions>
ProgAction(llvm::cl::desc("Choose output type:"), llvm::cl::ZeroOrMore,
           llvm::cl::init(ParseSyntaxOnly),
           llvm::cl::values(
             clEnumValN(RunPreprocessorOnly, "Eonly",
                        "Just run preprocessor, no output (for timings)"),
             clEnumValN(PrintPreprocessedInput, "E",
                        "Run preprocessor, emit preprocessed file"),
             clEnumValN(DumpRawTokens, "dump-raw-tokens",
                        "Lex file in raw mode and dump raw tokens"),
             clEnumValN(RunAnalysis, "analyze",
                        "Run static analysis engine"),
             clEnumValN(DumpTokens, "dump-tokens",
                        "Run preprocessor, dump internal rep of tokens"),
             clEnumValN(ParseNoop, "parse-noop",
                        "Run parser with noop callbacks (for timings)"),
             clEnumValN(ParseSyntaxOnly, "fsyntax-only",
                        "Run parser and perform semantic analysis"),
             clEnumValN(FixIt, "fixit",
                        "Apply fix-it advice to the input source"),
             clEnumValN(ParsePrintCallbacks, "parse-print-callbacks",
                        "Run parser and print each callback invoked"),
             clEnumValN(EmitHTML, "emit-html",
                        "Output input source as HTML"),
             clEnumValN(ASTPrint, "ast-print",
                        "Build ASTs and then pretty-print them"),
             clEnumValN(ASTPrintXML, "ast-print-xml",
                        "Build ASTs and then print them in XML format"),
             clEnumValN(ASTDump, "ast-dump",
                        "Build ASTs and then debug dump them"),
             clEnumValN(ASTView, "ast-view",
                        "Build ASTs and view them with GraphViz"),
             clEnumValN(PrintDeclContext, "print-decl-contexts",
                        "Print DeclContexts and their Decls"),
             clEnumValN(DumpRecordLayouts, "dump-record-layouts",
                        "Dump record layout information"),
             clEnumValN(GeneratePTH, "emit-pth",
                        "Generate pre-tokenized header file"),
             clEnumValN(GeneratePCH, "emit-pch",
                        "Generate pre-compiled header file"),
             clEnumValN(EmitAssembly, "S",
                        "Emit native assembly code"),
             clEnumValN(EmitLLVM, "emit-llvm",
                        "Build ASTs then convert to LLVM, emit .ll file"),
             clEnumValN(EmitBC, "emit-llvm-bc",
                        "Build ASTs then convert to LLVM, emit .bc file"),
             clEnumValN(EmitLLVMOnly, "emit-llvm-only",
                        "Build ASTs and convert to LLVM, discarding output"),
             clEnumValN(RewriteTest, "rewrite-test",
                        "Rewriter playground"),
             clEnumValN(RewriteObjC, "rewrite-objc",
                        "Rewrite ObjC into C (code rewriter example)"),
             clEnumValN(RewriteMacros, "rewrite-macros",
                        "Expand macros without full preprocessing"),
             clEnumValN(RewriteBlocks, "rewrite-blocks",
                        "Rewrite Blocks to C"),
             clEnumValEnd));

//===----------------------------------------------------------------------===//
// Utility Methods
//===----------------------------------------------------------------------===//

std::string GetBuiltinIncludePath(const char *Argv0) {
  llvm::sys::Path P =
    llvm::sys::Path::GetMainExecutable(Argv0,
                                       (void*)(intptr_t) GetBuiltinIncludePath);

  if (!P.isEmpty()) {
    P.eraseComponent();  // Remove /clang from foo/bin/clang
    P.eraseComponent();  // Remove /bin   from foo/bin

    // Get foo/lib/clang/<version>/include
    P.appendComponent("lib");
    P.appendComponent("clang");
    P.appendComponent(CLANG_VERSION_STRING);
    P.appendComponent("include");
  }

  return P.str();
}

//===----------------------------------------------------------------------===//
// Basic Parser driver
//===----------------------------------------------------------------------===//

static void ParseFile(Preprocessor &PP, MinimalAction *PA) {
  Parser P(PP, *PA);
  PP.EnterMainSourceFile();

  // Parsing the specified input file.
  P.ParseTranslationUnit();
  delete PA;
}

//===----------------------------------------------------------------------===//
// Main driver
//===----------------------------------------------------------------------===//

/// ClangFrontendTimer - The front-end activities should charge time to it with
/// TimeRegion.  The -ftime-report option controls whether this will do
/// anything.
llvm::Timer *ClangFrontendTimer = 0;

/// AddFixItLocations - Add any individual user specified "fix-it" locations,
/// and return true on success.
static bool AddFixItLocations(CompilerInstance &CI,
                              FixItRewriter &FixItRewrite) {
  const std::vector<ParsedSourceLocation> &Locs =
    CI.getFrontendOpts().FixItLocations;
  for (unsigned i = 0, e = Locs.size(); i != e; ++i) {
    const FileEntry *File = CI.getFileManager().getFile(Locs[i].FileName);
    if (!File) {
      CI.getDiagnostics().Report(diag::err_fe_unable_to_find_fixit_file)
        << Locs[i].FileName;
      return false;
    }

    RequestedSourceLocation Requested;
    Requested.File = File;
    Requested.Line = Locs[i].Line;
    Requested.Column = Locs[i].Column;
    FixItRewrite.addFixItLocation(Requested);
  }

  return true;
}

static ASTConsumer *CreateConsumerAction(CompilerInstance &CI,
                                         Preprocessor &PP,
                                         const std::string &InFile,
                                         ProgActions PA) {
  const FrontendOptions &FEOpts = CI.getFrontendOpts();

  switch (PA) {
  default:
    return 0;

  case ASTPrint:
    return CreateASTPrinter(CI.createDefaultOutputFile(false, InFile));

  case ASTPrintXML:
    return CreateASTPrinterXML(CI.createDefaultOutputFile(false, InFile,
                                                          "xml"));

  case ASTDump:
    return CreateASTDumper();

  case ASTView:
    return CreateASTViewer();

  case DumpRecordLayouts:
    return CreateRecordLayoutDumper();

  case InheritanceView:
    return CreateInheritanceViewer(FEOpts.ViewClassInheritance);

  case EmitAssembly:
  case EmitLLVM:
  case EmitBC:
  case EmitLLVMOnly: {
    BackendAction Act;
    llvm::OwningPtr<llvm::raw_ostream> OS;
    if (ProgAction == EmitAssembly) {
      Act = Backend_EmitAssembly;
      OS.reset(CI.createDefaultOutputFile(false, InFile, "s"));
    } else if (ProgAction == EmitLLVM) {
      Act = Backend_EmitLL;
      OS.reset(CI.createDefaultOutputFile(false, InFile, "ll"));
    } else if (ProgAction == EmitLLVMOnly) {
      Act = Backend_EmitNothing;
    } else {
      Act = Backend_EmitBC;
      OS.reset(CI.createDefaultOutputFile(true, InFile, "bc"));
    }

    return CreateBackendConsumer(Act, PP.getDiagnostics(), PP.getLangOptions(),
                                 CI.getCodeGenOpts(), InFile, OS.take(),
                                 CI.getLLVMContext());
  }

  case RewriteObjC:
    return CreateObjCRewriter(InFile,
                              CI.createDefaultOutputFile(true, InFile, "cpp"),
                              PP.getDiagnostics(), PP.getLangOptions(),
                              CI.getDiagnosticOpts().NoRewriteMacros);

  case RewriteBlocks:
    return CreateBlockRewriter(InFile, PP.getDiagnostics(),
                               PP.getLangOptions());

  case FixIt:
    return new ASTConsumer();

  case ParseSyntaxOnly:
    return new ASTConsumer();

  case PrintDeclContext:
    return CreateDeclContextPrinter();
  }
}

/// ProcessInputFile - Process a single input file with the specified state.
static void ProcessInputFile(CompilerInstance &CI, const std::string &InFile,
                             ProgActions PA) {
  Preprocessor &PP = CI.getPreprocessor();
  const FrontendOptions &FEOpts = CI.getFrontendOpts();
  llvm::OwningPtr<ASTConsumer> Consumer;
  llvm::OwningPtr<FixItRewriter> FixItRewrite;
  bool CompleteTranslationUnit = true;

  switch (PA) {
  default:
    Consumer.reset(CreateConsumerAction(CI, PP, InFile, PA));
    if (!Consumer.get()) {
      PP.getDiagnostics().Report(diag::err_fe_invalid_ast_action);
      return;
    }
    break;

  case EmitHTML:
    Consumer.reset(CreateHTMLPrinter(CI.createDefaultOutputFile(false, InFile),
                                     PP));
    break;

  case RunAnalysis:
    Consumer.reset(CreateAnalysisConsumer(PP, FEOpts.OutputFile,
                                          CI.getAnalyzerOpts()));
    break;

  case GeneratePCH: {
    const std::string &Sysroot = CI.getHeaderSearchOpts().Sysroot;
    bool Relocatable = FEOpts.RelocatablePCH;
    if (Relocatable && Sysroot.empty()) {
      PP.Diag(SourceLocation(), diag::err_relocatable_without_without_isysroot);
      Relocatable = false;
    }

    llvm::raw_ostream *OS = CI.createDefaultOutputFile(true, InFile);
    if (Relocatable)
      Consumer.reset(CreatePCHGenerator(PP, OS, Sysroot.c_str()));
    else
      Consumer.reset(CreatePCHGenerator(PP, OS));
    CompleteTranslationUnit = false;
    break;
  }

    // Do any necessary set up for non-consumer actions.
  case DumpRawTokens:
  case DumpTokens:
  case RunPreprocessorOnly:
  case ParseNoop:
  case GeneratePTH:
  case PrintPreprocessedInput:
  case ParsePrintCallbacks:
  case RewriteMacros:
  case RewriteTest:
    break; // No setup.
  }

  // Check if we want a fix-it rewriter.
  if (PA == FixIt) {
    FixItRewrite.reset(new FixItRewriter(PP.getDiagnostics(),
                                         PP.getSourceManager(),
                                         PP.getLangOptions()));
    if (!AddFixItLocations(CI, *FixItRewrite))
      return;
  }

  if (Consumer) {
    // Create the ASTContext.
    CI.createASTContext();

    // Create the external AST source when using PCH.
    const std::string &ImplicitPCHInclude =
      CI.getPreprocessorOpts().getImplicitPCHInclude();
    if (!ImplicitPCHInclude.empty()) {
      CI.createPCHExternalASTSource(ImplicitPCHInclude);
      if (!CI.getASTContext().getExternalSource())
        return;
    }
  }

  // Initialize builtin info as long as we aren't using an external AST
  // source.
  if (!CI.hasASTContext() || !CI.getASTContext().getExternalSource())
    PP.getBuiltinInfo().InitializeBuiltins(PP.getIdentifierTable(),
                                           PP.getLangOptions().NoBuiltin);

  // Initialize the main file entry. This needs to be delayed until after PCH
  // has loaded.
  if (!CI.InitializeSourceManager(InFile))
    return;

  if (Consumer) {
    // FIXME: Move the truncation aspect of this into Sema.
    if (!FEOpts.CodeCompletionAt.FileName.empty())
      CI.createCodeCompletionConsumer();

    // Run the AST consumer action.
    CodeCompleteConsumer *CompletionConsumer =
      CI.hasCodeCompletionConsumer() ? &CI.getCodeCompletionConsumer() : 0;
    ParseAST(PP, Consumer.get(), CI.getASTContext(), FEOpts.ShowStats,
             CompleteTranslationUnit, CompletionConsumer);
  } else {
    // Run the preprocessor actions.
    llvm::TimeRegion Timer(ClangFrontendTimer);
    switch (PA) {
    default:
      assert(0 && "unexpected program action");

    case DumpRawTokens: {
      SourceManager &SM = PP.getSourceManager();
      // Start lexing the specified input file.
      Lexer RawLex(SM.getMainFileID(), SM, PP.getLangOptions());
      RawLex.SetKeepWhitespaceMode(true);

      Token RawTok;
      RawLex.LexFromRawLexer(RawTok);
      while (RawTok.isNot(tok::eof)) {
        PP.DumpToken(RawTok, true);
        fprintf(stderr, "\n");
        RawLex.LexFromRawLexer(RawTok);
      }
      break;
    }

    case DumpTokens: {
      Token Tok;
      // Start preprocessing the specified input file.
      PP.EnterMainSourceFile();
      do {
        PP.Lex(Tok);
        PP.DumpToken(Tok, true);
        fprintf(stderr, "\n");
      } while (Tok.isNot(tok::eof));
      break;
    }

    case GeneratePTH:
      if (FEOpts.OutputFile.empty() || FEOpts.OutputFile == "-") {
        // FIXME: Don't fail this way.
        // FIXME: Verify that we can actually seek in the given file.
        llvm::errs() << "ERROR: PTH requires an seekable file for output!\n";
        ::exit(1);
      }
      CacheTokens(PP, CI.createDefaultOutputFile(true, InFile));
      break;

    case ParseNoop:
      ParseFile(PP, new MinimalAction(PP));
      break;

    case ParsePrintCallbacks: {
      llvm::raw_ostream *OS = CI.createDefaultOutputFile(false, InFile);
      ParseFile(PP, CreatePrintParserActionsAction(PP, OS));
      break;
    }
    case PrintPreprocessedInput:
      DoPrintPreprocessedInput(PP, CI.createDefaultOutputFile(false, InFile),
                               CI.getPreprocessorOutputOpts());
      break;

    case RewriteMacros:
      RewriteMacrosInInput(PP, CI.createDefaultOutputFile(true, InFile));
      break;

    case RewriteTest:
      DoRewriteTest(PP, CI.createDefaultOutputFile(false, InFile));
      break;

    case RunPreprocessorOnly: {    // Just lex as fast as we can, no output.
      Token Tok;
      // Start parsing the specified input file.
      PP.EnterMainSourceFile();
      do {
        PP.Lex(Tok);
      } while (Tok.isNot(tok::eof));
      break;
    }
    }
  }

  if (FixItRewrite)
    FixItRewrite->WriteFixedFile(InFile, FEOpts.OutputFile);

  // Release the consumer and the AST, in that order since the consumer may
  // perform actions in its destructor which require the context.
  if (FEOpts.DisableFree) {
    Consumer.take();
    CI.takeASTContext();
  } else {
    Consumer.reset();
    CI.setASTContext(0);
  }

  if (FEOpts.ShowStats) {
    fprintf(stderr, "\nSTATISTICS FOR '%s':\n", InFile.c_str());
    PP.PrintStats();
    PP.getIdentifierTable().PrintStats();
    PP.getHeaderSearchInfo().PrintStats();
    PP.getSourceManager().PrintStats();
    fprintf(stderr, "\n");
  }

  // Cleanup the output streams, and erase the output files if we encountered an
  // error.
  CI.ClearOutputFiles(/*EraseFiles=*/PP.getDiagnostics().getNumErrors());
}

/// ProcessASTInputFile - Process a single AST input file with the specified
/// state.
static void ProcessASTInputFile(CompilerInstance &CI, const std::string &InFile,
                                ProgActions PA) {
  std::string Error;
  llvm::OwningPtr<ASTUnit> AST(ASTUnit::LoadFromPCHFile(InFile, &Error));
  if (!AST) {
    CI.getDiagnostics().Report(diag::err_fe_invalid_ast_file) << Error;
    return;
  }

  Preprocessor &PP = AST->getPreprocessor();
  llvm::OwningPtr<ASTConsumer> Consumer(CreateConsumerAction(CI, PP,
                                                             InFile, PA));
  if (!Consumer.get()) {
    CI.getDiagnostics().Report(diag::err_fe_invalid_ast_action);
    return;
  }

  // Set the main file ID to an empty file.
  //
  // FIXME: We probably shouldn't need this, but for now this is the simplest
  // way to reuse the logic in ParseAST.
  const char *EmptyStr = "";
  llvm::MemoryBuffer *SB =
    llvm::MemoryBuffer::getMemBuffer(EmptyStr, EmptyStr, "<dummy input>");
  AST->getSourceManager().createMainFileIDForMemBuffer(SB);

  // Stream the input AST to the consumer.
  CI.getDiagnostics().getClient()->BeginSourceFile(PP.getLangOptions(), &PP);
  ParseAST(PP, Consumer.get(), AST->getASTContext(),
           CI.getFrontendOpts().ShowStats);
  CI.getDiagnostics().getClient()->EndSourceFile();

  // Release the consumer and the AST, in that order since the consumer may
  // perform actions in its destructor which require the context.
  if (CI.getFrontendOpts().DisableFree) {
    Consumer.take();
    AST.take();
  } else {
    Consumer.reset();
    AST.reset();
  }

  // Cleanup the output streams, and erase the output files if we encountered an
  // error.
  CI.ClearOutputFiles(/*EraseFiles=*/PP.getDiagnostics().getNumErrors());
}

static void LLVMErrorHandler(void *UserData, const std::string &Message) {
  Diagnostic &Diags = *static_cast<Diagnostic*>(UserData);

  Diags.Report(diag::err_fe_error_backend) << Message;

  // We cannot recover from llvm errors.
  exit(1);
}

static TargetInfo *
ConstructCompilerInvocation(CompilerInvocation &Opts, Diagnostic &Diags,
                            const char *Argv0, bool &IsAST) {
  // Initialize frontend options.
  InitializeFrontendOptions(Opts.getFrontendOpts());

  // FIXME: The target information in frontend options should be split out into
  // TargetOptions, and the target options in codegen options should move there
  // as well. Then we could properly initialize in layering order.

  // Initialize base triple.  If a -triple option has been specified, use
  // that triple.  Otherwise, default to the host triple.
  llvm::Triple Triple(Opts.getFrontendOpts().TargetTriple);
  if (Triple.getTriple().empty())
    Triple = llvm::Triple(llvm::sys::getHostTriple());

  // Get information about the target being compiled for.
  TargetInfo *Target = TargetInfo::CreateTargetInfo(Triple.getTriple());
  if (!Target) {
    Diags.Report(diag::err_fe_unknown_triple) << Triple.getTriple().c_str();
    return 0;
  }

  // Set the target ABI if specified.
  if (!Opts.getFrontendOpts().TargetABI.empty() &&
      !Target->setABI(Opts.getFrontendOpts().TargetABI)) {
    Diags.Report(diag::err_fe_unknown_target_abi)
      << Opts.getFrontendOpts().TargetABI;
    return 0;
  }

  // Initialize backend options, which may also be used to key some language
  // options.
  InitializeCodeGenOptions(Opts.getCodeGenOpts(), *Target);

  // Determine the input language, we currently require all files to match.
  FrontendOptions::InputKind IK = Opts.getFrontendOpts().Inputs[0].first;
  for (unsigned i = 1, e = Opts.getFrontendOpts().Inputs.size(); i != e; ++i) {
    if (Opts.getFrontendOpts().Inputs[i].first != IK) {
      llvm::errs() << "error: cannot have multiple input files of distinct "
                   << "language kinds without -x\n";
      return 0;
    }
  }

  // Initialize language options.
  //
  // FIXME: These aren't used during operations on ASTs. Split onto a separate
  // code path to make this obvious.
  IsAST = (IK == FrontendOptions::IK_AST);
  if (!IsAST)
    InitializeLangOptions(Opts.getLangOpts(), IK, *Target,
                          Opts.getCodeGenOpts());

  // Initialize the static analyzer options.
  InitializeAnalyzerOptions(Opts.getAnalyzerOpts());

  // Initialize the dependency output options (-M...).
  InitializeDependencyOutputOptions(Opts.getDependencyOutputOpts());

  // Initialize the header search options.
  InitializeHeaderSearchOptions(Opts.getHeaderSearchOpts(),
                                GetBuiltinIncludePath(Argv0),
                                Opts.getLangOpts());

  // Initialize the other preprocessor options.
  InitializePreprocessorOptions(Opts.getPreprocessorOpts());

  // Initialize the preprocessed output options.
  InitializePreprocessorOutputOptions(Opts.getPreprocessorOutputOpts());

  // Finalize some code generation options which are derived from other places.
  if (Opts.getLangOpts().NoBuiltin)
    Opts.getCodeGenOpts().SimplifyLibCalls = 0;
  if (Opts.getLangOpts().CPlusPlus)
    Opts.getCodeGenOpts().NoCommon = 1;
  Opts.getCodeGenOpts().TimePasses = Opts.getFrontendOpts().ShowTimers;

  return Target;
}

int main(int argc, char **argv) {
  llvm::sys::PrintStackTraceOnErrorSignal();
  llvm::PrettyStackTraceProgram X(argc, argv);
  CompilerInstance Clang(&llvm::getGlobalContext(), false);

  // Initialize targets first, so that --version shows registered targets.
  llvm::InitializeAllTargets();
  llvm::InitializeAllAsmPrinters();

  llvm::cl::ParseCommandLineOptions(argc, argv,
                              "LLVM 'Clang' Compiler: http://clang.llvm.org\n");

  // Construct the diagnostic engine first, so that we can build a diagnostic
  // client to use for any errors during option handling.
  InitializeDiagnosticOptions(Clang.getDiagnosticOpts());
  Clang.createDiagnostics(argc, argv);
  if (!Clang.hasDiagnostics())
    return 1;

  // Set an error handler, so that any LLVM backend diagnostics go through our
  // error handler.
  llvm::llvm_install_error_handler(LLVMErrorHandler,
                                   static_cast<void*>(&Clang.getDiagnostics()));

  // Now that we have initialized the diagnostics engine, create the target and
  // the compiler invocation object.
  //
  // FIXME: We should move .ast inputs to taking a separate path, they are
  // really quite different.
  bool IsAST;
  Clang.setTarget(
    ConstructCompilerInvocation(Clang.getInvocation(), Clang.getDiagnostics(),
                                argv[0], IsAST));
  if (!Clang.hasTarget())
    return 1;

  // Validate/process some options
  if (Clang.getHeaderSearchOpts().Verbose)
    llvm::errs() << "clang-cc version " CLANG_VERSION_STRING
                 << " based upon " << PACKAGE_STRING
                 << " hosted on " << llvm::sys::getHostTriple() << "\n";

  if (Clang.getFrontendOpts().ShowTimers)
    ClangFrontendTimer = new llvm::Timer("Clang front-end time");

  // Enforce certain implications.
  if (!Clang.getFrontendOpts().ViewClassInheritance.empty())
    ProgAction = InheritanceView;
  if (!Clang.getFrontendOpts().FixItLocations.empty())
    ProgAction = FixIt;

  // Create the source manager.
  Clang.createSourceManager();

  // Create a file manager object to provide access to and cache the filesystem.
  Clang.createFileManager();

  for (unsigned i = 0, e = Clang.getFrontendOpts().Inputs.size(); i != e; ++i) {
    const std::string &InFile = Clang.getFrontendOpts().Inputs[i].second;

    // AST inputs are handled specially.
    if (IsAST) {
      ProcessASTInputFile(Clang, InFile, ProgAction);
      continue;
    }

    // Reset the ID tables if we are reusing the SourceManager.
    if (i)
      Clang.getSourceManager().clearIDTables();

    // Create the preprocessor.
    Clang.createPreprocessor();

    // Process the source file.
    Clang.getDiagnostics().getClient()->BeginSourceFile(Clang.getLangOpts(),
                                                      &Clang.getPreprocessor());
    ProcessInputFile(Clang, InFile, ProgAction);
    Clang.getDiagnostics().getClient()->EndSourceFile();
  }

  if (Clang.getDiagnosticOpts().ShowCarets)
    if (unsigned NumDiagnostics = Clang.getDiagnostics().getNumDiagnostics())
      fprintf(stderr, "%d diagnostic%s generated.\n", NumDiagnostics,
              (NumDiagnostics == 1 ? "" : "s"));

  if (Clang.getFrontendOpts().ShowStats) {
    Clang.getFileManager().PrintStats();
    fprintf(stderr, "\n");
  }

  delete ClangFrontendTimer;

  // Return the appropriate status when verifying diagnostics.
  //
  // FIXME: If we could make getNumErrors() do the right thing, we wouldn't need
  // this.
  if (Clang.getDiagnosticOpts().VerifyDiagnostics)
    return static_cast<VerifyDiagnosticsClient&>(
      Clang.getDiagnosticClient()).HadErrors();

  // Managed static deconstruction. Useful for making things like
  // -time-passes usable.
  llvm::llvm_shutdown();

  return (Clang.getDiagnostics().getNumErrors() != 0);
}
