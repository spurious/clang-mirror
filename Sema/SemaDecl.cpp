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
#include "clang/AST/ASTContext.h"
#include "clang/AST/Builtins.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/Expr.h"
#include "clang/AST/Type.h"
#include "clang/Parse/DeclSpec.h"
#include "clang/Parse/Scope.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/TargetInfo.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/DenseSet.h"
using namespace clang;

Sema::DeclTy *Sema::isTypeName(const IdentifierInfo &II, Scope *S) const {
  Decl *IIDecl = II.getFETokenInfo<Decl>();
  // Find first occurance of none-tagged declaration
  while(IIDecl && IIDecl->getIdentifierNamespace() != Decl::IDNS_Ordinary)
    IIDecl = cast<ScopedDecl>(IIDecl)->getNext();
  if (!IIDecl)
    return 0;
  if (isa<TypedefDecl>(IIDecl) || isa<ObjcInterfaceDecl>(IIDecl))
    return IIDecl;
  if (ObjcCompatibleAliasDecl *ADecl = 
      dyn_cast<ObjcCompatibleAliasDecl>(IIDecl))
    return ADecl->getClassInterface(); 
  return 0;
}

void Sema::ActOnPopScope(SourceLocation Loc, Scope *S) {
  if (S->decl_empty()) return;
  assert((S->getFlags() & Scope::DeclScope) &&"Scope shouldn't contain decls!");
         
  for (Scope::decl_iterator I = S->decl_begin(), E = S->decl_end();
       I != E; ++I) {
    Decl *TmpD = static_cast<Decl*>(*I);
    assert(TmpD && "This decl didn't get pushed??");
    ScopedDecl *D = dyn_cast<ScopedDecl>(TmpD);
    assert(D && "This decl isn't a ScopedDecl?");
    
    IdentifierInfo *II = D->getIdentifier();
    if (!II) continue;
    
    // Unlink this decl from the identifier.  Because the scope contains decls
    // in an unordered collection, and because we have multiple identifier
    // namespaces (e.g. tag, normal, label),the decl may not be the first entry.
    if (II->getFETokenInfo<Decl>() == D) {
      // Normal case, no multiple decls in different namespaces.
      II->setFETokenInfo(D->getNext());
    } else {
      // Scan ahead.  There are only three namespaces in C, so this loop can
      // never execute more than 3 times.
      ScopedDecl *SomeDecl = II->getFETokenInfo<ScopedDecl>();
      while (SomeDecl->getNext() != D) {
        SomeDecl = SomeDecl->getNext();
        assert(SomeDecl && "Didn't find this decl on its identifier's chain!");
      }
      SomeDecl->setNext(D->getNext());
    }
    
    // This will have to be revisited for C++: there we want to nest stuff in
    // namespace decls etc.  Even for C, we might want a top-level translation
    // unit decl or something.
    if (!CurFunctionDecl)
      continue;

    // Chain this decl to the containing function, it now owns the memory for
    // the decl.
    D->setNext(CurFunctionDecl->getDeclChain());
    CurFunctionDecl->setDeclChain(D);
  }
}

/// getObjcInterfaceDecl - Look up a for a class declaration in the scope.
/// return 0 if one not found.
ObjcInterfaceDecl *Sema::getObjCInterfaceDecl(IdentifierInfo *Id) {

  // Scan up the scope chain looking for a decl that matches this identifier
  // that is in the appropriate namespace.  This search should not take long, as
  // shadowing of names is uncommon, and deep shadowing is extremely uncommon.
  ScopedDecl *IdDecl = NULL;
  for (ScopedDecl *D = Id->getFETokenInfo<ScopedDecl>(); D; D = D->getNext()) {
    if (D->getIdentifierNamespace() == Decl::IDNS_Ordinary) {
      IdDecl = D;
      break;
    }
  }
  if (IdDecl && !isa<ObjcInterfaceDecl>(IdDecl))
    IdDecl = 0;
  return cast_or_null<ObjcInterfaceDecl>(static_cast<Decl*>(IdDecl));
}

/// LookupScopedDecl - Look up the inner-most declaration in the specified
/// namespace.
ScopedDecl *Sema::LookupScopedDecl(IdentifierInfo *II, unsigned NSI,
                                   SourceLocation IdLoc, Scope *S) {
  if (II == 0) return 0;
  Decl::IdentifierNamespace NS = (Decl::IdentifierNamespace)NSI;
  
  // Scan up the scope chain looking for a decl that matches this identifier
  // that is in the appropriate namespace.  This search should not take long, as
  // shadowing of names is uncommon, and deep shadowing is extremely uncommon.
  for (ScopedDecl *D = II->getFETokenInfo<ScopedDecl>(); D; D = D->getNext())
    if (D->getIdentifierNamespace() == NS)
      return D;
  
  // If we didn't find a use of this identifier, and if the identifier
  // corresponds to a compiler builtin, create the decl object for the builtin
  // now, injecting it into translation unit scope, and return it.
  if (NS == Decl::IDNS_Ordinary) {
    // If this is a builtin on some other target, or if this builtin varies
    // across targets (e.g. in type), emit a diagnostic and mark the translation
    // unit non-portable for using it.
    if (II->isNonPortableBuiltin()) {
      // Only emit this diagnostic once for this builtin.
      II->setNonPortableBuiltin(false);
      Context.Target.DiagnoseNonPortability(IdLoc,
                                            diag::port_target_builtin_use);
    }
    // If this is a builtin on this (or all) targets, create the decl.
    if (unsigned BuiltinID = II->getBuiltinID())
      return LazilyCreateBuiltin(II, BuiltinID, S);
  }
  return 0;
}

/// LazilyCreateBuiltin - The specified Builtin-ID was first used at file scope.
/// lazily create a decl for it.
ScopedDecl *Sema::LazilyCreateBuiltin(IdentifierInfo *II, unsigned bid,
                                      Scope *S) {
  Builtin::ID BID = (Builtin::ID)bid;

  if (BID == Builtin::BI__builtin_va_start &&
      Context.getBuiltinVaListType().isNull()) {
    IdentifierInfo *VaIdent = &Context.Idents.get("__builtin_va_list");
    ScopedDecl *VaDecl = LookupScopedDecl(VaIdent, Decl::IDNS_Ordinary, 
                                          SourceLocation(), TUScope);
    TypedefDecl *VaTypedef = cast<TypedefDecl>(VaDecl);
    Context.setBuiltinVaListType(Context.getTypedefType(VaTypedef));
  }
  
  QualType R = Context.BuiltinInfo.GetBuiltinType(BID, Context);  
  FunctionDecl *New = new FunctionDecl(SourceLocation(), II, R,
                                       FunctionDecl::Extern, false, 0);
  
  // Find translation-unit scope to insert this function into.
  if (Scope *FnS = S->getFnParent())
    S = FnS->getParent();   // Skip all scopes in a function at once.
  while (S->getParent())
    S = S->getParent();
  S->AddDecl(New);
  
  // Add this decl to the end of the identifier info.
  if (ScopedDecl *LastDecl = II->getFETokenInfo<ScopedDecl>()) {
    // Scan until we find the last (outermost) decl in the id chain. 
    while (LastDecl->getNext())
      LastDecl = LastDecl->getNext();
    // Insert before (outside) it.
    LastDecl->setNext(New);
  } else {
    II->setFETokenInfo(New);
  }    
  // Make sure clients iterating over decls see this.
  LastInGroupList.push_back(New);
  
  return New;
}

/// MergeTypeDefDecl - We just parsed a typedef 'New' which has the same name
/// and scope as a previous declaration 'Old'.  Figure out how to resolve this
/// situation, merging decls or emitting diagnostics as appropriate.
///
TypedefDecl *Sema::MergeTypeDefDecl(TypedefDecl *New, ScopedDecl *OldD) {
  // Verify the old decl was also a typedef.
  TypedefDecl *Old = dyn_cast<TypedefDecl>(OldD);
  if (!Old) {
    Diag(New->getLocation(), diag::err_redefinition_different_kind,
         New->getName());
    Diag(OldD->getLocation(), diag::err_previous_definition);
    return New;
  }
  
  // TODO: CHECK FOR CONFLICTS, multiple decls with same name in one scope.
  // TODO: This is totally simplistic.  It should handle merging functions
  // together etc, merging extern int X; int X; ...
  Diag(New->getLocation(), diag::err_redefinition, New->getName());
  Diag(Old->getLocation(), diag::err_previous_definition);
  return New;
}

/// MergeFunctionDecl - We just parsed a function 'New' which has the same name
/// and scope as a previous declaration 'Old'.  Figure out how to resolve this
/// situation, merging decls or emitting diagnostics as appropriate.
///
FunctionDecl *Sema::MergeFunctionDecl(FunctionDecl *New, ScopedDecl *OldD) {
  // Verify the old decl was also a function.
  FunctionDecl *Old = dyn_cast<FunctionDecl>(OldD);
  if (!Old) {
    Diag(New->getLocation(), diag::err_redefinition_different_kind,
         New->getName());
    Diag(OldD->getLocation(), diag::err_previous_definition);
    return New;
  }
  
  // This is not right, but it's a start.  If 'Old' is a function prototype with
  // the same type as 'New', silently allow this.  FIXME: We should link up decl
  // objects here.
  if (Old->getBody() == 0 && 
      Old->getCanonicalType() == New->getCanonicalType()) {
    return New;
  }
  
  // TODO: CHECK FOR CONFLICTS, multiple decls with same name in one scope.
  // TODO: This is totally simplistic.  It should handle merging functions
  // together etc, merging extern int X; int X; ...
  Diag(New->getLocation(), diag::err_redefinition, New->getName());
  Diag(Old->getLocation(), diag::err_previous_definition);
  return New;
}

/// MergeVarDecl - We just parsed a variable 'New' which has the same name
/// and scope as a previous declaration 'Old'.  Figure out how to resolve this
/// situation, merging decls or emitting diagnostics as appropriate.
///
/// FIXME: Need to carefully consider tentative definition rules (C99 6.9.2p2).
/// For example, we incorrectly complain about i1, i4 from C99 6.9.2p4.
/// 
VarDecl *Sema::MergeVarDecl(VarDecl *New, ScopedDecl *OldD) {
  // Verify the old decl was also a variable.
  VarDecl *Old = dyn_cast<VarDecl>(OldD);
  if (!Old) {
    Diag(New->getLocation(), diag::err_redefinition_different_kind,
         New->getName());
    Diag(OldD->getLocation(), diag::err_previous_definition);
    return New;
  }
  FileVarDecl *OldFSDecl = dyn_cast<FileVarDecl>(Old);
  FileVarDecl *NewFSDecl = dyn_cast<FileVarDecl>(New);
  bool OldIsTentative = false;
  
  if (OldFSDecl && NewFSDecl) { // C99 6.9.2
    // Handle C "tentative" external object definitions. FIXME: finish!
    if (!OldFSDecl->getInit() &&
        (OldFSDecl->getStorageClass() == VarDecl::None ||
         OldFSDecl->getStorageClass() == VarDecl::Static))
      OldIsTentative = true;
  }
  // Verify the types match.
  if (Old->getCanonicalType() != New->getCanonicalType()) {
    Diag(New->getLocation(), diag::err_redefinition, New->getName());
    Diag(Old->getLocation(), diag::err_previous_definition);
    return New;
  }
  // We've verified the types match, now check if Old is "extern".
  if (Old->getStorageClass() != VarDecl::Extern) {
    Diag(New->getLocation(), diag::err_redefinition, New->getName());
    Diag(Old->getLocation(), diag::err_previous_definition);
  }
  return New;
}

/// ParsedFreeStandingDeclSpec - This method is invoked when a declspec with
/// no declarator (e.g. "struct foo;") is parsed.
Sema::DeclTy *Sema::ParsedFreeStandingDeclSpec(Scope *S, DeclSpec &DS) {
  // TODO: emit error on 'int;' or 'const enum foo;'.
  // TODO: emit error on 'typedef int;'
  // if (!DS.isMissingDeclaratorOk()) Diag(...);
  
  return 0;
}

