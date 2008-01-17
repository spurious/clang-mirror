//===--- Expr.h - Classes for representing expressions ----------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the Expr interface and subclasses.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_EXPR_H
#define LLVM_CLANG_AST_EXPR_H

#include "clang/AST/Stmt.h"
#include "clang/AST/Type.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclObjC.h"
#include "clang/Basic/IdentifierTable.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/APFloat.h"

namespace clang {
  class IdentifierInfo;
  class Selector;
  class Decl;
  class ASTContext;
  
/// Expr - This represents one expression.  Note that Expr's are subclasses of
/// Stmt.  This allows an expression to be transparently used any place a Stmt
/// is required.
///
class Expr : public Stmt {
  QualType TR;
protected:
  Expr(StmtClass SC, QualType T) : Stmt(SC), TR(T) {}
public:  
  QualType getType() const { return TR; }
  void setType(QualType t) { TR = t; }

  /// SourceLocation tokens are not useful in isolation - they are low level
  /// value objects created/interpreted by SourceManager. We assume AST
  /// clients will have a pointer to the respective SourceManager.
  virtual SourceRange getSourceRange() const = 0;

  /// getExprLoc - Return the preferred location for the arrow when diagnosing
  /// a problem with a generic expression.
  virtual SourceLocation getExprLoc() const { return getLocStart(); }
  
  /// hasLocalSideEffect - Return true if this immediate expression has side
  /// effects, not counting any sub-expressions.
  bool hasLocalSideEffect() const;
  
  /// isLvalue - C99 6.3.2.1: an lvalue is an expression with an object type or
  /// incomplete type other than void. Nonarray expressions that can be lvalues:
  ///  - name, where name must be a variable
  ///  - e[i]
  ///  - (e), where e must be an lvalue
  ///  - e.name, where e must be an lvalue
  ///  - e->name
  ///  - *e, the type of e cannot be a function type
  ///  - string-constant
  ///  - reference type [C++ [expr]]
  ///
  enum isLvalueResult {
    LV_Valid,
    LV_NotObjectType,
    LV_IncompleteVoidType,
    LV_DuplicateVectorComponents,
    LV_InvalidExpression
  };
  isLvalueResult isLvalue() const;
  
  /// isModifiableLvalue - C99 6.3.2.1: an lvalue that does not have array type,
  /// does not have an incomplete type, does not have a const-qualified type,
  /// and if it is a structure or union, does not have any member (including, 
  /// recursively, any member or element of all contained aggregates or unions)
  /// with a const-qualified type.
  enum isModifiableLvalueResult {
    MLV_Valid,
    MLV_NotObjectType,
    MLV_IncompleteVoidType,
    MLV_DuplicateVectorComponents,
    MLV_InvalidExpression,
    MLV_IncompleteType,
    MLV_ConstQualified,
    MLV_ArrayType
  };
  isModifiableLvalueResult isModifiableLvalue() const;
  
  bool isNullPointerConstant(ASTContext &Ctx) const;

  /// isIntegerConstantExpr - Return true if this expression is a valid integer
  /// constant expression, and, if so, return its value in Result.  If not a
  /// valid i-c-e, return false and fill in Loc (if specified) with the location
  /// of the invalid expression.
  bool isIntegerConstantExpr(llvm::APSInt &Result, ASTContext &Ctx,
                             SourceLocation *Loc = 0,
                             bool isEvaluated = true) const;
  bool isIntegerConstantExpr(ASTContext &Ctx, SourceLocation *Loc = 0) const {
    llvm::APSInt X(32);
    return isIntegerConstantExpr(X, Ctx, Loc);
  }
  /// isConstantExpr - Return true if this expression is a valid constant expr.
  bool isConstantExpr(ASTContext &Ctx, SourceLocation *Loc) const;
  
  /// hasStaticStorage - Return true if this expression has static storage
  /// duration.  This means that the address of this expression is a link-time
  /// constant.
  bool hasStaticStorage() const;  
  
  /// IgnoreParens - Ignore parentheses.  If this Expr is a ParenExpr, return
  ///  its subexpression.  If that subexpression is also a ParenExpr, 
  ///  then this method recursively returns its subexpression, and so forth.
  ///  Otherwise, the method returns the current Expr.
  Expr* IgnoreParens();
  
  const Expr* IgnoreParens() const {
    return const_cast<Expr*>(this)->IgnoreParens();
  }

  static bool classof(const Stmt *T) { 
    return T->getStmtClass() >= firstExprConstant &&
           T->getStmtClass() <= lastExprConstant; 
  }
  static bool classof(const Expr *) { return true; }
  
  static inline Expr* Create(llvm::Deserializer& D) {
    return cast<Expr>(Stmt::Create(D));    
  }
};

//===----------------------------------------------------------------------===//
// Primary Expressions.
//===----------------------------------------------------------------------===//

/// DeclRefExpr - [C99 6.5.1p2] - A reference to a declared variable, function,
/// enum, etc.
class DeclRefExpr : public Expr {
  ValueDecl *D; 
  SourceLocation Loc;
public:
  DeclRefExpr(ValueDecl *d, QualType t, SourceLocation l) : 
    Expr(DeclRefExprClass, t), D(d), Loc(l) {}
  
  ValueDecl *getDecl() { return D; }
  const ValueDecl *getDecl() const { return D; }
  virtual SourceRange getSourceRange() const { return SourceRange(Loc); }
  
  
  static bool classof(const Stmt *T) { 
    return T->getStmtClass() == DeclRefExprClass; 
  }
  static bool classof(const DeclRefExpr *) { return true; }
  
  // Iterators
  virtual child_iterator child_begin();
  virtual child_iterator child_end();
  
  virtual void EmitImpl(llvm::Serializer& S) const;
  static DeclRefExpr* CreateImpl(llvm::Deserializer& D);
};

/// PreDefinedExpr - [C99 6.4.2.2] - A pre-defined identifier such as __func__.
class PreDefinedExpr : public Expr {
public:
  enum IdentType {
    Func,
    Function,
    PrettyFunction
  };
  
private:
  SourceLocation Loc;
  IdentType Type;
public:
  PreDefinedExpr(SourceLocation l, QualType type, IdentType IT) 
    : Expr(PreDefinedExprClass, type), Loc(l), Type(IT) {}
  
  IdentType getIdentType() const { return Type; }
  
  virtual SourceRange getSourceRange() const { return SourceRange(Loc); }

  static bool classof(const Stmt *T) { 
    return T->getStmtClass() == PreDefinedExprClass; 
  }
  static bool classof(const PreDefinedExpr *) { return true; }
  
  // Iterators
  virtual child_iterator child_begin();
  virtual child_iterator child_end();
  
  virtual void EmitImpl(llvm::Serializer& S) const;
  static PreDefinedExpr* CreateImpl(llvm::Deserializer& D);
};

class IntegerLiteral : public Expr {
  llvm::APInt Value;
  SourceLocation Loc;
public:
  // type should be IntTy, LongTy, LongLongTy, UnsignedIntTy, UnsignedLongTy, 
  // or UnsignedLongLongTy
  IntegerLiteral(const llvm::APInt &V, QualType type, SourceLocation l)
    : Expr(IntegerLiteralClass, type), Value(V), Loc(l) {
    assert(type->isIntegerType() && "Illegal type in IntegerLiteral");
  }
  const llvm::APInt &getValue() const { return Value; }
  virtual SourceRange getSourceRange() const { return SourceRange(Loc); }

