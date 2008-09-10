//===--- SemaExprCXX.cpp - Semantic Analysis for Expressions --------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file implements semantic analysis for C++ expressions.
//
//===----------------------------------------------------------------------===//

#include "Sema.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/ASTContext.h"
#include "clang/Parse/DeclSpec.h"
#include "clang/Basic/Diagnostic.h"
using namespace clang;

/// ActOnCXXCasts - Parse {dynamic,static,reinterpret,const}_cast's.
Action::ExprResult
Sema::ActOnCXXCasts(SourceLocation OpLoc, tok::TokenKind Kind,
                    SourceLocation LAngleBracketLoc, TypeTy *Ty,
                    SourceLocation RAngleBracketLoc,
                    SourceLocation LParenLoc, ExprTy *E,
                    SourceLocation RParenLoc) {
  CXXCastExpr::Opcode Op;

  switch (Kind) {
  default: assert(0 && "Unknown C++ cast!");
  case tok::kw_const_cast:       Op = CXXCastExpr::ConstCast;       break;
  case tok::kw_dynamic_cast:     Op = CXXCastExpr::DynamicCast;     break;
  case tok::kw_reinterpret_cast: Op = CXXCastExpr::ReinterpretCast; break;
  case tok::kw_static_cast:      Op = CXXCastExpr::StaticCast;      break;
  }
  
  return new CXXCastExpr(Op, QualType::getFromOpaquePtr(Ty), (Expr*)E, OpLoc);
}

/// ActOnCXXBoolLiteral - Parse {true,false} literals.
Action::ExprResult
Sema::ActOnCXXBoolLiteral(SourceLocation OpLoc, tok::TokenKind Kind) {
  assert((Kind != tok::kw_true || Kind != tok::kw_false) &&
         "Unknown C++ Boolean value!");
  return new CXXBoolLiteralExpr(Kind == tok::kw_true, Context.BoolTy, OpLoc);
}

/// ActOnCXXThrow - Parse throw expressions.
Action::ExprResult
Sema::ActOnCXXThrow(SourceLocation OpLoc, ExprTy *E) {
  return new CXXThrowExpr((Expr*)E, Context.VoidTy, OpLoc);
}

Action::ExprResult Sema::ActOnCXXThis(SourceLocation ThisLoc) {
  /// C++ 9.3.2: In the body of a non-static member function, the keyword this
  /// is a non-lvalue expression whose value is the address of the object for
  /// which the function is called.

  if (!isa<FunctionDecl>(CurContext)) {
    Diag(ThisLoc, diag::err_invalid_this_use);
    return ExprResult(true);
  }

  if (CXXMethodDecl *MD = dyn_cast<CXXMethodDecl>(CurContext))
    if (MD->isInstance())
      return new PredefinedExpr(ThisLoc, MD->getThisType(Context),
                                PredefinedExpr::CXXThis);

  return Diag(ThisLoc, diag::err_invalid_this_use);
}

/// ActOnCXXTypeConstructExpr - Parse construction of a specified type.
/// Can be interpreted either as function-style casting ("int(x)")
/// or class type construction ("ClassType(x,y,z)")
/// or creation of a value-initialized type ("int()").
Action::ExprResult
Sema::ActOnCXXTypeConstructExpr(SourceRange TypeRange, TypeTy *TypeRep,
                                SourceLocation LParenLoc,
                                ExprTy **ExprTys, unsigned NumExprs,
                                SourceLocation *CommaLocs,
                                SourceLocation RParenLoc) {
  assert(TypeRep && "Missing type!");
  QualType Ty = QualType::getFromOpaquePtr(TypeRep);
  Expr **Exprs = (Expr**)ExprTys;
  SourceLocation TyBeginLoc = TypeRange.getBegin();
  SourceRange FullRange = SourceRange(TyBeginLoc, RParenLoc);

  if (const RecordType *RT = Ty->getAsRecordType()) {
    // C++ 5.2.3p1:
    // If the simple-type-specifier specifies a class type, the class type shall
    // be complete.
    //
    if (!RT->getDecl()->isDefinition())
      return Diag(TyBeginLoc, diag::err_invalid_incomplete_type_use,
                  Ty.getAsString(), FullRange);

    // "class constructors are not supported yet"
    return Diag(TyBeginLoc, diag::err_unsupported_class_constructor, FullRange);
  }

  // C++ 5.2.3p1:
  // If the expression list is a single expression, the type conversion
  // expression is equivalent (in definedness, and if defined in meaning) to the
  // corresponding cast expression.
  //
  if (NumExprs == 1) {
    if (CheckCastTypes(TypeRange, Ty, Exprs[0]))
      return true;
    return new CXXFunctionalCastExpr(Ty, TyBeginLoc, Exprs[0], RParenLoc);
  }

  // C++ 5.2.3p1:
  // If the expression list specifies more than a single value, the type shall
  // be a class with a suitably declared constructor.
  //
  if (NumExprs > 1)
    return Diag(CommaLocs[0], diag::err_builtin_func_cast_more_than_one_arg,
                FullRange);

  assert(NumExprs == 0 && "Expected 0 expressions");

  // C++ 5.2.3p2:
  // The expression T(), where T is a simple-type-specifier for a non-array
  // complete object type or the (possibly cv-qualified) void type, creates an
  // rvalue of the specified type, which is value-initialized.
  //
  if (Ty->isArrayType())
    return Diag(TyBeginLoc, diag::err_value_init_for_array_type, FullRange);
  if (Ty->isIncompleteType() && !Ty->isVoidType())
    return Diag(TyBeginLoc, diag::err_invalid_incomplete_type_use,
                Ty.getAsString(), FullRange);

  return new CXXZeroInitValueExpr(Ty, TyBeginLoc, RParenLoc);
}