bool Sema::CheckSingleInitializer(Expr *&Init, QualType DeclType) {
  AssignmentCheckResult result;
  SourceLocation loc = Init->getLocStart();
  // Get the type before calling CheckSingleAssignmentConstraints(), since
  // it can promote the expression.
  QualType rhsType = Init->getType(); 
  
  result = CheckSingleAssignmentConstraints(DeclType, Init);
  
  // decode the result (notice that extensions still return a type).
  switch (result) {
  case Compatible:
    break;
  case Incompatible:
    // FIXME: tighten up this check which should allow:
    // char s[] = "abc", which is identical to char s[] = { 'a', 'b', 'c' };
    if (rhsType == Context.getPointerType(Context.CharTy))
      break;
    Diag(loc, diag::err_typecheck_assign_incompatible, 
         DeclType.getAsString(), rhsType.getAsString(), 
         Init->getSourceRange());
    return true;
  case PointerFromInt:
    // check for null pointer constant (C99 6.3.2.3p3)
    if (!Init->isNullPointerConstant(Context)) {
      Diag(loc, diag::ext_typecheck_assign_pointer_int,
           DeclType.getAsString(), rhsType.getAsString(), 
           Init->getSourceRange());
      return true;
    }
    break;
  case IntFromPointer: 
    Diag(loc, diag::ext_typecheck_assign_pointer_int, 
         DeclType.getAsString(), rhsType.getAsString(), 
         Init->getSourceRange());
    break;
  case IncompatiblePointer:
    Diag(loc, diag::ext_typecheck_assign_incompatible_pointer,
         DeclType.getAsString(), rhsType.getAsString(), 
         Init->getSourceRange());
    break;
  case CompatiblePointerDiscardsQualifiers:
    Diag(loc, diag::ext_typecheck_assign_discards_qualifiers,
         DeclType.getAsString(), rhsType.getAsString(), 
         Init->getSourceRange());
    break;
  }
  return false;
}

bool Sema::CheckInitExpr(Expr *expr, InitListExpr *IList, unsigned slot,
                         bool isStatic, QualType ElementType) {
  SourceLocation loc;
  Expr *savExpr = expr; // Might be promoted by CheckSingleInitializer.

  if (isStatic && !expr->isConstantExpr(Context, &loc)) { // C99 6.7.8p4.
    Diag(loc, diag::err_init_element_not_constant, expr->getSourceRange());
    return true;
  } else if (CheckSingleInitializer(expr, ElementType)) {
    return true; // types weren't compatible.
  }
  if (savExpr != expr) // The type was promoted, update initializer list.
    IList->setInit(slot, expr);
  return false;
}

void Sema::CheckVariableInitList(QualType DeclType, InitListExpr *IList, 
                                 QualType ElementType, bool isStatic, 
                                 int &nInitializers, bool &hadError) {
  for (unsigned i = 0; i < IList->getNumInits(); i++) {
    Expr *expr = IList->getInit(i);
    
    if (InitListExpr *InitList = dyn_cast<InitListExpr>(expr)) {
      if (const ConstantArrayType *CAT = DeclType->getAsConstantArrayType()) {
        int maxElements = CAT->getMaximumElements();
        CheckConstantInitList(DeclType, InitList, ElementType, isStatic, 
                              maxElements, hadError);
      }
    } else {
      hadError = CheckInitExpr(expr, IList, i, isStatic, ElementType);
    }
    nInitializers++;
  }
  return;
}

// FIXME: Doesn't deal with arrays of structures yet.
void Sema::CheckConstantInitList(QualType DeclType, InitListExpr *IList, 
                                 QualType ElementType, bool isStatic,
                                 int &totalInits, bool &hadError) {
  int maxElementsAtThisLevel = 0;
  int nInitsAtLevel = 0;

  if (const ConstantArrayType *CAT = DeclType->getAsConstantArrayType()) {
    // We have a constant array type, compute maxElements *at this level*.
    maxElementsAtThisLevel = CAT->getMaximumElements();
    // Set DeclType, used below to recurse (for multi-dimensional arrays).
    DeclType = CAT->getElementType();
  } else if (DeclType->isScalarType()) {
    Diag(IList->getLocStart(), diag::warn_braces_around_scalar_init, 
         IList->getSourceRange());
    maxElementsAtThisLevel = 1;
  } 
  // The empty init list "{ }" is treated specially below.
  unsigned numInits = IList->getNumInits();
  if (numInits) {
    for (unsigned i = 0; i < numInits; i++) {
      Expr *expr = IList->getInit(i);
      
      if (InitListExpr *InitList = dyn_cast<InitListExpr>(expr)) {
        CheckConstantInitList(DeclType, InitList, ElementType, isStatic, 
                              totalInits, hadError);
      } else {
        hadError = CheckInitExpr(expr, IList, i, isStatic, ElementType);
        nInitsAtLevel++; // increment the number of initializers at this level.
        totalInits--;    // decrement the total number of initializers.
        
        // Check if we have space for another initializer.
        if ((nInitsAtLevel > maxElementsAtThisLevel) || (totalInits < 0))
          Diag(expr->getLocStart(), diag::warn_excess_initializers, 
               expr->getSourceRange());
      }
    }
    if (nInitsAtLevel < maxElementsAtThisLevel) // fill the remaining elements.
      totalInits -= (maxElementsAtThisLevel - nInitsAtLevel);
  } else { 
    // we have an initializer list with no elements.
    totalInits -= maxElementsAtThisLevel;
    if (totalInits < 0)
      Diag(IList->getLocStart(), diag::warn_excess_initializers, 
           IList->getSourceRange());
  }
  return;
}

bool Sema::CheckInitializer(Expr *&Init, QualType &DeclType, bool isStatic) {
  InitListExpr *InitList = dyn_cast<InitListExpr>(Init);
  if (!InitList)
    return CheckSingleInitializer(Init, DeclType);

  // We have an InitListExpr, make sure we set the type.
  Init->setType(DeclType);

  bool hadError = false;
  
  // C99 6.7.8p3: The type of the entity to be initialized shall be an array
  // of unknown size ("[]") or an object type that is not a variable array type.
  if (const VariableArrayType *VAT = DeclType->getAsVariableArrayType()) { 
    Expr *expr = VAT->getSizeExpr();
    if (expr)
      return Diag(expr->getLocStart(), diag::err_variable_object_no_init, 
                  expr->getSourceRange());

    // We have a VariableArrayType with unknown size. Note that only the first
    // array can have unknown size. For example, "int [][]" is illegal.
    int numInits = 0;
    CheckVariableInitList(VAT->getElementType(), InitList, VAT->getBaseType(), 
                          isStatic, numInits, hadError);
    if (!hadError) {
      // Return a new array type from the number of initializers (C99 6.7.8p22).
      llvm::APSInt ConstVal(32);
      ConstVal = numInits;
      DeclType = Context.getConstantArrayType(DeclType, ConstVal, 
                                              ArrayType::Normal, 0);
    }
    return hadError;
  }
  if (const ConstantArrayType *CAT = DeclType->getAsConstantArrayType()) {
    int maxElements = CAT->getMaximumElements();
    CheckConstantInitList(DeclType, InitList, CAT->getBaseType(), 
                          isStatic, maxElements, hadError);
    return hadError;
  }
  if (DeclType->isScalarType()) { // C99 6.7.8p11: Allow "int x = { 1, 2 };"
    int maxElements = 1;
    CheckConstantInitList(DeclType, InitList, DeclType, isStatic, maxElements, 
                          hadError);
    return hadError;
  }
  // FIXME: Handle struct/union types.
  return hadError;
}

Sema::DeclTy *
Sema::ActOnDeclarator(Scope *S, Declarator &D, DeclTy *lastDecl) {
  ScopedDecl *LastDeclarator = dyn_cast_or_null<ScopedDecl>((Decl *)lastDecl);
  IdentifierInfo *II = D.getIdentifier();
  
  // All of these full declarators require an identifier.  If it doesn't have
  // one, the ParsedFreeStandingDeclSpec action should be used.
  if (II == 0) {
    Diag(D.getDeclSpec().getSourceRange().Begin(),
         diag::err_declarator_need_ident,
         D.getDeclSpec().getSourceRange(), D.getSourceRange());
    return 0;
  }
  
  // The scope passed in may not be a decl scope.  Zip up the scope tree until
  // we find one that is.
  while ((S->getFlags() & Scope::DeclScope) == 0)
    S = S->getParent();
  
  // See if this is a redefinition of a variable in the same scope.
  ScopedDecl *PrevDecl = LookupScopedDecl(II, Decl::IDNS_Ordinary,
                                          D.getIdentifierLoc(), S);
  if (PrevDecl && !S->isDeclScope(PrevDecl))
    PrevDecl = 0;   // If in outer scope, it isn't the same thing.

  ScopedDecl *New;
  bool InvalidDecl = false;
  
  if (D.getDeclSpec().getStorageClassSpec() == DeclSpec::SCS_typedef) {
    TypedefDecl *NewTD = ParseTypedefDecl(S, D, LastDeclarator);
    if (!NewTD) return 0;

    // Handle attributes prior to checking for duplicates in MergeVarDecl
    HandleDeclAttributes(NewTD, D.getDeclSpec().getAttributes(),
                         D.getAttributes());
    // Merge the decl with the existing one if appropriate.
    if (PrevDecl) {
      NewTD = MergeTypeDefDecl(NewTD, PrevDecl);
      if (NewTD == 0) return 0;
    }
    New = NewTD;
    if (S->getParent() == 0) {
      // C99 6.7.7p2: If a typedef name specifies a variably modified type
      // then it shall have block scope.
      if (const VariableArrayType *VAT = 
            NewTD->getUnderlyingType()->getAsVariablyModifiedType()) {
        Diag(D.getIdentifierLoc(), diag::err_typecheck_illegal_vla, 
             VAT->getSizeExpr()->getSourceRange());
        InvalidDecl = true;
      }
    }
  } else if (D.isFunctionDeclarator()) {
    QualType R = GetTypeForDeclarator(D, S);
    assert(!R.isNull() && "GetTypeForDeclarator() returned null type");

    FunctionDecl::StorageClass SC = FunctionDecl::None;
    switch (D.getDeclSpec().getStorageClassSpec()) {
      default: assert(0 && "Unknown storage class!");
      case DeclSpec::SCS_auto:        
      case DeclSpec::SCS_register:
        Diag(D.getIdentifierLoc(), diag::err_typecheck_sclass_func,
             R.getAsString());
        InvalidDecl = true;
        break;
      case DeclSpec::SCS_unspecified: SC = FunctionDecl::None; break;
      case DeclSpec::SCS_extern:      SC = FunctionDecl::Extern; break;
      case DeclSpec::SCS_static:      SC = FunctionDecl::Static; break;
    }

    FunctionDecl *NewFD = new FunctionDecl(D.getIdentifierLoc(), II, R, SC,
                                           D.getDeclSpec().isInlineSpecified(),
                                           LastDeclarator);
    
    // Merge the decl with the existing one if appropriate.
    if (PrevDecl) {
      NewFD = MergeFunctionDecl(NewFD, PrevDecl);
      if (NewFD == 0) return 0;
    }
    New = NewFD;
  } else {
    QualType R = GetTypeForDeclarator(D, S);
    assert(!R.isNull() && "GetTypeForDeclarator() returned null type");

    VarDecl *NewVD;
    VarDecl::StorageClass SC;
    switch (D.getDeclSpec().getStorageClassSpec()) {
      default: assert(0 && "Unknown storage class!");
      case DeclSpec::SCS_unspecified: SC = VarDecl::None; break;
      case DeclSpec::SCS_extern:      SC = VarDecl::Extern; break;
      case DeclSpec::SCS_static:      SC = VarDecl::Static; break;
      case DeclSpec::SCS_auto:        SC = VarDecl::Auto; break;
      case DeclSpec::SCS_register:    SC = VarDecl::Register; break;
    }    
    if (S->getParent() == 0) {
      // C99 6.9p2: The storage-class specifiers auto and register shall not
      // appear in the declaration specifiers in an external declaration.
      if (SC == VarDecl::Auto || SC == VarDecl::Register) {
        Diag(D.getIdentifierLoc(), diag::err_typecheck_sclass_fscope,
             R.getAsString());
        InvalidDecl = true;
      }
      NewVD = new FileVarDecl(D.getIdentifierLoc(), II, R, SC, LastDeclarator);
    } else {
      NewVD = new BlockVarDecl(D.getIdentifierLoc(), II, R, SC, LastDeclarator);
    }
    // Handle attributes prior to checking for duplicates in MergeVarDecl
    HandleDeclAttributes(NewVD, D.getDeclSpec().getAttributes(),
                         D.getAttributes());
     
    // Merge the decl with the existing one if appropriate.
    if (PrevDecl) {
      NewVD = MergeVarDecl(NewVD, PrevDecl);
      if (NewVD == 0) return 0;
    }
    New = NewVD;
  }
  
  // If this has an identifier, add it to the scope stack.
  if (II) {
    New->setNext(II->getFETokenInfo<ScopedDecl>());
    II->setFETokenInfo(New);
    S->AddDecl(New);
  }
  
  if (S->getParent() == 0)
    AddTopLevelDecl(New, LastDeclarator);

  // If any semantic error occurred, mark the decl as invalid.
  if (D.getInvalidType() || InvalidDecl)
    New->setInvalidDecl();
  
  return New;
}

