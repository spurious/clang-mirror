//===--- ExprCXX.h - Classes for representing expressions -------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the Expr interface and subclasses for C++ expressions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_EXPRCXX_H
#define LLVM_CLANG_AST_EXPRCXX_H

#include "clang/Basic/TypeTraits.h"
#include "clang/AST/Expr.h"
#include "clang/AST/Decl.h"

namespace clang {

  class CXXConstructorDecl;
  class CXXDestructorDecl;
  class CXXMethodDecl;
  class CXXTemporary;

//===--------------------------------------------------------------------===//
// C++ Expressions.
//===--------------------------------------------------------------------===//

/// \brief A call to an overloaded operator written using operator
/// syntax.
///
/// Represents a call to an overloaded operator written using operator
/// syntax, e.g., "x + y" or "*p". While semantically equivalent to a
/// normal call, this AST node provides better information about the
/// syntactic representation of the call.
///
/// In a C++ template, this expression node kind will be used whenever
/// any of the arguments are type-dependent. In this case, the
/// function itself will be a (possibly empty) set of functions and
/// function templates that were found by name lookup at template
/// definition time.
class CXXOperatorCallExpr : public CallExpr {
  /// \brief The overloaded operator.
  OverloadedOperatorKind Operator;

public:
  CXXOperatorCallExpr(ASTContext& C, OverloadedOperatorKind Op, Expr *fn, 
                      Expr **args, unsigned numargs, QualType t, 
                      SourceLocation operatorloc)
    : CallExpr(C, CXXOperatorCallExprClass, fn, args, numargs, t, operatorloc),
      Operator(Op) {}
  explicit CXXOperatorCallExpr(ASTContext& C, EmptyShell Empty) : 
    CallExpr(C, CXXOperatorCallExprClass, Empty) { }
  

  /// getOperator - Returns the kind of overloaded operator that this
  /// expression refers to.
  OverloadedOperatorKind getOperator() const { return Operator; }
  void setOperator(OverloadedOperatorKind Kind) { Operator = Kind; }

  /// getOperatorLoc - Returns the location of the operator symbol in
  /// the expression. When @c getOperator()==OO_Call, this is the
  /// location of the right parentheses; when @c
  /// getOperator()==OO_Subscript, this is the location of the right
  /// bracket.
  SourceLocation getOperatorLoc() const { return getRParenLoc(); }

  virtual SourceRange getSourceRange() const;
  
  static bool classof(const Stmt *T) { 
    return T->getStmtClass() == CXXOperatorCallExprClass; 
  }
  static bool classof(const CXXOperatorCallExpr *) { return true; }
};

/// CXXMemberCallExpr - Represents a call to a member function that
/// may be written either with member call syntax (e.g., "obj.func()"
/// or "objptr->func()") or with normal function-call syntax
/// ("func()") within a member function that ends up calling a member
/// function. The callee in either case is a MemberExpr that contains
/// both the object argument and the member function, while the
/// arguments are the arguments within the parentheses (not including
/// the object argument).
class CXXMemberCallExpr : public CallExpr {
public:
  CXXMemberCallExpr(ASTContext& C, Expr *fn, Expr **args, unsigned numargs,
                    QualType t, SourceLocation rparenloc)
    : CallExpr(C, CXXMemberCallExprClass, fn, args, numargs, t, rparenloc) {}

  /// getImplicitObjectArgument - Retrieves the implicit object
  /// argument for the member call. For example, in "x.f(5)", this
  /// operation would return "x".
  Expr *getImplicitObjectArgument();

  static bool classof(const Stmt *T) { 
    return T->getStmtClass() == CXXMemberCallExprClass;
  }
  static bool classof(const CXXMemberCallExpr *) { return true; }
};

/// CXXNamedCastExpr - Abstract class common to all of the C++ "named"
/// casts, @c static_cast, @c dynamic_cast, @c reinterpret_cast, or @c
/// const_cast.
///
/// This abstract class is inherited by all of the classes
/// representing "named" casts, e.g., CXXStaticCastExpr,
/// CXXDynamicCastExpr, CXXReinterpretCastExpr, and CXXConstCastExpr.
class CXXNamedCastExpr : public ExplicitCastExpr {
private:
  SourceLocation Loc; // the location of the casting op

protected:
  CXXNamedCastExpr(StmtClass SC, QualType ty, const CastInfo &info, Expr *op, 
                   QualType writtenTy, SourceLocation l)
    : ExplicitCastExpr(SC, ty, info, op, writtenTy), Loc(l) {}

public:
  const char *getCastName() const;

  /// \brief Retrieve the location of the cast operator keyword, e.g.,
  /// "static_cast".
  SourceLocation getOperatorLoc() const { return Loc; }
  void setOperatorLoc(SourceLocation L) { Loc = L; }

  virtual SourceRange getSourceRange() const {
    return SourceRange(Loc, getSubExpr()->getSourceRange().getEnd());
  }
  static bool classof(const Stmt *T) { 
    switch (T->getStmtClass()) {
    case CXXNamedCastExprClass:
    case CXXStaticCastExprClass:
    case CXXDynamicCastExprClass:
    case CXXReinterpretCastExprClass:
    case CXXConstCastExprClass:
      return true;
    default:
      return false;
    }
  }
  static bool classof(const CXXNamedCastExpr *) { return true; }
};

/// CXXStaticCastExpr - A C++ @c static_cast expression (C++ [expr.static.cast]).
/// 
/// This expression node represents a C++ static cast, e.g.,
/// @c static_cast<int>(1.0).
class CXXStaticCastExpr : public CXXNamedCastExpr {
public:
  CXXStaticCastExpr(QualType ty, const CastInfo &info, Expr *op, 
                    QualType writtenTy, SourceLocation l)
    : CXXNamedCastExpr(CXXStaticCastExprClass, ty, info, op, writtenTy, l) {}

  static bool classof(const Stmt *T) { 
    return T->getStmtClass() == CXXStaticCastExprClass;
  }
  static bool classof(const CXXStaticCastExpr *) { return true; }
};

/// CXXDynamicCastExpr - A C++ @c dynamic_cast expression
/// (C++ [expr.dynamic.cast]), which may perform a run-time check to 
/// determine how to perform the type cast.
/// 
/// This expression node represents a dynamic cast, e.g.,
/// @c dynamic_cast<Derived*>(BasePtr).
class CXXDynamicCastExpr : public CXXNamedCastExpr {
public:
  CXXDynamicCastExpr(QualType ty, CastKind kind, Expr *op, QualType writtenTy, 
                     SourceLocation l)
    : CXXNamedCastExpr(CXXDynamicCastExprClass, ty, kind, op, writtenTy, l) {}