  static bool classof(const Stmt *T) { 
    return T->getStmtClass() == IntegerLiteralClass; 
  }
  static bool classof(const IntegerLiteral *) { return true; }
  
  // Iterators
  virtual child_iterator child_begin();
  virtual child_iterator child_end();
  
  virtual void EmitImpl(llvm::Serializer& S) const;
  static IntegerLiteral* CreateImpl(llvm::Deserializer& D);
};

class CharacterLiteral : public Expr {
  unsigned Value;
  SourceLocation Loc;
public:
  // type should be IntTy
  CharacterLiteral(unsigned value, QualType type, SourceLocation l)
    : Expr(CharacterLiteralClass, type), Value(value), Loc(l) {
  }
  SourceLocation getLoc() const { return Loc; }
  
  virtual SourceRange getSourceRange() const { return SourceRange(Loc); }
  
  unsigned getValue() const { return Value; }

  static bool classof(const Stmt *T) { 
    return T->getStmtClass() == CharacterLiteralClass; 
  }
  static bool classof(const CharacterLiteral *) { return true; }

  // Iterators
  virtual child_iterator child_begin();
  virtual child_iterator child_end();
  
  virtual void EmitImpl(llvm::Serializer& S) const;
  static CharacterLiteral* CreateImpl(llvm::Deserializer& D);
};

class FloatingLiteral : public Expr {
  llvm::APFloat Value;
  bool IsExact : 1;
  SourceLocation Loc;
public:
  FloatingLiteral(const llvm::APFloat &V, bool* isexact, 
                  QualType Type, SourceLocation L)
    : Expr(FloatingLiteralClass, Type), Value(V), IsExact(*isexact), Loc(L) {} 

  const llvm::APFloat &getValue() const { return Value; }
  
  bool isExact() const { return IsExact; }

  /// getValueAsDouble - This returns the value as an inaccurate double.  Note
  /// that this may cause loss of precision, but is useful for debugging dumps
  /// etc.
  double getValueAsDouble() const {
    // FIXME: We need something for long double here.
    if (cast<BuiltinType>(getType())->getKind() == BuiltinType::Float)
      return Value.convertToFloat();
    else
      return Value.convertToDouble();
  }
  
  virtual SourceRange getSourceRange() const { return SourceRange(Loc); }

  static bool classof(const Stmt *T) { 
    return T->getStmtClass() == FloatingLiteralClass; 
  }
  static bool classof(const FloatingLiteral *) { return true; }
  
  // Iterators
  virtual child_iterator child_begin();
  virtual child_iterator child_end();
  
  virtual void EmitImpl(llvm::Serializer& S) const;
  static FloatingLiteral* CreateImpl(llvm::Deserializer& D);
};

/// ImaginaryLiteral - We support imaginary integer and floating point literals,
/// like "1.0i".  We represent these as a wrapper around FloatingLiteral and
/// IntegerLiteral classes.  Instances of this class always have a Complex type
/// whose element type matches the subexpression.
///
class ImaginaryLiteral : public Expr {
  Expr *Val;
public:
  ImaginaryLiteral(Expr *val, QualType Ty)
    : Expr(ImaginaryLiteralClass, Ty), Val(val) {}
  
  const Expr *getSubExpr() const { return Val; }
  Expr *getSubExpr() { return Val; }
  
  virtual SourceRange getSourceRange() const { return Val->getSourceRange(); }
  static bool classof(const Stmt *T) { 
    return T->getStmtClass() == ImaginaryLiteralClass; 
  }
  static bool classof(const ImaginaryLiteral *) { return true; }
  
  // Iterators
  virtual child_iterator child_begin();
  virtual child_iterator child_end();
  
  virtual void EmitImpl(llvm::Serializer& S) const;
  static ImaginaryLiteral* CreateImpl(llvm::Deserializer& D);
};

/// StringLiteral - This represents a string literal expression, e.g. "foo"
/// or L"bar" (wide strings).  The actual string is returned by getStrData()
/// is NOT null-terminated, and the length of the string is determined by
/// calling getByteLength().
class StringLiteral : public Expr {
  const char *StrData;
  unsigned ByteLength;
  bool IsWide;
  // if the StringLiteral was composed using token pasting, both locations
  // are needed. If not (the common case), firstTokLoc == lastTokLoc.
  // FIXME: if space becomes an issue, we should create a sub-class.
  SourceLocation firstTokLoc, lastTokLoc;
public:
  StringLiteral(const char *strData, unsigned byteLength, bool Wide, 
                QualType t, SourceLocation b, SourceLocation e);
  virtual ~StringLiteral();
  
  const char *getStrData() const { return StrData; }
  unsigned getByteLength() const { return ByteLength; }
  bool isWide() const { return IsWide; }

  virtual SourceRange getSourceRange() const { 
    return SourceRange(firstTokLoc,lastTokLoc); 
  }
  static bool classof(const Stmt *T) { 
    return T->getStmtClass() == StringLiteralClass; 
  }
  static bool classof(const StringLiteral *) { return true; }
  
  // Iterators
  virtual child_iterator child_begin();
  virtual child_iterator child_end();
  
  virtual void EmitImpl(llvm::Serializer& S) const;
  static StringLiteral* CreateImpl(llvm::Deserializer& D);
};

/// ParenExpr - This represents a parethesized expression, e.g. "(1)".  This
/// AST node is only formed if full location information is requested.
class ParenExpr : public Expr {
  SourceLocation L, R;
  Expr *Val;
public:
  ParenExpr(SourceLocation l, SourceLocation r, Expr *val)
    : Expr(ParenExprClass, val->getType()), L(l), R(r), Val(val) {}
  
  const Expr *getSubExpr() const { return Val; }
  Expr *getSubExpr() { return Val; }
  SourceRange getSourceRange() const { return SourceRange(L, R); }

  static bool classof(const Stmt *T) { 
    return T->getStmtClass() == ParenExprClass; 
  }
  static bool classof(const ParenExpr *) { return true; }
  
  // Iterators
  virtual child_iterator child_begin();
  virtual child_iterator child_end();
  
  virtual void EmitImpl(llvm::Serializer& S) const;
  static ParenExpr* CreateImpl(llvm::Deserializer& D);
};


/// UnaryOperator - This represents the unary-expression's (except sizeof of
/// types), the postinc/postdec operators from postfix-expression, and various
/// extensions.
///
/// Notes on various nodes:
///
/// Real/Imag - These return the real/imag part of a complex operand.  If
///   applied to a non-complex value, the former returns its operand and the
///   later returns zero in the type of the operand.
///
/// __builtin_offsetof(type, a.b[10]) is represented as a unary operator whose
///   subexpression is a compound literal with the various MemberExpr and 
///   ArraySubscriptExpr's applied to it.
///
class UnaryOperator : public Expr {
public:
  // Note that additions to this should also update the StmtVisitor class.
  enum Opcode {
    PostInc, PostDec, // [C99 6.5.2.4] Postfix increment and decrement operators
    PreInc, PreDec,   // [C99 6.5.3.1] Prefix increment and decrement operators.
    AddrOf, Deref,    // [C99 6.5.3.2] Address and indirection operators.
    Plus, Minus,      // [C99 6.5.3.3] Unary arithmetic operators.
    Not, LNot,        // [C99 6.5.3.3] Unary arithmetic operators.
    SizeOf, AlignOf,  // [C99 6.5.3.4] Sizeof (expr, not type) operator.
    Real, Imag,       // "__real expr"/"__imag expr" Extension.
    Extension,        // __extension__ marker.
    OffsetOf          // __builtin_offsetof
  };
private:
  Expr *Val;
  Opcode Opc;
  SourceLocation Loc;
public:  

