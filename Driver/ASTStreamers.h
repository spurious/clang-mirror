//===--- ASTStreamers.h - ASTStreamer Drivers -------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file was developed by Bill Wendling and is distributed under the
// University of Illinois Open Source License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// AST Streamers.
//
//===----------------------------------------------------------------------===//

#ifndef DRIVER_ASTSTREAMERS_H_
#define DRIVER_ASTSTREAMERS_H_

namespace clang {

class Preprocessor;
class FunctionDecl;
class TypedefDecl;
class ASTConsumer;

ASTConsumer *CreateASTPrinter();
ASTConsumer *CreateASTDumper();

void DumpCFGs(Preprocessor &PP, unsigned MainFileID,
              bool Stats, bool use_graphviz = false);  
              
void AnalyzeLiveVariables(Preprocessor &PP, unsigned MainFileID);

void RunDeadStoresCheck(Preprocessor &PP, unsigned MainFileID, bool Stats);

} // end clang namespace

#endif
