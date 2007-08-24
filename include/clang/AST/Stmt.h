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
#include "llvm/ADT/iterator"
#include <iosfwd>

namespace clang {
  class Expr;
  class Decl;
  class IdentifierInfo;
  class SwitchStmt;
  
/// Stmt - This represents one statement.
///
class Stmt {
public:
  enum StmtClass {
#define STMT(N, CLASS, PARENT) CLASS##Class = N,
#define FIRST_STMT(N) firstStmtConstant = N,
#define LAST_STMT(N) lastStmtConstant = N,
#define FIRST_EXPR(N) firstExprConstant = N,
#define LAST_EXPR(N) lastExprConstant = N
#include "clang/AST/StmtNodes.def"
};
private:
  const StmtClass sClass;
public:
  Stmt(StmtClass SC) : sClass(SC) { 
    if (Stmt::CollectingStats()) Stmt::addStmtClass(SC);
  }
  virtual ~Stmt() {}

  StmtClass getStmtClass() const { return sClass; }
  const char *getStmtClassName() const;

  // global temp stats (until we have a per-module visitor)
  static void addStmtClass(const StmtClass s);
  static bool CollectingStats(bool enable=false);
  static void PrintStats();

  /// dump - This does a local dump of the specified AST fragment.  It dumps the
  /// specified node and a few nodes underneath it, but not the whole subtree.
  /// This is useful in a debugger.
  void dump() const;

  /// dumpAll - This does a dump of the specified AST fragment and all subtrees.
  void dumpAll() const;

  /// dumpPretty/printPretty - These two methods do a "pretty print" of the AST
  /// back to its original source language syntax.
  void dumpPretty() const;
  void printPretty(std::ostream &OS) const;
  
  // Implement isa<T> support.
  static bool classof(const Stmt *) { return true; }
  
  
  /// Child Iterators: All subclasses must implement child_begin and child_end
  ///  to permit easy iteration over the substatements/subexpessions of an
  ///  AST node.  This permits easy iteration over all nodes in the AST.
  typedef Stmt**                                               child_iterator;
  typedef Stmt* const *                                  const_child_iterator;
  
  typedef std::reverse_iterator<child_iterator>                
  reverse_child_iterator;
  typedef std::reverse_iterator<const_child_iterator> 
  const_reverse_child_iterator;
  
  // FIXME: Still implementing the the child_begin and child_end functions
  // for all subclasses.
#if 0
  virtual child_iterator         child_begin() = 0;
  virtual child_iterator         child_end()   = 0;
  
  const_child_iterator child_begin() const {
    return (child_iterator) const_cast<Stmt*>(this)->child_begin();
  }
  
  const_child_iterator child_end() const {
    return (child_iterator) const_cast<Stmt*>(this)->child_end();  
  }
  
  reverse_child_iterator child_rbegin() {
    return reverse_child_iterator(child_end());
  }
  
  reverse_child_iterator child_rend() {
    return reverse_child_iterator(child_begin());
  }
  
  const_reverse_child_iterator child_rbegin() const {
    return const_reverse_child_iterator(child_end());
  }
  
  const_reverse_child_iterator child_rend() const {
    return const_reverse_child_iterator(child_begin());
  }  
#endif  
};

/// DeclStmt - Adaptor class for mixing declarations with statements and
/// expressions. For example, CompoundStmt mixes statements, expressions
/// and declarations (variables, types). Another example is ForStmt, where 
/// the first statement can be an expression or a declaration.
///
class DeclStmt : public Stmt {
  Decl *TheDecl;
public:
  DeclStmt(Decl *D) : Stmt(DeclStmtClass), TheDecl(D) {}
  
  const Decl *getDecl() const { return TheDecl; }
  Decl *getDecl() { return TheDecl; }
  