  UnaryOperator(Expr *input, Opcode opc, QualType type, SourceLocation l)
    : Expr(UnaryOperatorClass, type), Val(input), Opc(opc), Loc(l) {}

  Opcode getOpcode() const { return Opc; }
  Expr *getSubExpr() const { return Val; }
  
  /// getOperatorLoc - Return the location of the operator.
  SourceLocation getOperatorLoc() const { return Loc; }
  
  /// isPostfix - Return true if this is a postfix operation, like x++.
  static bool isPostfix(Opcode Op);

  bool isPostfix() const { return isPostfix(Opc); }
  bool isIncrementDecrementOp() const { return Opc>=PostInc && Opc<=PreDec; }
  bool isSizeOfAlignOfOp() const { return Opc == SizeOf || Opc == AlignOf; }
  static bool isArithmeticOp(Opcode Op) { return Op >= Plus && Op <= LNot; }
  
  /// getOpcodeStr - Turn an Opcode enum value into the punctuation char it
  /// corresponds to, e.g. "sizeof" or "[pre]++"
  static const char *getOpcodeStr(Opcode Op);

  virtual SourceRange getSourceRange() const {
    if (isPostfix())
      return SourceRange(Val->getLocStart(), Loc);
    else
      return SourceRange(Loc, Val->getLocEnd());
  }
  virtual SourceLocation getExprLoc() const { return Loc; }
  
  static bool classof(const Stmt *T) { 
    return T->getStmtClass() == UnaryOperatorClass; 
  }
  static bool classof(const UnaryOperator *) { return true; }
  
  // Iterators
  virtual child_iterator child_begin();
  virtual child_iterator child_end();
  
  virtual void EmitImpl(llvm::Serializer& S) const;
  static UnaryOperator* CreateImpl(llvm::Deserializer& D);
};

/// SizeOfAlignOfTypeExpr - [C99 6.5.3.4] - This is only for sizeof/alignof of
/// *types*.  sizeof(expr) is handled by UnaryOperator.
class SizeOfAlignOfTypeExpr : public Expr {
  bool isSizeof;  // true if sizeof, false if alignof.
  QualType Ty;
  SourceLocation OpLoc, RParenLoc;
public:
  SizeOfAlignOfTypeExpr(bool issizeof, QualType argType, QualType resultType,
                        SourceLocation op, SourceLocation rp) : 
    Expr(SizeOfAlignOfTypeExprClass, resultType),
    isSizeof(issizeof), Ty(argType), OpLoc(op), RParenLoc(rp) {}
  
  bool isSizeOf() const { return isSizeof; }
  QualType getArgumentType() const { return Ty; }
  
  SourceLocation getOperatorLoc() const { return OpLoc; }
  SourceRange getSourceRange() const { return SourceRange(OpLoc, RParenLoc); }

  static bool classof(const Stmt *T) { 
    return T->getStmtClass() == SizeOfAlignOfTypeExprClass; 
  }
  static bool classof(const SizeOfAlignOfTypeExpr *) { return true; }
  
  // Iterators
  virtual child_iterator child_begin();
  virtual child_iterator child_end();
  
  virtual void EmitImpl(llvm::Serializer& S) const;
  static SizeOfAlignOfTypeExpr* CreateImpl(llvm::Deserializer& D);
};

//===----------------------------------------------------------------------===//
// Postfix Operators.
//===----------------------------------------------------------------------===//

/// ArraySubscriptExpr - [C99 6.5.2.1] Array Subscripting.
class ArraySubscriptExpr : public Expr {
  enum { LHS, RHS, END_EXPR=2 };
  Expr* SubExprs[END_EXPR]; 
  SourceLocation RBracketLoc;
public:
  ArraySubscriptExpr(Expr *lhs, Expr *rhs, QualType t,
                     SourceLocation rbracketloc)
  : Expr(ArraySubscriptExprClass, t), RBracketLoc(rbracketloc) {
    SubExprs[LHS] = lhs;
    SubExprs[RHS] = rhs;
  }
  
  /// An array access can be written A[4] or 4[A] (both are equivalent).
  /// - getBase() and getIdx() always present the normalized view: A[4].
  ///    In this case getBase() returns "A" and getIdx() returns "4".
  /// - getLHS() and getRHS() present the syntactic view. e.g. for
  ///    4[A] getLHS() returns "4".
  /// Note: Because vector element access is also written A[4] we must
  /// predicate the format conversion in getBase and getIdx only on the
  /// the type of the RHS, as it is possible for the LHS to be a vector of
  /// integer type
  Expr *getLHS() { return SubExprs[LHS]; }
  const Expr *getLHS() const { return SubExprs[LHS]; }
  
  Expr *getRHS() { return SubExprs[RHS]; }
  const Expr *getRHS() const { return SubExprs[RHS]; }
  
  Expr *getBase() { 
    return (getRHS()->getType()->isIntegerType()) ? getLHS() : getRHS();
  }
    
  const Expr *getBase() const { 
    return (getRHS()->getType()->isIntegerType()) ? getLHS() : getRHS();
  }
  
  Expr *getIdx() { 
    return (getRHS()->getType()->isIntegerType()) ? getRHS() : getLHS();
  }
  
  const Expr *getIdx() const {
    return (getRHS()->getType()->isIntegerType()) ? getRHS() : getLHS(); 
  }  

  
  SourceRange getSourceRange() const { 
    return SourceRange(getLHS()->getLocStart(), RBracketLoc);
  }
  virtual SourceLocation getExprLoc() const { return RBracketLoc; }

  static bool classof(const Stmt *T) { 
    return T->getStmtClass() == ArraySubscriptExprClass; 
  }
  static bool classof(const ArraySubscriptExpr *) { return true; }
  
  // Iterators
  virtual child_iterator child_begin();
  virtual child_iterator child_end();
  
  virtual void EmitImpl(llvm::Serializer& S) const;
  static ArraySubscriptExpr* CreateImpl(llvm::Deserializer& D);
};


/// CallExpr - [C99 6.5.2.2] Function Calls.
///
class CallExpr : public Expr {
  enum { FN=0, ARGS_START=1 };
  Expr **SubExprs;
  unsigned NumArgs;
  SourceLocation RParenLoc;
  
  // This version of the ctor is for deserialization.
  CallExpr(Expr** subexprs, unsigned numargs, QualType t, 
           SourceLocation rparenloc)
  : Expr(CallExprClass,t), SubExprs(subexprs), 
    NumArgs(numargs), RParenLoc(rparenloc) {}
  
public:
  CallExpr(Expr *fn, Expr **args, unsigned numargs, QualType t, 
           SourceLocation rparenloc);
  ~CallExpr() {
    delete [] SubExprs;
  }
  