  static bool classof(const Stmt *T) { 
    return T->getStmtClass() == CXXDynamicCastExprClass;
  }
  static bool classof(const CXXDynamicCastExpr *) { return true; }
};

/// CXXReinterpretCastExpr - A C++ @c reinterpret_cast expression (C++
/// [expr.reinterpret.cast]), which provides a differently-typed view
/// of a value but performs no actual work at run time.
/// 
/// This expression node represents a reinterpret cast, e.g.,
/// @c reinterpret_cast<int>(VoidPtr).
class CXXReinterpretCastExpr : public CXXNamedCastExpr {
public:
  CXXReinterpretCastExpr(QualType ty, Expr *op, QualType writtenTy, 
                         SourceLocation l)
    : CXXNamedCastExpr(CXXReinterpretCastExprClass, ty, CK_BitCast, op, 
                       writtenTy, l) {}

  static bool classof(const Stmt *T) { 
    return T->getStmtClass() == CXXReinterpretCastExprClass;
  }
  static bool classof(const CXXReinterpretCastExpr *) { return true; }
};

/// CXXConstCastExpr - A C++ @c const_cast expression (C++ [expr.const.cast]),
/// which can remove type qualifiers but does not change the underlying value.
/// 
/// This expression node represents a const cast, e.g.,
/// @c const_cast<char*>(PtrToConstChar).
class CXXConstCastExpr : public CXXNamedCastExpr {
public:
  CXXConstCastExpr(QualType ty, Expr *op, QualType writtenTy, 
                   SourceLocation l)
    : CXXNamedCastExpr(CXXConstCastExprClass, ty, CK_NoOp, op, writtenTy, l) {}

  static bool classof(const Stmt *T) { 
    return T->getStmtClass() == CXXConstCastExprClass;
  }
  static bool classof(const CXXConstCastExpr *) { return true; }
};

/// CXXBoolLiteralExpr - [C++ 2.13.5] C++ Boolean Literal.
/// 
class CXXBoolLiteralExpr : public Expr {
  bool Value;
  SourceLocation Loc;
public:
  CXXBoolLiteralExpr(bool val, QualType Ty, SourceLocation l) : 
    Expr(CXXBoolLiteralExprClass, Ty), Value(val), Loc(l) {}

  bool getValue() const { return Value; }

  virtual SourceRange getSourceRange() const { return SourceRange(Loc); }
    
  static bool classof(const Stmt *T) { 
    return T->getStmtClass() == CXXBoolLiteralExprClass;
  }
  static bool classof(const CXXBoolLiteralExpr *) { return true; }
      
  // Iterators
  virtual child_iterator child_begin();
  virtual child_iterator child_end();
};

/// CXXNullPtrLiteralExpr - [C++0x 2.14.7] C++ Pointer Literal
class CXXNullPtrLiteralExpr : public Expr {
  SourceLocation Loc;
public:
  CXXNullPtrLiteralExpr(QualType Ty, SourceLocation l) :
    Expr(CXXNullPtrLiteralExprClass, Ty), Loc(l) {}

  virtual SourceRange getSourceRange() const { return SourceRange(Loc); }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == CXXNullPtrLiteralExprClass;
  }
  static bool classof(const CXXNullPtrLiteralExpr *) { return true; }

  virtual child_iterator child_begin();
  virtual child_iterator child_end();
};

/// CXXTypeidExpr - A C++ @c typeid expression (C++ [expr.typeid]), which gets
/// the type_info that corresponds to the supplied type, or the (possibly
/// dynamic) type of the supplied expression.
///
/// This represents code like @c typeid(int) or @c typeid(*objPtr)
class CXXTypeidExpr : public Expr {
private:
  bool isTypeOp : 1;
  union {
    void *Ty;
    Stmt *Ex;
  } Operand;
  SourceRange Range;

public:
  CXXTypeidExpr(bool isTypeOp, void *op, QualType Ty, const SourceRange r) :
      Expr(CXXTypeidExprClass, Ty,
        // typeid is never type-dependent (C++ [temp.dep.expr]p4)
        false,
        // typeid is value-dependent if the type or expression are dependent
        (isTypeOp ? QualType::getFromOpaquePtr(op)->isDependentType()
                  : static_cast<Expr*>(op)->isValueDependent())),
      isTypeOp(isTypeOp), Range(r) {
    if (isTypeOp)
      Operand.Ty = op;
    else
      // op was an Expr*, so cast it back to that to be safe
      Operand.Ex = static_cast<Expr*>(op);
  }

  bool isTypeOperand() const { return isTypeOp; }
  QualType getTypeOperand() const {
    assert(isTypeOperand() && "Cannot call getTypeOperand for typeid(expr)");
    return QualType::getFromOpaquePtr(Operand.Ty);
  }
  Expr* getExprOperand() const {
    assert(!isTypeOperand() && "Cannot call getExprOperand for typeid(type)");
    return static_cast<Expr*>(Operand.Ex);
  }

  virtual SourceRange getSourceRange() const {
    return Range;
  }
  static bool classof(const Stmt *T) {
    return T->getStmtClass() == CXXTypeidExprClass;
  }
  static bool classof(const CXXTypeidExpr *) { return true; }

  // Iterators
  virtual child_iterator child_begin();
  virtual child_iterator child_end();
};

/// CXXThisExpr - Represents the "this" expression in C++, which is a
/// pointer to the object on which the current member function is
/// executing (C++ [expr.prim]p3). Example:
///
/// @code
/// class Foo {
/// public:
///   void bar();
///   void test() { this->bar(); }
/// };
/// @endcode
class CXXThisExpr : public Expr {
  SourceLocation Loc;

public:
  CXXThisExpr(SourceLocation L, QualType Type) 
    : Expr(CXXThisExprClass, Type,
           // 'this' is type-dependent if the class type of the enclosing
           // member function is dependent (C++ [temp.dep.expr]p2)
           Type->isDependentType(), Type->isDependentType()),
      Loc(L) { }

  virtual SourceRange getSourceRange() const { return SourceRange(Loc); }

  static bool classof(const Stmt *T) { 
    return T->getStmtClass() == CXXThisExprClass;
  }
  static bool classof(const CXXThisExpr *) { return true; }

  // Iterators
  virtual child_iterator child_begin();
  virtual child_iterator child_end();
};