void Sema::AddInitializerToDecl(DeclTy *dcl, ExprTy *init) {
  Decl *RealDecl = static_cast<Decl *>(dcl);
  Expr *Init = static_cast<Expr *>(init);
  
  assert((RealDecl && Init) && "missing decl or initializer");
  
  VarDecl *VDecl = dyn_cast<VarDecl>(RealDecl);
  if (!VDecl) {
    Diag(dyn_cast<ScopedDecl>(RealDecl)->getLocation(), 
         diag::err_illegal_initializer);
    RealDecl->setInvalidDecl();
    return;
  }  
  // Get the decls type and save a reference for later, since
  // CheckInitializer may change it.
  QualType DclT = VDecl->getType(), SavT = DclT;
  if (BlockVarDecl *BVD = dyn_cast<BlockVarDecl>(VDecl)) {
    VarDecl::StorageClass SC = BVD->getStorageClass();
    if (SC == VarDecl::Extern) { // C99 6.7.8p5
      Diag(VDecl->getLocation(), diag::err_block_extern_cant_init);
      BVD->setInvalidDecl();
    } else if (!BVD->isInvalidDecl()) {
      CheckInitializer(Init, DclT, SC == VarDecl::Static);
    }
  } else if (FileVarDecl *FVD = dyn_cast<FileVarDecl>(VDecl)) {
    if (FVD->getStorageClass() == VarDecl::Extern)
      Diag(VDecl->getLocation(), diag::warn_extern_init);
    if (!FVD->isInvalidDecl())
      CheckInitializer(Init, DclT, true);
  }
  // If the type changed, it means we had an incomplete type that was
  // completed by the initializer. For example: 
  //   int ary[] = { 1, 3, 5 };
  // "ary" transitions from a VariableArrayType to a ConstantArrayType.
  if (!VDecl->isInvalidDecl() && (DclT != SavT))
    VDecl->setType(DclT);
    
  // Attach the initializer to the decl.
  VDecl->setInit(Init);
  return;
}

/// The declarators are chained together backwards, reverse the list.
Sema::DeclTy *Sema::FinalizeDeclaratorGroup(Scope *S, DeclTy *group) {
  // Often we have single declarators, handle them quickly.
  Decl *GroupDecl = static_cast<Decl*>(group);
  if (GroupDecl == 0)
    return 0;
  
  ScopedDecl *Group = dyn_cast<ScopedDecl>(GroupDecl);
  ScopedDecl *NewGroup = 0;
  if (Group->getNextDeclarator() == 0) 
    NewGroup = Group;
  else { // reverse the list.
    while (Group) {
      ScopedDecl *Next = Group->getNextDeclarator();
      Group->setNextDeclarator(NewGroup);
      NewGroup = Group;
      Group = Next;
    }
  }
  // Perform semantic analysis that depends on having fully processed both
  // the declarator and initializer.
  for (ScopedDecl *ID = NewGroup; ID; ID = ID->getNextDeclarator()) {
    VarDecl *IDecl = dyn_cast<VarDecl>(ID);
    if (!IDecl)
      continue;
    FileVarDecl *FVD = dyn_cast<FileVarDecl>(IDecl);
    BlockVarDecl *BVD = dyn_cast<BlockVarDecl>(IDecl);
    QualType T = IDecl->getType();
    
    // C99 6.7.5.2p2: If an identifier is declared to be an object with 
    // static storage duration, it shall not have a variable length array.
    if ((FVD || BVD) && IDecl->getStorageClass() == VarDecl::Static) {
      if (const VariableArrayType *VLA = T->getAsVariableArrayType()) {
        if (VLA->getSizeExpr()) {  
          Diag(IDecl->getLocation(), diag::err_typecheck_illegal_vla);
          IDecl->setInvalidDecl();
        }
      }
    }
    // Block scope. C99 6.7p7: If an identifier for an object is declared with
    // no linkage (C99 6.2.2p6), the type for the object shall be complete...
    if (BVD && IDecl->getStorageClass() != VarDecl::Extern) {
      if (T->isIncompleteType()) {
        Diag(IDecl->getLocation(), diag::err_typecheck_decl_incomplete_type,
             T.getAsString());
        IDecl->setInvalidDecl();
      }
    }
    // File scope. C99 6.9.2p2: A declaration of an identifier for and 
    // object that has file scope without an initializer, and without a
    // storage-class specifier or with the storage-class specifier "static",
    // constitutes a tentative definition. Note: A tentative definition with
    // external linkage is valid (C99 6.2.2p5).
    if (FVD && !FVD->getInit() && FVD->getStorageClass() == VarDecl::Static) {
      // C99 6.9.2p3: If the declaration of an identifier for an object is
      // a tentative definition and has internal linkage (C99 6.2.2p3), the  
      // declared type shall not be an incomplete type.
      if (T->isIncompleteType()) {
        Diag(IDecl->getLocation(), diag::err_typecheck_decl_incomplete_type,
             T.getAsString());
        IDecl->setInvalidDecl();
      }
    }
  }
  return NewGroup;
}

// Called from Sema::ParseStartOfFunctionDef().
ParmVarDecl *
Sema::ParseParamDeclarator(DeclaratorChunk &FTI, unsigned ArgNo,
                           Scope *FnScope) {
  const DeclaratorChunk::ParamInfo &PI = FTI.Fun.ArgInfo[ArgNo];

  IdentifierInfo *II = PI.Ident;
  // TODO: CHECK FOR CONFLICTS, multiple decls with same name in one scope.
  // Can this happen for params?  We already checked that they don't conflict
  // among each other.  Here they can only shadow globals, which is ok.
  if (/*Decl *PrevDecl = */LookupScopedDecl(II, Decl::IDNS_Ordinary,
                                        PI.IdentLoc, FnScope)) {
    
  }
  
  // FIXME: Handle storage class (auto, register). No declarator?
  // TODO: Chain to previous parameter with the prevdeclarator chain?

  // Perform the default function/array conversion (C99 6.7.5.3p[7,8]).
  // Doing the promotion here has a win and a loss. The win is the type for
  // both Decl's and DeclRefExpr's will match (a convenient invariant for the
  // code generator). The loss is the orginal type isn't preserved. For example:
  //
  // void func(int parmvardecl[5]) { // convert "int [5]" to "int *"
  //    int blockvardecl[5];
  //    sizeof(parmvardecl);  // size == 4
  //    sizeof(blockvardecl); // size == 20
  // }
  //
  // For expressions, all implicit conversions are captured using the
  // ImplicitCastExpr AST node (we have no such mechanism for Decl's).
  //
  // FIXME: If a source translation tool needs to see the original type, then
  // we need to consider storing both types (in ParmVarDecl)...
  // 
  QualType parmDeclType = QualType::getFromOpaquePtr(PI.TypeInfo);
  if (const ArrayType *AT = parmDeclType->getAsArrayType())
    parmDeclType = Context.getPointerType(AT->getElementType());
  else if (parmDeclType->isFunctionType())
    parmDeclType = Context.getPointerType(parmDeclType);
  
  ParmVarDecl *New = new ParmVarDecl(PI.IdentLoc, II, parmDeclType, 
                                     VarDecl::None, 0);
  if (PI.InvalidType)
    New->setInvalidDecl();
    
  // If this has an identifier, add it to the scope stack.
  if (II) {
    New->setNext(II->getFETokenInfo<ScopedDecl>());
    II->setFETokenInfo(New);
    FnScope->AddDecl(New);
  }

  return New;
}
  

Sema::DeclTy *Sema::ActOnStartOfFunctionDef(Scope *FnBodyScope, Declarator &D) {
  assert(CurFunctionDecl == 0 && "Function parsing confused");
  assert(D.getTypeObject(0).Kind == DeclaratorChunk::Function &&
         "Not a function declarator!");
  DeclaratorChunk::FunctionTypeInfo &FTI = D.getTypeObject(0).Fun;
  
  // Verify 6.9.1p6: 'every identifier in the identifier list shall be declared'
  // for a K&R function.
  if (!FTI.hasPrototype) {
    for (unsigned i = 0, e = FTI.NumArgs; i != e; ++i) {
      if (FTI.ArgInfo[i].TypeInfo == 0) {
        Diag(FTI.ArgInfo[i].IdentLoc, diag::ext_param_not_declared,
             FTI.ArgInfo[i].Ident->getName());
        // Implicitly declare the argument as type 'int' for lack of a better
        // type.
        FTI.ArgInfo[i].TypeInfo = Context.IntTy.getAsOpaquePtr();
      }
    }
   
    // Since this is a function definition, act as though we have information
    // about the arguments.
    FTI.hasPrototype = true;
  } else {
    // FIXME: Diagnose arguments without names in C.
    
  }
  
  Scope *GlobalScope = FnBodyScope->getParent();
  
  FunctionDecl *FD =
    static_cast<FunctionDecl*>(ActOnDeclarator(GlobalScope, D, 0));
  CurFunctionDecl = FD;
  
  // Create Decl objects for each parameter, adding them to the FunctionDecl.
  llvm::SmallVector<ParmVarDecl*, 16> Params;
  
  // Check for C99 6.7.5.3p10 - foo(void) is a non-varargs function that takes
  // no arguments, not a function that takes a single void argument.
  if (FTI.NumArgs == 1 && !FTI.isVariadic && FTI.ArgInfo[0].Ident == 0 &&
      FTI.ArgInfo[0].TypeInfo == Context.VoidTy.getAsOpaquePtr()) {
    // empty arg list, don't push any params.
  } else {
    for (unsigned i = 0, e = FTI.NumArgs; i != e; ++i)
      Params.push_back(ParseParamDeclarator(D.getTypeObject(0), i,FnBodyScope));
  }
  
  FD->setParams(&Params[0], Params.size());
  
  return FD;
}

Sema::DeclTy *Sema::ActOnFunctionDefBody(DeclTy *D, StmtTy *Body) {
  FunctionDecl *FD = static_cast<FunctionDecl*>(D);
  FD->setBody((Stmt*)Body);
  
  assert(FD == CurFunctionDecl && "Function parsing confused");
  CurFunctionDecl = 0;
  
  // Verify and clean out per-function state.
  
  // Check goto/label use.
  for (llvm::DenseMap<IdentifierInfo*, LabelStmt*>::iterator
       I = LabelMap.begin(), E = LabelMap.end(); I != E; ++I) {
    // Verify that we have no forward references left.  If so, there was a goto
    // or address of a label taken, but no definition of it.  Label fwd
    // definitions are indicated with a null substmt.
    if (I->second->getSubStmt() == 0) {
      LabelStmt *L = I->second;
      // Emit error.
      Diag(L->getIdentLoc(), diag::err_undeclared_label_use, L->getName());
      
      // At this point, we have gotos that use the bogus label.  Stitch it into
      // the function body so that they aren't leaked and that the AST is well
      // formed.
      L->setSubStmt(new NullStmt(L->getIdentLoc()));
      cast<CompoundStmt>((Stmt*)Body)->push_back(L);
    }
  }
  LabelMap.clear();
  
  return FD;
}