  const Expr *getCallee() const { return SubExprs[FN]; }
  Expr *getCallee() { return SubExprs[FN]; }
  void setCallee(Expr *F) { SubExprs[FN] = F; }
  
  /// getNumArgs - Return the number of actual arguments to this call.
  ///
  unsigned getNumArgs() const { return NumArgs; }
  
  /// getArg - Return the specified argument.
  Expr *getArg(unsigned Arg) {
    assert(Arg < NumArgs && "Arg access out of range!");
    return SubExprs[Arg+ARGS_START];
  }
  const Expr *getArg(unsigned Arg) const {
    assert(Arg < NumArgs && "Arg access out of range!");
    return SubExprs[Arg+ARGS_START];
  }
  /// setArg - Set the specified argument.
  void setArg(unsigned Arg, Expr *ArgExpr) {
    assert(Arg < NumArgs && "Arg access out of range!");
    SubExprs[Arg+ARGS_START] = ArgExpr;
  }
  
  /// setNumArgs - This changes the number of arguments present in this call.
  /// Any orphaned expressions are deleted by this, and any new operands are set
  /// to null.
  void setNumArgs(unsigned NumArgs);
  
  typedef Expr **arg_iterator;
  typedef Expr * const *arg_const_iterator;
  arg_iterator arg_begin() { return SubExprs+ARGS_START; }
  arg_iterator arg_end() { return SubExprs+ARGS_START+getNumArgs(); }
  arg_const_iterator arg_begin() const { return SubExprs+ARGS_START; }
  arg_const_iterator arg_end() const { return SubExprs+ARGS_START+getNumArgs(); }
  
  
  /// getNumCommas - Return the number of commas that must have been present in
  /// this function call.
  unsigned getNumCommas() const { return NumArgs ? NumArgs - 1 : 0; }

  bool isBuiltinClassifyType(llvm::APSInt &Result) const;
  
  SourceLocation getRParenLoc() const { return RParenLoc; }
  SourceRange getSourceRange() const { 
    return SourceRange(getCallee()->getLocStart(), RParenLoc);
  }
  
  static bool classof(const Stmt *T) { 
    return T->getStmtClass() == CallExprClass; 
  }
  static bool classof(const CallExpr *) { return true; }
  
  // Iterators
  virtual child_iterator child_begin();
  virtual child_iterator child_end();
  
  virtual void EmitImpl(llvm::Serializer& S) const;
  static CallExpr* CreateImpl(llvm::Deserializer& D);
};

/// MemberExpr - [C99 6.5.2.3] Structure and Union Members.
///
class MemberExpr : public Expr {
  Expr *Base;
  FieldDecl *MemberDecl;
  SourceLocation MemberLoc;
  bool IsArrow;      // True if this is "X->F", false if this is "X.F".
public:
  MemberExpr(Expr *base, bool isarrow, FieldDecl *memberdecl, SourceLocation l) 
    : Expr(MemberExprClass, memberdecl->getType()),
      Base(base), MemberDecl(memberdecl), MemberLoc(l), IsArrow(isarrow) {}
  
  Expr *getBase() const { return Base; }
  FieldDecl *getMemberDecl() const { return MemberDecl; }
  bool isArrow() const { return IsArrow; }

  virtual SourceRange getSourceRange() const {
    return SourceRange(getBase()->getLocStart(), MemberLoc);
  }
  virtual SourceLocation getExprLoc() const { return MemberLoc; }

  static bool classof(const Stmt *T) { 
    return T->getStmtClass() == MemberExprClass; 
  }
  static bool classof(const MemberExpr *) { return true; }
  
  // Iterators
  virtual child_iterator child_begin();
  virtual child_iterator child_end();
  
  virtual void EmitImpl(llvm::Serializer& S) const;
  static MemberExpr* CreateImpl(llvm::Deserializer& D);
};

/// OCUVectorElementExpr - This represents access to specific elements of a
/// vector, and may occur on the left hand side or right hand side.  For example
/// the following is legal:  "V.xy = V.zw" if V is a 4 element ocu vector.
///
class OCUVectorElementExpr : public Expr {
  Expr *Base;
  IdentifierInfo &Accessor;
  SourceLocation AccessorLoc;
public:
  enum ElementType {
    Point,   // xywz
    Color,   // rgba
    Texture  // stpq
  };
  OCUVectorElementExpr(QualType ty, Expr *base, IdentifierInfo &accessor,
                       SourceLocation loc)
    : Expr(OCUVectorElementExprClass, ty), 
      Base(base), Accessor(accessor), AccessorLoc(loc) {}
                     
  const Expr *getBase() const { return Base; }
  Expr *getBase() { return Base; }
  
  IdentifierInfo &getAccessor() const { return Accessor; }
  
  /// getNumElements - Get the number of components being selected.
  unsigned getNumElements() const;
  
  /// getElementType - Determine whether the components of this access are
  /// "point" "color" or "texture" elements.
  ElementType getElementType() const;

  /// containsDuplicateElements - Return true if any element access is
  /// repeated.
  bool containsDuplicateElements() const;
  
  /// getEncodedElementAccess - Encode the elements accessed into a bit vector.
  /// The encoding currently uses 2-bit bitfields, but clients should use the
  /// accessors below to access them.
  ///
  unsigned getEncodedElementAccess() const;
  
  /// getAccessedFieldNo - Given an encoded value and a result number, return
  /// the input field number being accessed.
  static unsigned getAccessedFieldNo(unsigned Idx, unsigned EncodedVal) {
    return (EncodedVal >> (Idx*2)) & 3;
  }
  
  virtual SourceRange getSourceRange() const {
    return SourceRange(getBase()->getLocStart(), AccessorLoc);
  }
  static bool classof(const Stmt *T) { 
    return T->getStmtClass() == OCUVectorElementExprClass; 
  }
  static bool classof(const OCUVectorElementExpr *) { return true; }
  
  // Iterators
  virtual child_iterator child_begin();
  virtual child_iterator child_end();
};

/// CompoundLiteralExpr - [C99 6.5.2.5] 
///
class CompoundLiteralExpr : public Expr {
  /// LParenLoc - If non-null, this is the location of the left paren in a
  /// compound literal like "(int){4}".  This can be null if this is a
  /// synthesized compound expression.
  SourceLocation LParenLoc;
  Expr *Init;
  bool FileScope;
public:
  CompoundLiteralExpr(SourceLocation lparenloc, QualType ty, Expr *init, bool fileScope) : 
    Expr(CompoundLiteralExprClass, ty), LParenLoc(lparenloc), Init(init), FileScope(fileScope) {}
  
  const Expr *getInitializer() const { return Init; }
  Expr *getInitializer() { return Init; }

  bool isFileScope() const { return FileScope; }
  
  SourceLocation getLParenLoc() const { return LParenLoc; }
  
  virtual SourceRange getSourceRange() const {
    // FIXME: Init should never be null.
    if (!Init)
      return SourceRange();
    if (LParenLoc.isInvalid())
      return Init->getSourceRange();
    return SourceRange(LParenLoc, Init->getLocEnd());
  }

