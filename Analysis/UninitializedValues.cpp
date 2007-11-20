//==- UninitializedValues.cpp - Find Unintialized Values --------*- C++ --*-==//
//
//                     The LLVM Compiler Infrastructure
//
// This file was developed by Ted Kremenek and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements Uninitialized Values analysis for source-level CFGs.
//
//===----------------------------------------------------------------------===//

#include "clang/Analysis/UninitializedValues.h"
#include "clang/Analysis/Visitors/CFGRecStmtDeclVisitor.h"
#include "clang/Analysis/LocalCheckers.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/AST/ASTContext.h"
#include "clang/Analysis/FlowSensitive/DataflowSolver.h"

#include "llvm/ADT/SmallPtrSet.h"

using namespace clang;

//===----------------------------------------------------------------------===//
// Dataflow initialization logic.
//===----------------------------------------------------------------------===//      

namespace {

class RegisterDecls : public CFGRecStmtDeclVisitor<RegisterDecls> {  
  UninitializedValues::AnalysisDataTy& AD;
public:
  RegisterDecls(UninitializedValues::AnalysisDataTy& ad) :  AD(ad) {}
  
  void VisitBlockVarDecl(BlockVarDecl* VD) { AD.Register(VD); }
  CFG& getCFG() { return AD.getCFG(); }
};
  
} // end anonymous namespace

void UninitializedValues::InitializeValues(const CFG& cfg) {
  RegisterDecls R(getAnalysisData());
  cfg.VisitBlockStmts(R);
}

//===----------------------------------------------------------------------===//
// Transfer functions.
//===----------------------------------------------------------------------===//      

namespace {

class TransferFuncs : public CFGStmtVisitor<TransferFuncs,bool> {
  UninitializedValues::ValTy V;
  UninitializedValues::AnalysisDataTy& AD;
public:
  TransferFuncs(UninitializedValues::AnalysisDataTy& ad) : AD(ad) {
    V.resetValues(AD);
  }
  
  UninitializedValues::ValTy& getVal() { return V; }
  CFG& getCFG() { return AD.getCFG(); }
  
  bool VisitDeclRefExpr(DeclRefExpr* DR);
  bool VisitBinaryOperator(BinaryOperator* B);
  bool VisitUnaryOperator(UnaryOperator* U);
  bool VisitStmt(Stmt* S);
  bool VisitCallExpr(CallExpr* C);
  bool VisitDeclStmt(DeclStmt* D);
  bool VisitConditionalOperator(ConditionalOperator* C);
  
  bool Visit(Stmt *S);
  bool BlockStmt_VisitExpr(Expr* E);
  
