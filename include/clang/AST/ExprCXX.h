//===--- ExprCXX.h - Classes for representing expressions -------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file was developed by Bill Wendling and is distributed under the
// University of Illinois Open Source License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the Expr interface and subclasses for C++ expressions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_EXPRCXX_H
#define LLVM_CLANG_AST_EXPRCXX_H

#include "clang/AST/Expr.h"

namespace clang {

  //===--------------------------------------------------------------------===//
  // C++ Expressions.
  //===--------------------------------------------------------------------===//

  /// CXXCastExpr - [C++ 5.2.7, 5.2.9, 5.2.10, 5.2.11] C++ Cast Operators.
  /// 
  class CXXCastExpr : public Expr {
  public:
    enum Opcode {
      DynamicCast,
      StaticCast,
      ReinterpretCast,
      ConstCast
    };
  private:
    QualType Ty;
    Opcode Opc;
    Expr *Op;
    SourceLocation Loc; // the location of the casting op
  public:
    CXXCastExpr(Opcode op, QualType ty, Expr *expr, SourceLocation l)
      : Expr(CXXCastExprClass, ty), Ty(ty), Opc(op), Op(expr), Loc(l) {}

    QualType getDestType() const { return Ty; }
    Expr *getSubExpr() const { return Op; }
  
    Opcode getOpcode() const { return Opc; }

    virtual SourceRange getSourceRange() const {
      return SourceRange(Loc, getSubExpr()->getSourceRange().End());
    }
    virtual void visit(StmtVisitor &Visitor);
    static bool classof(const Stmt *T) { 
      return T->getStmtClass() == CXXCastExprClass;
    }
    static bool classof(const CXXCastExpr *) { return true; }
  };

  /// CXXBoolLiteralExpr - [C++ 2.13.5] C++ Boolean Literal.
  /// 
  class CXXBoolLiteralExpr : public Expr {
    bool Value;
    SourceLocation Loc;
  public:
    CXXBoolLiteralExpr(bool val, SourceLocation l) : 
      Expr(CXXBoolLiteralExprClass, QualType()), Value(val), Loc(l) {}
    
    bool getValue() const { return Value; }

    virtual SourceRange getSourceRange() const { return SourceRange(Loc); }
      
    virtual void visit(StmtVisitor &Visitor);
    static bool classof(const Stmt *T) { 
      return T->getStmtClass() == CXXBoolLiteralExprClass;
    }
    static bool classof(const CXXBoolLiteralExpr *) { return true; }
  };

}  // end namespace clang

#endif