  static bool classof(const Stmt *T) { 
    return T->getStmtClass() == CompoundLiteralExprClass; 
  }
  static bool classof(const CompoundLiteralExpr *) { return true; }
  
  // Iterators
  virtual child_iterator child_begin();
  virtual child_iterator child_end();
  
  virtual void EmitImpl(llvm::Serializer& S) const;
  static CompoundLiteralExpr* CreateImpl(llvm::Deserializer& D);
};

/// ImplicitCastExpr - Allows us to explicitly represent implicit type 
/// conversions. For example: converting T[]->T*, void f()->void (*f)(), 
/// float->double, short->int, etc.
///
class ImplicitCastExpr : public Expr {
  Expr *Op;
public:
  ImplicitCastExpr(QualType ty, Expr *op) : 
    Expr(ImplicitCastExprClass, ty), Op(op) {}
    
  Expr *getSubExpr() { return Op; }
  const Expr *getSubExpr() const { return Op; }

  virtual SourceRange getSourceRange() const { return Op->getSourceRange(); }

  static bool classof(const Stmt *T) { 
    return T->getStmtClass() == ImplicitCastExprClass; 
  }
  static bool classof(const ImplicitCastExpr *) { return true; }
  
  // Iterators
  virtual child_iterator child_begin();
  virtual child_iterator child_end();
  
  virtual void EmitImpl(llvm::Serializer& S) const;
  static ImplicitCastExpr* CreateImpl(llvm::Deserializer& D);
};

/// CastExpr - [C99 6.5.4] Cast Operators.
///
class CastExpr : public Expr {
  Expr *Op;
  SourceLocation Loc; // the location of the left paren
public:
  CastExpr(QualType ty, Expr *op, SourceLocation l) : 
    Expr(CastExprClass, ty), Op(op), Loc(l) {}

  SourceLocation getLParenLoc() const { return Loc; }
  
  Expr *getSubExpr() const { return Op; }
  
  virtual SourceRange getSourceRange() const {
    return SourceRange(Loc, getSubExpr()->getSourceRange().getEnd());
  }
  static bool classof(const Stmt *T) { 
    return T->getStmtClass() == CastExprClass; 
  }
  static bool classof(const CastExpr *) { return true; }
  
  // Iterators
  virtual child_iterator child_begin();
  virtual child_iterator child_end();
  
  virtual void EmitImpl(llvm::Serializer& S) const;
  static CastExpr* CreateImpl(llvm::Deserializer& D);
};

class BinaryOperator : public Expr {
public:
  enum Opcode {
    // Operators listed in order of precedence.
    // Note that additions to this should also update the StmtVisitor class.
    Mul, Div, Rem,    // [C99 6.5.5] Multiplicative operators.
    Add, Sub,         // [C99 6.5.6] Additive operators.
    Shl, Shr,         // [C99 6.5.7] Bitwise shift operators.
    LT, GT, LE, GE,   // [C99 6.5.8] Relational operators.
    EQ, NE,           // [C99 6.5.9] Equality operators.
    And,              // [C99 6.5.10] Bitwise AND operator.
    Xor,              // [C99 6.5.11] Bitwise XOR operator.
    Or,               // [C99 6.5.12] Bitwise OR operator.
    LAnd,             // [C99 6.5.13] Logical AND operator.
    LOr,              // [C99 6.5.14] Logical OR operator.
    Assign, MulAssign,// [C99 6.5.16] Assignment operators.
    DivAssign, RemAssign,
    AddAssign, SubAssign,
    ShlAssign, ShrAssign,
    AndAssign, XorAssign,
    OrAssign,
    Comma             // [C99 6.5.17] Comma operator.
  };
private:
  enum { LHS, RHS, END_EXPR };
  Expr* SubExprs[END_EXPR];
  Opcode Opc;
  SourceLocation OpLoc;
public:  
  
  BinaryOperator(Expr *lhs, Expr *rhs, Opcode opc, QualType ResTy,
                 SourceLocation opLoc)
    : Expr(BinaryOperatorClass, ResTy), Opc(opc), OpLoc(opLoc) {
    SubExprs[LHS] = lhs;
    SubExprs[RHS] = rhs;
    assert(!isCompoundAssignmentOp() && 
           "Use ArithAssignBinaryOperator for compound assignments");
  }

  SourceLocation getOperatorLoc() const { return OpLoc; }
  Opcode getOpcode() const { return Opc; }
  Expr *getLHS() const { return SubExprs[LHS]; }
  Expr *getRHS() const { return SubExprs[RHS]; }
  virtual SourceRange getSourceRange() const {
    return SourceRange(getLHS()->getLocStart(), getRHS()->getLocEnd());
  }
  
  /// getOpcodeStr - Turn an Opcode enum value into the punctuation char it
  /// corresponds to, e.g. "<<=".
  static const char *getOpcodeStr(Opcode Op);

  /// predicates to categorize the respective opcodes.
  bool isMultiplicativeOp() const { return Opc >= Mul && Opc <= Rem; }
  bool isAdditiveOp() const { return Opc == Add || Opc == Sub; }
  bool isShiftOp() const { return Opc == Shl || Opc == Shr; }
  bool isBitwiseOp() const { return Opc >= And && Opc <= Or; }
  bool isRelationalOp() const { return Opc >= LT && Opc <= GE; }
  bool isEqualityOp() const { return Opc == EQ || Opc == NE; }
  bool isLogicalOp() const { return Opc == LAnd || Opc == LOr; }
  bool isAssignmentOp() const { return Opc >= Assign && Opc <= OrAssign; }
  bool isCompoundAssignmentOp() const { return Opc > Assign && Opc <= OrAssign;}
  bool isShiftAssignOp() const { return Opc == ShlAssign || Opc == ShrAssign; }
  
  static bool classof(const Stmt *S) { 
    return S->getStmtClass() == BinaryOperatorClass ||
           S->getStmtClass() == CompoundAssignOperatorClass; 
  }
  static bool classof(const BinaryOperator *) { return true; }

  // Iterators
  virtual child_iterator child_begin();
  virtual child_iterator child_end();
  
  virtual void EmitImpl(llvm::Serializer& S) const;
  static BinaryOperator* CreateImpl(llvm::Deserializer& D);

protected:
  BinaryOperator(Expr *lhs, Expr *rhs, Opcode opc, QualType ResTy,
                 SourceLocation oploc, bool dead)
    : Expr(CompoundAssignOperatorClass, ResTy), Opc(opc), OpLoc(oploc) {
    SubExprs[LHS] = lhs;
    SubExprs[RHS] = rhs;
  }
};

/// CompoundAssignOperator - For compound assignments (e.g. +=), we keep
/// track of the type the operation is performed in.  Due to the semantics of
/// these operators, the operands are promoted, the aritmetic performed, an
/// implicit conversion back to the result type done, then the assignment takes
/// place.  This captures the intermediate type which the computation is done
/// in.
class CompoundAssignOperator : public BinaryOperator {
  QualType ComputationType;
public:
  CompoundAssignOperator(Expr *lhs, Expr *rhs, Opcode opc,
                         QualType ResType, QualType CompType,
                         SourceLocation OpLoc)
    : BinaryOperator(lhs, rhs, opc, ResType, OpLoc, true),
      ComputationType(CompType) {
    assert(isCompoundAssignmentOp() && 
           "Only should be used for compound assignments");
  }

