//===--- ASTConsumer.h - Abstract interface for reading ASTs ----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file was developed by Chris Lattner and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the ASTConsumer class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_ASTCONSUMER_H
#define LLVM_CLANG_AST_ASTCONSUMER_H

namespace clang {
  class ASTContext;
  class Decl;
  
/// ASTConsumer - This is an abstract interface that should be implemented by
/// clients that read ASTs.  This abstraction layer allows the client to be
/// independent of the AST producer (e.g. parser vs AST dump file reader, etc).
class ASTConsumer {
public:
  virtual ~ASTConsumer();
  
  /// Initialize - This is called to initialize the consumer, providing the
  /// ASTContext.
  virtual void Initialize(ASTContext &Context) {}
  
  /// HandleTopLevelDecl - Handle the specified top-level declaration.  This is
  ///  called by HandleTopLevelDeclaration to process every top-level Decl*.
  virtual void HandleTopLevelDecl(Decl *D) {};
    
  
  /// HandleTopLevelDeclaration - Handle the specified top-level declaration.
  ///  This is called only for Decl* that are the head of a chain of
  ///  Decl's (in the case that the Decl* is a ScopedDecl*).  Subclasses
  ///  can override its behavior; by default it calls HandleTopLevelDecl
  ///  for every Decl* in a decl chain.
  virtual void HandleTopLevelDeclaration(Decl *D);
  
  /// PrintStats - If desired, print any statistics.
  virtual void PrintStats() {
  }
};

} // end namespace clang.

#endif
