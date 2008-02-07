//===--- SemaDecl.cpp - Semantic Analysis for Declarations ----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file implements semantic analysis for declarations.
//
//===----------------------------------------------------------------------===//

#include "Sema.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Builtins.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/Type.h"
#include "clang/Parse/DeclSpec.h"
#include "clang/Parse/Scope.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Basic/SourceManager.h"
// FIXME: layering (ideally, Sema shouldn't be dependent on Lex API's)
#include "clang/Lex/Preprocessor.h"
#include "clang/Lex/HeaderSearch.h" 
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
  if (isa<TypedefDecl>(IIDecl) || isa<ObjCInterfaceDecl>(IIDecl))
    return IIDecl;
  if (ObjCCompatibleAliasDecl *ADecl = 
      dyn_cast<ObjCCompatibleAliasDecl>(IIDecl))
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

/// LookupInterfaceDecl - Lookup interface declaration in the scope chain.
/// Return the first declaration found (which may or may not be a class
/// declaration. Caller is responsible for handling the none-class case.
/// Bypassing the alias of a class by returning the aliased class.
ScopedDecl *Sema::LookupInterfaceDecl(IdentifierInfo *ClassName) {
  ScopedDecl *IDecl;
  // Scan up the scope chain looking for a decl that matches this identifier
  // that is in the appropriate namespace.
  for (IDecl = ClassName->getFETokenInfo<ScopedDecl>(); IDecl; 
       IDecl = IDecl->getNext())
    if (IDecl->getIdentifierNamespace() == Decl::IDNS_Ordinary)
      break;
  
  if (ObjCCompatibleAliasDecl *ADecl =
      dyn_cast_or_null<ObjCCompatibleAliasDecl>(IDecl))
    return ADecl->getClassInterface();
  return IDecl;
}

/// getObjCInterfaceDecl - Look up a for a class declaration in the scope.
/// return 0 if one not found.
ObjCInterfaceDecl *Sema::getObjCInterfaceDecl(IdentifierInfo *Id) {
  ScopedDecl *IdDecl = LookupInterfaceDecl(Id);
  return cast_or_null<ObjCInterfaceDecl>(IdDecl);
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
      Context.Target.DiagnoseNonPortability(Context.getFullLoc(IdLoc),
                                            diag::port_target_builtin_use);
    }
    // If this is a builtin on this (or all) targets, create the decl.
    if (unsigned BuiltinID = II->getBuiltinID())
      return LazilyCreateBuiltin(II, BuiltinID, S);
  }
  return 0;
}

void Sema::InitBuiltinVaListType()
{
  if (!Context.getBuiltinVaListType().isNull())
    return;
  
  IdentifierInfo *VaIdent = &Context.Idents.get("__builtin_va_list");
  ScopedDecl *VaDecl = LookupScopedDecl(VaIdent, Decl::IDNS_Ordinary, 
                                          SourceLocation(), TUScope);
  TypedefDecl *VaTypedef = cast<TypedefDecl>(VaDecl);
  Context.setBuiltinVaListType(Context.getTypedefType(VaTypedef));
}

/// LazilyCreateBuiltin - The specified Builtin-ID was first used at file scope.
/// lazily create a decl for it.
ScopedDecl *Sema::LazilyCreateBuiltin(IdentifierInfo *II, unsigned bid,
                                      Scope *S) {
  Builtin::ID BID = (Builtin::ID)bid;

  if (BID == Builtin::BI__builtin_va_start ||
       BID == Builtin::BI__builtin_va_copy ||
       BID == Builtin::BI__builtin_va_end)
    InitBuiltinVaListType();
    
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
  
  // Allow multiple definitions for ObjC built-in typedefs.
  // FIXME: Verify the underlying types are equivalent!
  if (getLangOptions().ObjC1 && isBuiltinObjCType(New))
    return Old;
  
  // Redeclaration of a type is a constraint violation (6.7.2.3p1).
  // Apparently GCC, Intel, and Sun all silently ignore the redeclaration if
  // *either* declaration is in a system header. The code below implements
  // this adhoc compatibility rule. FIXME: The following code will not
  // work properly when compiling ".i" files (containing preprocessed output).
  SourceManager &SrcMgr = Context.getSourceManager();
  const FileEntry *OldDeclFile = SrcMgr.getFileEntryForLoc(Old->getLocation());
  const FileEntry *NewDeclFile = SrcMgr.getFileEntryForLoc(New->getLocation());
  HeaderSearch &HdrInfo = PP.getHeaderSearchInfo();
  DirectoryLookup::DirType OldDirType = HdrInfo.getFileDirFlavor(OldDeclFile);
  DirectoryLookup::DirType NewDirType = HdrInfo.getFileDirFlavor(NewDeclFile);
  
  if ((OldDirType == DirectoryLookup::ExternCSystemHeaderDir || 
       NewDirType == DirectoryLookup::ExternCSystemHeaderDir) ||
      getLangOptions().Microsoft)
    return New;
    
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
  
  QualType OldQType = Old->getCanonicalType();
  QualType NewQType = New->getCanonicalType();
  
  // This is not right, but it's a start.
  // If Old is a function prototype with no defined arguments we only compare 
  // the return type;  If arguments are defined on the prototype we validate the
  // entire function type.
  // FIXME: We should link up decl objects here.
  if (Old->getBody() == 0) {
    if (OldQType.getTypePtr()->getTypeClass() == Type::FunctionNoProto && 
        Old->getResultType() == New->getResultType())
      return New;
  }
  // Function types need to be compatible, not identical. This handles
  // duplicate function decls like "void f(int); void f(enum X);" properly.
  if (Context.functionTypesAreCompatible(OldQType, NewQType))
    return New;

  // A function that has already been declared has been redeclared or defined
  // with a different type- show appropriate diagnostic
  diag::kind PrevDiag = Old->getBody() ? diag::err_previous_definition :
                                         diag::err_previous_declaration; 

  // TODO: CHECK FOR CONFLICTS, multiple decls with same name in one scope.
  // TODO: This is totally simplistic.  It should handle merging functions
  // together etc, merging extern int X; int X; ...
  Diag(New->getLocation(), diag::err_conflicting_types, New->getName());
  Diag(Old->getLocation(), PrevDiag);
  return New;
}


