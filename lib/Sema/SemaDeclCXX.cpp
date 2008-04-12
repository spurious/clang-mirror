//===------ SemaDeclCXX.cpp - Semantic Analysis for C++ Declarations ------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file implements semantic analysis for C++ declarations.
//
//===----------------------------------------------------------------------===//

#include "Sema.h"
#include "clang/Basic/LangOptions.h"
#include "clang/AST/Expr.h"
#include "clang/AST/StmtVisitor.h"
#include "clang/AST/Type.h"
#include "llvm/ADT/OwningPtr.h"
#include "llvm/Support/Compiler.h"

using namespace clang;

//===----------------------------------------------------------------------===//
// CheckDefaultArgumentVisitor
//===----------------------------------------------------------------------===//

namespace {
  /// CheckDefaultArgumentVisitor - C++ [dcl.fct.default] Traverses
  /// the default argument of a parameter to determine whether it
  /// contains any ill-formed subexpressions. For example, this will
  /// diagnose the use of local variables or parameters within the
  /// default argument expression.
  class VISIBILITY_HIDDEN CheckDefaultArgumentVisitor 
    : public StmtVisitor<CheckDefaultArgumentVisitor, bool>
  {
    Expr *DefaultArg;
    Sema *S;

  public:
    CheckDefaultArgumentVisitor(Expr *defarg, Sema *s) 
      : DefaultArg(defarg), S(s) {}

    bool VisitExpr(Expr *Node);
    bool VisitDeclRefExpr(DeclRefExpr *DRE);
  };

  /// VisitExpr - Visit all of the children of this expression.
  bool CheckDefaultArgumentVisitor::VisitExpr(Expr *Node) {
    bool IsInvalid = false;
    for (Stmt::child_iterator first = Node->child_begin(), 
           last = Node->child_end();
         first != last; ++first)
      IsInvalid |= Visit(*first);

    return IsInvalid;
  }

  /// VisitDeclRefExpr - Visit a reference to a declaration, to
  /// determine whether this declaration can be used in the default
  /// argument expression.
  bool CheckDefaultArgumentVisitor::VisitDeclRefExpr(DeclRefExpr *DRE) {
    ValueDecl *Decl = DRE->getDecl();
    if (ParmVarDecl *Param = dyn_cast<ParmVarDecl>(Decl)) {
      // C++ [dcl.fct.default]p9
      //   Default arguments are evaluated each time the function is
      //   called. The order of evaluation of function arguments is
      //   unspecified. Consequently, parameters of a function shall not
      //   be used in default argument expressions, even if they are not
      //   evaluated. Parameters of a function declared before a default
      //   argument expression are in scope and can hide namespace and
      //   class member names.
      return S->Diag(DRE->getSourceRange().getBegin(), 
                     diag::err_param_default_argument_references_param,
                     Param->getName(), DefaultArg->getSourceRange());
    } else if (BlockVarDecl *BlockVar = dyn_cast<BlockVarDecl>(Decl)) {
      // C++ [dcl.fct.default]p7
      //   Local variables shall not be used in default argument
      //   expressions.
      return S->Diag(DRE->getSourceRange().getBegin(), 
                     diag::err_param_default_argument_references_local,
                     BlockVar->getName(), DefaultArg->getSourceRange());
    }

    // FIXME: when Clang has support for member functions, "this"
    // will also need to be diagnosed.

    return false;
  }
}

/// ActOnParamDefaultArgument - Check whether the default argument
/// provided for a function parameter is well-formed. If so, attach it
/// to the parameter declaration.
void
Sema::ActOnParamDefaultArgument(DeclTy *param, SourceLocation EqualLoc, 
                                ExprTy *defarg) {
  ParmVarDecl *Param = (ParmVarDecl *)param;
  llvm::OwningPtr<Expr> DefaultArg((Expr *)defarg);
  QualType ParamType = Param->getType();

  // Default arguments are only permitted in C++
  if (!getLangOptions().CPlusPlus) {
    Diag(EqualLoc, diag::err_param_default_argument, 
         DefaultArg->getSourceRange());
    return;
  }

  // C++ [dcl.fct.default]p5
  //   A default argument expression is implicitly converted (clause
  //   4) to the parameter type. The default argument expression has
  //   the same semantic constraints as the initializer expression in
  //   a declaration of a variable of the parameter type, using the
  //   copy-initialization semantics (8.5).
  //
  // FIXME: CheckSingleAssignmentConstraints has the wrong semantics
  // for C++ (since we want copy-initialization, not copy-assignment),
  // but we don't have the right semantics implemented yet. Because of
  // this, our error message is also very poor.
  QualType DefaultArgType = DefaultArg->getType();   
  Expr *DefaultArgPtr = DefaultArg.get();
  AssignConvertType ConvTy = CheckSingleAssignmentConstraints(ParamType,
                                                              DefaultArgPtr);
  if (DefaultArgPtr != DefaultArg.get()) {
    DefaultArg.take();
    DefaultArg.reset(DefaultArgPtr);
  }
  if (DiagnoseAssignmentResult(ConvTy, DefaultArg->getLocStart(), 
                               ParamType, DefaultArgType, DefaultArg.get(), 
                               "in default argument")) {
    return;
  }

  // FIXME: C++ [dcl.fct.default]p3
  //   A default argument expression shall be specified only in the
  //   parameter-declaration-clause of a function declaration or in a
  //   template-parameter (14.1). It shall not be specified for a
  //   parameter pack. If it is specified in a
  //   parameter-declaration-clause, it shall not occur within a
  //   declarator or abstract-declarator of a parameter-declaration.

  // Check that the default argument is well-formed
  CheckDefaultArgumentVisitor DefaultArgChecker(DefaultArg.get(), this);
  if (DefaultArgChecker.Visit(DefaultArg.get()))
    return;

  // Okay: add the default argument to the parameter
  Param->setDefaultArg(DefaultArg.take());
}