///  CXXThrowExpr - [C++ 15] C++ Throw Expression.  This handles
///  'throw' and 'throw' assignment-expression.  When
///  assignment-expression isn't present, Op will be null.
///
class CXXThrowExpr : public Expr {
  Stmt *Op;
  SourceLocation ThrowLoc;
public:
  // Ty is the void type which is used as the result type of the
  // exepression.  The l is the location of the throw keyword.  expr
  // can by null, if the optional expression to throw isn't present.
  CXXThrowExpr(Expr *expr, QualType Ty, SourceLocation l) :
    Expr(CXXThrowExprClass, Ty, false, false), Op(expr), ThrowLoc(l) {}
  const Expr *getSubExpr() const { return cast_or_null<Expr>(Op); }
  Expr *getSubExpr() { return cast_or_null<Expr>(Op); }
  void setSubExpr(Expr *E) { Op = E; }

  SourceLocation getThrowLoc() const { return ThrowLoc; }
  void setThrowLoc(SourceLocation L) { ThrowLoc = L; }

  virtual SourceRange getSourceRange() const {
    if (getSubExpr() == 0)
      return SourceRange(ThrowLoc, ThrowLoc);
    return SourceRange(ThrowLoc, getSubExpr()->getSourceRange().getEnd());
  }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == CXXThrowExprClass;
  }
  static bool classof(const CXXThrowExpr *) { return true; }

  // Iterators
  virtual child_iterator child_begin();
  virtual child_iterator child_end();
};

/// CXXDefaultArgExpr - C++ [dcl.fct.default]. This wraps up a
/// function call argument that was created from the corresponding
/// parameter's default argument, when the call did not explicitly
/// supply arguments for all of the parameters.
class CXXDefaultArgExpr : public Expr {
  ParmVarDecl *Param;
    
protected:
  CXXDefaultArgExpr(StmtClass SC, ParmVarDecl *param) 
    : Expr(SC, param->hasUnparsedDefaultArg() ? 
           param->getType().getNonReferenceType()
           : param->getDefaultArg()->getType()),
    Param(param) { }
    
public:
  // Param is the parameter whose default argument is used by this
  // expression.
  static CXXDefaultArgExpr *Create(ASTContext &C, ParmVarDecl *Param) {
    return new (C) CXXDefaultArgExpr(CXXDefaultArgExprClass, Param);
  }

  // Retrieve the parameter that the argument was created from.
  const ParmVarDecl *getParam() const { return Param; }
  ParmVarDecl *getParam() { return Param; }

  // Retrieve the actual argument to the function call.
  const Expr *getExpr() const { return Param->getDefaultArg(); }
  Expr *getExpr() { return Param->getDefaultArg(); }

  virtual SourceRange getSourceRange() const {
    // Default argument expressions have no representation in the
    // source, so they have an empty source range.
    return SourceRange();
  }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == CXXDefaultArgExprClass;
  }
  static bool classof(const CXXDefaultArgExpr *) { return true; }

  // Iterators
  virtual child_iterator child_begin();
  virtual child_iterator child_end();
};

/// CXXTemporary - Represents a C++ temporary.
class CXXTemporary {
  /// Destructor - The destructor that needs to be called.
  const CXXDestructorDecl *Destructor;
  
  CXXTemporary(const CXXDestructorDecl *destructor)
    : Destructor(destructor) { }
  ~CXXTemporary() { }

public:
  static CXXTemporary *Create(ASTContext &C, 
                              const CXXDestructorDecl *Destructor);
  
  void Destroy(ASTContext &Ctx);
  
  const CXXDestructorDecl *getDestructor() const { return Destructor; }
};

/// CXXBindTemporaryExpr - Represents binding an expression to a temporary, 
/// so its destructor can be called later.
class CXXBindTemporaryExpr : public Expr {
  CXXTemporary *Temp;
  
  Stmt *SubExpr;

  CXXBindTemporaryExpr(CXXTemporary *temp, Expr* subexpr) 
   : Expr(CXXBindTemporaryExprClass,
          subexpr->getType()), Temp(temp), SubExpr(subexpr) { }
  ~CXXBindTemporaryExpr() { } 

protected:
  virtual void DoDestroy(ASTContext &C);

public:
  static CXXBindTemporaryExpr *Create(ASTContext &C, CXXTemporary *Temp, 
                                      Expr* SubExpr);
  
  CXXTemporary *getTemporary() { return Temp; }
  const CXXTemporary *getTemporary() const { return Temp; }

  const Expr *getSubExpr() const { return cast<Expr>(SubExpr); }
  Expr *getSubExpr() { return cast<Expr>(SubExpr); }
  void setSubExpr(Expr *E) { SubExpr = E; }

  virtual SourceRange getSourceRange() const { return SourceRange(); }

  // Implement isa/cast/dyncast/etc.
  static bool classof(const Stmt *T) {
    return T->getStmtClass() == CXXBindTemporaryExprClass;
  }
  static bool classof(const CXXBindTemporaryExpr *) { return true; }

  // Iterators
  virtual child_iterator child_begin();
  virtual child_iterator child_end();
};

/// CXXConstructExpr - Represents a call to a C++ constructor.
class CXXConstructExpr : public Expr {
  CXXConstructorDecl *Constructor;

  bool Elidable;
  
  Stmt **Args;
  unsigned NumArgs;
  
protected:
  CXXConstructExpr(ASTContext &C, StmtClass SC, QualType T, 
                   CXXConstructorDecl *d, bool elidable,
                   Expr **args, unsigned numargs);
  ~CXXConstructExpr() { } 

  virtual void DoDestroy(ASTContext &C);

public:
  static CXXConstructExpr *Create(ASTContext &C, QualType T,
                                  CXXConstructorDecl *D, bool Elidable, 
                                  Expr **Args, unsigned NumArgs);
  
  
  CXXConstructorDecl* getConstructor() const { return Constructor; }

  /// \brief Whether this construction is elidable.
  bool isElidable() const { return Elidable; }

  typedef ExprIterator arg_iterator;
  typedef ConstExprIterator const_arg_iterator;
  
  arg_iterator arg_begin() { return Args; }
  arg_iterator arg_end() { return Args + NumArgs; }
  const_arg_iterator arg_begin() const { return Args; }
  const_arg_iterator arg_end() const { return Args + NumArgs; }

  unsigned getNumArgs() const { return NumArgs; }

  /// setArg - Set the specified argument.
  void setArg(unsigned Arg, Expr *ArgExpr) {
    assert(Arg < NumArgs && "Arg access out of range!");
    Args[Arg] = ArgExpr;
  }

  virtual SourceRange getSourceRange() const { return SourceRange(); }

  static bool classof(const Stmt *T) { 
    return T->getStmtClass() == CXXConstructExprClass ||
      T->getStmtClass() == CXXTemporaryObjectExprClass;
  }
  static bool classof(const CXXConstructExpr *) { return true; }
  