  static bool classof(const Stmt *T) { 
    return T->getStmtClass() == DeclStmtClass; 
  }
  static bool classof(const DeclStmt *) { return true; }
};

/// NullStmt - This is the null statement ";": C99 6.8.3p3.
///
class NullStmt : public Stmt {
  SourceLocation SemiLoc;
public:
  NullStmt(SourceLocation L) : Stmt(NullStmtClass), SemiLoc(L) {}

  SourceLocation getSemiLoc() const { return SemiLoc; }
  
  static bool classof(const Stmt *T) { 
    return T->getStmtClass() == NullStmtClass; 
  }
  static bool classof(const NullStmt *) { return true; }
};

/// CompoundStmt - This represents a group of statements like { stmt stmt }.
///
class CompoundStmt : public Stmt {
  llvm::SmallVector<Stmt*, 16> Body;
public:
  CompoundStmt(Stmt **StmtStart, unsigned NumStmts)
    : Stmt(CompoundStmtClass), Body(StmtStart, StmtStart+NumStmts) {}
  
  bool body_empty() const { return Body.empty(); }
  
  typedef llvm::SmallVector<Stmt*, 16>::iterator body_iterator;
  body_iterator body_begin() { return Body.begin(); }
  body_iterator body_end() { return Body.end(); }
  Stmt *body_back() { return Body.back(); }

  typedef llvm::SmallVector<Stmt*, 16>::const_iterator const_body_iterator;
  const_body_iterator body_begin() const { return Body.begin(); }
  const_body_iterator body_end() const { return Body.end(); }
  const Stmt *body_back() const { return Body.back(); }

  typedef llvm::SmallVector<Stmt*, 16>::reverse_iterator reverse_body_iterator;
  reverse_body_iterator body_rbegin() { return Body.rbegin(); }
  reverse_body_iterator body_rend() { return Body.rend(); }

  typedef llvm::SmallVector<Stmt*, 16>::const_reverse_iterator 
    const_reverse_body_iterator;
  const_reverse_body_iterator body_rbegin() const { return Body.rbegin(); }
  const_reverse_body_iterator body_rend() const { return Body.rend(); }
    
  void push_back(Stmt *S) { Body.push_back(S); }
    
  static bool classof(const Stmt *T) { 
    return T->getStmtClass() == CompoundStmtClass; 
  }
  static bool classof(const CompoundStmt *) { return true; }
};

// SwitchCase is the base class for CaseStmt and DefaultStmt,
class SwitchCase : public Stmt {
  // A pointer to the following CaseStmt or DefaultStmt class,
  // used by SwitchStmt.
  SwitchCase *NextSwitchCase;
  Stmt *SubStmt;
protected:
  SwitchCase(StmtClass SC, Stmt* substmt) : Stmt(SC), NextSwitchCase(0),
                                            SubStmt(substmt) {}
  
public:
  const SwitchCase *getNextSwitchCase() const { return NextSwitchCase; }

  SwitchCase *getNextSwitchCase() { return NextSwitchCase; }

  void setNextSwitchCase(SwitchCase *SC) { NextSwitchCase = SC; }
  
  Stmt *getSubStmt() { return SubStmt; }
  
  static bool classof(const Stmt *T) { 
    return T->getStmtClass() == CaseStmtClass || 
    T->getStmtClass() == DefaultStmtClass;
  }
  static bool classof(const SwitchCase *) { return true; }
};

class CaseStmt : public SwitchCase {
  Expr *LHSVal;
  Expr *RHSVal;  // Non-null for GNU "case 1 ... 4" extension
public:
  CaseStmt(Expr *lhs, Expr *rhs, Stmt *substmt) 
    : SwitchCase(CaseStmtClass,substmt), LHSVal(lhs), RHSVal(rhs) {}
  
  Expr *getLHS() { return LHSVal; }
  Expr *getRHS() { return RHSVal; }

  static bool classof(const Stmt *T) { 
    return T->getStmtClass() == CaseStmtClass; 
  }
  static bool classof(const CaseStmt *) { return true; }
};

class DefaultStmt : public SwitchCase {
  SourceLocation DefaultLoc;
public:
  DefaultStmt(SourceLocation DL, Stmt *substmt) : 
    SwitchCase(DefaultStmtClass,substmt), DefaultLoc(DL) {}
  
