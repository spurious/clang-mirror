//===--- StmtIterator.cpp - Iterators for Statements ------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file was developed by Ted Kremenek and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines internal methods for StmtIterator.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/StmtIterator.h"
#include "clang/AST/Expr.h"
#include "clang/AST/Decl.h"

using namespace clang;

static inline bool declHasExpr(ScopedDecl *decl) {
  if (VarDecl* D = dyn_cast<VarDecl>(decl))
    if (D->getInit())
      return true;
  
  if (EnumConstantDecl* D = dyn_cast<EnumConstantDecl>(decl))
    if (D->getInitExpr())
      return true;
  
  return false;  
}

void StmtIteratorBase::NextDecl() {
  assert (FirstDecl && decl);

  do decl = decl->getNextDeclarator();
  while (decl != NULL && !declHasExpr(decl));
  
  if (decl == NULL) FirstDecl = NULL;
}

StmtIteratorBase::StmtIteratorBase(ScopedDecl* d) {
  assert (d);
  
  while (d != NULL && !declHasExpr(d))
    d = d->getNextDeclarator();
  
  FirstDecl = d;
  decl = d;
}

void StmtIteratorBase::PrevDecl() {
  assert (FirstDecl);
  assert (decl != FirstDecl);
  
  // March through the list of decls until we find the decl just before
  // the one we currently point 
  
  ScopedDecl* d = FirstDecl;
  ScopedDecl* lastVD = d;
  
  while (d->getNextDeclarator() != decl) {
    if (VarDecl* V = dyn_cast<VarDecl>(d))
      if (V->getInit())
        lastVD = d;

    d = d->getNextDeclarator();
  }
  
  decl = lastVD;
}

Stmt*& StmtIteratorBase::GetDeclExpr() const {
  if (VarDecl* D = dyn_cast<VarDecl>(decl))
    return reinterpret_cast<Stmt*&>(D->Init);
  else {
    EnumConstantDecl* Decl = cast<EnumConstantDecl>(decl);
    return reinterpret_cast<Stmt*&>(Decl->Init);
  }
}