/// ImplicitlyDefineFunction - An undeclared identifier was used in a function
/// call, forming a call to an implicitly defined function (per C99 6.5.1p2).
ScopedDecl *Sema::ImplicitlyDefineFunction(SourceLocation Loc, 
                                           IdentifierInfo &II, Scope *S) {
  if (getLangOptions().C99)  // Extension in C99.
    Diag(Loc, diag::ext_implicit_function_decl, II.getName());
  else  // Legal in C90, but warn about it.
    Diag(Loc, diag::warn_implicit_function_decl, II.getName());
  
  // FIXME: handle stuff like:
  // void foo() { extern float X(); }
  // void bar() { X(); }  <-- implicit decl for X in another scope.

  // Set a Declarator for the implicit definition: int foo();
  const char *Dummy;
  DeclSpec DS;
  bool Error = DS.SetTypeSpecType(DeclSpec::TST_int, Loc, Dummy);
  Error = Error; // Silence warning.
  assert(!Error && "Error setting up implicit decl!");
  Declarator D(DS, Declarator::BlockContext);
  D.AddTypeInfo(DeclaratorChunk::getFunction(false, false, 0, 0, Loc));
  D.SetIdentifier(&II, Loc);
  
  // Find translation-unit scope to insert this function into.
  if (Scope *FnS = S->getFnParent())
    S = FnS->getParent();   // Skip all scopes in a function at once.
  while (S->getParent())
    S = S->getParent();
  
  return dyn_cast<ScopedDecl>(static_cast<Decl*>(ActOnDeclarator(S, D, 0)));
}


TypedefDecl *Sema::ParseTypedefDecl(Scope *S, Declarator &D,
                                    ScopedDecl *LastDeclarator) {
  assert(D.getIdentifier() && "Wrong callback for declspec without declarator");
  
  QualType T = GetTypeForDeclarator(D, S);
  assert(!T.isNull() && "GetTypeForDeclarator() returned null type");
  
  // Scope manipulation handled by caller.
  TypedefDecl *NewTD = new TypedefDecl(D.getIdentifierLoc(), D.getIdentifier(), 
                                       T, LastDeclarator);
  if (D.getInvalidType())
    NewTD->setInvalidDecl();
  return NewTD;
}

Sema::DeclTy *Sema::ActOnStartClassInterface(
                    SourceLocation AtInterfaceLoc,
                    IdentifierInfo *ClassName, SourceLocation ClassLoc,
                    IdentifierInfo *SuperName, SourceLocation SuperLoc,
                    IdentifierInfo **ProtocolNames, unsigned NumProtocols,
                    AttributeList *AttrList) {
  assert(ClassName && "Missing class identifier");
  
  // Check for another declaration kind with the same name.
  ScopedDecl *PrevDecl = LookupScopedDecl(ClassName, Decl::IDNS_Ordinary,
                                          ClassLoc, TUScope);
  if (PrevDecl && !isa<ObjcInterfaceDecl>(PrevDecl)) {
    Diag(ClassLoc, diag::err_redefinition_different_kind,
         ClassName->getName());
    Diag(PrevDecl->getLocation(), diag::err_previous_definition);
  }
  
  ObjcInterfaceDecl* IDecl = getObjCInterfaceDecl(ClassName);
  if (IDecl) {
    // Class already seen. Is it a forward declaration?
    if (!IDecl->isForwardDecl())
      Diag(AtInterfaceLoc, diag::err_duplicate_class_def, ClassName->getName());
    else {
      IDecl->setForwardDecl(false);
      IDecl->AllocIntfRefProtocols(NumProtocols);
    }
  }
  else {
    IDecl = new ObjcInterfaceDecl(AtInterfaceLoc, NumProtocols, ClassName);
  
    // Chain & install the interface decl into the identifier.
    IDecl->setNext(ClassName->getFETokenInfo<ScopedDecl>());
    ClassName->setFETokenInfo(IDecl);
  }
  
  if (SuperName) {
    ObjcInterfaceDecl* SuperClassEntry = 0;
    // Check if a different kind of symbol declared in this scope.
    PrevDecl = LookupScopedDecl(SuperName, Decl::IDNS_Ordinary,
                                SuperLoc, TUScope);
    if (PrevDecl && !isa<ObjcInterfaceDecl>(PrevDecl)) {
      Diag(SuperLoc, diag::err_redefinition_different_kind,
           SuperName->getName());
      Diag(PrevDecl->getLocation(), diag::err_previous_definition);
    }
    else {
      // Check that super class is previously defined
      SuperClassEntry = getObjCInterfaceDecl(SuperName); 
                              
      if (!SuperClassEntry || SuperClassEntry->isForwardDecl()) {
        Diag(AtInterfaceLoc, diag::err_undef_superclass, SuperName->getName(),
             ClassName->getName()); 
      }
    }
    IDecl->setSuperClass(SuperClassEntry);
  }
  
  /// Check then save referenced protocols
  for (unsigned int i = 0; i != NumProtocols; i++) {
    ObjcProtocolDecl* RefPDecl = ObjcProtocols[ProtocolNames[i]];
    if (!RefPDecl || RefPDecl->isForwardDecl())
      Diag(ClassLoc, diag::err_undef_protocolref,
           ProtocolNames[i]->getName(),
           ClassName->getName());
    IDecl->setIntfRefProtocols((int)i, RefPDecl);
  }
  
  return IDecl;
}

/// ActOnCompatiblityAlias - this action is called after complete parsing of
/// @compaatibility_alias declaration. It sets up the alias relationships.
Sema::DeclTy *Sema::ActOnCompatiblityAlias(
                      SourceLocation AtCompatibilityAliasLoc,
                      IdentifierInfo *AliasName,  SourceLocation AliasLocation,
                      IdentifierInfo *ClassName, SourceLocation ClassLocation) {
  // Look for previous declaration of alias name
  ScopedDecl *ADecl = LookupScopedDecl(AliasName, Decl::IDNS_Ordinary,
                                       AliasLocation, TUScope);
  if (ADecl) {
    if (isa<ObjcCompatibleAliasDecl>(ADecl)) {
      Diag(AliasLocation, diag::warn_previous_alias_decl);
      Diag(ADecl->getLocation(), diag::warn_previous_declaration);
    }
    else {
      Diag(AliasLocation, diag::err_conflicting_aliasing_type,
           AliasName->getName());
      Diag(ADecl->getLocation(), diag::err_previous_declaration);
    }
    return 0;
  }
  // Check for class declaration
  ScopedDecl *CDecl = LookupScopedDecl(ClassName, Decl::IDNS_Ordinary,
                                       ClassLocation, TUScope);
  if (!CDecl || !isa<ObjcInterfaceDecl>(CDecl)) {
    Diag(ClassLocation, diag::warn_undef_interface,
         ClassName->getName());
    if (CDecl)
      Diag(CDecl->getLocation(), diag::warn_previous_declaration);
    return 0;
  }
  // Everything checked out, instantiate a new alias declaration ast
  ObjcCompatibleAliasDecl *AliasDecl = 
    new ObjcCompatibleAliasDecl(AtCompatibilityAliasLoc, 
                                AliasName,
                                dyn_cast<ObjcInterfaceDecl>(CDecl));
    
  // Chain & install the interface decl into the identifier.
  AliasDecl->setNext(AliasName->getFETokenInfo<ScopedDecl>());
  AliasName->setFETokenInfo(AliasDecl);
  return AliasDecl;
}

Sema::DeclTy *Sema::ActOnStartProtocolInterface(
                SourceLocation AtProtoInterfaceLoc,
                IdentifierInfo *ProtocolName, SourceLocation ProtocolLoc,
                IdentifierInfo **ProtoRefNames, unsigned NumProtoRefs) {
  assert(ProtocolName && "Missing protocol identifier");
  ObjcProtocolDecl *PDecl = ObjcProtocols[ProtocolName];
  if (PDecl) {
    // Protocol already seen. Better be a forward protocol declaration
    if (!PDecl->isForwardDecl())
      Diag(ProtocolLoc, diag::err_duplicate_protocol_def, 
           ProtocolName->getName());
    else {
      PDecl->setForwardDecl(false);
      PDecl->AllocReferencedProtocols(NumProtoRefs);
    }
  }
  else {
    PDecl = new ObjcProtocolDecl(AtProtoInterfaceLoc, NumProtoRefs, 
                                 ProtocolName);
    ObjcProtocols[ProtocolName] = PDecl;
  }    
  
  /// Check then save referenced protocols
  for (unsigned int i = 0; i != NumProtoRefs; i++) {
    ObjcProtocolDecl* RefPDecl = ObjcProtocols[ProtoRefNames[i]];
    if (!RefPDecl || RefPDecl->isForwardDecl())
      Diag(ProtocolLoc, diag::err_undef_protocolref,
           ProtoRefNames[i]->getName(),
           ProtocolName->getName());
    PDecl->setReferencedProtocols((int)i, RefPDecl);
  }

  return PDecl;
}

/// FindProtocolDeclaration - This routine looks up protocols and
/// issuer error if they are not declared. It returns list of protocol
/// declarations in its 'Protocols' argument.
void
Sema::FindProtocolDeclaration(SourceLocation TypeLoc,
                              IdentifierInfo **ProtocolId,
                              unsigned NumProtocols,
                              llvm::SmallVector<DeclTy *,8> &Protocols) {
  for (unsigned i = 0; i != NumProtocols; ++i) {
    ObjcProtocolDecl *PDecl = ObjcProtocols[ProtocolId[i]];
    if (!PDecl)
      Diag(TypeLoc, diag::err_undeclared_protocol, 
           ProtocolId[i]->getName());
    else
      Protocols.push_back(PDecl); 
  }
}

/// ActOnForwardProtocolDeclaration - 
Action::DeclTy *
Sema::ActOnForwardProtocolDeclaration(SourceLocation AtProtocolLoc,
        IdentifierInfo **IdentList, unsigned NumElts) {
  llvm::SmallVector<ObjcProtocolDecl*, 32> Protocols;
  
  for (unsigned i = 0; i != NumElts; ++i) {
    IdentifierInfo *P = IdentList[i];
    ObjcProtocolDecl *PDecl = ObjcProtocols[P];
    if (!PDecl)  { // Not already seen?
      // FIXME: Pass in the location of the identifier!
      PDecl = new ObjcProtocolDecl(AtProtocolLoc, 0, P, true);
      ObjcProtocols[P] = PDecl;
    }
    
    Protocols.push_back(PDecl);
  }
  return new ObjcForwardProtocolDecl(AtProtocolLoc,
                                     &Protocols[0], Protocols.size());
}

Sema::DeclTy *Sema::ActOnStartCategoryInterface(
                      SourceLocation AtInterfaceLoc,
                      IdentifierInfo *ClassName, SourceLocation ClassLoc,
                      IdentifierInfo *CategoryName, SourceLocation CategoryLoc,
                      IdentifierInfo **ProtoRefNames, unsigned NumProtoRefs) {
  ObjcInterfaceDecl *IDecl = getObjCInterfaceDecl(ClassName);
  
  /// Check that class of this category is already completely declared.
  if (!IDecl || IDecl->isForwardDecl()) {
    Diag(ClassLoc, diag::err_undef_interface, ClassName->getName());
    return 0;
  }
  ObjcCategoryDecl *CDecl = new ObjcCategoryDecl(AtInterfaceLoc, NumProtoRefs,
                                                 CategoryName);
  CDecl->setClassInterface(IDecl);
  /// Check for duplicate interface declaration for this category
  ObjcCategoryDecl *CDeclChain;
  for (CDeclChain = IDecl->getListCategories(); CDeclChain;
       CDeclChain = CDeclChain->getNextClassCategory()) {
    if (CDeclChain->getIdentifier() == CategoryName) {
      Diag(CategoryLoc, diag::err_dup_category_def, ClassName->getName(),
           CategoryName->getName());
      break;
    }
  }
  if (!CDeclChain)
    CDecl->insertNextClassCategory();

  /// Check then save referenced protocols
  for (unsigned int i = 0; i != NumProtoRefs; i++) {
    ObjcProtocolDecl* RefPDecl = ObjcProtocols[ProtoRefNames[i]];
    if (!RefPDecl || RefPDecl->isForwardDecl()) {
      Diag(CategoryLoc, diag::err_undef_protocolref,
           ProtoRefNames[i]->getName(),
           CategoryName->getName());
    }
    CDecl->setCatReferencedProtocols((int)i, RefPDecl);
  }
  
  return CDecl;
}

