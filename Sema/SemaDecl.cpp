//===--- SemaDecl.cpp - Semantic Analysis for Declarations ----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file was developed by Chris Lattner and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file implements semantic analysis for declarations.
//
//===----------------------------------------------------------------------===//

#include "Sema.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Type.h"
#include "clang/Parse/Scope.h"
#include "clang/Lex/IdentifierTable.h"
using namespace llvm;
using namespace clang;


bool Sema::isTypeName(const IdentifierInfo &II, Scope *S) const {
  Decl *D = II.getFETokenInfo<Decl>();
  return D != 0 && D->getDeclSpec().StorageClassSpec == DeclSpec::SCS_typedef;
}

void Sema::PopScope(SourceLocation Loc, Scope *S) {
  for (Scope::decl_iterator I = S->decl_begin(), E = S->decl_end();
       I != E; ++I) {
    IdentifierInfo &II = *static_cast<IdentifierInfo*>(*I);
    Decl *D = II.getFETokenInfo<Decl>();
    assert(D && "This decl didn't get pushed??");
    
    Decl *Next = D->getNext();
    
    // FIXME: Push the decl on the parent function list if in a function.
    // FIXME: Don't delete the decl when it gets popped!
    delete D;
    
    II.setFETokenInfo(Next);
  }
}

Action::DeclTy *
Sema::ParseDeclarator(Scope *S, Declarator &D, ExprTy *Init, 
                      DeclTy *LastInGroup) {
  TypeRef DeclaratorType = GetTypeForDeclarator(D, S);
  
  IdentifierInfo *II = D.getIdentifier();
  Decl *PrevDecl = 0;
  
  if (II) {
    PrevDecl = II->getFETokenInfo<Decl>();
    
    // TODO: CHECK FOR CONFLICTS, multiple decls with same name in one scope.
  }
  
  Decl *New;
  if (D.getDeclSpec().StorageClassSpec == DeclSpec::SCS_typedef) {
    New = ParseTypedefDecl(S, D, PrevDecl);
  } else if (D.isFunctionDeclarator())
    New = new FunctionDecl(II, D, PrevDecl);
  else
    New = new VarDecl(II, D, PrevDecl);
  
  if (!New) return 0;
  
  
  // If this has an identifier, add it to the scope stack.
  if (II) {
    // If PrevDecl includes conflicting name here, emit a diagnostic.
    II->setFETokenInfo(New);
    S->AddDecl(II);
  }
  
  // If this is a top-level decl that is chained to some other (e.g. int A,B,C;)
  // remember this in the LastInGroupList list.
  if (LastInGroup && S->getParent() == 0)
    LastInGroupList.push_back((Decl*)LastInGroup);
  
  return New;
}

Action::DeclTy *
Sema::ParseFunctionDefinition(Scope *S, Declarator &D, StmtTy *Body) {
  FunctionDecl *FD = (FunctionDecl *)ParseDeclarator(S, D, 0, 0);
  
  FD->setBody((Stmt*)Body);
  
  return FD;
}


Decl *Sema::ParseTypedefDecl(Scope *S, Declarator &D, Decl *PrevDecl) {
  assert(D.getIdentifier());
  
  return new TypedefDecl(D.getIdentifier(), D, PrevDecl);
}

