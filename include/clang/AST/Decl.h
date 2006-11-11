//===--- Decl.h - Classes for representing declarations ---------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file was developed by Chris Lattner and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the Decl interface and subclasses.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_DECL_H
#define LLVM_CLANG_AST_DECL_H

#include "clang/Basic/SourceLocation.h"
#include "clang/Parse/DeclSpec.h"   // FIXME: Eliminate.

namespace llvm {
namespace clang {
class IdentifierInfo;
class Stmt;
class FunctionDecl;
  
/// Decl - This represents one declaration (or definition), e.g. a variable, 
/// typedef, function, struct, etc.  
///
class Decl {
  /// Identifier - The identifier for this declaration (e.g. the name for the
  /// variable, the tag for a struct).
  IdentifierInfo *Identifier;
  
  /// DeclarationSpecifier - Information about storage class, type specifiers,
  /// etc.  FIXME: This should be eliminated once we have fully converted types
  /// over.
  DeclSpec DeclarationSpecifier;
  
  /// Type.
  /// Kind.
  
  /// Scope stack info when parsing, otherwise decl list when scope is popped.
  ///
  Decl *Next;
public:
  Decl(IdentifierInfo *Id, const Declarator &D, Decl *next)
    : Identifier(Id), DeclarationSpecifier(D.getDeclSpec()), Next(next) {}
  virtual ~Decl();
  
  const IdentifierInfo *getIdentifier() const { return Identifier; }
  
  const DeclSpec &getDeclSpec() const { return DeclarationSpecifier; }
  
  Decl *getNext() const { return Next; }
  
  // FIXME: Implement cast/dyn_cast/etc
  virtual FunctionDecl *isFunctionDecl() { return 0; }
  virtual const FunctionDecl *isFunctionDecl() const { return 0; }
};

/// FunctionDecl - An instance of this class is created to represent a function
/// declaration or definition.
class FunctionDecl : public Decl {
  // Args etc.
  Stmt *Body;  // Null if a prototype.
public:
  FunctionDecl(IdentifierInfo *Id, const Declarator &D, Decl *Next)
    : Decl(Id, D, Next), Body(0) {}
  
  Stmt *getBody() const { return Body; }
  void setBody(Stmt *B) { Body = B; }
  
  
  // FIXME: Implement cast/dyn_cast/etc
  virtual FunctionDecl *isFunctionDecl() { return this; }
  virtual const FunctionDecl *isFunctionDecl() const { return this; }
};

/// VarDecl - An instance of this class is created to represent a variable
/// declaration or definition.
class VarDecl : public Decl {
  // Initializer.
public:
  VarDecl(IdentifierInfo *Id, const Declarator &D, Decl *Next)
    : Decl(Id, D, Next) {}
  
};
  
}  // end namespace clang
}  // end namespace llvm

#endif