/// hasUndefinedLength - Used by equivalentArrayTypes to determine whether the
/// the outermost VariableArrayType has no size defined.
static bool hasUndefinedLength(const ArrayType *Array) {
  const VariableArrayType *VAT = Array->getAsVariableArrayType();
  return VAT && !VAT->getSizeExpr();
}

/// equivalentArrayTypes - Used to determine whether two array types are 
/// equivalent.
/// We need to check this explicitly as an incomplete array definition is
/// considered a VariableArrayType, so will not match a complete array 
/// definition that would be otherwise equivalent.
static bool areEquivalentArrayTypes(QualType NewQType, QualType OldQType) {
  const ArrayType *NewAT = NewQType->getAsArrayType();
  const ArrayType *OldAT = OldQType->getAsArrayType();

  if (!NewAT || !OldAT)
    return false;
  
  // If either (or both) array types in incomplete we need to strip off the
  // outer VariableArrayType.  Once the outer VAT is removed the remaining
  // types must be identical if the array types are to be considered 
  // equivalent.
  // eg. int[][1] and int[1][1] become
  //     VAT(null, CAT(1, int)) and CAT(1, CAT(1, int))
  // removing the outermost VAT gives
  //     CAT(1, int) and CAT(1, int)
  // which are equal, therefore the array types are equivalent.
  if (hasUndefinedLength(NewAT) || hasUndefinedLength(OldAT)) {
    if (NewAT->getIndexTypeQualifier() != OldAT->getIndexTypeQualifier())
      return false;
    NewQType = NewAT->getElementType().getCanonicalType();
    OldQType = OldAT->getElementType().getCanonicalType();
  }
  
  return NewQType == OldQType;
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
  // Verify the types match.
  if (Old->getCanonicalType() != New->getCanonicalType() && 
      !areEquivalentArrayTypes(New->getCanonicalType(), Old->getCanonicalType())) {
    Diag(New->getLocation(), diag::err_redefinition, New->getName());
    Diag(Old->getLocation(), diag::err_previous_definition);
    return New;
  }
  // C99 6.2.2p4: Check if we have a static decl followed by a non-static.
  if (New->getStorageClass() == VarDecl::Static &&
      (Old->getStorageClass() == VarDecl::None ||
       Old->getStorageClass() == VarDecl::Extern)) {
    Diag(New->getLocation(), diag::err_static_non_static, New->getName());
    Diag(Old->getLocation(), diag::err_previous_definition);
    return New;
  }
  // C99 6.2.2p4: Check if we have a non-static decl followed by a static.
  if (New->getStorageClass() != VarDecl::Static &&
      Old->getStorageClass() == VarDecl::Static) {
    Diag(New->getLocation(), diag::err_non_static_static, New->getName());
    Diag(Old->getLocation(), diag::err_previous_definition);
    return New;
  }
  // We've verified the types match, now handle "tentative" definitions.
  FileVarDecl *OldFSDecl = dyn_cast<FileVarDecl>(Old);
  FileVarDecl *NewFSDecl = dyn_cast<FileVarDecl>(New);
  
  if (OldFSDecl && NewFSDecl) {
    // Handle C "tentative" external object definitions (C99 6.9.2).
    bool OldIsTentative = false;
    bool NewIsTentative = false;
    
    if (!OldFSDecl->getInit() &&
        (OldFSDecl->getStorageClass() == VarDecl::None ||
         OldFSDecl->getStorageClass() == VarDecl::Static))
      OldIsTentative = true;
      
    // FIXME: this check doesn't work (since the initializer hasn't been
    // attached yet). This check should be moved to FinalizeDeclaratorGroup.
    // Unfortunately, by the time we get to FinializeDeclaratorGroup, we've 
    // thrown out the old decl.
    if (!NewFSDecl->getInit() &&
        (NewFSDecl->getStorageClass() == VarDecl::None ||
         NewFSDecl->getStorageClass() == VarDecl::Static))
      ; // change to NewIsTentative = true; once the code is moved.
    
    if (NewIsTentative || OldIsTentative)
      return New;
  }
  if (Old->getStorageClass() != VarDecl::Extern &&
      New->getStorageClass() != VarDecl::Extern) {
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
  
  return dyn_cast_or_null<TagDecl>(static_cast<Decl *>(DS.getTypeRep()));
}

bool Sema::CheckSingleInitializer(Expr *&Init, QualType DeclType) {  
  // Get the type before calling CheckSingleAssignmentConstraints(), since
  // it can promote the expression.
  QualType InitType = Init->getType(); 
  
  AssignConvertType ConvTy = CheckSingleAssignmentConstraints(DeclType, Init);
  return DiagnoseAssignmentResult(ConvTy, Init->getLocStart(), DeclType,
                                  InitType, Init, "initializing");
}

bool Sema::CheckInitExpr(Expr *expr, InitListExpr *IList, unsigned slot,
                         QualType ElementType) {
  Expr *savExpr = expr; // Might be promoted by CheckSingleInitializer.
  if (CheckSingleInitializer(expr, ElementType))
    return true; // types weren't compatible.
  
  if (savExpr != expr) // The type was promoted, update initializer list.
    IList->setInit(slot, expr);
  return false;
}

bool Sema::CheckStringLiteralInit(StringLiteral *strLiteral, QualType &DeclT) {
  if (const VariableArrayType *VAT = DeclT->getAsIncompleteArrayType()) {
    // C99 6.7.8p14. We have an array of character type with unknown size 
    // being initialized to a string literal.
    llvm::APSInt ConstVal(32);
    ConstVal = strLiteral->getByteLength() + 1;
    // Return a new array type (C99 6.7.8p22).
    DeclT = Context.getConstantArrayType(VAT->getElementType(), ConstVal, 
                                         ArrayType::Normal, 0);
  } else if (const ConstantArrayType *CAT = DeclT->getAsConstantArrayType()) {
    // C99 6.7.8p14. We have an array of character type with known size.
    if (strLiteral->getByteLength() > (unsigned)CAT->getMaximumElements())
      Diag(strLiteral->getSourceRange().getBegin(),
           diag::warn_initializer_string_for_char_array_too_long,
           strLiteral->getSourceRange());
  } else {
    assert(0 && "HandleStringLiteralInit(): Invalid array type");
  }
  // Set type from "char *" to "constant array of char".
  strLiteral->setType(DeclT);
  // For now, we always return false (meaning success).
  return false;
}

StringLiteral *Sema::IsStringLiteralInit(Expr *Init, QualType DeclType) {
  const ArrayType *AT = DeclType->getAsArrayType();
  if (AT && AT->getElementType()->isCharType()) {
    return dyn_cast<StringLiteral>(Init);
  }
  return 0;
}

// CheckInitializerListTypes - Checks the types of elements of an initializer
// list. This function is recursive: it calls itself to initialize subelements
// of aggregate types.  Note that the topLevel parameter essentially refers to
// whether this expression "owns" the initializer list passed in, or if this
// initialization is taking elements out of a parent initializer.  Each
// call to this function adds zero or more to startIndex, reports any errors,
// and returns true if it found any inconsistent types.
bool Sema::CheckInitializerListTypes(InitListExpr*& IList, QualType &DeclType,
                                     bool topLevel, unsigned& startIndex) {
  bool hadError = false;

  if (DeclType->isScalarType()) {
    // The simplest case: initializing a single scalar
    if (topLevel) {
      Diag(IList->getLocStart(), diag::warn_braces_around_scalar_init, 
           IList->getSourceRange());
    }
    if (startIndex < IList->getNumInits()) {
      Expr* expr = IList->getInit(startIndex);
      if (InitListExpr *SubInitList = dyn_cast<InitListExpr>(expr)) {
        // FIXME: Should an error be reported here instead?
        unsigned newIndex = 0;
        CheckInitializerListTypes(SubInitList, DeclType, true, newIndex);
      } else {
        hadError |= CheckInitExpr(expr, IList, startIndex, DeclType);
      }
      ++startIndex;
    }
    // FIXME: Should an error be reported for empty initializer list + scalar?
  } else if (DeclType->isVectorType()) {
    if (startIndex < IList->getNumInits()) {
      const VectorType *VT = DeclType->getAsVectorType();
      int maxElements = VT->getNumElements();
      QualType elementType = VT->getElementType();
      
      for (int i = 0; i < maxElements; ++i) {
        // Don't attempt to go past the end of the init list
        if (startIndex >= IList->getNumInits())
          break;
        Expr* expr = IList->getInit(startIndex);
        if (InitListExpr *SubInitList = dyn_cast<InitListExpr>(expr)) {
          unsigned newIndex = 0;
          hadError |= CheckInitializerListTypes(SubInitList, elementType, 
                                                true, newIndex);
          ++startIndex;
        } else {
          hadError |= CheckInitializerListTypes(IList, elementType, 
                                                false, startIndex);
        }
      }
    }
  } else if (DeclType->isAggregateType() || DeclType->isUnionType()) {
    if (DeclType->isStructureType() || DeclType->isUnionType()) {
      if (startIndex < IList->getNumInits() && !topLevel &&
          Context.typesAreCompatible(IList->getInit(startIndex)->getType(), 
                                     DeclType)) {
        // We found a compatible struct; per the standard, this initializes the
        // struct.  (The C standard technically says that this only applies for
        // initializers for declarations with automatic scope; however, this
        // construct is unambiguous anyway because a struct cannot contain
        // a type compatible with itself. We'll output an error when we check
        // if the initializer is constant.)
        // FIXME: Is a call to CheckSingleInitializer required here?
        ++startIndex;
      } else {
        RecordDecl* structDecl = DeclType->getAsRecordType()->getDecl();
        // If structDecl is a forward declaration, this loop won't do anything;
        // That's okay, because an error should get printed out elsewhere. It
        // might be worthwhile to skip over the rest of the initializer, though.
        int numMembers = structDecl->getNumMembers() -
                         structDecl->hasFlexibleArrayMember();
        for (int i = 0; i < numMembers; i++) {
          // Don't attempt to go past the end of the init list
          if (startIndex >= IList->getNumInits())
            break;
          FieldDecl * curField = structDecl->getMember(i);
          if (!curField->getIdentifier()) {
            // Don't initialize unnamed fields, e.g. "int : 20;"
            continue;
          }
          QualType fieldType = curField->getType();
          Expr* expr = IList->getInit(startIndex);
          if (InitListExpr *SubInitList = dyn_cast<InitListExpr>(expr)) {
            unsigned newStart = 0;
            hadError |= CheckInitializerListTypes(SubInitList, fieldType, 
                                                  true, newStart);
            ++startIndex;
          } else {
            hadError |= CheckInitializerListTypes(IList, fieldType, 
                                                  false, startIndex);
          }
          if (DeclType->isUnionType())
            break;
        }
        // FIXME: Implement flexible array initialization GCC extension (it's a 
        // really messy extension to implement, unfortunately...the necessary
        // information isn't actually even here!)
      }
    } else if (DeclType->isArrayType()) {
      // Check for the special-case of initializing an array with a string.
      if (startIndex < IList->getNumInits()) {
        if (StringLiteral *lit = IsStringLiteralInit(IList->getInit(startIndex), 
                                                     DeclType)) {
          CheckStringLiteralInit(lit, DeclType);
          ++startIndex;
          if (topLevel && startIndex < IList->getNumInits()) {
            // We have leftover initializers; warn
            Diag(IList->getInit(startIndex)->getLocStart(), 
                 diag::err_excess_initializers_in_char_array_initializer, 
                 IList->getInit(startIndex)->getSourceRange());
          }
          return false;
        }
      }
      int maxElements;
      if (const VariableArrayType *VAT = DeclType->getAsVariableArrayType()) {
        // FIXME: use a proper constant
        maxElements = 0x7FFFFFFF;
        // Check for VLAs; in standard C it would be possible to check this
        // earlier, but I don't know where clang accepts VLAs (gcc accepts
        // them in all sorts of strange places).
        if (const Expr *expr = VAT->getSizeExpr()) {
          Diag(expr->getLocStart(), diag::err_variable_object_no_init,
               expr->getSourceRange());
          hadError = true;
        }
      } else {
        const ConstantArrayType *CAT = DeclType->getAsConstantArrayType();
        maxElements = static_cast<int>(CAT->getSize().getZExtValue());
      }
      QualType elementType = DeclType->getAsArrayType()->getElementType();
      int numElements = 0;
      for (int i = 0; i < maxElements; ++i, ++numElements) {
        // Don't attempt to go past the end of the init list
        if (startIndex >= IList->getNumInits())
          break;
        Expr* expr = IList->getInit(startIndex);
        if (InitListExpr *SubInitList = dyn_cast<InitListExpr>(expr)) {
          unsigned newIndex = 0;
          hadError |= CheckInitializerListTypes(SubInitList, elementType, 
                                                true, newIndex);
          ++startIndex;
        } else {
          hadError |= CheckInitializerListTypes(IList, elementType, 
                                                false, startIndex);
        }
      }
      if (DeclType->getAsVariableArrayType()) {
        // If this is an incomplete array type, the actual type needs to
        // be calculated here
        if (numElements == 0) {
          // Sizing an array implicitly to zero is not allowed
          // (It could in theory be allowed, but it doesn't really matter.)
          Diag(IList->getLocStart(),
               diag::err_at_least_one_initializer_needed_to_size_array);
          hadError = true;
        } else {
          llvm::APSInt ConstVal(32);
          ConstVal = numElements;
          DeclType = Context.getConstantArrayType(elementType, ConstVal, 
                                                  ArrayType::Normal, 0);
        }
      }
    } else {
      assert(0 && "Aggregate that isn't a function or array?!");
    }
  } else {
    // In C, all types are either scalars or aggregates, but
    // additional handling is needed here for C++ (and possibly others?). 
    assert(0 && "Unsupported initializer type");
  }

  // If this init list is a base list, we set the type; an initializer doesn't
  // fundamentally have a type, but this makes the ASTs a bit easier to read
  if (topLevel)
    IList->setType(DeclType);

  if (topLevel && startIndex < IList->getNumInits()) {
    // We have leftover initializers; warn
    Diag(IList->getInit(startIndex)->getLocStart(), 
         diag::warn_excess_initializers, 
         IList->getInit(startIndex)->getSourceRange());
  }
  return hadError;
}

bool Sema::CheckInitializerTypes(Expr *&Init, QualType &DeclType) {  
  // C99 6.7.8p3: The type of the entity to be initialized shall be an array
  // of unknown size ("[]") or an object type that is not a variable array type.
  if (const VariableArrayType *VAT = DeclType->getAsVariablyModifiedType())
    return Diag(VAT->getSizeExpr()->getLocStart(), 
                diag::err_variable_object_no_init, 
                VAT->getSizeExpr()->getSourceRange());
  
  InitListExpr *InitList = dyn_cast<InitListExpr>(Init);
  if (!InitList) {
    // FIXME: Handle wide strings
    if (StringLiteral *strLiteral = IsStringLiteralInit(Init, DeclType))
      return CheckStringLiteralInit(strLiteral, DeclType);
    return CheckSingleInitializer(Init, DeclType);
  }
  unsigned newIndex = 0;
  return CheckInitializerListTypes(InitList, DeclType, true, newIndex);
}

Sema::DeclTy *
Sema::ActOnDeclarator(Scope *S, Declarator &D, DeclTy *lastDecl) {
  ScopedDecl *LastDeclarator = dyn_cast_or_null<ScopedDecl>((Decl *)lastDecl);
  IdentifierInfo *II = D.getIdentifier();
  
  // All of these full declarators require an identifier.  If it doesn't have
  // one, the ParsedFreeStandingDeclSpec action should be used.
  if (II == 0) {
    Diag(D.getDeclSpec().getSourceRange().getBegin(),
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
  ScopedDecl *New;
  bool InvalidDecl = false;
  
  QualType R = GetTypeForDeclarator(D, S);
  assert(!R.isNull() && "GetTypeForDeclarator() returned null type");

  if (D.getDeclSpec().getStorageClassSpec() == DeclSpec::SCS_typedef) {
    TypedefDecl *NewTD = ParseTypedefDecl(S, D, R, LastDeclarator);
    if (!NewTD) return 0;

    // Handle attributes prior to checking for duplicates in MergeVarDecl
    HandleDeclAttributes(NewTD, D.getDeclSpec().getAttributes(),
                         D.getAttributes());
    // Merge the decl with the existing one if appropriate. If the decl is
    // in an outer scope, it isn't the same thing.
    if (PrevDecl && S->isDeclScope(PrevDecl)) {
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
  } else if (R.getTypePtr()->isFunctionType()) {
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
      case DeclSpec::SCS_private_extern: SC = FunctionDecl::PrivateExtern;break;
    }

    FunctionDecl *NewFD = new FunctionDecl(D.getIdentifierLoc(), II, R, SC,
                                           D.getDeclSpec().isInlineSpecified(),
                                           LastDeclarator,
                                           D.getDeclSpec().getAttributes());

    // Transfer ownership of DeclSpec attributes to FunctionDecl
    D.getDeclSpec().clearAttributes();
    
    // Merge the decl with the existing one if appropriate. Since C functions
    // are in a flat namespace, make sure we consider decls in outer scopes.
    if (PrevDecl) {
      NewFD = MergeFunctionDecl(NewFD, PrevDecl);
      if (NewFD == 0) return 0;
    }
    New = NewFD;
  } else {
    if (R.getTypePtr()->isObjCInterfaceType()) {
      Diag(D.getIdentifierLoc(), diag::err_statically_allocated_object,
           D.getIdentifier()->getName());
      InvalidDecl = true;
    }

    VarDecl *NewVD;
    VarDecl::StorageClass SC;
    switch (D.getDeclSpec().getStorageClassSpec()) {
      default: assert(0 && "Unknown storage class!");
      case DeclSpec::SCS_unspecified:    SC = VarDecl::None; break;
      case DeclSpec::SCS_extern:         SC = VarDecl::Extern; break;
      case DeclSpec::SCS_static:         SC = VarDecl::Static; break;
      case DeclSpec::SCS_auto:           SC = VarDecl::Auto; break;
      case DeclSpec::SCS_register:       SC = VarDecl::Register; break;
      case DeclSpec::SCS_private_extern: SC = VarDecl::PrivateExtern; break;
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
     
    // Merge the decl with the existing one if appropriate. If the decl is
    // in an outer scope, it isn't the same thing.
    if (PrevDecl && S->isDeclScope(PrevDecl)) {
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
  // If any semantic error occurred, mark the decl as invalid.
  if (D.getInvalidType() || InvalidDecl)
    New->setInvalidDecl();
  
  return New;
}

bool Sema::CheckForConstantInitializer(Expr *Init, QualType DclT) {
  SourceLocation loc;
  // FIXME: Remove the isReference check and handle assignment to a reference.
  if (!DclT->isReferenceType() && !Init->isConstantExpr(Context, &loc)) { 
    assert(loc.isValid() && "isConstantExpr didn't return a loc!");
    Diag(loc, diag::err_init_element_not_constant, Init->getSourceRange());
    return true;
  }
  return false;
}

void Sema::AddInitializerToDecl(DeclTy *dcl, ExprTy *init) {
  Decl *RealDecl = static_cast<Decl *>(dcl);
  Expr *Init = static_cast<Expr *>(init);
  assert(Init && "missing initializer");
  
  // If there is no declaration, there was an error parsing it.  Just ignore
  // the initializer.
  if (RealDecl == 0) {
    delete Init;
    return;
  }
  
  VarDecl *VDecl = dyn_cast<VarDecl>(RealDecl);
  if (!VDecl) {
    Diag(dyn_cast<ScopedDecl>(RealDecl)->getLocation(), 
         diag::err_illegal_initializer);
    RealDecl->setInvalidDecl();
    return;
  }  
  // Get the decls type and save a reference for later, since
  // CheckInitializerTypes may change it.
  QualType DclT = VDecl->getType(), SavT = DclT;
  if (BlockVarDecl *BVD = dyn_cast<BlockVarDecl>(VDecl)) {
    VarDecl::StorageClass SC = BVD->getStorageClass();
    if (SC == VarDecl::Extern) { // C99 6.7.8p5
      Diag(VDecl->getLocation(), diag::err_block_extern_cant_init);
      BVD->setInvalidDecl();
    } else if (!BVD->isInvalidDecl()) {
      if (CheckInitializerTypes(Init, DclT))
        BVD->setInvalidDecl();
      if (SC == VarDecl::Static) // C99 6.7.8p4.
        CheckForConstantInitializer(Init, DclT);
    }
  } else if (FileVarDecl *FVD = dyn_cast<FileVarDecl>(VDecl)) {
    if (FVD->getStorageClass() == VarDecl::Extern)
      Diag(VDecl->getLocation(), diag::warn_extern_init);
    if (!FVD->isInvalidDecl())
      if (CheckInitializerTypes(Init, DclT))
        FVD->setInvalidDecl();
    
    // C99 6.7.8p4. All file scoped initializers need to be constant.
    CheckForConstantInitializer(Init, DclT);
  }
  // If the type changed, it means we had an incomplete type that was
  // completed by the initializer. For example: 
  //   int ary[] = { 1, 3, 5 };
  // "ary" transitions from a VariableArrayType to a ConstantArrayType.
  if (!VDecl->isInvalidDecl() && (DclT != SavT)) {
    VDecl->setType(DclT);
    Init->setType(DclT);
  }
    
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
    if (FVD && !FVD->getInit() && (FVD->getStorageClass() == VarDecl::Static || 
                                   FVD->getStorageClass() == VarDecl::None)) {
      const VariableArrayType *VAT = T->getAsVariableArrayType();
      
      if (VAT && VAT->getSizeExpr() == 0) {
        // C99 6.9.2 (p2, p5): Implicit initialization causes an incomplete
        // array to be completed. Don't issue a diagnostic.
      } else if (T->isIncompleteType()) {
        // C99 6.9.2p3: If the declaration of an identifier for an object is
        // a tentative definition and has internal linkage (C99 6.2.2p3), the  
        // declared type shall not be an incomplete type.
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
Sema::ActOnParamDeclarator(struct DeclaratorChunk::ParamInfo &PI, Scope *FnScope) 
{
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
  if (const ArrayType *AT = parmDeclType->getAsArrayType()) {
    // int x[restrict 4] ->  int *restrict
    parmDeclType = Context.getPointerType(AT->getElementType());
    parmDeclType = parmDeclType.getQualifiedType(AT->getIndexTypeQualifier());
  } else if (parmDeclType->isFunctionType())
    parmDeclType = Context.getPointerType(parmDeclType);
  
  ParmVarDecl *New = new ParmVarDecl(PI.IdentLoc, II, parmDeclType, 
                                     VarDecl::None, 0, PI.AttrList);
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

  // See if this is a redefinition.
  ScopedDecl *PrevDcl = LookupScopedDecl(D.getIdentifier(), Decl::IDNS_Ordinary,
                                         D.getIdentifierLoc(), GlobalScope);
  if (FunctionDecl *FD = dyn_cast_or_null<FunctionDecl>(PrevDcl)) {
    if (FD->getBody()) {
      Diag(D.getIdentifierLoc(), diag::err_redefinition, 
           D.getIdentifier()->getName());
      Diag(FD->getLocation(), diag::err_previous_definition);
    }
  }
  FunctionDecl *FD =
    static_cast<FunctionDecl*>(ActOnDeclarator(GlobalScope, D, 0));
  assert(FD != 0 && "ActOnDeclarator() didn't return a FunctionDecl");
  CurFunctionDecl = FD;
  
  // Create Decl objects for each parameter, adding them to the FunctionDecl.
  llvm::SmallVector<ParmVarDecl*, 16> Params;
  
  // Check for C99 6.7.5.3p10 - foo(void) is a non-varargs function that takes
  // no arguments, not a function that takes a single void argument.
  if (FTI.NumArgs == 1 && !FTI.isVariadic && FTI.ArgInfo[0].Ident == 0 &&
      !QualType::getFromOpaquePtr(FTI.ArgInfo[0].TypeInfo).getQualifiers() &&
      QualType::getFromOpaquePtr(FTI.ArgInfo[0].TypeInfo)->isVoidType()) {
    // empty arg list, don't push any params.
  } else {
    for (unsigned i = 0, e = FTI.NumArgs; i != e; ++i) {
      Params.push_back(ActOnParamDeclarator(D.getTypeObject(0).Fun.ArgInfo[i],
                                            FnBodyScope));
    }
  }
  
  FD->setParams(&Params[0], Params.size());
  
  return FD;
}

Sema::DeclTy *Sema::ActOnFinishFunctionBody(DeclTy *D, StmtTy *Body) {
  Decl *dcl = static_cast<Decl *>(D);
  if (FunctionDecl *FD = dyn_cast<FunctionDecl>(dcl)) {
    FD->setBody((Stmt*)Body);
    assert(FD == CurFunctionDecl && "Function parsing confused");
    CurFunctionDecl = 0;
  } else if (ObjCMethodDecl *MD = dyn_cast<ObjCMethodDecl>(dcl)) {
    MD->setBody((Stmt*)Body);
    CurMethodDecl = 0;
  }  
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
      if (Body) {
        L->setSubStmt(new NullStmt(L->getIdentLoc()));
        cast<CompoundStmt>((Stmt*)Body)->push_back(L);
      } else {
        // The whole function wasn't parsed correctly, just delete this.
        delete L;
      }
    }
  }
  LabelMap.clear();
  
  return D;
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


TypedefDecl *Sema::ParseTypedefDecl(Scope *S, Declarator &D, QualType T,
                                    ScopedDecl *LastDeclarator) {
  assert(D.getIdentifier() && "Wrong callback for declspec without declarator");
  assert(!T.isNull() && "GetTypeForDeclarator() returned null type");
  
  // Scope manipulation handled by caller.
  TypedefDecl *NewTD = new TypedefDecl(D.getIdentifierLoc(), D.getIdentifier(), 
                                       T, LastDeclarator);
  if (D.getInvalidType())
    NewTD->setInvalidDecl();
  return NewTD;
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
    NewFD = new FieldDecl(Loc, II, T, BitWidth);
  else if (isa<ObjCInterfaceDecl>(static_cast<Decl *>(TagDecl)) ||
           isa<ObjCImplementationDecl>(static_cast<Decl *>(TagDecl)) ||
           isa<ObjCCategoryDecl>(static_cast<Decl *>(TagDecl)) ||
           // FIXME: ivars are currently used to model properties, and
           // properties can appear within a protocol.
           // See corresponding FIXME in DeclObjC.h:ObjCPropertyDecl.
           isa<ObjCProtocolDecl>(static_cast<Decl *>(TagDecl)))
    NewFD = new ObjCIvarDecl(Loc, II, T);
  else
    assert(0 && "Sema::ActOnField(): Unknown TagDecl");
    
  if (D.getInvalidType() || InvalidDecl)
    NewFD->setInvalidDecl();
  return NewFD;
}

/// TranslateIvarVisibility - Translate visibility from a token ID to an 
///  AST enum value.
static ObjCIvarDecl::AccessControl
TranslateIvarVisibility(tok::ObjCKeywordKind ivarVisibility) {
  switch (ivarVisibility) {
    case tok::objc_private: return ObjCIvarDecl::Private;
    case tok::objc_public: return ObjCIvarDecl::Public;
    case tok::objc_protected: return ObjCIvarDecl::Protected;
    case tok::objc_package: return ObjCIvarDecl::Package;
    default: assert(false && "Unknown visitibility kind");
  }
}

void Sema::ActOnFields(Scope* S,
                       SourceLocation RecLoc, DeclTy *RecDecl,
                       DeclTy **Fields, unsigned NumFields,
                       SourceLocation LBrac, SourceLocation RBrac,
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
      cast<ObjCIvarDecl>(FD)->setAccessControl(
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
    /// A field cannot be an Objective-c object
    if (FDTy->isObjCInterfaceType()) {
      Diag(FD->getLocation(), diag::err_statically_allocated_object,
           FD->getName());
      FD->setInvalidDecl();
      EnclosingDecl->setInvalidDecl();
      continue;
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
  if (Record) {
    Record->defineBody(&RecFields[0], RecFields.size());
    Consumer.HandleTagDeclDefinition(Record);
  } else {
    ObjCIvarDecl **ClsFields = reinterpret_cast<ObjCIvarDecl**>(&RecFields[0]);
    if (ObjCInterfaceDecl *ID = dyn_cast<ObjCInterfaceDecl>(EnclosingDecl))
      ID->addInstanceVariablesToClass(ClsFields, RecFields.size(), RBrac);
    else if (ObjCImplementationDecl *IMPDecl = 
               dyn_cast<ObjCImplementationDecl>(EnclosingDecl)) {
      assert(IMPDecl && "ActOnFields - missing ObjCImplementationDecl");
      IMPDecl->ObjCAddInstanceVariablesToClassImpl(ClsFields, RecFields.size());
      CheckImplementationIvars(IMPDecl, ClsFields, RecFields.size(), RBrac);
    }
  }
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
  unsigned IntWidth =
    Context.Target.getIntWidth(Context.getFullLoc(Enum->getLocation()));
  
  
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
      NumPositiveBits = std::max(NumPositiveBits,
                                 (unsigned)InitVal.getActiveBits());
    else
      NumNegativeBits = std::max(NumNegativeBits,
                                 (unsigned)InitVal.getMinSignedBits());

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
      BestWidth = 
        Context.Target.getLongWidth(Context.getFullLoc(Enum->getLocation()));
      
      if (NumNegativeBits <= BestWidth && NumPositiveBits < BestWidth)
        BestType = Context.LongTy;
      else {
        BestWidth = Context.Target.getLongLongWidth(
                       Context.getFullLoc(Enum->getLocation()));
        
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
               (BestWidth = Context.Target.getLongWidth(
                              Context.getFullLoc(Enum->getLocation()))))

      BestType = Context.UnsignedLongTy;
    else {
      BestWidth =
       Context.Target.getLongLongWidth(Context.getFullLoc(Enum->getLocation()));
      
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
  Consumer.HandleTagDeclDefinition(Enum);
}

Sema::DeclTy *Sema::ActOnFileScopeAsmDecl(SourceLocation Loc,
                                          ExprTy *expr) {
  StringLiteral *AsmString = cast<StringLiteral>((Expr*)expr);
  
  return new FileScopeAsmDecl(Loc, AsmString);
}

Sema::DeclTy* Sema::ActOnLinkageSpec(SourceLocation Loc,
				     SourceLocation LBrace,
				     SourceLocation RBrace,
				     const char *Lang,
				     unsigned StrSize,
				     DeclTy *D) {
  LinkageSpecDecl::LanguageIDs Language;
  Decl *dcl = static_cast<Decl *>(D);
  if (strncmp(Lang, "\"C\"", StrSize) == 0)
    Language = LinkageSpecDecl::lang_c;
  else if (strncmp(Lang, "\"C++\"", StrSize) == 0)
    Language = LinkageSpecDecl::lang_cxx;
  else {
    Diag(Loc, diag::err_bad_language);
    return 0;
  }

  // FIXME: Add all the various semantics of linkage specifications
  return new LinkageSpecDecl(Loc, Language, dcl);
}

void Sema::HandleDeclAttribute(Decl *New, AttributeList *rawAttr) {
  const char *attrName = rawAttr->getAttributeName()->getName();
  unsigned attrLen = rawAttr->getAttributeName()->getLength();
  
  // Normalize the attribute name, __foo__ becomes foo.
  if (attrLen > 4 && attrName[0] == '_' && attrName[1] == '_' &&
      attrName[attrLen - 2] == '_' && attrName[attrLen - 1] == '_') {
    attrName += 2;
    attrLen -= 4;
  }
  
  if (attrLen == 11 && !memcmp(attrName, "vector_size", 11)) {
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
  } else if (attrLen == 15 && !memcmp(attrName, "ocu_vector_type", 15)) {
    if (TypedefDecl *tDecl = dyn_cast<TypedefDecl>(New))
      HandleOCUVectorTypeAttribute(tDecl, rawAttr);
    else
      Diag(rawAttr->getAttributeLoc(), 
           diag::err_typecheck_ocu_vector_not_typedef);
  } else if (attrLen == 13 && !memcmp(attrName, "address_space", 13)) {
    if (TypedefDecl *tDecl = dyn_cast<TypedefDecl>(New)) {
      QualType newType = HandleAddressSpaceTypeAttribute(
                                                  tDecl->getUnderlyingType(), 
                                                  rawAttr);
      if (!newType.isNull()) // install the new addr spaced type into the decl
        tDecl->setUnderlyingType(newType);
    } else if (ValueDecl *vDecl = dyn_cast<ValueDecl>(New)) {
      QualType newType = HandleAddressSpaceTypeAttribute(vDecl->getType(), 
                                                         rawAttr);
      if (!newType.isNull()) // install the new addr spaced  type into the decl
        vDecl->setType(newType);
    }
  } else if (attrLen == 7 && !memcmp(attrName, "aligned", 7)) {
      HandleAlignedAttribute(New, rawAttr);
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

QualType Sema::HandleAddressSpaceTypeAttribute(QualType curType, 
                                               AttributeList *rawAttr) {
  // check the attribute arugments.
  if (rawAttr->getNumArgs() != 1) {
    Diag(rawAttr->getAttributeLoc(), diag::err_attribute_wrong_number_arguments,
         std::string("1"));
    return QualType();
  }
  Expr *addrSpaceExpr = static_cast<Expr *>(rawAttr->getArg(0));
  llvm::APSInt addrSpace(32);
  if (!addrSpaceExpr->isIntegerConstantExpr(addrSpace, Context)) {
    Diag(rawAttr->getAttributeLoc(), diag::err_attribute_address_space_not_int,
         addrSpaceExpr->getSourceRange());
    return QualType();
  }
  unsigned addressSpace = static_cast<unsigned>(addrSpace.getZExtValue()); 
  
  // Zero is the default memory space, so no qualification is needed
  if (addressSpace == 0)
    return curType;
  
  // TODO: Should we convert contained types of address space 
  // qualified types here or or where they directly participate in conversions
  // (i.e. elsewhere)
  
  return Context.getASQualType(curType, addressSpace);
}

void Sema::HandleOCUVectorTypeAttribute(TypedefDecl *tDecl, 
                                        AttributeList *rawAttr) {
  QualType curType = tDecl->getUnderlyingType();
  // check the attribute arguments.
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
    assert(0 && "HandleVector(): Complex type construction unimplemented");
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

void Sema::HandleAlignedAttribute(Decl *d, AttributeList *rawAttr)
{
  // check the attribute arguments.
  if (rawAttr->getNumArgs() > 1) {
    Diag(rawAttr->getAttributeLoc(), diag::err_attribute_wrong_number_arguments,
         std::string("1"));
    return;
  }

  // TODO: We probably need to actually do something with aligned attribute.
  if (rawAttr->getNumArgs() == 0)
    return;

  Expr *alignmentExpr = static_cast<Expr *>(rawAttr->getArg(0));
  llvm::APSInt alignment(32);
  if (!alignmentExpr->isIntegerConstantExpr(alignment, Context)) {
    Diag(rawAttr->getAttributeLoc(), diag::err_attribute_vector_size_not_int,
         alignmentExpr->getSourceRange());
    return;
  }    
}