  SourceLocation getDefaultLoc() const { return DefaultLoc; }

  static bool classof(const Stmt *T) { 
    return T->getStmtClass() == DefaultStmtClass; 
  }
  static bool classof(const DefaultStmt *) { return true; }
};

class LabelStmt : public Stmt {
  SourceLocation IdentLoc;
  IdentifierInfo *Label;
  Stmt *SubStmt;
public:
  LabelStmt(SourceLocation IL, IdentifierInfo *label, Stmt *substmt)
    : Stmt(LabelStmtClass), IdentLoc(IL), Label(label), SubStmt(substmt) {}
  
  SourceLocation getIdentLoc() const { return IdentLoc; }
  IdentifierInfo *getID() const { return Label; }
  const char *getName() const;
  Stmt *getSubStmt() { return SubStmt; }
  const Stmt *getSubStmt() const { return SubStmt; }

  void setIdentLoc(SourceLocation L) { IdentLoc = L; }
  void setSubStmt(Stmt *SS) { SubStmt = SS; }
  
  static bool classof(const Stmt *T) { 
    return T->getStmtClass() == LabelStmtClass; 
  }
  static bool classof(const LabelStmt *) { return true; }
};


/// IfStmt - This represents an if/then/else.
///
class IfStmt : public Stmt {
  Expr *Cond;
  Stmt *Then, *Else;
public:
  IfStmt(Expr *cond, Stmt *then, Stmt *elsev = 0)
    : Stmt(IfStmtClass), Cond(cond), Then(then), Else(elsev) {}
  
  const Expr *getCond() const { return Cond; }
  const Stmt *getThen() const { return Then; }
  const Stmt *getElse() const { return Else; }

  Expr *getCond() { return Cond; }
  Stmt *getThen() { return Then; }
  Stmt *getElse() { return Else; }
  
  static bool classof(const Stmt *T) { 
    return T->getStmtClass() == IfStmtClass; 
  }
  static bool classof(const IfStmt *) { return true; }
};

/// SwitchStmt - This represents a 'switch' stmt.
///
class SwitchStmt : public Stmt {
  Expr *Cond;
  Stmt *Body;
  
  // This points to a linked list of case and default statements.
  SwitchCase *FirstCase;
public:
  SwitchStmt(Expr *cond)
    : Stmt(SwitchStmtClass), Cond(cond), Body(0), FirstCase(0) {}
  
  const Expr *getCond() const { return Cond; }
  const Stmt *getBody() const { return Body; }
  const SwitchCase *getSwitchCaseList() const { return FirstCase; }

  Expr *getCond() { return Cond; }
  Stmt *getBody() { return Body; }
  SwitchCase *getSwitchCaseList() { return FirstCase; }

  void setBody(Stmt *S) { Body = S; }  
  
  void addSwitchCase(SwitchCase *SC) {
    if (FirstCase)
      SC->setNextSwitchCase(FirstCase);

    FirstCase = SC;
  }
  
  static bool classof(const Stmt *T) { 
    return T->getStmtClass() == SwitchStmtClass; 
  }
  static bool classof(const SwitchStmt *) { return true; }
};


/// WhileStmt - This represents a 'while' stmt.
///
class WhileStmt : public Stmt {
  Expr *Cond;
  Stmt *Body;
public:
  WhileStmt(Expr *cond, Stmt *body)
    : Stmt(WhileStmtClass), Cond(cond), Body(body) {}
  
  Expr *getCond() { return Cond; }
  const Expr *getCond() const { return Cond; }
  Stmt *getBody() { return Body; }
  const Stmt *getBody() const { return Body; }
  
