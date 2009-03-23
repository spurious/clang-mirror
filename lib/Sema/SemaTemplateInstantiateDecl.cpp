//===--- SemaTemplateInstantiateDecl.cpp - C++ Template Decl Instantiation ===/
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//===----------------------------------------------------------------------===/
//
//  This file implements C++ template instantiation for declarations.
//
//===----------------------------------------------------------------------===/
#include "Sema.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/DeclVisitor.h"
#include "clang/AST/Expr.h"
#include "llvm/Support/Compiler.h"

using namespace clang;

namespace {
  class VISIBILITY_HIDDEN TemplateDeclInstantiator 
    : public DeclVisitor<TemplateDeclInstantiator, Decl *> 
  {
    Sema &SemaRef;
    DeclContext *Owner;
    const TemplateArgument *TemplateArgs;
    unsigned NumTemplateArgs;
    
  public:
    typedef Sema::OwningExprResult OwningExprResult;

    TemplateDeclInstantiator(Sema &SemaRef, DeclContext *Owner,
                             const TemplateArgument *TemplateArgs,
                             unsigned NumTemplateArgs)
      : SemaRef(SemaRef), Owner(Owner), TemplateArgs(TemplateArgs), 
        NumTemplateArgs(NumTemplateArgs) { }
    
    // FIXME: Once we get closer to completion, replace these
    // manually-written declarations with automatically-generated ones
    // from clang/AST/DeclNodes.def.
    Decl *VisitTypedefDecl(TypedefDecl *D);
    Decl *VisitFieldDecl(FieldDecl *D);
    Decl *VisitStaticAssertDecl(StaticAssertDecl *D);
    Decl *VisitEnumDecl(EnumDecl *D);
    Decl *VisitCXXMethodDecl(CXXMethodDecl *D);
    Decl *VisitParmVarDecl(ParmVarDecl *D);
    Decl *VisitOriginalParmVarDecl(OriginalParmVarDecl *D);

    // Base case. FIXME: Remove once we can instantiate everything.
    Decl *VisitDecl(Decl *) { 
      return 0;
    }
  };
}

Decl *TemplateDeclInstantiator::VisitTypedefDecl(TypedefDecl *D) {
  bool Invalid = false;
  QualType T = D->getUnderlyingType();
  if (T->isDependentType()) {
    T = SemaRef.InstantiateType(T, TemplateArgs, NumTemplateArgs,
                                D->getLocation(),
                                D->getDeclName());
    if (T.isNull()) {
      Invalid = true;
      T = SemaRef.Context.IntTy;
    }
  }
       
  // Create the new typedef
  TypedefDecl *Typedef
    = TypedefDecl::Create(SemaRef.Context, Owner, D->getLocation(),
                          D->getIdentifier(), T);
  if (Invalid)
    Typedef->setInvalidDecl();

  Owner->addDecl(Typedef);
  return Typedef;
}

Decl *TemplateDeclInstantiator::VisitFieldDecl(FieldDecl *D) {
  bool Invalid = false;
  QualType T = D->getType();
  if (T->isDependentType())  {
    T = SemaRef.InstantiateType(T, TemplateArgs, NumTemplateArgs,
                                D->getLocation(),
                                D->getDeclName());
    if (!T.isNull() && T->isFunctionType()) {
      // C++ [temp.arg.type]p3:
      //   If a declaration acquires a function type through a type
      //   dependent on a template-parameter and this causes a
      //   declaration that does not use the syntactic form of a
      //   function declarator to have function type, the program is
      //   ill-formed.
      SemaRef.Diag(D->getLocation(), diag::err_field_instantiates_to_function)
        << T;
      T = QualType();
      Invalid = true;
    }
  }

  Expr *BitWidth = D->getBitWidth();
  if (Invalid)
    BitWidth = 0;
  else if (BitWidth) {
    OwningExprResult InstantiatedBitWidth
      = SemaRef.InstantiateExpr(BitWidth, TemplateArgs, NumTemplateArgs);
    if (InstantiatedBitWidth.isInvalid()) {
      Invalid = true;
      BitWidth = 0;
    } else
      BitWidth = (Expr *)InstantiatedBitWidth.release();
  }

  FieldDecl *Field = SemaRef.CheckFieldDecl(D->getDeclName(), T,
                                            cast<RecordDecl>(Owner), 
                                            D->getLocation(),
                                            D->isMutable(),
                                            BitWidth,
                                            D->getAccess(),
                                            0);
  if (Field) {
    if (Invalid)
      Field->setInvalidDecl();
    
    Owner->addDecl(Field);
  }

  return Field;
}