  // Iterators
  virtual child_iterator child_begin();
  virtual child_iterator child_end();
};

/// CXXFunctionalCastExpr - Represents an explicit C++ type conversion
/// that uses "functional" notion (C++ [expr.type.conv]). Example: @c
/// x = int(0.5);
class CXXFunctionalCastExpr : public ExplicitCastExpr {
  CXXMethodDecl *TypeConversionMethod;
  SourceLocation TyBeginLoc;
  SourceLocation RParenLoc;
public:
  CXXFunctionalCastExpr(QualType ty, QualType writtenTy, 
                        SourceLocation tyBeginLoc, CastKind kind, 
                        Expr *castExpr, CXXMethodDecl *typeConversionMethod,
                        SourceLocation rParenLoc) : 
    ExplicitCastExpr(CXXFunctionalCastExprClass, ty, kind, castExpr, writtenTy),
    TypeConversionMethod(typeConversionMethod),
    TyBeginLoc(tyBeginLoc), RParenLoc(rParenLoc) {}

  CXXMethodDecl *getTypeConversionMethod() const 
  { return TypeConversionMethod; }
  SourceLocation getTypeBeginLoc() const { return TyBeginLoc; }
  SourceLocation getRParenLoc() const { return RParenLoc; }
  
  virtual SourceRange getSourceRange() const {
    return SourceRange(TyBeginLoc, RParenLoc);
  }
  static bool classof(const Stmt *T) { 
    return T->getStmtClass() == CXXFunctionalCastExprClass; 
  }
  static bool classof(const CXXFunctionalCastExpr *) { return true; }
};

/// @brief Represents a C++ functional cast expression that builds a
/// temporary object.
///
/// This expression type represents a C++ "functional" cast 
/// (C++[expr.type.conv]) with N != 1 arguments that invokes a
/// constructor to build a temporary object. If N == 0 but no
/// constructor will be called (because the functional cast is
/// performing a value-initialized an object whose class type has no
/// user-declared constructors), CXXZeroInitValueExpr will represent
/// the functional cast. Finally, with N == 1 arguments the functional
/// cast expression will be represented by CXXFunctionalCastExpr.
/// Example:
/// @code
/// struct X { X(int, float); }
///
/// X create_X() {
///   return X(1, 3.14f); // creates a CXXTemporaryObjectExpr
/// };
/// @endcode
class CXXTemporaryObjectExpr : public CXXConstructExpr {
  SourceLocation TyBeginLoc;
  SourceLocation RParenLoc;

public:
  CXXTemporaryObjectExpr(ASTContext &C, CXXConstructorDecl *Cons, 
                         QualType writtenTy, SourceLocation tyBeginLoc, 
                         Expr **Args,unsigned NumArgs, 
                         SourceLocation rParenLoc);

  ~CXXTemporaryObjectExpr() { } 

  SourceLocation getTypeBeginLoc() const { return TyBeginLoc; }
  SourceLocation getRParenLoc() const { return RParenLoc; }

  virtual SourceRange getSourceRange() const {
    return SourceRange(TyBeginLoc, RParenLoc);
  }
  static bool classof(const Stmt *T) { 
    return T->getStmtClass() == CXXTemporaryObjectExprClass;
  }
  static bool classof(const CXXTemporaryObjectExpr *) { return true; }
};

/// CXXZeroInitValueExpr - [C++ 5.2.3p2]
/// Expression "T()" which creates a value-initialized rvalue of type
/// T, which is either a non-class type or a class type without any
/// user-defined constructors.
///
class CXXZeroInitValueExpr : public Expr {
  SourceLocation TyBeginLoc;
  SourceLocation RParenLoc;

public:
  CXXZeroInitValueExpr(QualType ty, SourceLocation tyBeginLoc,
                       SourceLocation rParenLoc ) : 
    Expr(CXXZeroInitValueExprClass, ty, false, false),
    TyBeginLoc(tyBeginLoc), RParenLoc(rParenLoc) {}
  
  SourceLocation getTypeBeginLoc() const { return TyBeginLoc; }
  SourceLocation getRParenLoc() const { return RParenLoc; }

  /// @brief Whether this initialization expression was
  /// implicitly-generated.
  bool isImplicit() const { 
    return TyBeginLoc.isInvalid() && RParenLoc.isInvalid(); 
  }

  virtual SourceRange getSourceRange() const {
    return SourceRange(TyBeginLoc, RParenLoc);
  }
    
  static bool classof(const Stmt *T) { 
    return T->getStmtClass() == CXXZeroInitValueExprClass;
  }
  static bool classof(const CXXZeroInitValueExpr *) { return true; }
      
  // Iterators
  virtual child_iterator child_begin();
  virtual child_iterator child_end();
};

/// CXXConditionDeclExpr - Condition declaration of a if/switch/while/for
/// statement, e.g: "if (int x = f()) {...}".
/// The main difference with DeclRefExpr is that CXXConditionDeclExpr owns the
/// decl that it references.
///
class CXXConditionDeclExpr : public DeclRefExpr {
public:
  CXXConditionDeclExpr(SourceLocation startLoc,
                       SourceLocation eqLoc, VarDecl *var)
    : DeclRefExpr(CXXConditionDeclExprClass, var, 
                  var->getType().getNonReferenceType(), startLoc,
                  var->getType()->isDependentType(),
                  /*FIXME:integral constant?*/
                    var->getType()->isDependentType()) {}

  SourceLocation getStartLoc() const { return getLocation(); }
  
  VarDecl *getVarDecl() { return cast<VarDecl>(getDecl()); }
  const VarDecl *getVarDecl() const { return cast<VarDecl>(getDecl()); }

  virtual SourceRange getSourceRange() const {
    return SourceRange(getStartLoc(), getVarDecl()->getInit()->getLocEnd());
  }
    
  static bool classof(const Stmt *T) { 
    return T->getStmtClass() == CXXConditionDeclExprClass;
  }
  static bool classof(const CXXConditionDeclExpr *) { return true; }
      
  // Iterators
  virtual child_iterator child_begin();
  virtual child_iterator child_end();
};