/// ActOnStartCategoryImplementation - Perform semantic checks on the
/// category implementation declaration and build an ObjcCategoryImplDecl
/// object.
Sema::DeclTy *Sema::ActOnStartCategoryImplementation(
                      SourceLocation AtCatImplLoc,
                      IdentifierInfo *ClassName, SourceLocation ClassLoc,
                      IdentifierInfo *CatName, SourceLocation CatLoc) {
  ObjcInterfaceDecl *IDecl = getObjCInterfaceDecl(ClassName);
  ObjcCategoryImplDecl *CDecl = new ObjcCategoryImplDecl(AtCatImplLoc, 
                                                         CatName, IDecl);
  /// Check that class of this category is already completely declared.
  if (!IDecl || IDecl->isForwardDecl())
    Diag(ClassLoc, diag::err_undef_interface, ClassName->getName());
  /// TODO: Check that CatName, category name, is not used in another
  // implementation.
  return CDecl;
}

Sema::DeclTy *Sema::ActOnStartClassImplementation(
                      SourceLocation AtClassImplLoc,
                      IdentifierInfo *ClassName, SourceLocation ClassLoc,
                      IdentifierInfo *SuperClassname, 
                      SourceLocation SuperClassLoc) {
  ObjcInterfaceDecl* IDecl = 0;
  // Check for another declaration kind with the same name.
  ScopedDecl *PrevDecl = LookupScopedDecl(ClassName, Decl::IDNS_Ordinary,
                                          ClassLoc, TUScope);
  if (PrevDecl && !isa<ObjcInterfaceDecl>(PrevDecl)) {
    Diag(ClassLoc, diag::err_redefinition_different_kind,
         ClassName->getName());
    Diag(PrevDecl->getLocation(), diag::err_previous_definition);
  }
  else {
    // Is there an interface declaration of this class; if not, warn!
    IDecl = getObjCInterfaceDecl(ClassName); 
    if (!IDecl)
      Diag(ClassLoc, diag::warn_undef_interface, ClassName->getName());
  }
  
  // Check that super class name is valid class name
  ObjcInterfaceDecl* SDecl = 0;
  if (SuperClassname) {
    // Check if a different kind of symbol declared in this scope.
    PrevDecl = LookupScopedDecl(SuperClassname, Decl::IDNS_Ordinary,
                                SuperClassLoc, TUScope);
    if (PrevDecl && !isa<ObjcInterfaceDecl>(PrevDecl)) {
      Diag(SuperClassLoc, diag::err_redefinition_different_kind,
           SuperClassname->getName());
      Diag(PrevDecl->getLocation(), diag::err_previous_definition);
    }
    else {
      SDecl = getObjCInterfaceDecl(SuperClassname); 
      if (!SDecl)
        Diag(SuperClassLoc, diag::err_undef_superclass, 
             SuperClassname->getName(), ClassName->getName());
      else if (IDecl && IDecl->getSuperClass() != SDecl) {
        // This implementation and its interface do not have the same
        // super class.
        Diag(SuperClassLoc, diag::err_conflicting_super_class, 
             SuperClassname->getName());
        Diag(SDecl->getLocation(), diag::err_previous_definition);
      }
    }
  }
  
  ObjcImplementationDecl* IMPDecl = 
    new ObjcImplementationDecl(AtClassImplLoc, ClassName, SDecl);
  if (!IDecl) {
    // Legacy case of @implementation with no corresponding @interface.
    // Build, chain & install the interface decl into the identifier.
    IDecl = new ObjcInterfaceDecl(SourceLocation(), 0, ClassName);
    IDecl->setNext(ClassName->getFETokenInfo<ScopedDecl>());
    ClassName->setFETokenInfo(IDecl);
    
  }
  
  // Check that there is no duplicate implementation of this class.
  if (!ObjcImplementations.insert(ClassName))
    Diag(ClassLoc, diag::err_dup_implementation_class, ClassName->getName());
      
  return IMPDecl;
}

void Sema::CheckImplementationIvars(ObjcImplementationDecl *ImpDecl,
                                    ObjcIvarDecl **ivars, unsigned numIvars) {
  assert(ImpDecl && "missing implementation decl");
  ObjcInterfaceDecl* IDecl = getObjCInterfaceDecl(ImpDecl->getIdentifier());
  /// 2nd check is added to accomodate case of non-existing @interface decl.
  /// (legacy objective-c @implementation decl without an @interface decl).
  if (!IDecl || IDecl->ImplicitInterfaceDecl())
    return;
  assert(ivars && "missing @implementation ivars");
  
  // Check interface's Ivar list against those in the implementation.
  // names and types must match.
  //
  ObjcIvarDecl** IntfIvars = IDecl->getIntfDeclIvars();
  int IntfNumIvars = IDecl->getIntfDeclNumIvars();
  unsigned j = 0;
  bool err = false;
  while (numIvars > 0 && IntfNumIvars > 0) {
    ObjcIvarDecl* ImplIvar = ivars[j];
    ObjcIvarDecl* ClsIvar = IntfIvars[j++];
    assert (ImplIvar && "missing implementation ivar");
    assert (ClsIvar && "missing class ivar");
    if (ImplIvar->getCanonicalType() != ClsIvar->getCanonicalType()) {
      Diag(ImplIvar->getLocation(), diag::err_conflicting_ivar_type,
           ImplIvar->getIdentifier()->getName());
      Diag(ClsIvar->getLocation(), diag::err_previous_definition,
           ClsIvar->getIdentifier()->getName());
    }
    // TODO: Two mismatched (unequal width) Ivar bitfields should be diagnosed 
    // as error.
    else if (ImplIvar->getIdentifier() != ClsIvar->getIdentifier()) {
      Diag(ImplIvar->getLocation(), diag::err_conflicting_ivar_name,
           ImplIvar->getIdentifier()->getName());
      Diag(ClsIvar->getLocation(), diag::err_previous_definition,
           ClsIvar->getIdentifier()->getName());
      err = true;
      break;
    }
    --numIvars;
    --IntfNumIvars;
  }
  if (!err && (numIvars > 0 || IntfNumIvars > 0))
    Diag(numIvars > 0 ? ivars[j]->getLocation() : IntfIvars[j]->getLocation(), 
         diag::err_inconsistant_ivar);
      
}

/// CheckProtocolMethodDefs - This routine checks unimpletented methods
/// Declared in protocol, and those referenced by it.
void Sema::CheckProtocolMethodDefs(ObjcProtocolDecl *PDecl,
                                   bool& IncompleteImpl,
             const llvm::DenseSet<Selector> &InsMap,
             const llvm::DenseSet<Selector> &ClsMap) {
  // check unimplemented instance methods.
  ObjcMethodDecl** methods = PDecl->getInstanceMethods();
  for (int j = 0; j < PDecl->getNumInstanceMethods(); j++) {
    if (!InsMap.count(methods[j]->getSelector())) {
      Diag(methods[j]->getLocation(), diag::warn_undef_method_impl,
           methods[j]->getSelector().getName());
      IncompleteImpl = true;
    }
  }
  // check unimplemented class methods
  methods = PDecl->getClassMethods();
  for (int j = 0; j < PDecl->getNumClassMethods(); j++)
    if (!ClsMap.count(methods[j]->getSelector())) {
      Diag(methods[j]->getLocation(), diag::warn_undef_method_impl,
           methods[j]->getSelector().getName());
      IncompleteImpl = true;
    }
  
  // Check on this protocols's referenced protocols, recursively
  ObjcProtocolDecl** RefPDecl = PDecl->getReferencedProtocols();
  for (int i = 0; i < PDecl->getNumReferencedProtocols(); i++)
    CheckProtocolMethodDefs(RefPDecl[i], IncompleteImpl, InsMap, ClsMap);
}

void Sema::ImplMethodsVsClassMethods(ObjcImplementationDecl* IMPDecl, 
                                     ObjcInterfaceDecl* IDecl) {
  llvm::DenseSet<Selector> InsMap;
  // Check and see if instance methods in class interface have been
  // implemented in the implementation class.
  ObjcMethodDecl **methods = IMPDecl->getInstanceMethods();
  for (int i=0; i < IMPDecl->getNumInstanceMethods(); i++) 
    InsMap.insert(methods[i]->getSelector());
  
  bool IncompleteImpl = false;
  methods = IDecl->getInstanceMethods();
  for (int j = 0; j < IDecl->getNumInstanceMethods(); j++)
    if (!InsMap.count(methods[j]->getSelector())) {
      Diag(methods[j]->getLocation(), diag::warn_undef_method_impl,
           methods[j]->getSelector().getName());
      IncompleteImpl = true;
    }
  llvm::DenseSet<Selector> ClsMap;
  // Check and see if class methods in class interface have been
  // implemented in the implementation class.
  methods = IMPDecl->getClassMethods();
  for (int i=0; i < IMPDecl->getNumClassMethods(); i++)
    ClsMap.insert(methods[i]->getSelector());
  
  methods = IDecl->getClassMethods();
  for (int j = 0; j < IDecl->getNumClassMethods(); j++)
    if (!ClsMap.count(methods[j]->getSelector())) {
      Diag(methods[j]->getLocation(), diag::warn_undef_method_impl,
           methods[j]->getSelector().getName());
      IncompleteImpl = true;
    }
  
  // Check the protocol list for unimplemented methods in the @implementation
  // class.
  ObjcProtocolDecl** protocols = IDecl->getReferencedProtocols();
  for (int i = 0; i < IDecl->getNumIntfRefProtocols(); i++)
    CheckProtocolMethodDefs(protocols[i], IncompleteImpl, InsMap, ClsMap);

  if (IncompleteImpl)
    Diag(IMPDecl->getLocation(), diag::warn_incomplete_impl_class, 
         IMPDecl->getName());
}

/// ImplCategoryMethodsVsIntfMethods - Checks that methods declared in the
/// category interface is implemented in the category @implementation.
void Sema::ImplCategoryMethodsVsIntfMethods(ObjcCategoryImplDecl *CatImplDecl,
                                            ObjcCategoryDecl *CatClassDecl) {
  llvm::DenseSet<Selector> InsMap;
  // Check and see if instance methods in category interface have been
  // implemented in its implementation class.
  ObjcMethodDecl **methods = CatImplDecl->getInstanceMethods();
  for (int i=0; i < CatImplDecl->getNumInstanceMethods(); i++)
    InsMap.insert(methods[i]->getSelector());
  
  bool IncompleteImpl = false;
  methods = CatClassDecl->getInstanceMethods();
  for (int j = 0; j < CatClassDecl->getNumInstanceMethods(); j++)
    if (!InsMap.count(methods[j]->getSelector())) {
      Diag(methods[j]->getLocation(), diag::warn_undef_method_impl,
           methods[j]->getSelector().getName());
      IncompleteImpl = true;
    }
  llvm::DenseSet<Selector> ClsMap;
  // Check and see if class methods in category interface have been
  // implemented in its implementation class.
  methods = CatImplDecl->getClassMethods();
  for (int i=0; i < CatImplDecl->getNumClassMethods(); i++)
    ClsMap.insert(methods[i]->getSelector());
  
  methods = CatClassDecl->getClassMethods();
  for (int j = 0; j < CatClassDecl->getNumClassMethods(); j++)
    if (!ClsMap.count(methods[j]->getSelector())) {
      Diag(methods[j]->getLocation(), diag::warn_undef_method_impl,
           methods[j]->getSelector().getName());
      IncompleteImpl = true;
    }
  
  // Check the protocol list for unimplemented methods in the @implementation
  // class.
  ObjcProtocolDecl** protocols = CatClassDecl->getReferencedProtocols();
  for (int i = 0; i < CatClassDecl->getNumReferencedProtocols(); i++) {
    ObjcProtocolDecl* PDecl = protocols[i];
    CheckProtocolMethodDefs(PDecl, IncompleteImpl, InsMap, ClsMap);
  }
  if (IncompleteImpl)
    Diag(CatImplDecl->getLocation(), diag::warn_incomplete_impl_category, 
         CatClassDecl->getName());
}