  QualType getComputationType() const { return ComputationType; }
  
  static bool classof(const CompoundAssignOperator *) { return true; }
  static bool classof(const Stmt *S) { 
    return S->getStmtClass() == CompoundAssignOperatorClass; 
  }
  
  virtual void EmitImpl(llvm::Serializer& S) const;
  static CompoundAssignOperator* CreateImpl(llvm::Deserializer& D);
};

/// ConditionalOperator - The ?: operator.  Note that LHS may be null when the
/// GNU "missing LHS" extension is in use.
///
class ConditionalOperator : public Expr {
  enum { COND, LHS, RHS, END_EXPR };
  Expr* SubExprs[END_EXPR]; // Left/Middle/Right hand sides.
public:
  ConditionalOperator(Expr *cond, Expr *lhs, Expr *rhs, QualType t)
    : Expr(ConditionalOperatorClass, t) {
    SubExprs[COND] = cond;
    SubExprs[LHS] = lhs;
    SubExprs[RHS] = rhs;
  }

  // getCond - Return the expression representing the condition for
  //  the ?: operator.
  Expr *getCond() const { return SubExprs[COND]; }

  // getTrueExpr - Return the subexpression representing the value of the ?:
  //  expression if the condition evaluates to true.  In most cases this value
  //  will be the same as getLHS() except a GCC extension allows the left
  //  subexpression to be omitted, and instead of the condition be returned.
  //  e.g: x ?: y is shorthand for x ? x : y, except that the expression "x"
  //  is only evaluated once.  
  Expr *getTrueExpr() const {
    return SubExprs[LHS] ? SubExprs[COND] : SubExprs[LHS];
  }
  
  // getTrueExpr - Return the subexpression representing the value of the ?:
  // expression if the condition evaluates to false. This is the same as getRHS.
  Expr *getFalseExpr() const { return SubExprs[RHS]; }
  
  Expr *getLHS() const { return SubExprs[LHS]; }
  Expr *getRHS() const { return SubExprs[RHS]; }

  virtual SourceRange getSourceRange() const {
    return SourceRange(getCond()->getLocStart(), getRHS()->getLocEnd());
  }
  static bool classof(const Stmt *T) { 
    return T->getStmtClass() == ConditionalOperatorClass; 
  }
  static bool classof(const ConditionalOperator *) { return true; }
  
  // Iterators
  virtual child_iterator child_begin();
  virtual child_iterator child_end();
  
  virtual void EmitImpl(llvm::Serializer& S) const;
  static ConditionalOperator* CreateImpl(llvm::Deserializer& D);
};

/// AddrLabelExpr - The GNU address of label extension, representing &&label.
class AddrLabelExpr : public Expr {
  SourceLocation AmpAmpLoc, LabelLoc;
  LabelStmt *Label;
public:
  AddrLabelExpr(SourceLocation AALoc, SourceLocation LLoc, LabelStmt *L,
                QualType t)
    : Expr(AddrLabelExprClass, t), AmpAmpLoc(AALoc), LabelLoc(LLoc), Label(L) {}
  
  virtual SourceRange getSourceRange() const {
    return SourceRange(AmpAmpLoc, LabelLoc);
  }
  
  LabelStmt *getLabel() const { return Label; }
  
  static bool classof(const Stmt *T) {
    return T->getStmtClass() == AddrLabelExprClass; 
  }
  static bool classof(const AddrLabelExpr *) { return true; }
  
  // Iterators
  virtual child_iterator child_begin();
  virtual child_iterator child_end();
  
  virtual void EmitImpl(llvm::Serializer& S) const;
  static AddrLabelExpr* CreateImpl(llvm::Deserializer& D);
};

/// StmtExpr - This is the GNU Statement Expression extension: ({int X=4; X;}).
/// The StmtExpr contains a single CompoundStmt node, which it evaluates and
/// takes the value of the last subexpression.
class StmtExpr : public Expr {
  CompoundStmt *SubStmt;
  SourceLocation LParenLoc, RParenLoc;
public:
  StmtExpr(CompoundStmt *substmt, QualType T,
           SourceLocation lp, SourceLocation rp) :
    Expr(StmtExprClass, T), SubStmt(substmt),  LParenLoc(lp), RParenLoc(rp) { }
  
  CompoundStmt *getSubStmt() { return SubStmt; }
  const CompoundStmt *getSubStmt() const { return SubStmt; }
  
  virtual SourceRange getSourceRange() const {
    return SourceRange(LParenLoc, RParenLoc);
  }
  
  static bool classof(const Stmt *T) {
    return T->getStmtClass() == StmtExprClass; 
  }
  static bool classof(const StmtExpr *) { return true; }
  
  // Iterators
  virtual child_iterator child_begin();
  virtual child_iterator child_end();
  
  virtual void EmitImpl(llvm::Serializer& S) const;
  static StmtExpr* CreateImpl(llvm::Deserializer& D);
};

/// TypesCompatibleExpr - GNU builtin-in function __builtin_type_compatible_p.
/// This AST node represents a function that returns 1 if two *types* (not
/// expressions) are compatible. The result of this built-in function can be
/// used in integer constant expressions.
class TypesCompatibleExpr : public Expr {
  QualType Type1;
  QualType Type2;
  SourceLocation BuiltinLoc, RParenLoc;
public:
  TypesCompatibleExpr(QualType ReturnType, SourceLocation BLoc, 
                      QualType t1, QualType t2, SourceLocation RP) : 
    Expr(TypesCompatibleExprClass, ReturnType), Type1(t1), Type2(t2),
    BuiltinLoc(BLoc), RParenLoc(RP) {}

  QualType getArgType1() const { return Type1; }
  QualType getArgType2() const { return Type2; }
    
  virtual SourceRange getSourceRange() const {
    return SourceRange(BuiltinLoc, RParenLoc);
  }
  static bool classof(const Stmt *T) {
    return T->getStmtClass() == TypesCompatibleExprClass; 
  }
  static bool classof(const TypesCompatibleExpr *) { return true; }
  
  // Iterators
  virtual child_iterator child_begin();
  virtual child_iterator child_end();
};

/// ChooseExpr - GNU builtin-in function __builtin_choose_expr.
/// This AST node is similar to the conditional operator (?:) in C, with 
/// the following exceptions:
/// - the test expression much be a constant expression.
/// - the expression returned has it's type unaltered by promotion rules.
/// - does not evaluate the expression that was not chosen.
class ChooseExpr : public Expr {
  enum { COND, LHS, RHS, END_EXPR };
  Expr* SubExprs[END_EXPR]; // Left/Middle/Right hand sides.
  SourceLocation BuiltinLoc, RParenLoc;
public:
  ChooseExpr(SourceLocation BLoc, Expr *cond, Expr *lhs, Expr *rhs, QualType t,
             SourceLocation RP)
    : Expr(ChooseExprClass, t),  
      BuiltinLoc(BLoc), RParenLoc(RP) {
      SubExprs[COND] = cond;
      SubExprs[LHS] = lhs;
      SubExprs[RHS] = rhs;
    }        
  