// MergeCXXFunctionDecl - Merge two declarations of the same C++
// function, once we already know that they have the same
// type. Subroutine of MergeFunctionDecl.
FunctionDecl * 
Sema::MergeCXXFunctionDecl(FunctionDecl *New, FunctionDecl *Old) {
  // C++ [dcl.fct.default]p4:
  //
  //   For non-template functions, default arguments can be added in
  //   later declarations of a function in the same
  //   scope. Declarations in different scopes have completely
  //   distinct sets of default arguments. That is, declarations in
  //   inner scopes do not acquire default arguments from
  //   declarations in outer scopes, and vice versa. In a given
  //   function declaration, all parameters subsequent to a
  //   parameter with a default argument shall have default
  //   arguments supplied in this or previous declarations. A
  //   default argument shall not be redefined by a later
  //   declaration (not even to the same value).
  for (unsigned p = 0, NumParams = Old->getNumParams(); p < NumParams; ++p) {
    ParmVarDecl *OldParam = Old->getParamDecl(p);
    ParmVarDecl *NewParam = New->getParamDecl(p);

    if(OldParam->getDefaultArg() && NewParam->getDefaultArg()) {
      Diag(NewParam->getLocation(), 
           diag::err_param_default_argument_redefinition,
           NewParam->getDefaultArg()->getSourceRange());
      Diag(OldParam->getLocation(), diag::err_previous_definition);
    } else if (OldParam->getDefaultArg()) {
      // Merge the old default argument into the new parameter
      NewParam->setDefaultArg(OldParam->getDefaultArg());
    }
  }

  return New;  
}

/// CheckCXXDefaultArguments - Verify that the default arguments for a
/// function declaration are well-formed according to C++
/// [dcl.fct.default].
void Sema::CheckCXXDefaultArguments(FunctionDecl *FD) {
  unsigned NumParams = FD->getNumParams();
  unsigned p;

  // Find first parameter with a default argument
  for (p = 0; p < NumParams; ++p) {
    ParmVarDecl *Param = FD->getParamDecl(p);
    if (Param->getDefaultArg())
      break;
  }

  // C++ [dcl.fct.default]p4:
  //   In a given function declaration, all parameters
  //   subsequent to a parameter with a default argument shall
  //   have default arguments supplied in this or previous
  //   declarations. A default argument shall not be redefined
  //   by a later declaration (not even to the same value).
  unsigned LastMissingDefaultArg = 0;
  for(; p < NumParams; ++p) {
    ParmVarDecl *Param = FD->getParamDecl(p);
    if (!Param->getDefaultArg()) {
      if (Param->getIdentifier())
        Diag(Param->getLocation(), 
             diag::err_param_default_argument_missing_name,
             Param->getIdentifier()->getName());
      else
        Diag(Param->getLocation(), 
             diag::err_param_default_argument_missing);
    
      LastMissingDefaultArg = p;
    }
  }

  if (LastMissingDefaultArg > 0) {
    // Some default arguments were missing. Clear out all of the
    // default arguments up to (and including) the last missing
    // default argument, so that we leave the function parameters
    // in a semantically valid state.
    for (p = 0; p <= LastMissingDefaultArg; ++p) {
      ParmVarDecl *Param = FD->getParamDecl(p);
      if (Param->getDefaultArg()) {
        delete Param->getDefaultArg();
        Param->setDefaultArg(0);
      }
    }
  }
}
