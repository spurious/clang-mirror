//===--- Stmt.h - Classes for representing statements -----------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file was developed by Chris Lattner and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the Stmt interface and subclasses.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_STMT_H
#define LLVM_CLANG_AST_STMT_H

#include "clang/Basic/SourceLocation.h"
#include "llvm/ADT/SmallVector.h"

namespace llvm {
namespace clang {
  class Expr;
  
/// Stmt - This represents one statement.  Note that statements are modelled as
/// subclasses of exprs so that 
///
class Stmt {
  /// Type.
public:
  Stmt() {}
  virtual ~Stmt() {}
  
  // FIXME: Change to non-virtual method that uses visitor pattern to do this.
  void dump() const;
  
private:
  virtual void dump_impl() const = 0;
};

/// CompoundStmt - This represents a group of statements like { stmt stmt }.
///
class CompoundStmt : public Stmt {
  SmallVector<Stmt*, 16> Body;
public:
  CompoundStmt(Stmt **StmtStart, unsigned NumStmts)
    : Body(StmtStart, StmtStart+NumStmts) {}
  
  virtual void dump_impl() const;
};




/// ReturnStmt - This represents a return, optionally of an expression.
///
class ReturnStmt : public Stmt {
  Expr *RetExpr;
public:
  ReturnStmt(Expr *E = 0) : RetExpr(E) {}
  
  virtual void dump_impl() const;
};


  
}  // end namespace clang
}  // end namespace llvm

#endif