Decl *TemplateDeclInstantiator::VisitStaticAssertDecl(StaticAssertDecl *D) {
  Expr *AssertExpr = D->getAssertExpr();
      
  OwningExprResult InstantiatedAssertExpr
    = SemaRef.InstantiateExpr(AssertExpr, TemplateArgs, NumTemplateArgs);
  if (InstantiatedAssertExpr.isInvalid())
    return 0;

  OwningExprResult Message = SemaRef.Clone(D->getMessage());
  Decl *StaticAssert 
    = (Decl *)SemaRef.ActOnStaticAssertDeclaration(D->getLocation(), 
                                                move(InstantiatedAssertExpr),
                                                   move(Message));
  return StaticAssert;
}

Decl *TemplateDeclInstantiator::VisitEnumDecl(EnumDecl *D) {
  EnumDecl *Enum = EnumDecl::Create(SemaRef.Context, Owner, 
                                    D->getLocation(), D->getIdentifier(),
                                    /*PrevDecl=*/0);
  Owner->addDecl(Enum);
  Enum->startDefinition();

  llvm::SmallVector<Sema::DeclTy *, 16> Enumerators;

  EnumConstantDecl *LastEnumConst = 0;
  for (EnumDecl::enumerator_iterator EC = D->enumerator_begin(),
         ECEnd = D->enumerator_end();
       EC != ECEnd; ++EC) {
    // The specified value for the enumerator.
    OwningExprResult Value = SemaRef.Owned((Expr *)0);
    if (Expr *UninstValue = EC->getInitExpr())
      Value = SemaRef.InstantiateExpr(UninstValue, 
                                      TemplateArgs, NumTemplateArgs);

    // Drop the initial value and continue.
    bool isInvalid = false;
    if (Value.isInvalid()) {
      Value = SemaRef.Owned((Expr *)0);
      isInvalid = true;
    }

    EnumConstantDecl *EnumConst 
      = SemaRef.CheckEnumConstant(Enum, LastEnumConst,
                                  EC->getLocation(), EC->getIdentifier(),
                                  move(Value));

    if (isInvalid) {
      if (EnumConst)
        EnumConst->setInvalidDecl();
      Enum->setInvalidDecl();
    }

    if (EnumConst) {
      Enum->addDecl(EnumConst);
      Enumerators.push_back(EnumConst);
      LastEnumConst = EnumConst;
    }
  }
      
  SemaRef.ActOnEnumBody(Enum->getLocation(), Enum,
                        &Enumerators[0], Enumerators.size());

  return Enum;
}

