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

void BuildASTs(Preprocessor &PP, unsigned MainFileID, bool Stats);
void PrintASTs(Preprocessor &PP, unsigned MainFileID, bool Stats);
void DumpASTs(Preprocessor &PP, unsigned MainFileID, bool Stats);
void DumpCFGs(Preprocessor &PP, unsigned MainFileID, bool Stats);  

} // end clang namespace

#endif
