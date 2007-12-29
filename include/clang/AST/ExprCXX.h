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

    /// getOpcodeStr - Turn an Opcode enum value into the string it represents,
    /// e.g. "reinterpret_cast".
    static const char *getOpcodeStr(Opcode Op) {
      // FIXME: move out of line.
      switch (Op) {
      default: assert(0 && "Not a C++ cast expression");
      case CXXCastExpr::ConstCast:       return "const_cast";
      case CXXCastExpr::DynamicCast:     return "dynamic_cast";
      case CXXCastExpr::ReinterpretCast: return "reinterpret_cast";
      case CXXCastExpr::StaticCast:      return "static_cast";
      }
    }
    
    virtual SourceRange getSourceRange() const {
      return SourceRange(Loc, getSubExpr()->getSourceRange().getEnd());
    }
    static bool classof(const Stmt *T) { 
      return T->getStmtClass() == CXXCastExprClass;
    }
    static bool classof(const CXXCastExpr *) { return true; }
        
    // Iterators
    virtual child_iterator child_begin();
    virtual child_iterator child_end();
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

}  // end namespace clang

#endif