Decl *TemplateDeclInstantiator::VisitCXXMethodDecl(CXXMethodDecl *D) {
  // Only handle actual methods; we'll deal with constructors,
  // destructors, etc. separately.
  if (D->getKind() != Decl::CXXMethod)
    return 0;

  QualType T = SemaRef.InstantiateType(D->getType(), TemplateArgs,
                                       NumTemplateArgs, D->getLocation(),
                                       D->getDeclName());
  if (T.isNull())
    return 0;

  // Build the instantiated method declaration.
  CXXRecordDecl *Record = cast<CXXRecordDecl>(Owner);
  CXXMethodDecl *Method
    = CXXMethodDecl::Create(SemaRef.Context, Record, D->getLocation(), 
                            D->getDeclName(), T, D->isStatic(), 
                            D->isInline());
  Method->setAccess(D->getAccess());
  // FIXME: Duplicates some logic in ActOnFunctionDeclarator.
  if (D->isVirtual()) {
    Method->setVirtual();
    Record->setAggregate(false);
    Record->setPOD(false);
    Record->setPolymorphic(true);
  }
  if (D->isDeleted())
    Method->setDeleted();
  if (D->isPure()) {
    Method->setPure();
    Record->setAbstract(true);
  }
  // FIXME: attributes
  // FIXME: Method needs a pointer referencing where it came from.

  // Instantiate the function parameters
  {
    TemplateDeclInstantiator ParamInstantiator(SemaRef, Method,
                                               TemplateArgs, NumTemplateArgs);
    llvm::SmallVector<ParmVarDecl *, 16> Params;
    for (FunctionDecl::param_iterator P = Method->param_begin(), 
                                   PEnd = Method->param_end();
         P != PEnd; ++P) {
      if (ParmVarDecl *PInst = (ParmVarDecl *)ParamInstantiator.Visit(*P))
        Params.push_back(PInst);
      else 
        Method->setInvalidDecl();
    }
  }

  NamedDecl *PrevDecl 
    = SemaRef.LookupQualifiedName(Owner, Method->getDeclName(), 
                                  Sema::LookupOrdinaryName, true);
  // In C++, the previous declaration we find might be a tag type
  // (class or enum). In this case, the new declaration will hide the
  // tag type. Note that this does does not apply if we're declaring a
  // typedef (C++ [dcl.typedef]p4).
  if (PrevDecl && PrevDecl->getIdentifierNamespace() == Decl::IDNS_Tag)
    PrevDecl = 0;
  bool Redeclaration = false;
  bool OverloadableAttrRequired = false;
  if (SemaRef.CheckFunctionDeclaration(Method, PrevDecl, Redeclaration,
                                       /*FIXME:*/OverloadableAttrRequired))
    Method->setInvalidDecl();

  if (!Method->isInvalidDecl() || !PrevDecl)
    Owner->addDecl(Method);
  return Method;
}

Decl *TemplateDeclInstantiator::VisitParmVarDecl(ParmVarDecl *D) {
  QualType OrigT = SemaRef.InstantiateType(D->getOriginalType(), TemplateArgs,
                                           NumTemplateArgs, D->getLocation(),
                                           D->getDeclName());
  if (OrigT.isNull())
    return 0;

  QualType T = SemaRef.adjustParameterType(OrigT);

  if (D->getDefaultArg()) {
    // FIXME: Leave a marker for "uninstantiated" default
    // arguments. They only get instantiated on demand at the call
    // site.
    unsigned DiagID = SemaRef.Diags.getCustomDiagID(Diagnostic::Warning,
        "sorry, dropping default argument during template instantiation");
    SemaRef.Diag(D->getDefaultArg()->getSourceRange().getBegin(), DiagID)
      << D->getDefaultArg()->getSourceRange();
  }

  // Allocate the parameter
  ParmVarDecl *Param = 0;
  if (T == OrigT)
    Param = ParmVarDecl::Create(SemaRef.Context, Owner, D->getLocation(),
                                D->getIdentifier(), T, D->getStorageClass(), 
                                0);
  else
    Param = OriginalParmVarDecl::Create(SemaRef.Context, Owner, 
                                        D->getLocation(), D->getIdentifier(),
                                        T, OrigT, D->getStorageClass(), 0);

  // Note: we don't try to instantiate function parameters until after
  // we've instantiated the function's type. Therefore, we don't have
  // to check for 'void' parameter types here.
  return Param;
}

Decl *
TemplateDeclInstantiator::VisitOriginalParmVarDecl(OriginalParmVarDecl *D) {
  // Since parameter types can decay either before or after
  // instantiation, we simply treat OriginalParmVarDecls as
  // ParmVarDecls the same way, and create one or the other depending
  // on what happens after template instantiation.
  return VisitParmVarDecl(D);
}

Decl *Sema::InstantiateDecl(Decl *D, DeclContext *Owner,
                            const TemplateArgument *TemplateArgs,
                            unsigned NumTemplateArgs) {
  TemplateDeclInstantiator Instantiator(*this, Owner, TemplateArgs,
                                        NumTemplateArgs);
  return Instantiator.Visit(D);
}