/// CXXNewExpr - A new expression for memory allocation and constructor calls,
/// e.g: "new CXXNewExpr(foo)".
class CXXNewExpr : public Expr {
  // Was the usage ::new, i.e. is the global new to be used?
  bool GlobalNew : 1;
  // Was the form (type-id) used? Otherwise, it was new-type-id.
  bool ParenTypeId : 1;
  // Is there an initializer? If not, built-ins are uninitialized, else they're
  // value-initialized.
  bool Initializer : 1;
  // Do we allocate an array? If so, the first SubExpr is the size expression.
  bool Array : 1;
  // The number of placement new arguments.
  unsigned NumPlacementArgs : 14;
  // The number of constructor arguments. This may be 1 even for non-class
  // types; use the pseudo copy constructor.
  unsigned NumConstructorArgs : 14;
  // Contains an optional array size expression, any number of optional
  // placement arguments, and any number of optional constructor arguments,
  // in that order.
  Stmt **SubExprs;
  // Points to the allocation function used.
  FunctionDecl *OperatorNew;
  // Points to the deallocation function used in case of error. May be null.
  FunctionDecl *OperatorDelete;
  // Points to the constructor used. Cannot be null if AllocType is a record;
  // it would still point at the default constructor (even an implicit one).
  // Must be null for all other types.
  CXXConstructorDecl *Constructor;

  SourceLocation StartLoc;
  SourceLocation EndLoc;

public:
  CXXNewExpr(bool globalNew, FunctionDecl *operatorNew, Expr **placementArgs,
             unsigned numPlaceArgs, bool ParenTypeId, Expr *arraySize,
             CXXConstructorDecl *constructor, bool initializer,
             Expr **constructorArgs, unsigned numConsArgs,
             FunctionDecl *operatorDelete, QualType ty,
             SourceLocation startLoc, SourceLocation endLoc);
  ~CXXNewExpr() {
    delete[] SubExprs;
  }

  QualType getAllocatedType() const {
    assert(getType()->isPointerType());
    return getType()->getAs<PointerType>()->getPointeeType();
  }

  FunctionDecl *getOperatorNew() const { return OperatorNew; }
  FunctionDecl *getOperatorDelete() const { return OperatorDelete; }
  CXXConstructorDecl *getConstructor() const { return Constructor; }

  bool isArray() const { return Array; }
  Expr *getArraySize() {
    return Array ? cast<Expr>(SubExprs[0]) : 0;
  }
  const Expr *getArraySize() const {
    return Array ? cast<Expr>(SubExprs[0]) : 0;
  }

  unsigned getNumPlacementArgs() const { return NumPlacementArgs; }
  Expr *getPlacementArg(unsigned i) {
    assert(i < NumPlacementArgs && "Index out of range");
    return cast<Expr>(SubExprs[Array + i]);
  }
  const Expr *getPlacementArg(unsigned i) const {
    assert(i < NumPlacementArgs && "Index out of range");
    return cast<Expr>(SubExprs[Array + i]);
  }

  bool isGlobalNew() const { return GlobalNew; }
  bool isParenTypeId() const { return ParenTypeId; }
  bool hasInitializer() const { return Initializer; }

  unsigned getNumConstructorArgs() const { return NumConstructorArgs; }
  Expr *getConstructorArg(unsigned i) {
    assert(i < NumConstructorArgs && "Index out of range");
    return cast<Expr>(SubExprs[Array + NumPlacementArgs + i]);
  }
  const Expr *getConstructorArg(unsigned i) const {
    assert(i < NumConstructorArgs && "Index out of range");
    return cast<Expr>(SubExprs[Array + NumPlacementArgs + i]);
  }

  typedef ExprIterator arg_iterator;
  typedef ConstExprIterator const_arg_iterator;

  arg_iterator placement_arg_begin() {
    return SubExprs + Array;
  }
  arg_iterator placement_arg_end() {
    return SubExprs + Array + getNumPlacementArgs();
  }
  const_arg_iterator placement_arg_begin() const {
    return SubExprs + Array;
  }
  const_arg_iterator placement_arg_end() const {
    return SubExprs + Array + getNumPlacementArgs();
  }

  arg_iterator constructor_arg_begin() {
    return SubExprs + Array + getNumPlacementArgs();
  }
  arg_iterator constructor_arg_end() {
    return SubExprs + Array + getNumPlacementArgs() + getNumConstructorArgs();
  }
  const_arg_iterator constructor_arg_begin() const {
    return SubExprs + Array + getNumPlacementArgs();
  }
  const_arg_iterator constructor_arg_end() const {
    return SubExprs + Array + getNumPlacementArgs() + getNumConstructorArgs();
  }

  virtual SourceRange getSourceRange() const {
    return SourceRange(StartLoc, EndLoc);
  }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == CXXNewExprClass;
  }
  static bool classof(const CXXNewExpr *) { return true; }

  // Iterators
  virtual child_iterator child_begin();
  virtual child_iterator child_end();
};

/// CXXDeleteExpr - A delete expression for memory deallocation and destructor
/// calls, e.g. "delete[] pArray".
class CXXDeleteExpr : public Expr {
  // Is this a forced global delete, i.e. "::delete"?
  bool GlobalDelete : 1;
  // Is this the array form of delete, i.e. "delete[]"?
  bool ArrayForm : 1;
  // Points to the operator delete overload that is used. Could be a member.
  FunctionDecl *OperatorDelete;
  // The pointer expression to be deleted.
  Stmt *Argument;
  // Location of the expression.
  SourceLocation Loc;
public:
  CXXDeleteExpr(QualType ty, bool globalDelete, bool arrayForm,
                FunctionDecl *operatorDelete, Expr *arg, SourceLocation loc)
    : Expr(CXXDeleteExprClass, ty, false, false), GlobalDelete(globalDelete),
      ArrayForm(arrayForm), OperatorDelete(operatorDelete), Argument(arg),
      Loc(loc) { }

  bool isGlobalDelete() const { return GlobalDelete; }
  bool isArrayForm() const { return ArrayForm; }

  FunctionDecl *getOperatorDelete() const { return OperatorDelete; }

  Expr *getArgument() { return cast<Expr>(Argument); }
  const Expr *getArgument() const { return cast<Expr>(Argument); }

  virtual SourceRange getSourceRange() const {
    return SourceRange(Loc, Argument->getLocEnd());
  }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == CXXDeleteExprClass;
  }
  static bool classof(const CXXDeleteExpr *) { return true; }

  // Iterators
  virtual child_iterator child_begin();
  virtual child_iterator child_end();
};

/// \brief Represents the name of a function that has not been
/// resolved to any declaration.
///
/// Unresolved function names occur when a function name is
/// encountered prior to an open parentheses ('(') in a C++ function
/// call, and the function name itself did not resolve to a
/// declaration. These function names can only be resolved when they
/// form the postfix-expression of a function call, so that
/// argument-dependent lookup finds declarations corresponding to
/// these functions.