  static bool classof(const Stmt *T) { 
    return T->getStmtClass() == WhileStmtClass; 
  }
  static bool classof(const WhileStmt *) { return true; }
};

/// DoStmt - This represents a 'do/while' stmt.
///
class DoStmt : public Stmt {
  Stmt *Body;
  Expr *Cond;
public:
  DoStmt(Stmt *body, Expr *cond)
    : Stmt(DoStmtClass), Body(body), Cond(cond) {}
  
  Stmt *getBody() { return Body; }
  const Stmt *getBody() const { return Body; }
  Expr *getCond() { return Cond; }
  const Expr *getCond() const { return Cond; }
  
  static bool classof(const Stmt *T) { 
    return T->getStmtClass() == DoStmtClass; 
  }
  static bool classof(const DoStmt *) { return true; }
};


/// ForStmt - This represents a 'for (init;cond;inc)' stmt.  Note that any of
/// the init/cond/inc parts of the ForStmt will be null if they were not
/// specified in the source.
///
class ForStmt : public Stmt {
  Stmt *Init;  // Expression or declstmt.
  Expr *Cond, *Inc;
  Stmt *Body;
public:
  ForStmt(Stmt *init, Expr *cond, Expr *inc, Stmt *body)
    : Stmt(ForStmtClass), Init(init), Cond(cond), Inc(inc), Body(body) {}
  
  Stmt *getInit() { return Init; }
  Expr *getCond() { return Cond; }
  Expr *getInc()  { return Inc; }
  Stmt *getBody() { return Body; }
 
  const Stmt *getInit() const { return Init; }
  const Expr *getCond() const { return Cond; }
  const Expr *getInc()  const { return Inc; }
  const Stmt *getBody() const { return Body; }
  
  static bool classof(const Stmt *T) { 
    return T->getStmtClass() == ForStmtClass; 
  }
  static bool classof(const ForStmt *) { return true; }
};

/// GotoStmt - This represents a direct goto.
///
class GotoStmt : public Stmt {
  LabelStmt *Label;
public:
  GotoStmt(LabelStmt *label) : Stmt(GotoStmtClass), Label(label) {}
  
  LabelStmt *getLabel() const { return Label; }
  
  static bool classof(const Stmt *T) { 
    return T->getStmtClass() == GotoStmtClass; 
  }
  static bool classof(const GotoStmt *) { return true; }
};

/// IndirectGotoStmt - This represents an indirect goto.
///
class IndirectGotoStmt : public Stmt {
  Expr *Target;
public:
  IndirectGotoStmt(Expr *target) : Stmt(IndirectGotoStmtClass), Target(target){}
  
  Expr *getTarget() { return Target; }
  const Expr *getTarget() const { return Target; }
  
  static bool classof(const Stmt *T) { 
    return T->getStmtClass() == IndirectGotoStmtClass; 
  }
  static bool classof(const IndirectGotoStmt *) { return true; }
};


/// ContinueStmt - This represents a continue.
///
class ContinueStmt : public Stmt {
public:
  ContinueStmt() : Stmt(ContinueStmtClass) {}
  static bool classof(const Stmt *T) { 
    return T->getStmtClass() == ContinueStmtClass; 
  }
  static bool classof(const ContinueStmt *) { return true; }
};

/// BreakStmt - This represents a break.
///
class BreakStmt : public Stmt {
public:
  BreakStmt() : Stmt(BreakStmtClass) {}
  static bool classof(const Stmt *T) { 
    return T->getStmtClass() == BreakStmtClass; 
  }
  static bool classof(const BreakStmt *) { return true; }
};


/// ReturnStmt - This represents a return, optionally of an expression.
///
class ReturnStmt : public Stmt {
  Expr *RetExpr;
public:
  ReturnStmt(Expr *E = 0) : Stmt(ReturnStmtClass), RetExpr(E) {}
  
  const Expr *getRetValue() const { return RetExpr; }
  Expr *getRetValue() { return RetExpr; }
  
  static bool classof(const Stmt *T) { 
    return T->getStmtClass() == ReturnStmtClass; 
  }
  static bool classof(const ReturnStmt *) { return true; }
};

}  // end namespace clang

#endif