  /// isConditionTrue - Return true if the condition is true.  This is always
  /// statically knowable for a well-formed choosexpr.
  bool isConditionTrue(ASTContext &C) const;
  
  Expr *getCond() const { return SubExprs[COND]; }
  Expr *getLHS() const { return SubExprs[LHS]; }
  Expr *getRHS() const { return SubExprs[RHS]; }

  virtual SourceRange getSourceRange() const {
    return SourceRange(BuiltinLoc, RParenLoc);
  }
  static bool classof(const Stmt *T) {
    return T->getStmtClass() == ChooseExprClass; 
  }
  static bool classof(const ChooseExpr *) { return true; }
  
  // Iterators
  virtual child_iterator child_begin();
  virtual child_iterator child_end();
};

/// OverloadExpr - Clang builtin-in function __builtin_overload.
/// This AST node provides a way to overload functions in C
/// i.e. float Z = __builtin_overload(2, X, Y, modf, mod, modl);
/// would pick whichever of the functions modf, mod, and modl that took two
/// arguments of the same type as X and Y.  
class OverloadExpr : public Expr {
  Expr **SubExprs;
  unsigned NumArgs;
  unsigned FnIndex;
  SourceLocation BuiltinLoc;
  SourceLocation RParenLoc;
public:
  OverloadExpr(Expr **args, unsigned narg, unsigned idx, QualType t, 
               SourceLocation bloc, SourceLocation rploc)
    : Expr(OverloadExprClass, t), NumArgs(narg), FnIndex(idx), BuiltinLoc(bloc), 
      RParenLoc(rploc) {
    SubExprs = new Expr*[narg];
    for (unsigned i = 0; i != narg; ++i)
      SubExprs[i] = args[i];
  }
  ~OverloadExpr() {
    delete [] SubExprs;
  }

  typedef Expr * const *arg_const_iterator;
  arg_const_iterator arg_begin() const { return SubExprs+1; }

  /// getNumArgs - Return the number of actual arguments to this call.
  ///
  unsigned getNumArgs() const { return NumArgs; }
  
  /// getArg - Return the specified argument.
  Expr *getArg(unsigned Arg) {
    assert(Arg < NumArgs && "Arg access out of range!");
    return SubExprs[Arg];
  }
  Expr *getFn() { return SubExprs[FnIndex]; }
  
  virtual SourceRange getSourceRange() const {
    return SourceRange(BuiltinLoc, RParenLoc);
  }
  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OverloadExprClass; 
  }
  static bool classof(const OverloadExpr *) { return true; }
  
  // Iterators
  virtual child_iterator child_begin();
  virtual child_iterator child_end();
};

/// VAArgExpr, used for the builtin function __builtin_va_start.
class VAArgExpr : public Expr {
  Expr *Val;
  SourceLocation BuiltinLoc, RParenLoc;
public:
  VAArgExpr(SourceLocation BLoc, Expr* e, QualType t, SourceLocation RPLoc)
    : Expr(VAArgExprClass, t),
      Val(e),
      BuiltinLoc(BLoc),
      RParenLoc(RPLoc) { }
  
  const Expr *getSubExpr() const { return Val; }
  Expr *getSubExpr() { return Val; }
  virtual SourceRange getSourceRange() const {
    return SourceRange(BuiltinLoc, RParenLoc);
  }  
  static bool classof(const Stmt *T) {
    return T->getStmtClass() == VAArgExprClass;
  }
  static bool classof(const VAArgExpr *) { return true; }
  
  // Iterators
  virtual child_iterator child_begin();
  virtual child_iterator child_end();  
};
  
/// InitListExpr, used for struct and array initializers.
class InitListExpr : public Expr {
  Expr **InitExprs;
  unsigned NumInits;
  SourceLocation LBraceLoc, RBraceLoc;
public:
  InitListExpr(SourceLocation lbraceloc, Expr **initexprs, unsigned numinits,
               SourceLocation rbraceloc);
  ~InitListExpr() {
    delete [] InitExprs;
  }
  
  unsigned getNumInits() const { return NumInits; }
  
  const Expr* getInit(unsigned Init) const { 
    assert(Init < NumInits && "Initializer access out of range!");
    return InitExprs[Init];
  }
  
  Expr* getInit(unsigned Init) { 
    assert(Init < NumInits && "Initializer access out of range!");
    return InitExprs[Init];
  }
  
  void setInit(unsigned Init, Expr *expr) { 
    assert(Init < NumInits && "Initializer access out of range!");
    InitExprs[Init] = expr;
  }
  
  virtual SourceRange getSourceRange() const {
    return SourceRange(LBraceLoc, RBraceLoc);
  } 
  static bool classof(const Stmt *T) {
    return T->getStmtClass() == InitListExprClass; 
  }
  static bool classof(const InitListExpr *) { return true; }
  
  // Iterators
  virtual child_iterator child_begin();
  virtual child_iterator child_end();
  
  virtual void EmitImpl(llvm::Serializer& S) const;
  static InitListExpr* CreateImpl(llvm::Deserializer& D);

private:
  // Used by serializer.
  InitListExpr() : Expr(InitListExprClass, QualType()), 
                   InitExprs(NULL), NumInits(0) {}
};

/// ObjCStringLiteral, used for Objective-C string literals
/// i.e. @"foo".
class ObjCStringLiteral : public Expr {
  StringLiteral *String;
  SourceLocation AtLoc;
public:
  ObjCStringLiteral(StringLiteral *SL, QualType T, SourceLocation L)
    : Expr(ObjCStringLiteralClass, T), String(SL), AtLoc(L) {}
  
  StringLiteral* getString() { return String; }

  const StringLiteral* getString() const { return String; }

  SourceLocation getAtLoc() const { return AtLoc; }

  virtual SourceRange getSourceRange() const { 
    return SourceRange(AtLoc, String->getLocEnd());
  }
  
  static bool classof(const Stmt *T) { 
    return T->getStmtClass() == ObjCStringLiteralClass; 
  }
  static bool classof(const ObjCStringLiteral *) { return true; }  
  
  // Iterators
  virtual child_iterator child_begin();
  virtual child_iterator child_end();
  
  virtual void EmitImpl(llvm::Serializer& S) const;
  static ObjCStringLiteral* CreateImpl(llvm::Deserializer& D);
};
  
/// ObjCEncodeExpr, used for @encode in Objective-C.
class ObjCEncodeExpr : public Expr {
  QualType EncType;
  SourceLocation AtLoc, RParenLoc;
public:
  ObjCEncodeExpr(QualType T, QualType ET, 
                 SourceLocation at, SourceLocation rp)
    : Expr(ObjCEncodeExprClass, T), EncType(ET), AtLoc(at), RParenLoc(rp) {}
  
  SourceLocation getAtLoc() const { return AtLoc; }
  SourceLocation getRParenLoc() const { return RParenLoc; }
  
  SourceRange getSourceRange() const { return SourceRange(AtLoc, RParenLoc); }

  QualType getEncodedType() const { return EncType; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == ObjCEncodeExprClass;
  }
  static bool classof(const ObjCEncodeExpr *) { return true; }
  