/// @code
/// template<typename T> void f(T x) {
///   g(x); // g is an unresolved function name (that is also a dependent name)
/// }
/// @endcode
class UnresolvedFunctionNameExpr : public Expr {
  /// The name that was present in the source 
  DeclarationName Name;

  /// The location of this name in the source code
  SourceLocation Loc;

public:
  UnresolvedFunctionNameExpr(DeclarationName N, QualType T, SourceLocation L)
    : Expr(UnresolvedFunctionNameExprClass, T, false, false), Name(N), Loc(L) { }

  /// \brief Retrieves the name that occurred in the source code.
  DeclarationName getName() const { return Name; }

  /// getLocation - Retrieves the location in the source code where
  /// the name occurred.
  SourceLocation getLocation() const { return Loc; }

  virtual SourceRange getSourceRange() const { return SourceRange(Loc); }

  static bool classof(const Stmt *T) { 
    return T->getStmtClass() == UnresolvedFunctionNameExprClass;
  }
  static bool classof(const UnresolvedFunctionNameExpr *) { return true; }

  // Iterators
  virtual child_iterator child_begin();
  virtual child_iterator child_end();
};

/// UnaryTypeTraitExpr - A GCC or MS unary type trait, as used in the
/// implementation of TR1/C++0x type trait templates.
/// Example:
/// __is_pod(int) == true
/// __is_enum(std::string) == false
class UnaryTypeTraitExpr : public Expr {
  /// UTT - The trait.
  UnaryTypeTrait UTT;

  /// Loc - The location of the type trait keyword.
  SourceLocation Loc;

  /// RParen - The location of the closing paren.
  SourceLocation RParen;

  /// QueriedType - The type we're testing.
  QualType QueriedType;

public:
  UnaryTypeTraitExpr(SourceLocation loc, UnaryTypeTrait utt, QualType queried,
                     SourceLocation rparen, QualType ty)
    : Expr(UnaryTypeTraitExprClass, ty, false, queried->isDependentType()),
      UTT(utt), Loc(loc), RParen(rparen), QueriedType(queried) { }

  virtual SourceRange getSourceRange() const { return SourceRange(Loc, RParen);}

  UnaryTypeTrait getTrait() const { return UTT; }

  QualType getQueriedType() const { return QueriedType; }

  bool EvaluateTrait(ASTContext&) const;

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == UnaryTypeTraitExprClass;
  }
  static bool classof(const UnaryTypeTraitExpr *) { return true; }

  // Iterators
  virtual child_iterator child_begin();
  virtual child_iterator child_end();
};

/// QualifiedDeclRefExpr - A reference to a declared variable,
/// function, enum, etc., that includes a qualification, e.g.,
/// "N::foo".
class QualifiedDeclRefExpr : public DeclRefExpr {
  /// QualifierRange - The source range that covers the
  /// nested-name-specifier.
  SourceRange QualifierRange;

  /// \brief The nested-name-specifier that qualifies this declaration
  /// name.
  NestedNameSpecifier *NNS;

public:
  QualifiedDeclRefExpr(NamedDecl *d, QualType t, SourceLocation l, bool TD, 
                       bool VD, SourceRange R, NestedNameSpecifier *NNS)
    : DeclRefExpr(QualifiedDeclRefExprClass, d, t, l, TD, VD), 
      QualifierRange(R), NNS(NNS) { }

  /// \brief Retrieve the source range of the nested-name-specifier.
  SourceRange getQualifierRange() const { return QualifierRange; }

  /// \brief Retrieve the nested-name-specifier that qualifies this
  /// declaration.
  NestedNameSpecifier *getQualifier() const { return NNS; }

  virtual SourceRange getSourceRange() const { 
    return SourceRange(QualifierRange.getBegin(), getLocation()); 
  }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == QualifiedDeclRefExprClass;
  }
  static bool classof(const QualifiedDeclRefExpr *) { return true; }
};

/// \brief A qualified reference to a name whose declaration cannot
/// yet be resolved.
///
/// UnresolvedDeclRefExpr is similar to QualifiedDeclRefExpr in that
/// it expresses a qualified reference to a declaration such as
/// X<T>::value. The difference, however, is that an
/// UnresolvedDeclRefExpr node is used only within C++ templates when
/// the qualification (e.g., X<T>::) refers to a dependent type. In
/// this case, X<T>::value cannot resolve to a declaration because the
/// declaration will differ from on instantiation of X<T> to the
/// next. Therefore, UnresolvedDeclRefExpr keeps track of the
/// qualifier (X<T>::) and the name of the entity being referenced
/// ("value"). Such expressions will instantiate to
/// QualifiedDeclRefExprs.
class UnresolvedDeclRefExpr : public Expr {
  /// The name of the entity we will be referencing.
  DeclarationName Name;

  /// Location of the name of the declaration we're referencing.
  SourceLocation Loc;

  /// QualifierRange - The source range that covers the
  /// nested-name-specifier.
  SourceRange QualifierRange;

  /// \brief The nested-name-specifier that qualifies this unresolved
  /// declaration name.
  NestedNameSpecifier *NNS;

  /// \brief Whether this expr is an address of (&) operand.
  bool IsAddressOfOperand;
  
public:
  UnresolvedDeclRefExpr(DeclarationName N, QualType T, SourceLocation L,
                        SourceRange R, NestedNameSpecifier *NNS, 
                        bool IsAddressOfOperand)
    : Expr(UnresolvedDeclRefExprClass, T, true, true), 
      Name(N), Loc(L), QualifierRange(R), NNS(NNS), 
      IsAddressOfOperand(IsAddressOfOperand) { }

  /// \brief Retrieve the name that this expression refers to.
  DeclarationName getDeclName() const { return Name; }

  /// \brief Retrieve the location of the name within the expression.
  SourceLocation getLocation() const { return Loc; }

  /// \brief Retrieve the source range of the nested-name-specifier.
  SourceRange getQualifierRange() const { return QualifierRange; }

  /// \brief Retrieve the nested-name-specifier that qualifies this
  /// declaration.
  NestedNameSpecifier *getQualifier() const { return NNS; }

  /// \brief Retrieve whether this is an address of (&) operand.
  
  bool isAddressOfOperand() const { return IsAddressOfOperand; }
  virtual SourceRange getSourceRange() const { 
    return SourceRange(QualifierRange.getBegin(), getLocation()); 
  }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == UnresolvedDeclRefExprClass;
  }
  static bool classof(const UnresolvedDeclRefExpr *) { return true; }

  virtual StmtIterator child_begin();
  virtual StmtIterator child_end();
};