/// ActOnForwardClassDeclaration - 
Action::DeclTy *
Sema::ActOnForwardClassDeclaration(SourceLocation AtClassLoc,
                                   IdentifierInfo **IdentList, unsigned NumElts) 
{
  llvm::SmallVector<ObjcInterfaceDecl*, 32> Interfaces;
  
  for (unsigned i = 0; i != NumElts; ++i) {
    ObjcInterfaceDecl *IDecl = getObjCInterfaceDecl(IdentList[i]); 
    if (!IDecl) {  // Not already seen?  Make a forward decl.
      IDecl = new ObjcInterfaceDecl(SourceLocation(), 0, IdentList[i], true);
      // Chain & install the interface decl into the identifier.
      IDecl->setNext(IdentList[i]->getFETokenInfo<ScopedDecl>());
      IdentList[i]->setFETokenInfo(IDecl);

      // Remember that this needs to be removed when the scope is popped.
      TUScope->AddDecl(IDecl);
    }

    Interfaces.push_back(IDecl);
  }
  
  return new ObjcClassDecl(AtClassLoc, &Interfaces[0], Interfaces.size());
}


/// ActOnTag - This is invoked when we see 'struct foo' or 'struct {'.  In the
/// former case, Name will be non-null.  In the later case, Name will be null.
/// TagType indicates what kind of tag this is. TK indicates whether this is a
/// reference/declaration/definition of a tag.
Sema::DeclTy *Sema::ActOnTag(Scope *S, unsigned TagType, TagKind TK,
                             SourceLocation KWLoc, IdentifierInfo *Name,
                             SourceLocation NameLoc, AttributeList *Attr) {
  // If this is a use of an existing tag, it must have a name.
  assert((Name != 0 || TK == TK_Definition) &&
         "Nameless record must be a definition!");
  
  Decl::Kind Kind;
  switch (TagType) {
  default: assert(0 && "Unknown tag type!");
  case DeclSpec::TST_struct: Kind = Decl::Struct; break;
  case DeclSpec::TST_union:  Kind = Decl::Union; break;
//case DeclSpec::TST_class:  Kind = Decl::Class; break;
  case DeclSpec::TST_enum:   Kind = Decl::Enum; break;
  }
  
  // If this is a named struct, check to see if there was a previous forward
  // declaration or definition.
  if (TagDecl *PrevDecl = 
          dyn_cast_or_null<TagDecl>(LookupScopedDecl(Name, Decl::IDNS_Tag,
                                                     NameLoc, S))) {
    
    // If this is a use of a previous tag, or if the tag is already declared in
    // the same scope (so that the definition/declaration completes or
    // rementions the tag), reuse the decl.
    if (TK == TK_Reference || S->isDeclScope(PrevDecl)) {
      // Make sure that this wasn't declared as an enum and now used as a struct
      // or something similar.
      if (PrevDecl->getKind() != Kind) {
        Diag(KWLoc, diag::err_use_with_wrong_tag, Name->getName());
        Diag(PrevDecl->getLocation(), diag::err_previous_use);
      }
      
      // If this is a use or a forward declaration, we're good.
      if (TK != TK_Definition)
        return PrevDecl;

      // Diagnose attempts to redefine a tag.
      if (PrevDecl->isDefinition()) {
        Diag(NameLoc, diag::err_redefinition, Name->getName());
        Diag(PrevDecl->getLocation(), diag::err_previous_definition);
        // If this is a redefinition, recover by making this struct be
        // anonymous, which will make any later references get the previous
        // definition.
        Name = 0;
      } else {
        // Okay, this is definition of a previously declared or referenced tag.
        // Move the location of the decl to be the definition site.
        PrevDecl->setLocation(NameLoc);
        return PrevDecl;
      }
    }
    // If we get here, this is a definition of a new struct type in a nested
    // scope, e.g. "struct foo; void bar() { struct foo; }", just create a new
    // type.
  }
  
  // If there is an identifier, use the location of the identifier as the
  // location of the decl, otherwise use the location of the struct/union
  // keyword.
  SourceLocation Loc = NameLoc.isValid() ? NameLoc : KWLoc;
  
  // Otherwise, if this is the first time we've seen this tag, create the decl.
  TagDecl *New;
  switch (Kind) {
  default: assert(0 && "Unknown tag kind!");
  case Decl::Enum:
    // FIXME: Tag decls should be chained to any simultaneous vardecls, e.g.:
    // enum X { A, B, C } D;    D should chain to X.
    New = new EnumDecl(Loc, Name, 0);
    // If this is an undefined enum, warn.
    if (TK != TK_Definition) Diag(Loc, diag::ext_forward_ref_enum);
    break;
  case Decl::Union:
  case Decl::Struct:
  case Decl::Class:
    // FIXME: Tag decls should be chained to any simultaneous vardecls, e.g.:
    // struct X { int A; } D;    D should chain to X.
    New = new RecordDecl(Kind, Loc, Name, 0);
    break;
  }    
  
  // If this has an identifier, add it to the scope stack.
  if (Name) {
    // The scope passed in may not be a decl scope.  Zip up the scope tree until
    // we find one that is.
    while ((S->getFlags() & Scope::DeclScope) == 0)
      S = S->getParent();
    
    // Add it to the decl chain.
    New->setNext(Name->getFETokenInfo<ScopedDecl>());
    Name->setFETokenInfo(New);
    S->AddDecl(New);
  }
  
  return New;
}

/// ActOnField - Each field of a struct/union/class is passed into this in order
/// to create a FieldDecl object for it.
Sema::DeclTy *Sema::ActOnField(Scope *S, DeclTy *TagDecl,
                               SourceLocation DeclStart, 
                               Declarator &D, ExprTy *BitfieldWidth) {
  IdentifierInfo *II = D.getIdentifier();
  Expr *BitWidth = (Expr*)BitfieldWidth;
  SourceLocation Loc = DeclStart;
  if (II) Loc = D.getIdentifierLoc();
  
  // FIXME: Unnamed fields can be handled in various different ways, for
  // example, unnamed unions inject all members into the struct namespace!
  
  
  if (BitWidth) {
    // TODO: Validate.
    //printf("WARNING: BITFIELDS IGNORED!\n");
    
    // 6.7.2.1p3
    // 6.7.2.1p4
    
  } else {
    // Not a bitfield.

    // validate II.
    
  }
  
  QualType T = GetTypeForDeclarator(D, S);
  assert(!T.isNull() && "GetTypeForDeclarator() returned null type");
  bool InvalidDecl = false;

  // C99 6.7.2.1p8: A member of a structure or union may have any type other
  // than a variably modified type.
  if (const VariableArrayType *VAT = T->getAsVariablyModifiedType()) {
    Diag(Loc, diag::err_typecheck_illegal_vla, 
           VAT->getSizeExpr()->getSourceRange());
    InvalidDecl = true;
  }
  // FIXME: Chain fielddecls together.
  FieldDecl *NewFD;
  
  if (isa<RecordDecl>(static_cast<Decl *>(TagDecl)))
    NewFD = new FieldDecl(Loc, II, T);
  else if (isa<ObjcInterfaceDecl>(static_cast<Decl *>(TagDecl))
           || isa<ObjcImplementationDecl>(static_cast<Decl *>(TagDecl)))
    NewFD = new ObjcIvarDecl(Loc, II, T);
  else
    assert(0 && "Sema::ActOnField(): Unknown TagDecl");
    
  if (D.getInvalidType() || InvalidDecl)
    NewFD->setInvalidDecl();
  return NewFD;
}

/// TranslateIvarVisibility - Translate visibility from a token ID to an 
///  AST enum value.
static ObjcIvarDecl::AccessControl
TranslateIvarVisibility(tok::ObjCKeywordKind ivarVisibility) {
  switch (ivarVisibility) {
    case tok::objc_private: return ObjcIvarDecl::Private;
    case tok::objc_public: return ObjcIvarDecl::Public;
    case tok::objc_protected: return ObjcIvarDecl::Protected;
    case tok::objc_package: return ObjcIvarDecl::Package;
    default: assert(false && "Unknown visitibility kind");
  }
}

void Sema::ActOnFields(Scope* S,
                       SourceLocation RecLoc, DeclTy *RecDecl,
                       DeclTy **Fields, unsigned NumFields,
                       tok::ObjCKeywordKind *visibility) {
  Decl *EnclosingDecl = static_cast<Decl*>(RecDecl);
  assert(EnclosingDecl && "missing record or interface decl");
  RecordDecl *Record = dyn_cast<RecordDecl>(EnclosingDecl);
  
  if (Record && Record->isDefinition()) {
    // Diagnose code like:
    //     struct S { struct S {} X; };
    // We discover this when we complete the outer S.  Reject and ignore the
    // outer S.
    Diag(Record->getLocation(), diag::err_nested_redefinition,
         Record->getKindName());
    Diag(RecLoc, diag::err_previous_definition);
    Record->setInvalidDecl();
    return;
  }
  // Verify that all the fields are okay.
  unsigned NumNamedMembers = 0;
  llvm::SmallVector<FieldDecl*, 32> RecFields;
  llvm::SmallSet<const IdentifierInfo*, 32> FieldIDs;
  
  for (unsigned i = 0; i != NumFields; ++i) {
    
    FieldDecl *FD = cast_or_null<FieldDecl>(static_cast<Decl*>(Fields[i]));
    assert(FD && "missing field decl");
    
    // Remember all fields.
    RecFields.push_back(FD);
    
    // Get the type for the field.
    Type *FDTy = FD->getType().getTypePtr();
    
    // If we have visibility info, make sure the AST is set accordingly.
    if (visibility)
      cast<ObjcIvarDecl>(FD)->setAccessControl(
                                TranslateIvarVisibility(visibility[i]));
      
    // C99 6.7.2.1p2 - A field may not be a function type.
    if (FDTy->isFunctionType()) {
      Diag(FD->getLocation(), diag::err_field_declared_as_function, 
           FD->getName());
      FD->setInvalidDecl();
      EnclosingDecl->setInvalidDecl();
      continue;
    }
    // C99 6.7.2.1p2 - A field may not be an incomplete type except...
    if (FDTy->isIncompleteType()) {
      if (!Record) {  // Incomplete ivar type is always an error.
        Diag(FD->getLocation(), diag::err_field_incomplete, FD->getName());
        FD->setInvalidDecl();
        EnclosingDecl->setInvalidDecl();
        continue;
      }
      if (i != NumFields-1 ||                   // ... that the last member ...
          Record->getKind() != Decl::Struct ||  // ... of a structure ...
          !FDTy->isArrayType()) {         //... may have incomplete array type.
        Diag(FD->getLocation(), diag::err_field_incomplete, FD->getName());
        FD->setInvalidDecl();
        EnclosingDecl->setInvalidDecl();
        continue;
      }
      if (NumNamedMembers < 1) {  //... must have more than named member ...
        Diag(FD->getLocation(), diag::err_flexible_array_empty_struct,
             FD->getName());
        FD->setInvalidDecl();
        EnclosingDecl->setInvalidDecl();
        continue;
      }
      // Okay, we have a legal flexible array member at the end of the struct.
      if (Record)
        Record->setHasFlexibleArrayMember(true);
    }
    /// C99 6.7.2.1p2 - a struct ending in a flexible array member cannot be the
    /// field of another structure or the element of an array.
    if (const RecordType *FDTTy = FDTy->getAsRecordType()) {
      if (FDTTy->getDecl()->hasFlexibleArrayMember()) {
        // If this is a member of a union, then entire union becomes "flexible".
        if (Record && Record->getKind() == Decl::Union) {
          Record->setHasFlexibleArrayMember(true);
        } else {
          // If this is a struct/class and this is not the last element, reject
          // it.  Note that GCC supports variable sized arrays in the middle of
          // structures.
          if (i != NumFields-1) {
            Diag(FD->getLocation(), diag::err_variable_sized_type_in_struct,
                 FD->getName());
            FD->setInvalidDecl();
            EnclosingDecl->setInvalidDecl();
            continue;
          }
          // We support flexible arrays at the end of structs in other structs
          // as an extension.
          Diag(FD->getLocation(), diag::ext_flexible_array_in_struct,
               FD->getName());
          if (Record)
            Record->setHasFlexibleArrayMember(true);
        }
      }
    }
    // Keep track of the number of named members.
    if (IdentifierInfo *II = FD->getIdentifier()) {
      // Detect duplicate member names.
      if (!FieldIDs.insert(II)) {
        Diag(FD->getLocation(), diag::err_duplicate_member, II->getName());
        // Find the previous decl.
        SourceLocation PrevLoc;
        for (unsigned i = 0, e = RecFields.size(); ; ++i) {
          assert(i != e && "Didn't find previous def!");
          if (RecFields[i]->getIdentifier() == II) {
            PrevLoc = RecFields[i]->getLocation();
            break;
          }
        }
        Diag(PrevLoc, diag::err_previous_definition);
        FD->setInvalidDecl();
        EnclosingDecl->setInvalidDecl();
        continue;
      }
      ++NumNamedMembers;
    }
  }
 
  // Okay, we successfully defined 'Record'.
  if (Record)
    Record->defineBody(&RecFields[0], RecFields.size());
  else {
    ObjcIvarDecl **ClsFields = 
                    reinterpret_cast<ObjcIvarDecl**>(&RecFields[0]);
    if (isa<ObjcInterfaceDecl>(static_cast<Decl*>(RecDecl)))
      cast<ObjcInterfaceDecl>(static_cast<Decl*>(RecDecl))->
        ObjcAddInstanceVariablesToClass(ClsFields, RecFields.size());
    else if (isa<ObjcImplementationDecl>(static_cast<Decl*>(RecDecl))) {
      ObjcImplementationDecl* IMPDecl = 
        cast<ObjcImplementationDecl>(static_cast<Decl*>(RecDecl));
      assert(IMPDecl && "ActOnFields - missing ObjcImplementationDecl");
      IMPDecl->ObjcAddInstanceVariablesToClassImpl(ClsFields, RecFields.size());
      CheckImplementationIvars(IMPDecl, ClsFields, RecFields.size());
    }
  }
}

