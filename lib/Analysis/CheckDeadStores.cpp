//==- DeadStores.cpp - Check for stores to dead variables --------*- C++ -*-==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines a DeadStores, a flow-sensitive checker that looks for
//  stores to variables that are no longer live.
//
//===----------------------------------------------------------------------===//

#include "clang/Analysis/LocalCheckers.h"
#include "clang/Analysis/Analyses/LiveVariables.h"
#include "clang/Analysis/Visitors/CFGRecStmtVisitor.h"
#include "clang/Analysis/PathSensitive/BugReporter.h"
#include "clang/Analysis/PathSensitive/GRExprEngine.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/ParentMap.h"
#include "llvm/Support/Compiler.h"

using namespace clang;

namespace {

class VISIBILITY_HIDDEN DeadStoreObs : public LiveVariables::ObserverTy {
  ASTContext &Ctx;
  BugReporter& BR;
  ParentMap& Parents;
    
public:
  DeadStoreObs(ASTContext &ctx, BugReporter& br, ParentMap& parents)
    : Ctx(ctx), BR(br), Parents(parents) {}
  
  virtual ~DeadStoreObs() {}
  
  void Report(VarDecl* V, bool inEnclosing, SourceLocation L, SourceRange R,
              bool isInitialization = false) {

    std::string name(V->getName());
    
    if (isInitialization) {
      std::string msg = "Value stored to '" + name +
        "' during its initialization is never read";
      
      BR.EmitBasicReport("dead initialization", msg.c_str(), L, R);      
    }
    else {
      std::string msg = inEnclosing
        ? "Although the value stored to '" + name +
          "' is used in the enclosing expression, the value is never actually"
          " read from '" + name + "'"
        : "Value stored to '" + name + "' is never read";
    
      BR.EmitBasicReport("dead store", msg.c_str(), L, R);
    }
  }
  
  void CheckVarDecl(VarDecl* VD, Expr* Ex, Expr* Val,
                    bool hasEnclosing,
                    const LiveVariables::AnalysisDataTy& AD,
                    const LiveVariables::ValTy& Live) {

    if (VD->hasLocalStorage() && !Live(VD, AD))
      Report(VD, hasEnclosing, Ex->getSourceRange().getBegin(),
             Val->getSourceRange());      
  }
  
  void CheckDeclRef(DeclRefExpr* DR, Expr* Val,
                    const LiveVariables::AnalysisDataTy& AD,
                    const LiveVariables::ValTy& Live) {
    
    if (VarDecl* VD = dyn_cast<VarDecl>(DR->getDecl()))
      CheckVarDecl(VD, DR, Val, false, AD, Live);
  }
  
  virtual void ObserveStmt(Stmt* S,
                           const LiveVariables::AnalysisDataTy& AD,
                           const LiveVariables::ValTy& Live) {
    
    // Skip statements in macros.
    if (S->getLocStart().isMacroID())
      return;
    
    if (BinaryOperator* B = dyn_cast<BinaryOperator>(S)) {    
      if (!B->isAssignmentOp()) return; // Skip non-assignments.
      
      if (DeclRefExpr* DR = dyn_cast<DeclRefExpr>(B->getLHS()))
        if (VarDecl *VD = dyn_cast<VarDecl>(DR->getDecl())) {
     
          // Special case: check for assigning null to a pointer.  This
          //  is a common form of defensive programming.
          // FIXME: Make this optional?
          
          Expr* Val = B->getRHS();
          llvm::APSInt Result(Ctx.getTypeSize(Val->getType()));
          
          if (VD->getType()->isPointerType() &&
              Val->IgnoreParenCasts()->isIntegerConstantExpr(Result, Ctx, 0))
            if (Result == 0)
              return;

          CheckVarDecl(VD, DR, Val, Parents.isSubExpr(B), AD, Live);
        }              
    }
    else if (UnaryOperator* U = dyn_cast<UnaryOperator>(S)) {
      if (!U->isIncrementOp())
        return;
      
      Expr *Ex = U->getSubExpr()->IgnoreParenCasts();
      
      if (DeclRefExpr* DR = dyn_cast<DeclRefExpr>(Ex))
        CheckDeclRef(DR, U, AD, Live);
    }    
    else if (DeclStmt* DS = dyn_cast<DeclStmt>(S))
      // Iterate through the decls.  Warn if any initializers are complex
      // expressions that are not live (never used).
      for (ScopedDecl* SD = DS->getDecl(); SD; SD = SD->getNextDeclarator()) {        
        
        VarDecl* V = dyn_cast<VarDecl>(SD);
        if (!V) continue;
        
        if (V->hasLocalStorage())
          if (Expr* E = V->getInit())
            if (!Live(V, AD)) {
              // Special case: check for initializations with constants.
              //
              //  e.g. : int x = 0;
              //
              // If x is EVER assigned a new value later, don't issue
              // a warning.  This is because such initialization can be
              // due to defensive programming.
              if (!E->isConstantExpr(Ctx,NULL))
                Report(V, false, V->getLocation(), E->getSourceRange(), true);
            }
      }
  }
};
  
} // end anonymous namespace

//===----------------------------------------------------------------------===//
// Driver function to invoke the Dead-Stores checker on a CFG.
//===----------------------------------------------------------------------===//

void clang::CheckDeadStores(LiveVariables& L, BugReporter& BR) {  
  DeadStoreObs A(BR.getContext(), BR, BR.getParentMap());
  L.runOnAllBlocks(*BR.getCFG(), &A);
}
