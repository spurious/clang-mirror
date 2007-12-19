//===--- clang.h - C-Language Front-end -----------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file was developed by Chris Lattner and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This is the header file that pulls together the top-level driver.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_CLANG_H
#define LLVM_CLANG_CLANG_H

#include <vector>
#include <string>

namespace clang {
class Preprocessor;
class MinimalAction;
class TargetInfo;
class Diagnostic;
class ASTConsumer;
class IdentifierTable;
class SourceManager;

/// DoPrintPreprocessedInput - Implement -E mode.
void DoPrintPreprocessedInput(Preprocessor &PP);

/// CreatePrintParserActionsAction - Return the actions implementation that
/// implements the -parse-print-callbacks option.
MinimalAction *CreatePrintParserActionsAction(IdentifierTable &);

/// EmitLLVMFromASTs - Implement -emit-llvm, which generates llvm IR from C.
void EmitLLVMFromASTs(Preprocessor &PP, bool PrintStats);
  
/// CheckASTConsumer - Implement diagnostic checking for AST consumers.
bool CheckASTConsumer(Preprocessor &PP, ASTConsumer* C);


}  // end namespace clang

#endif