/// MatchTwoMethodDeclarations - Checks that two methods have matching type and
/// returns true, or false, accordingly.
/// TODO: Handle protocol list; such as id<p1,p2> in type comparisons
bool Sema:: MatchTwoMethodDeclarations(const ObjcMethodDecl *Method, 
                                       const ObjcMethodDecl *PrevMethod) {
  if (Method->getMethodType().getCanonicalType() !=
      PrevMethod->getMethodType().getCanonicalType())
    return false;
  for (int i = 0; i < Method->getNumParams(); i++) {
    ParmVarDecl *ParamDecl = Method->getParamDecl(i);
    ParmVarDecl *PrevParamDecl = PrevMethod->getParamDecl(i);
    if (ParamDecl->getCanonicalType() != PrevParamDecl->getCanonicalType())
      return false;
  }
  return true;
}

void Sema::ActOnAddMethodsToObjcDecl(Scope* S, DeclTy *classDecl,
                                     DeclTy **allMethods, unsigned allNum) {
  Decl *ClassDecl = static_cast<Decl *>(classDecl);

  // FIXME: If we don't have a ClassDecl, we have an error. I (snaroff) would
  // prefer we always pass in a decl. If the decl has an error, isInvalidDecl()
  // should be true.
  if (!ClassDecl)
    return;
  
  llvm::SmallVector<ObjcMethodDecl*, 32> insMethods;
  llvm::SmallVector<ObjcMethodDecl*, 16> clsMethods;
  
  llvm::DenseMap<Selector, const ObjcMethodDecl*> InsMap;
  llvm::DenseMap<Selector, const ObjcMethodDecl*> ClsMap;
  
  bool isClassDeclaration = 
        (isa<ObjcInterfaceDecl>(ClassDecl) || isa<ObjcCategoryDecl>(ClassDecl));
  
  for (unsigned i = 0; i < allNum; i++ ) {
    ObjcMethodDecl *Method =
      cast_or_null<ObjcMethodDecl>(static_cast<Decl*>(allMethods[i]));
    if (!Method) continue;  // Already issued a diagnostic.
    if (Method->isInstance()) {
      if (isClassDeclaration) {
        /// Check for instance method of the same name with incompatible types
        const ObjcMethodDecl *&PrevMethod = InsMap[Method->getSelector()];
        if (PrevMethod && !MatchTwoMethodDeclarations(Method, PrevMethod)) {
          Diag(Method->getLocation(), diag::error_duplicate_method_decl,
               Method->getSelector().getName());
          Diag(PrevMethod->getLocation(), diag::err_previous_declaration);
        }
        else {
          insMethods.push_back(Method);
          InsMap[Method->getSelector()] = Method;
        }
      }
      else
        insMethods.push_back(Method);
    }
    else {
      if (isClassDeclaration) {
        /// Check for class method of the same name with incompatible types
        const ObjcMethodDecl *&PrevMethod = ClsMap[Method->getSelector()];
        if (PrevMethod && !MatchTwoMethodDeclarations(Method, PrevMethod)) {
          Diag(Method->getLocation(), diag::error_duplicate_method_decl,
               Method->getSelector().getName());
          Diag(PrevMethod->getLocation(), diag::err_previous_declaration);
        }
        else {        
          clsMethods.push_back(Method);
          ClsMap[Method->getSelector()] = Method;
        }
      }
      else
        clsMethods.push_back(Method);
    }
  }
  
  if (ObjcInterfaceDecl *I = dyn_cast<ObjcInterfaceDecl>(ClassDecl)) {
    I->ObjcAddMethods(&insMethods[0], insMethods.size(),
                      &clsMethods[0], clsMethods.size());
  } else if (ObjcProtocolDecl *P = dyn_cast<ObjcProtocolDecl>(ClassDecl)) {
    P->ObjcAddProtoMethods(&insMethods[0], insMethods.size(),
                           &clsMethods[0], clsMethods.size());
  }
  else if (ObjcCategoryDecl *C = dyn_cast<ObjcCategoryDecl>(ClassDecl)) {
    C->ObjcAddCatMethods(&insMethods[0], insMethods.size(),
                         &clsMethods[0], clsMethods.size());
  }
  else if (ObjcImplementationDecl *IC = 
                dyn_cast<ObjcImplementationDecl>(ClassDecl)) {
    IC->ObjcAddImplMethods(&insMethods[0], insMethods.size(),
                           &clsMethods[0], clsMethods.size());
    if (ObjcInterfaceDecl* IDecl = getObjCInterfaceDecl(IC->getIdentifier()))
      ImplMethodsVsClassMethods(IC, IDecl);
  } else {
    ObjcCategoryImplDecl* CatImplClass = cast<ObjcCategoryImplDecl>(ClassDecl);
    CatImplClass->ObjcAddCatImplMethods(&insMethods[0], insMethods.size(),
                                        &clsMethods[0], clsMethods.size());
    ObjcInterfaceDecl* IDecl = CatImplClass->getClassInterface();
    // Find category interface decl and then check that all methods declared
    // in this interface is implemented in the category @implementation.
    if (IDecl) {
      for (ObjcCategoryDecl *Categories = IDecl->getListCategories();
           Categories; Categories = Categories->getNextClassCategory()) {
        if (Categories->getIdentifier() == CatImplClass->getIdentifier()) {
          ImplCategoryMethodsVsIntfMethods(CatImplClass, Categories);
          break;
        }
      }
    }
  }
}

Sema::DeclTy *Sema::ActOnMethodDeclaration(SourceLocation MethodLoc, 
    tok::TokenKind MethodType, TypeTy *ReturnType, Selector Sel,
    // optional arguments. The number of types/arguments is obtained
    // from the Sel.getNumArgs().
    TypeTy **ArgTypes, IdentifierInfo **ArgNames,
    AttributeList *AttrList, tok::ObjCKeywordKind MethodDeclKind) {
  llvm::SmallVector<ParmVarDecl*, 16> Params;

  for (unsigned i = 0; i < Sel.getNumArgs(); i++) {
    // FIXME: arg->AttrList must be stored too!
    QualType argType;
    
    if (ArgTypes[i])
      argType = QualType::getFromOpaquePtr(ArgTypes[i]);
    else
      argType = GetObjcIdType(MethodLoc);
    ParmVarDecl* Param = new ParmVarDecl(SourceLocation(/*FIXME*/), ArgNames[i], 
                                         argType, VarDecl::None, 0);
    Params.push_back(Param);
  }
  QualType resultDeclType;
  
  if (ReturnType)
    resultDeclType = QualType::getFromOpaquePtr(ReturnType);
  else // get the type for "id".
    resultDeclType = GetObjcIdType(MethodLoc);

  ObjcMethodDecl* ObjcMethod =  new ObjcMethodDecl(MethodLoc, Sel,
                                      resultDeclType, 0, -1, AttrList, 
                                      MethodType == tok::minus,
                                      MethodDeclKind == tok::objc_optional ? 
                                      ObjcMethodDecl::Optional : 
                                      ObjcMethodDecl::Required);
  ObjcMethod->setMethodParams(&Params[0], Sel.getNumArgs());
  return ObjcMethod;
}

Sema::DeclTy *Sema::ActOnEnumConstant(Scope *S, DeclTy *theEnumDecl,
                                      DeclTy *lastEnumConst,
                                      SourceLocation IdLoc, IdentifierInfo *Id,
                                      SourceLocation EqualLoc, ExprTy *val) {
  theEnumDecl = theEnumDecl;  // silence unused warning.
  EnumConstantDecl *LastEnumConst =
    cast_or_null<EnumConstantDecl>(static_cast<Decl*>(lastEnumConst));
  Expr *Val = static_cast<Expr*>(val);

  // The scope passed in may not be a decl scope.  Zip up the scope tree until
  // we find one that is.
  while ((S->getFlags() & Scope::DeclScope) == 0)
    S = S->getParent();
  
  // Verify that there isn't already something declared with this name in this
  // scope.
  if (ScopedDecl *PrevDecl = LookupScopedDecl(Id, Decl::IDNS_Ordinary, 
                                              IdLoc, S)) {
    if (S->isDeclScope(PrevDecl)) {
      if (isa<EnumConstantDecl>(PrevDecl))
        Diag(IdLoc, diag::err_redefinition_of_enumerator, Id->getName());
      else
        Diag(IdLoc, diag::err_redefinition, Id->getName());
      Diag(PrevDecl->getLocation(), diag::err_previous_definition);
      // FIXME: Don't leak memory: delete Val;
      return 0;
    }
  }

  llvm::APSInt EnumVal(32);
  QualType EltTy;
  if (Val) {
    // Make sure to promote the operand type to int.
    UsualUnaryConversions(Val);
    
    // C99 6.7.2.2p2: Make sure we have an integer constant expression.
    SourceLocation ExpLoc;
    if (!Val->isIntegerConstantExpr(EnumVal, Context, &ExpLoc)) {
      Diag(ExpLoc, diag::err_enum_value_not_integer_constant_expr, 
           Id->getName());
      // FIXME: Don't leak memory: delete Val;
      Val = 0;  // Just forget about it.
    } else {
      EltTy = Val->getType();
    }
  }
  
  if (!Val) {
    if (LastEnumConst) {
      // Assign the last value + 1.
      EnumVal = LastEnumConst->getInitVal();
      ++EnumVal;

      // Check for overflow on increment.
      if (EnumVal < LastEnumConst->getInitVal())
        Diag(IdLoc, diag::warn_enum_value_overflow);
      
      EltTy = LastEnumConst->getType();
    } else {
      // First value, set to zero.
      EltTy = Context.IntTy;
      EnumVal.zextOrTrunc(
        static_cast<uint32_t>(Context.getTypeSize(EltTy, IdLoc)));
    }
  }
  
  EnumConstantDecl *New = new EnumConstantDecl(IdLoc, Id, EltTy, Val, EnumVal,
                                               LastEnumConst);
  
  // Register this decl in the current scope stack.
  New->setNext(Id->getFETokenInfo<ScopedDecl>());
  Id->setFETokenInfo(New);
  S->AddDecl(New);
  return New;
}