  BlockVarDecl* FindBlockVarDecl(Stmt* S);
};
  
static const bool Initialized = true;
static const bool Uninitialized = false;  

bool TransferFuncs::VisitDeclRefExpr(DeclRefExpr* DR) {
  if (BlockVarDecl* VD = dyn_cast<BlockVarDecl>(DR->getDecl())) {
    if (AD.Observer) AD.Observer->ObserveDeclRefExpr(V,AD,DR,VD);
     
    // Pseudo-hack to prevent cascade of warnings.  If an accessed variable
    // is uninitialized, then we are already going to flag a warning for
    // this variable, which a "source" of uninitialized values.
    // We can otherwise do a full "taint" of uninitialized values.  The
    // client has both options by toggling AD.FullUninitTaint.

    return AD.FullUninitTaint ? V(VD,AD) : Initialized;
  }
  else return Initialized;
}

BlockVarDecl* TransferFuncs::FindBlockVarDecl(Stmt *S) {
  for (;;)
    if (ParenExpr* P = dyn_cast<ParenExpr>(S)) {
      S = P->getSubExpr(); continue;
    }
    else if (DeclRefExpr* DR = dyn_cast<DeclRefExpr>(S)) {
      if (BlockVarDecl* VD = dyn_cast<BlockVarDecl>(DR->getDecl()))
        return VD;
    }
    else return NULL;
}

bool TransferFuncs::VisitBinaryOperator(BinaryOperator* B) {
  if (BlockVarDecl* VD = FindBlockVarDecl(B->getLHS()))
    if (B->isAssignmentOp()) {
      if (AD.FullUninitTaint) {
        if (B->getOpcode() == BinaryOperator::Assign)
          return V(VD,AD) = Visit(B->getRHS());
        else // Handle +=, -=, *=, etc.  We do want '&', not '&&'.
          return V(VD,AD) = Visit(B->getLHS()) & Visit(B->getRHS());
      }
      else {
        Visit(B->getLHS()); Visit(B->getRHS());
        return Initialized;
      }
    }

  return VisitStmt(B);
}

bool TransferFuncs::VisitDeclStmt(DeclStmt* S) {
  for (ScopedDecl* D = S->getDecl(); D != NULL; D = D->getNextDeclarator())
    if (BlockVarDecl* VD = dyn_cast<BlockVarDecl>(D)) {
      if (Stmt* I = VD->getInit()) 
        V(VD,AD) = AD.FullUninitTaint ? V(cast<Expr>(I),AD) : Initialized;
      else V(VD,AD) = Uninitialized;
    }
      
  return Uninitialized; // Value is never consumed.
}

bool TransferFuncs::VisitCallExpr(CallExpr* C) {
  VisitChildren(C);
  return Initialized;
}

bool TransferFuncs::VisitUnaryOperator(UnaryOperator* U) {
  if (U->getOpcode() == UnaryOperator::AddrOf)
    if (BlockVarDecl* VD = FindBlockVarDecl(U->getSubExpr()))
      return V(VD,AD) = Initialized;

  return Visit(U->getSubExpr());
}
  
bool TransferFuncs::VisitConditionalOperator(ConditionalOperator* C) {
  Visit(C->getCond());
  return Visit(C->getLHS()) & Visit(C->getRHS());  // Yes: we want &, not &&.
}

bool TransferFuncs::VisitStmt(Stmt* S) {
  bool x = Initialized;

  // We don't stop at the first subexpression that is Uninitialized because
  // evaluating some subexpressions may result in propogating "Uninitialized"
  // or "Initialized" to variables referenced in the other subexpressions.
  for (Stmt::child_iterator I=S->child_begin(), E=S->child_end(); I!=E; ++I)
    if (*I && Visit(*I) == Uninitialized) x = Uninitialized;
  
  return x;
}
  
bool TransferFuncs::Visit(Stmt *S) {
  if (AD.isTracked(static_cast<Expr*>(S))) return V(static_cast<Expr*>(S),AD);
  else return static_cast<CFGStmtVisitor<TransferFuncs,bool>*>(this)->Visit(S);
}

bool TransferFuncs::BlockStmt_VisitExpr(Expr* E) {
  assert (AD.isTracked(E));
  return V(E,AD) = 
    static_cast<CFGStmtVisitor<TransferFuncs,bool>*>(this)->Visit(E);
}
  
} // end anonymous namespace

//===----------------------------------------------------------------------===//
// Merge operator.
//
//  In our transfer functions we take the approach that any
//  combination of unintialized values, e.g. Unitialized + ___ = Unitialized.
//
//  Merges take the opposite approach.
//
//  In the merge of dataflow values we prefer unsoundness, and
//  prefer false negatives to false positives.  At merges, if a value for a
//  tracked Decl is EVER initialized in any of the predecessors we treat it as
//  initialized at the confluence point.
//===----------------------------------------------------------------------===//      

namespace {
  typedef ExprDeclBitVector_Types::Union Merge;
  typedef DataflowSolver<UninitializedValues,TransferFuncs,Merge> Solver;
}

//===----------------------------------------------------------------------===//
// Unitialized values checker.   Scan an AST and flag variable uses
//===----------------------------------------------------------------------===//      

UninitializedValues_ValueTypes::ObserverTy::~ObserverTy() {}

namespace {
class UninitializedValuesChecker : public UninitializedValues::ObserverTy {
  ASTContext &Ctx;
  Diagnostic &Diags;
  llvm::SmallPtrSet<BlockVarDecl*,10> AlreadyWarned;
  
public:
  UninitializedValuesChecker(ASTContext &ctx, Diagnostic &diags)
    : Ctx(ctx), Diags(diags) {}
    
  virtual void ObserveDeclRefExpr(UninitializedValues::ValTy& V,
                                  UninitializedValues::AnalysisDataTy& AD,
                                  DeclRefExpr* DR, BlockVarDecl* VD) {

    assert ( AD.isTracked(VD) && "Unknown VarDecl.");
    
    if (V(VD,AD) == Uninitialized)
      if (AlreadyWarned.insert(VD))
        Diags.Report(DR->getSourceRange().getBegin(), diag::warn_uninit_val);
  }
};
} // end anonymous namespace

namespace clang {
void CheckUninitializedValues(CFG& cfg, ASTContext &Ctx, Diagnostic &Diags,
                              bool FullUninitTaint) {
  
  // Compute the unitialized values information.
  UninitializedValues U(cfg);
  U.getAnalysisData().FullUninitTaint = FullUninitTaint;
  Solver S(U);
  S.runOnCFG(cfg);
  
  // Scan for DeclRefExprs that use uninitialized values.
  UninitializedValuesChecker Observer(Ctx,Diags);
  U.getAnalysisData().Observer = &Observer;
  S.runOnAllBlocks(cfg);
}
} // end namespace clang
