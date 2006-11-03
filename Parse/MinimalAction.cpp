//===--- EmptyAction.cpp - Minimalistic action implementation -------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file was developed by Chris Lattner and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file implements the EmptyAction interface.
//
//===----------------------------------------------------------------------===//

#include "clang/Parse/Parser.h"
#include "clang/Parse/Declarations.h"
#include "clang/Parse/Scope.h"
using namespace llvm;
using namespace clang;

/// TypeNameInfo - A link exists here for each scope that an identifier is
/// defined.
struct TypeNameInfo {
  TypeNameInfo *Prev;
  bool isTypeName;
  
  TypeNameInfo(bool istypename, TypeNameInfo *prev) {
    isTypeName = istypename;
	Prev = prev;
  }
    
};

/// isTypeName - This looks at the IdentifierInfo::FETokenInfo field to
/// determine whether the name is a type name (objc class name or typedef) or
/// not in this scope.
bool EmptyAction::isTypeName(const IdentifierInfo &II, Scope *S) const {
  TypeNameInfo *TI = II.getFETokenInfo<TypeNameInfo>();
  return TI != 0 && TI->isTypeName;
}

/// ParseDeclarator - If this is a typedef declarator, we modify the
/// IdentifierInfo::FETokenInfo field to keep track of this fact, until S is
/// popped.
Action::DeclTy *
EmptyAction::ParseDeclarator(Scope *S, Declarator &D, ExprTy *Init,
                             DeclTy *LastInGroup) {
  IdentifierInfo *II = D.getIdentifier();
  
  // If there is no identifier associated with this declarator, bail out.
  if (II == 0) return 0;
  
  TypeNameInfo *weCurrentlyHaveTypeInfo = II->getFETokenInfo<TypeNameInfo>();
  bool isTypeName = D.getDeclSpec().StorageClassSpec == DeclSpec::SCS_typedef;

  // this check avoids creating TypeNameInfo objects for the common case.
  // It does need to handle the uncommon case of shadowing a typedef name with a 
  // non-typedef name. e.g. { typedef int a; a xx; { int a; } }
  if (weCurrentlyHaveTypeInfo || isTypeName) {
    TypeNameInfo *TI = new TypeNameInfo(isTypeName, weCurrentlyHaveTypeInfo);

    II->setFETokenInfo(TI);
  
    // Remember that this needs to be removed when the scope is popped.
    S->AddDecl(II);
  } 
  return 0;
}

/// ParsedClassDeclaration - 
/// Scope will always be top level file scope. 
Action::DeclTy *
EmptyAction::ParsedClassDeclaration(Scope *S,
                                    IdentifierInfo **IdentList,
                                    unsigned NumElts) {
  for (unsigned i = 0; i != NumElts; ++i) {
    TypeNameInfo *TI =
      new TypeNameInfo(1, IdentList[i]->getFETokenInfo<TypeNameInfo>());

    IdentList[i]->setFETokenInfo(TI);
  
    // Remember that this needs to be removed when the scope is popped.
    S->AddDecl(IdentList[i]);
  }
  return 0;
}

/// PopScope - When a scope is popped, if any typedefs are now out-of-scope,
/// they are removed from the IdentifierInfo::FETokenInfo field.
void EmptyAction::PopScope(SourceLocation Loc, Scope *S) {
  for (Scope::decl_iterator I = S->decl_begin(), E = S->decl_end();
       I != E; ++I) {
    IdentifierInfo &II = *static_cast<IdentifierInfo*>(*I);
    TypeNameInfo *TI = II.getFETokenInfo<TypeNameInfo>();
    assert(TI && "This decl didn't get pushed??");
    
	if (TI) {
      TypeNameInfo *Next = TI->Prev;
      delete TI;
    
      II.setFETokenInfo(Next);
	}
  }
}