void Sema::ActOnEnumBody(SourceLocation EnumLoc, DeclTy *EnumDeclX,
                         DeclTy **Elements, unsigned NumElements) {
  EnumDecl *Enum = cast<EnumDecl>(static_cast<Decl*>(EnumDeclX));
  assert(!Enum->isDefinition() && "Enum redefinitions can't reach here");
  
  // TODO: If the result value doesn't fit in an int, it must be a long or long
  // long value.  ISO C does not support this, but GCC does as an extension,
  // emit a warning.
  unsigned IntWidth = Context.Target.getIntWidth(Enum->getLocation());
  
  
  // Verify that all the values are okay, compute the size of the values, and
  // reverse the list.
  unsigned NumNegativeBits = 0;
  unsigned NumPositiveBits = 0;
  
  // Keep track of whether all elements have type int.
  bool AllElementsInt = true;
  
  EnumConstantDecl *EltList = 0;
  for (unsigned i = 0; i != NumElements; ++i) {
    EnumConstantDecl *ECD =
      cast_or_null<EnumConstantDecl>(static_cast<Decl*>(Elements[i]));
    if (!ECD) continue;  // Already issued a diagnostic.
    
    // If the enum value doesn't fit in an int, emit an extension warning.
    assert(ECD->getInitVal().getBitWidth() >= IntWidth &&
           "Should have promoted value to int");
    const llvm::APSInt &InitVal = ECD->getInitVal();
    if (InitVal.getBitWidth() > IntWidth) {
      llvm::APSInt V(InitVal);
      V.trunc(IntWidth);
      V.extend(InitVal.getBitWidth());
      if (V != InitVal)
        Diag(ECD->getLocation(), diag::ext_enum_value_not_int,
             InitVal.toString());
    }
    
    // Keep track of the size of positive and negative values.
    if (InitVal.isUnsigned() || !InitVal.isNegative())
      NumPositiveBits = std::max(NumPositiveBits, InitVal.getActiveBits());
    else
      NumNegativeBits = std::max(NumNegativeBits, InitVal.getMinSignedBits());

    // Keep track of whether every enum element has type int (very commmon).
    if (AllElementsInt)
      AllElementsInt = ECD->getType() == Context.IntTy; 
    
    ECD->setNextDeclarator(EltList);
    EltList = ECD;
  }
  
  // Figure out the type that should be used for this enum.
  // FIXME: Support attribute(packed) on enums and -fshort-enums.
  QualType BestType;
  unsigned BestWidth;
  
  if (NumNegativeBits) {
    // If there is a negative value, figure out the smallest integer type (of 
    // int/long/longlong) that fits.
    if (NumNegativeBits <= IntWidth && NumPositiveBits < IntWidth) {
      BestType = Context.IntTy;
      BestWidth = IntWidth;
    } else {
      BestWidth = Context.Target.getLongWidth(Enum->getLocation());
      if (NumNegativeBits <= BestWidth && NumPositiveBits < BestWidth)
        BestType = Context.LongTy;
      else {
        BestWidth = Context.Target.getLongLongWidth(Enum->getLocation());
        if (NumNegativeBits > BestWidth || NumPositiveBits >= BestWidth)
          Diag(Enum->getLocation(), diag::warn_enum_too_large);
        BestType = Context.LongLongTy;
      }
    }
  } else {
    // If there is no negative value, figure out which of uint, ulong, ulonglong
    // fits.
    if (NumPositiveBits <= IntWidth) {
      BestType = Context.UnsignedIntTy;
      BestWidth = IntWidth;
    } else if (NumPositiveBits <=
               (BestWidth = Context.Target.getLongWidth(Enum->getLocation())))
      BestType = Context.UnsignedLongTy;
    else {
      BestWidth = Context.Target.getLongLongWidth(Enum->getLocation());
      assert(NumPositiveBits <= BestWidth &&
             "How could an initializer get larger than ULL?");
      BestType = Context.UnsignedLongLongTy;
    }
  }
  
  // Loop over all of the enumerator constants, changing their types to match
  // the type of the enum if needed.
  for (unsigned i = 0; i != NumElements; ++i) {
    EnumConstantDecl *ECD =
      cast_or_null<EnumConstantDecl>(static_cast<Decl*>(Elements[i]));
    if (!ECD) continue;  // Already issued a diagnostic.

    // Standard C says the enumerators have int type, but we allow, as an
    // extension, the enumerators to be larger than int size.  If each
    // enumerator value fits in an int, type it as an int, otherwise type it the
    // same as the enumerator decl itself.  This means that in "enum { X = 1U }"
    // that X has type 'int', not 'unsigned'.
    if (ECD->getType() == Context.IntTy)
      continue;  // Already int type.

    // Determine whether the value fits into an int.
    llvm::APSInt InitVal = ECD->getInitVal();
    bool FitsInInt;
    if (InitVal.isUnsigned() || !InitVal.isNegative())
      FitsInInt = InitVal.getActiveBits() < IntWidth;
    else
      FitsInInt = InitVal.getMinSignedBits() <= IntWidth;

    // If it fits into an integer type, force it.  Otherwise force it to match
    // the enum decl type.
    QualType NewTy;
    unsigned NewWidth;
    bool NewSign;
    if (FitsInInt) {
      NewTy = Context.IntTy;
      NewWidth = IntWidth;
      NewSign = true;
    } else if (ECD->getType() == BestType) {
      // Already the right type!
      continue;
    } else {
      NewTy = BestType;
      NewWidth = BestWidth;
      NewSign = BestType->isSignedIntegerType();
    }

    // Adjust the APSInt value.
    InitVal.extOrTrunc(NewWidth);
    InitVal.setIsSigned(NewSign);
    ECD->setInitVal(InitVal);
    
    // Adjust the Expr initializer and type.
    ECD->setInitExpr(new ImplicitCastExpr(NewTy, ECD->getInitExpr()));
    ECD->setType(NewTy);
  }
  
  Enum->defineElements(EltList, BestType);
}

void Sema::AddTopLevelDecl(Decl *current, Decl *last) {
  if (!current) return;

  // If this is a top-level decl that is chained to some other (e.g. int A,B,C;)
  // remember this in the LastInGroupList list.
  if (last)
    LastInGroupList.push_back((Decl*)last);
}

void Sema::HandleDeclAttribute(Decl *New, AttributeList *rawAttr) {
  if (strcmp(rawAttr->getAttributeName()->getName(), "vector_size") == 0) {
    if (ValueDecl *vDecl = dyn_cast<ValueDecl>(New)) {
      QualType newType = HandleVectorTypeAttribute(vDecl->getType(), rawAttr);
      if (!newType.isNull()) // install the new vector type into the decl
        vDecl->setType(newType);
    } 
    if (TypedefDecl *tDecl = dyn_cast<TypedefDecl>(New)) {
      QualType newType = HandleVectorTypeAttribute(tDecl->getUnderlyingType(), 
                                                   rawAttr);
      if (!newType.isNull()) // install the new vector type into the decl
        tDecl->setUnderlyingType(newType);
    }
  }
  if (strcmp(rawAttr->getAttributeName()->getName(), "ocu_vector_type") == 0) {
    if (TypedefDecl *tDecl = dyn_cast<TypedefDecl>(New))
      HandleOCUVectorTypeAttribute(tDecl, rawAttr);
    else
      Diag(rawAttr->getAttributeLoc(), 
           diag::err_typecheck_ocu_vector_not_typedef);
  }
  // FIXME: add other attributes...
}

void Sema::HandleDeclAttributes(Decl *New, AttributeList *declspec_prefix,
                                AttributeList *declarator_postfix) {
  while (declspec_prefix) {
    HandleDeclAttribute(New, declspec_prefix);
    declspec_prefix = declspec_prefix->getNext();
  }
  while (declarator_postfix) {
    HandleDeclAttribute(New, declarator_postfix);
    declarator_postfix = declarator_postfix->getNext();
  }
}

void Sema::HandleOCUVectorTypeAttribute(TypedefDecl *tDecl, 
                                        AttributeList *rawAttr) {
  QualType curType = tDecl->getUnderlyingType();
  // check the attribute arugments.
  if (rawAttr->getNumArgs() != 1) {
    Diag(rawAttr->getAttributeLoc(), diag::err_attribute_wrong_number_arguments,
         std::string("1"));
    return;
  }
  Expr *sizeExpr = static_cast<Expr *>(rawAttr->getArg(0));
  llvm::APSInt vecSize(32);
  if (!sizeExpr->isIntegerConstantExpr(vecSize, Context)) {
    Diag(rawAttr->getAttributeLoc(), diag::err_attribute_vector_size_not_int,
         sizeExpr->getSourceRange());
    return;
  }
  // unlike gcc's vector_size attribute, we do not allow vectors to be defined
  // in conjunction with complex types (pointers, arrays, functions, etc.).
  Type *canonType = curType.getCanonicalType().getTypePtr();
  if (!(canonType->isIntegerType() || canonType->isRealFloatingType())) {
    Diag(rawAttr->getAttributeLoc(), diag::err_attribute_invalid_vector_type,
         curType.getCanonicalType().getAsString());
    return;
  }
  // unlike gcc's vector_size attribute, the size is specified as the 
  // number of elements, not the number of bytes.
  unsigned vectorSize = static_cast<unsigned>(vecSize.getZExtValue()); 
  
  if (vectorSize == 0) {
    Diag(rawAttr->getAttributeLoc(), diag::err_attribute_zero_size,
         sizeExpr->getSourceRange());
    return;
  }
  // Instantiate/Install the vector type, the number of elements is > 0.
  tDecl->setUnderlyingType(Context.getOCUVectorType(curType, vectorSize));
  // Remember this typedef decl, we will need it later for diagnostics.
  OCUVectorDecls.push_back(tDecl);
}

QualType Sema::HandleVectorTypeAttribute(QualType curType, 
                                         AttributeList *rawAttr) {
  // check the attribute arugments.
  if (rawAttr->getNumArgs() != 1) {
    Diag(rawAttr->getAttributeLoc(), diag::err_attribute_wrong_number_arguments,
         std::string("1"));
    return QualType();
  }
  Expr *sizeExpr = static_cast<Expr *>(rawAttr->getArg(0));
  llvm::APSInt vecSize(32);
  if (!sizeExpr->isIntegerConstantExpr(vecSize, Context)) {
    Diag(rawAttr->getAttributeLoc(), diag::err_attribute_vector_size_not_int,
         sizeExpr->getSourceRange());
    return QualType();
  }
  // navigate to the base type - we need to provide for vector pointers, 
  // vector arrays, and functions returning vectors.
  Type *canonType = curType.getCanonicalType().getTypePtr();
  
  if (canonType->isPointerType() || canonType->isArrayType() ||
      canonType->isFunctionType()) {
    assert(1 && "HandleVector(): Complex type construction unimplemented");
    /* FIXME: rebuild the type from the inside out, vectorizing the inner type.
        do {
          if (PointerType *PT = dyn_cast<PointerType>(canonType))
            canonType = PT->getPointeeType().getTypePtr();
          else if (ArrayType *AT = dyn_cast<ArrayType>(canonType))
            canonType = AT->getElementType().getTypePtr();
          else if (FunctionType *FT = dyn_cast<FunctionType>(canonType))
            canonType = FT->getResultType().getTypePtr();
        } while (canonType->isPointerType() || canonType->isArrayType() ||
                 canonType->isFunctionType());
    */
  }
  // the base type must be integer or float.
  if (!(canonType->isIntegerType() || canonType->isRealFloatingType())) {
    Diag(rawAttr->getAttributeLoc(), diag::err_attribute_invalid_vector_type,
         curType.getCanonicalType().getAsString());
    return QualType();
  }
  unsigned typeSize = static_cast<unsigned>(
    Context.getTypeSize(curType, rawAttr->getAttributeLoc()));
  // vecSize is specified in bytes - convert to bits.
  unsigned vectorSize = static_cast<unsigned>(vecSize.getZExtValue() * 8); 
  
  // the vector size needs to be an integral multiple of the type size.
  if (vectorSize % typeSize) {
    Diag(rawAttr->getAttributeLoc(), diag::err_attribute_invalid_size,
         sizeExpr->getSourceRange());
    return QualType();
  }
  if (vectorSize == 0) {
    Diag(rawAttr->getAttributeLoc(), diag::err_attribute_zero_size,
         sizeExpr->getSourceRange());
    return QualType();
  }
  // Since OpenCU requires 3 element vectors (OpenCU 5.1.2), we don't restrict
  // the number of elements to be a power of two (unlike GCC).
  // Instantiate the vector type, the number of elements is > 0.
  return Context.getVectorType(curType, vectorSize/typeSize);
}