/// ActOnCXXConditionDeclarationExpr - Parsed a condition declaration of a
/// C++ if/switch/while/for statement.
/// e.g: "if (int x = f()) {...}"
Action::ExprResult
Sema::ActOnCXXConditionDeclarationExpr(Scope *S, SourceLocation StartLoc,
                                       Declarator &D,
                                       SourceLocation EqualLoc,
                                       ExprTy *AssignExprVal) {
  assert(AssignExprVal && "Null assignment expression");

  // C++ 6.4p2:
  // The declarator shall not specify a function or an array.
  // The type-specifier-seq shall not contain typedef and shall not declare a
  // new class or enumeration.

  assert(D.getDeclSpec().getStorageClassSpec() != DeclSpec::SCS_typedef &&
         "Parser allowed 'typedef' as storage class of condition decl.");

  QualType Ty = GetTypeForDeclarator(D, S);
  
  if (Ty->isFunctionType()) { // The declarator shall not specify a function...
    // We exit without creating a CXXConditionDeclExpr because a FunctionDecl
    // would be created and CXXConditionDeclExpr wants a VarDecl.
    return Diag(StartLoc, diag::err_invalid_use_of_function_type,
                SourceRange(StartLoc, EqualLoc));
  } else if (Ty->isArrayType()) { // ...or an array.
    Diag(StartLoc, diag::err_invalid_use_of_array_type,
         SourceRange(StartLoc, EqualLoc));
  } else if (const RecordType *RT = Ty->getAsRecordType()) {
    RecordDecl *RD = RT->getDecl();
    // The type-specifier-seq shall not declare a new class...
    if (RD->isDefinition() && (RD->getIdentifier() == 0 || S->isDeclScope(RD)))
      Diag(RD->getLocation(), diag::err_type_defined_in_condition);
  } else if (const EnumType *ET = Ty->getAsEnumType()) {
    EnumDecl *ED = ET->getDecl();
    // ...or enumeration.
    if (ED->isDefinition() && (ED->getIdentifier() == 0 || S->isDeclScope(ED)))
      Diag(ED->getLocation(), diag::err_type_defined_in_condition);
  }

  DeclTy *Dcl = ActOnDeclarator(S, D, 0);
  if (!Dcl)
    return true;
  AddInitializerToDecl(Dcl, AssignExprVal);

  return new CXXConditionDeclExpr(StartLoc, EqualLoc,
                                       cast<VarDecl>(static_cast<Decl *>(Dcl)));
}

/// CheckCXXBooleanCondition - Returns true if a conversion to bool is invalid.
bool Sema::CheckCXXBooleanCondition(Expr *&CondExpr) {
  // C++ 6.4p4:
  // The value of a condition that is an initialized declaration in a statement
  // other than a switch statement is the value of the declared variable
  // implicitly converted to type bool. If that conversion is ill-formed, the
  // program is ill-formed.
  // The value of a condition that is an expression is the value of the
  // expression, implicitly converted to bool.
  //
  QualType Ty = CondExpr->getType(); // Save the type.
  AssignConvertType
    ConvTy = CheckSingleAssignmentConstraints(Context.BoolTy, CondExpr);
  if (ConvTy == Incompatible)
    return Diag(CondExpr->getLocStart(), diag::err_typecheck_bool_condition,
                Ty.getAsString(), CondExpr->getSourceRange());
  return false;
}