/// \brief An expression that refers to a C++ template-id, such as 
/// @c isa<FunctionDecl>. 
class TemplateIdRefExpr : public Expr {
  /// \brief If this template-id was qualified-id, e.g., @c std::sort<int>,
  /// this nested name specifier contains the @c std::.
  NestedNameSpecifier *Qualifier;
  
  /// \brief If this template-id was a qualified-id, e.g., @c std::sort<int>,
  /// this covers the source code range of the @c std::.
  SourceRange QualifierRange;
  
  /// \brief The actual template to which this template-id refers.
  TemplateName Template;
  
  /// \brief The source location of the template name.
  SourceLocation TemplateNameLoc;

  /// \brief The source location of the left angle bracket ('<');
  SourceLocation LAngleLoc;
  
  /// \brief The source location of the right angle bracket ('>');
  SourceLocation RAngleLoc;
  
  /// \brief The number of template arguments in TemplateArgs.
  unsigned NumTemplateArgs;
  
  TemplateIdRefExpr(QualType T,
                    NestedNameSpecifier *Qualifier, SourceRange QualifierRange,
                    TemplateName Template, SourceLocation TemplateNameLoc,
                    SourceLocation LAngleLoc, 
                    const TemplateArgument *TemplateArgs,
                    unsigned NumTemplateArgs,
                    SourceLocation RAngleLoc);
  
  virtual void DoDestroy(ASTContext &Context);
  
public:
  static TemplateIdRefExpr *
  Create(ASTContext &Context, QualType T,
         NestedNameSpecifier *Qualifier, SourceRange QualifierRange,
         TemplateName Template, SourceLocation TemplateNameLoc,
         SourceLocation LAngleLoc, const TemplateArgument *TemplateArgs,
         unsigned NumTemplateArgs, SourceLocation RAngleLoc);
  
  /// \brief Retrieve the nested name specifier used to qualify the name of
  /// this template-id, e.g., the "std::sort" in @c std::sort<int>, or NULL
  /// if this template-id was an unqualified-id.
  NestedNameSpecifier *getQualifier() const { return Qualifier; }
  
  /// \brief Retrieve the source range describing the nested name specifier
  /// used to qualified the name of this template-id, if the name was qualified.
  SourceRange getQualifierRange() const { return QualifierRange; }
  
  /// \brief Retrieve the name of the template referenced, e.g., "sort" in
  /// @c std::sort<int>;
  TemplateName getTemplateName() const { return Template; }
  
  /// \brief Retrieve the location of the name of the template referenced, e.g.,
  /// the location of "sort" in @c std::sort<int>.
  SourceLocation getTemplateNameLoc() const { return TemplateNameLoc; }
  
  /// \brief Retrieve the location of the left angle bracket following the 
  /// template name ('<').
  SourceLocation getLAngleLoc() const { return LAngleLoc; }
  
  /// \brief Retrieve the template arguments provided as part of this
  /// template-id.
  const TemplateArgument *getTemplateArgs() const { 
    return reinterpret_cast<const TemplateArgument *>(this + 1);
  }
  
  /// \brief Retrieve the number of template arguments provided as part of this
  /// template-id.
  unsigned getNumTemplateArgs() const { return NumTemplateArgs; }
    
  /// \brief Retrieve the location of the right angle bracket following the 
  /// template arguments ('>').
  SourceLocation getRAngleLoc() const { return RAngleLoc; }
  
  virtual SourceRange getSourceRange() const {
    return SourceRange(Qualifier? QualifierRange.getBegin() : TemplateNameLoc,
                       RAngleLoc);
  }
  
  // Iterators
  virtual child_iterator child_begin();
  virtual child_iterator child_end();
  
  static bool classof(const Stmt *T) { 
    return T->getStmtClass() == TemplateIdRefExprClass;
  }
  static bool classof(const TemplateIdRefExpr *) { return true; }
};
  
class CXXExprWithTemporaries : public Expr {
  Stmt *SubExpr;
    
  CXXTemporary **Temps;
  unsigned NumTemps;

  bool ShouldDestroyTemps;
  
  CXXExprWithTemporaries(Expr *SubExpr, CXXTemporary **Temps, 
                         unsigned NumTemps, bool ShouldDestroyTemps);
  ~CXXExprWithTemporaries();

protected:
  virtual void DoDestroy(ASTContext &C);

public:
  static CXXExprWithTemporaries *Create(ASTContext &C, Expr *SubExpr,
                                        CXXTemporary **Temps, unsigned NumTemps,
                                        bool ShouldDestroyTemporaries);
  
  unsigned getNumTemporaries() const { return NumTemps; }
  CXXTemporary *getTemporary(unsigned i) {
    assert(i < NumTemps && "Index out of range");
    return Temps[i];
  }
  const CXXTemporary *getTemporary(unsigned i) const {
    assert(i < NumTemps && "Index out of range");
    return Temps[i];
  }
  
  bool shouldDestroyTemporaries() const { return ShouldDestroyTemps; }
  
  void removeLastTemporary() { NumTemps--; }
  
  Expr *getSubExpr() { return cast<Expr>(SubExpr); }
  const Expr *getSubExpr() const { return cast<Expr>(SubExpr); }
  void setSubExpr(Expr *E) { SubExpr = E; }

  virtual SourceRange getSourceRange() const { return SourceRange(); }

  // Implement isa/cast/dyncast/etc.
  static bool classof(const Stmt *T) {
    return T->getStmtClass() == CXXExprWithTemporariesClass;
  }
  static bool classof(const CXXExprWithTemporaries *) { return true; }

  // Iterators
  virtual child_iterator child_begin();
  virtual child_iterator child_end();
};

/// \brief Describes an explicit type conversion that uses functional
/// notion but could not be resolved because one or more arguments are
/// type-dependent.
///
/// The explicit type conversions expressed by
/// CXXUnresolvedConstructExpr have the form \c T(a1, a2, ..., aN),
/// where \c T is some type and \c a1, a2, ..., aN are values, and
/// either \C T is a dependent type or one or more of the \c a's is
/// type-dependent. For example, this would occur in a template such
/// as:
///
/// \code
///   template<typename T, typename A1>
///   inline T make_a(const A1& a1) {
///     return T(a1);
///   }
/// \endcode
///
/// When the returned expression is instantiated, it may resolve to a
/// constructor call, conversion function call, or some kind of type
/// conversion.
class CXXUnresolvedConstructExpr : public Expr {
  /// \brief The starting location of the type
  SourceLocation TyBeginLoc;

  /// \brief The type being constructed.
  QualType Type;

  /// \brief The location of the left parentheses ('(').
  SourceLocation LParenLoc;