  // Iterators
  virtual child_iterator child_begin();
  virtual child_iterator child_end();
  
  virtual void EmitImpl(llvm::Serializer& S) const;
  static ObjCEncodeExpr* CreateImpl(llvm::Deserializer& D);
};

/// ObjCSelectorExpr used for @selector in Objective-C.
class ObjCSelectorExpr : public Expr {
  Selector SelName;
  SourceLocation AtLoc, RParenLoc;
public:
  ObjCSelectorExpr(QualType T, Selector selInfo,
                   SourceLocation at, SourceLocation rp)
  : Expr(ObjCSelectorExprClass, T), SelName(selInfo), 
  AtLoc(at), RParenLoc(rp) {}
  
  const Selector &getSelector() const { return SelName; }
  Selector &getSelector() { return SelName; }
  
  SourceLocation getAtLoc() const { return AtLoc; }
  SourceLocation getRParenLoc() const { return RParenLoc; }
  SourceRange getSourceRange() const { return SourceRange(AtLoc, RParenLoc); }
  
  /// getNumArgs - Return the number of actual arguments to this call.
  unsigned getNumArgs() const { return SelName.getNumArgs(); }
  
  static bool classof(const Stmt *T) {
    return T->getStmtClass() == ObjCSelectorExprClass;
  }
  static bool classof(const ObjCSelectorExpr *) { return true; }
  
  // Iterators
  virtual child_iterator child_begin();
  virtual child_iterator child_end();

  virtual void EmitImpl(llvm::Serializer& S) const;
  static ObjCSelectorExpr* CreateImpl(llvm::Deserializer& D);
};
  
/// ObjCProtocolExpr used for protocol in Objective-C.
class ObjCProtocolExpr : public Expr {    
  ObjCProtocolDecl *Protocol;    
  SourceLocation AtLoc, RParenLoc;
public:
  ObjCProtocolExpr(QualType T, ObjCProtocolDecl *protocol,
                   SourceLocation at, SourceLocation rp)
  : Expr(ObjCProtocolExprClass, T), Protocol(protocol), 
  AtLoc(at), RParenLoc(rp) {}
    
  ObjCProtocolDecl *getProtocol() const { return Protocol; }
    
  SourceLocation getAtLoc() const { return AtLoc; }
  SourceLocation getRParenLoc() const { return RParenLoc; }
  SourceRange getSourceRange() const { return SourceRange(AtLoc, RParenLoc); }
        
  static bool classof(const Stmt *T) {
    return T->getStmtClass() == ObjCProtocolExprClass;
  }
  static bool classof(const ObjCProtocolExpr *) { return true; }
    
  // Iterators
  virtual child_iterator child_begin();
  virtual child_iterator child_end();
};

/// ObjCIvarRefExpr - A reference to an ObjC instance variable.
class ObjCIvarRefExpr : public Expr {
  class ObjCIvarDecl *D; 
  SourceLocation Loc;
  Expr *Base;
  bool IsArrow:1;      // True if this is "X->F", false if this is "X.F".
  bool IsFreeIvar:1;   // True if ivar reference has no base (self assumed).
  
public:
  ObjCIvarRefExpr(ObjCIvarDecl *d, QualType t, SourceLocation l, Expr *base=0, 
                  bool arrow = false, bool freeIvar = false) : 
    Expr(ObjCIvarRefExprClass, t), D(d), Loc(l), Base(base), IsArrow(arrow),
    IsFreeIvar(freeIvar) {}
  
  ObjCIvarDecl *getDecl() { return D; }
  const ObjCIvarDecl *getDecl() const { return D; }
  virtual SourceRange getSourceRange() const { return SourceRange(Loc); }
  Expr *const getBase() const { return Base; }
  const bool isArrow() const { return IsArrow; }
  const bool isFreeIvar() const { return IsFreeIvar; }
  
  SourceLocation getLocation() const { return Loc; }
  
  static bool classof(const Stmt *T) { 
    return T->getStmtClass() == ObjCIvarRefExprClass; 
  }
  static bool classof(const ObjCIvarRefExpr *) { return true; }
  
  // Iterators
  virtual child_iterator child_begin();
  virtual child_iterator child_end();
  
  virtual void EmitImpl(llvm::Serializer& S) const;
  static ObjCIvarRefExpr* CreateImpl(llvm::Deserializer& D);
};

class ObjCMessageExpr : public Expr {
  enum { RECEIVER=0, ARGS_START=1 };

  Expr **SubExprs;
  
  unsigned NumArgs;
  
  // A unigue name for this message.
  Selector SelName;
  
  // A method prototype for this message (optional). 
  // FIXME: Since method decls contain the selector, and most messages have a
  // prototype, consider devising a scheme for unifying SelName/MethodProto.
  ObjCMethodDecl *MethodProto;
  
  IdentifierInfo *ClassName; // optional - 0 for instance messages.
  
  SourceLocation LBracloc, RBracloc;
public:
  // constructor for class messages. 
  // FIXME: clsName should be typed to ObjCInterfaceType
  ObjCMessageExpr(IdentifierInfo *clsName, Selector selInfo,
                  QualType retType, ObjCMethodDecl *methDecl,
                  SourceLocation LBrac, SourceLocation RBrac,
                  Expr **ArgExprs, unsigned NumArgs);
  // constructor for instance messages.
  ObjCMessageExpr(Expr *receiver, Selector selInfo,
                  QualType retType, ObjCMethodDecl *methDecl,
                  SourceLocation LBrac, SourceLocation RBrac,
                  Expr **ArgExprs, unsigned NumArgs);
  ~ObjCMessageExpr() {
    delete [] SubExprs;
  }
  
  const Expr *getReceiver() const { return SubExprs[RECEIVER]; }
  Expr *getReceiver() { return SubExprs[RECEIVER]; }
  
  const Selector &getSelector() const { return SelName; }
  Selector &getSelector() { return SelName; }

  const ObjCMethodDecl *getMethodDecl() const { return MethodProto; }
  ObjCMethodDecl *getMethodDecl() { return MethodProto; }
  
  const IdentifierInfo *getClassName() const { return ClassName; }
  IdentifierInfo *getClassName() { return ClassName; }
  
  /// getNumArgs - Return the number of actual arguments to this call.
  unsigned getNumArgs() const { return NumArgs; }

/// getArg - Return the specified argument.
  Expr *getArg(unsigned Arg) {
    assert(Arg < NumArgs && "Arg access out of range!");
    return SubExprs[Arg+ARGS_START];
  }
  const Expr *getArg(unsigned Arg) const {
    assert(Arg < NumArgs && "Arg access out of range!");
    return SubExprs[Arg+ARGS_START];
  }
  /// setArg - Set the specified argument.
  void setArg(unsigned Arg, Expr *ArgExpr) {
    assert(Arg < NumArgs && "Arg access out of range!");
    SubExprs[Arg+ARGS_START] = ArgExpr;
  }
  SourceRange getSourceRange() const { return SourceRange(LBracloc, RBracloc); }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == ObjCMessageExprClass;
  }
  static bool classof(const ObjCMessageExpr *) { return true; }
  
  // Iterators
  virtual child_iterator child_begin();
  virtual child_iterator child_end();
};

}  // end namespace clang

#endif
