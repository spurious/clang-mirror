//===--- clang-cc.h - C-Language Front-end --------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This is the header file that pulls together the top-level driver.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_CLANG_CC_H
#define LLVM_CLANG_CLANG_CC_H

#include <vector>
#include <string>

namespace llvm {
class raw_ostream;
class raw_fd_ostream;
}

namespace clang {
class Preprocessor;
class MinimalAction;
class TargetInfo;
class Diagnostic;
class ASTConsumer;
class IdentifierTable;
class SourceManager;
class PreprocessorFactory;
class LangOptions;

/// ProcessWarningOptions - Initialize the diagnostic client and process the
/// warning options specified on the command line.
bool ProcessWarningOptions(Diagnostic &Diags);

/// DoPrintPreprocessedInput - Implement -E mode.
void DoPrintPreprocessedInput(Preprocessor &PP, llvm::raw_ostream* OS);

/// RewriteMacrosInInput - Implement -rewrite-macros mode.
void RewriteMacrosInInput(Preprocessor &PP, llvm::raw_ostream* OS);

/// RewriteMacrosInInput - A simple test for the TokenRewriter class.
void DoRewriteTest(Preprocessor &PP, llvm::raw_ostream* OS);
  
/// CreatePrintParserActionsAction - Return the actions implementation that
/// implements the -parse-print-callbacks option.
MinimalAction *CreatePrintParserActionsAction(Preprocessor &PP,
                                              llvm::raw_ostream* OS);

/// CheckDiagnostics - Gather the expected diagnostics and check them.
bool CheckDiagnostics(Preprocessor &PP);

/// CreateDependencyFileGen - Create dependency file generator.
/// This is only done if either -MD or -MMD has been specified.
bool CreateDependencyFileGen(Preprocessor *PP, std::string &ErrStr);

/// CacheTokens - Cache tokens for use with PCH. Note that this requires
/// a seekable stream.
void CacheTokens(Preprocessor& PP, llvm::raw_fd_ostream* OS);

/// CreateAnalysisConsumer - Creates an ASTConsumer to run various code
/// analysis passes.  (The set of analyses run is controlled by command-line
/// options.)
ASTConsumer* CreateAnalysisConsumer(Diagnostic &diags, Preprocessor *pp,
                                    PreprocessorFactory *ppf,
                                    const LangOptions &lopts,
                                    const std::string &output);

}  // end namespace clang

#endif