  /// \brief The location of the right parentheses (')').
  SourceLocation RParenLoc;

  /// \brief The number of arguments used to construct the type.
  unsigned NumArgs;
  
  CXXUnresolvedConstructExpr(SourceLocation TyBegin,
                             QualType T,
                             SourceLocation LParenLoc,
                             Expr **Args,
                             unsigned NumArgs,
                             SourceLocation RParenLoc);

public:
  static CXXUnresolvedConstructExpr *Create(ASTContext &C, 
                                            SourceLocation TyBegin,
                                            QualType T,
                                            SourceLocation LParenLoc,
                                            Expr **Args,
                                            unsigned NumArgs,
                                            SourceLocation RParenLoc);

  /// \brief Retrieve the source location where the type begins.
  SourceLocation getTypeBeginLoc() const { return TyBeginLoc; }
  void setTypeBeginLoc(SourceLocation L) { TyBeginLoc = L; }

  /// \brief Retrieve the type that is being constructed, as specified
  /// in the source code.
  QualType getTypeAsWritten() const { return Type; }
  void setTypeAsWritten(QualType T) { Type = T; }

  /// \brief Retrieve the location of the left parentheses ('(') that
  /// precedes the argument list.
  SourceLocation getLParenLoc() const { return LParenLoc; }
  void setLParenLoc(SourceLocation L) { LParenLoc = L; }

  /// \brief Retrieve the location of the right parentheses (')') that
  /// follows the argument list.
  SourceLocation getRParenLoc() const { return RParenLoc; }
  void setRParenLoc(SourceLocation L) { RParenLoc = L; }

  /// \brief Retrieve the number of arguments.
  unsigned arg_size() const { return NumArgs; }

  typedef Expr** arg_iterator;
  arg_iterator arg_begin() { return reinterpret_cast<Expr**>(this + 1); }
  arg_iterator arg_end() { return arg_begin() + NumArgs; }

  Expr *getArg(unsigned I) {
    assert(I < NumArgs && "Argument index out-of-range");
    return *(arg_begin() + I);
  }

  virtual SourceRange getSourceRange() const {
    return SourceRange(TyBeginLoc, RParenLoc);
  }
  static bool classof(const Stmt *T) { 
    return T->getStmtClass() == CXXUnresolvedConstructExprClass;
  }
  static bool classof(const CXXUnresolvedConstructExpr *) { return true; }

  // Iterators
  virtual child_iterator child_begin();
  virtual child_iterator child_end();
};

/// \brief Represents a C++ member access expression that was written using
/// a qualified name, e.g., "x->Base::f()".
class CXXQualifiedMemberExpr : public MemberExpr {
  /// QualifierRange - The source range that covers the
  /// nested-name-specifier.
  SourceRange QualifierRange;
  
  /// \brief The nested-name-specifier that qualifies this declaration
  /// name.
  NestedNameSpecifier *Qualifier;
  
public:
  CXXQualifiedMemberExpr(Expr *base, bool isarrow, NestedNameSpecifier *Qual,
                         SourceRange QualRange, NamedDecl *memberdecl, 
                         SourceLocation l, QualType ty) 
    : MemberExpr(CXXQualifiedMemberExprClass, base, isarrow, memberdecl, l, ty),
      QualifierRange(QualRange), Qualifier(Qual) { }

  /// \brief Retrieve the source range of the nested-name-specifier that 
  /// qualifies the member name.
  SourceRange getQualifierRange() const { return QualifierRange; }
  
  /// \brief Retrieve the nested-name-specifier that qualifies the
  /// member reference expression.
  NestedNameSpecifier *getQualifier() const { return Qualifier; }
  
  static bool classof(const Stmt *T) {
    return T->getStmtClass() == CXXQualifiedMemberExprClass;
  }
  static bool classof(const CXXQualifiedMemberExpr *) { return true; }  
};
  
/// \brief Represents a C++ member access expression where the actual member
/// referenced could not be resolved, e.g., because the base expression or the
/// member name was dependent.
class CXXUnresolvedMemberExpr : public Expr {
  /// \brief The expression for the base pointer or class reference,
  /// e.g., the \c x in x.f.
  Stmt *Base;
  
  /// \brief Whether this member expression used the '->' operator or
  /// the '.' operator.
  bool IsArrow;

  /// \brief The location of the '->' or '.' operator.
  SourceLocation OperatorLoc;

  /// \brief The member to which this member expression refers, which
  /// can be name, overloaded operator, or destructor.
  /// FIXME: could also be a template-id, and we might have a 
  /// nested-name-specifier as well.
  DeclarationName Member;

  /// \brief The location of the member name.
  SourceLocation MemberLoc;

public:
  CXXUnresolvedMemberExpr(ASTContext &C, 
                          Expr *Base, bool IsArrow, 
                          SourceLocation OperatorLoc,
                          DeclarationName Member,
                          SourceLocation MemberLoc)
    : Expr(CXXUnresolvedMemberExprClass, C.DependentTy, true, true),
      Base(Base), IsArrow(IsArrow), OperatorLoc(OperatorLoc),
      Member(Member), MemberLoc(MemberLoc) { }

  /// \brief Retrieve the base object of this member expressions,
  /// e.g., the \c x in \c x.m.
  Expr *getBase() { return cast<Expr>(Base); }
  void setBase(Expr *E) { Base = E; }

  /// \brief Determine whether this member expression used the '->'
  /// operator; otherwise, it used the '.' operator.
  bool isArrow() const { return IsArrow; }
  void setArrow(bool A) { IsArrow = A; }

  /// \brief Retrieve the location of the '->' or '.' operator.
  SourceLocation getOperatorLoc() const { return OperatorLoc; }
  void setOperatorLoc(SourceLocation L) { OperatorLoc = L; }

  /// \brief Retrieve the name of the member that this expression
  /// refers to.
  DeclarationName getMember() const { return Member; }
  void setMember(DeclarationName N) { Member = N; }

  // \brief Retrieve the location of the name of the member that this
  // expression refers to.
  SourceLocation getMemberLoc() const { return MemberLoc; }
  void setMemberLoc(SourceLocation L) { MemberLoc = L; }

  virtual SourceRange getSourceRange() const {
    return SourceRange(Base->getSourceRange().getBegin(),
                       MemberLoc);
  }
  static bool classof(const Stmt *T) { 
    return T->getStmtClass() == CXXUnresolvedMemberExprClass;
  }
  static bool classof(const CXXUnresolvedMemberExpr *) { return true; }

  // Iterators
  virtual child_iterator child_begin();
  virtual child_iterator child_end();
};

}  // end namespace clang

#endif
