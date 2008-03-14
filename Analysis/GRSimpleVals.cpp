// GRSimpleVals.cpp - Transfer functions for tracking simple values -*- C++ -*--
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines GRSimpleVals, a sub-class of GRTransferFuncs that
//  provides transfer functions for performing simple value tracking with
//  limited support for symbolics.
//
//===----------------------------------------------------------------------===//

#include "GRSimpleVals.h"
#include "clang/Analysis/PathSensitive/ValueState.h"
#include "clang/Basic/Diagnostic.h"
#include <sstream>

using namespace clang;

namespace clang {

template <typename ITERATOR>
static inline const PostStmt& GetLocation(ITERATOR I) {
  return cast<PostStmt>((*I)->getLocation());
}
  
template <>
inline const PostStmt& GetLocation(GRExprEngine::undef_arg_iterator I) {
  return cast<PostStmt>(I->first->getLocation());
}

template <typename ITERATOR>
static void EmitDiag(Diagnostic& Diag, SourceManager& SrcMgr,
                     unsigned ErrorDiag, ITERATOR I) {  
  
  Expr* Exp = cast<Expr>(GetLocation(I).getStmt());
  cast<Expr>(GetLocation(I).getStmt());  
  Diag.Report(FullSourceLoc(Exp->getExprLoc(), SrcMgr), ErrorDiag);    
}


template <>
static void EmitDiag(Diagnostic& Diag, SourceManager& SrcMgr,
                     unsigned ErrorDiag, GRExprEngine::undef_arg_iterator I) {

  Expr* E1 = cast<Expr>(GetLocation(I).getStmt());
  Expr* E2 = cast<Expr>(I->second);
  
  SourceLocation Loc = E1->getExprLoc();
  SourceRange R = E2->getSourceRange();
  Diag.Report(FullSourceLoc(Loc, SrcMgr), ErrorDiag, 0, 0, &R, 1);
}

template <typename ITERATOR>
void EmitWarning(Diagnostic& Diag, SourceManager& SrcMgr,
                 ITERATOR I, ITERATOR E, const char* msg) {
 
  std::ostringstream Out;
  Out << "[CHECKER] " << msg;
  msg = Out.str().c_str();
  
  bool isFirst = true;
  unsigned ErrorDiag;
  llvm::SmallPtrSet<void*,10> CachedErrors;  
  
  for (; I != E; ++I) {
  
    if (isFirst) {
      isFirst = false;    
      ErrorDiag = Diag.getCustomDiagID(Diagnostic::Warning, msg);
    }
    else {
      
      // HACK: Cache the location of the error.  Don't emit the same
      // warning for the same error type that occurs at the same program
      // location but along a different path.
      void* p = GetLocation(I).getRawData();

      if (CachedErrors.count(p))
        continue;
      
      CachedErrors.insert(p);
    }
    
    EmitDiag(Diag, SrcMgr, ErrorDiag, I);  
  }
}
  
unsigned RunGRSimpleVals(CFG& cfg, Decl& CD, ASTContext& Ctx,
                         Diagnostic& Diag, bool Visualize, bool TrimGraph) {
  
  if (Diag.hasErrorOccurred())
    return 0;
  
  GRCoreEngine<GRExprEngine> Eng(cfg, CD, Ctx);
  GRExprEngine* CheckerState = &Eng.getCheckerState();
  GRSimpleVals GRSV;
  CheckerState->setTransferFunctions(GRSV);
  
  // Execute the worklist algorithm.
  Eng.ExecuteWorkList(100000);
  
  SourceManager& SrcMgr = Ctx.getSourceManager();  

  EmitWarning(Diag, SrcMgr,
              CheckerState->null_derefs_begin(),
              CheckerState->null_derefs_end(),
              "NULL pointer is dereferenced after it is checked for NULL.");
  
  EmitWarning(Diag, SrcMgr,
              CheckerState->undef_derefs_begin(),
              CheckerState->undef_derefs_end(),
              "Dereference of undefined value.");
  
  EmitWarning(Diag, SrcMgr,
              CheckerState->undef_derefs_begin(),
              CheckerState->undef_derefs_end(),
              "Dereference of undefined value.");
  
  EmitWarning(Diag, SrcMgr,
              CheckerState->explicit_bad_divides_begin(),
              CheckerState->explicit_bad_divides_end(),
              "Division by zero/undefined value.");
  
  EmitWarning(Diag, SrcMgr,
              CheckerState->undef_results_begin(),
              CheckerState->undef_results_end(),
              "Result of operation is undefined.");
  
  EmitWarning(Diag, SrcMgr,
              CheckerState->bad_calls_begin(),
              CheckerState->bad_calls_end(),
              "Call using a NULL or undefined function pointer value.");
  
  EmitWarning(Diag, SrcMgr,
              CheckerState->undef_arg_begin(),
              CheckerState->undef_arg_end(),
      "Pass-by-value argument in function or message expression is undefined.");
      
#ifndef NDEBUG
  if (Visualize) CheckerState->ViewGraph(TrimGraph);
#endif
  
  return Eng.getGraph().size();
}
  
} // end clang namespace

//===----------------------------------------------------------------------===//
// Transfer function for Casts.
//===----------------------------------------------------------------------===//

RVal GRSimpleVals::EvalCast(GRExprEngine& Eng, NonLVal X, QualType T) {
  
  if (!isa<nonlval::ConcreteInt>(X))
    return UnknownVal();

  BasicValueFactory& BasicVals = Eng.getBasicVals();
  
  llvm::APSInt V = cast<nonlval::ConcreteInt>(X).getValue();
  V.setIsUnsigned(T->isUnsignedIntegerType() || T->isPointerType());
  V.extOrTrunc(Eng.getContext().getTypeSize(T));
  
  if (T->isPointerType())
    return lval::ConcreteInt(BasicVals.getValue(V));
  else
    return nonlval::ConcreteInt(BasicVals.getValue(V));
}

// Casts.

RVal GRSimpleVals::EvalCast(GRExprEngine& Eng, LVal X, QualType T) {
  
  if (T->isPointerType() || T->isReferenceType())
    return X;
  
  assert (T->isIntegerType());
  
  if (!isa<lval::ConcreteInt>(X))
    return UnknownVal();
  
  BasicValueFactory& BasicVals = Eng.getBasicVals();
  
  llvm::APSInt V = cast<lval::ConcreteInt>(X).getValue();
  V.setIsUnsigned(T->isUnsignedIntegerType() || T->isPointerType());
  V.extOrTrunc(Eng.getContext().getTypeSize(T));

  return nonlval::ConcreteInt(BasicVals.getValue(V));
}

// Unary operators.

RVal GRSimpleVals::EvalMinus(GRExprEngine& Eng, UnaryOperator* U, NonLVal X){
  
  switch (X.getSubKind()) {
      
    case nonlval::ConcreteIntKind:
      return cast<nonlval::ConcreteInt>(X).EvalMinus(Eng.getBasicVals(), U);
      
    default:
      return UnknownVal();
  }
}

RVal GRSimpleVals::EvalComplement(GRExprEngine& Eng, NonLVal X) {

  switch (X.getSubKind()) {
      
    case nonlval::ConcreteIntKind:
      return cast<nonlval::ConcreteInt>(X).EvalComplement(Eng.getBasicVals());
      
    default:
      return UnknownVal();
  }
}

// Binary operators.

RVal GRSimpleVals::EvalBinOp(GRExprEngine& Eng, BinaryOperator::Opcode Op,
                             NonLVal L, NonLVal R)  {
  
  BasicValueFactory& BasicVals = Eng.getBasicVals();
  
  while (1) {
    
    switch (L.getSubKind()) {
      default:
        return UnknownVal();
        
      case nonlval::ConcreteIntKind:
        
        if (isa<nonlval::ConcreteInt>(R)) {          
          const nonlval::ConcreteInt& L_CI = cast<nonlval::ConcreteInt>(L);
          const nonlval::ConcreteInt& R_CI = cast<nonlval::ConcreteInt>(R);          
          return L_CI.EvalBinOp(BasicVals, Op, R_CI);          
        }
        else {
          NonLVal tmp = R;
          R = L;
          L = tmp;
          continue;
        }
        
      case nonlval::SymbolValKind: {
        
        if (isa<nonlval::ConcreteInt>(R)) {
          const SymIntConstraint& C =
            BasicVals.getConstraint(cast<nonlval::SymbolVal>(L).getSymbol(), Op,
                                    cast<nonlval::ConcreteInt>(R).getValue());
          
          return nonlval::SymIntConstraintVal(C);
        }
        else
          return UnknownVal();
      }
    }
  }
}


// Binary Operators (except assignments and comma).

RVal GRSimpleVals::EvalBinOp(GRExprEngine& Eng, BinaryOperator::Opcode Op,
                             LVal L, LVal R) {
  
  switch (Op) {

    default:
      return UnknownVal();
      
    case BinaryOperator::EQ:
      return EvalEQ(Eng, L, R);
      
    case BinaryOperator::NE:
      return EvalNE(Eng, L, R);      
  }
}

// Pointer arithmetic.

RVal GRSimpleVals::EvalBinOp(GRExprEngine& Eng, BinaryOperator::Opcode Op,
                             LVal L, NonLVal R) {  
  return UnknownVal();
}

// Equality operators for LVals.

RVal GRSimpleVals::EvalEQ(GRExprEngine& Eng, LVal L, LVal R) {
  
  BasicValueFactory& BasicVals = Eng.getBasicVals();
  
  switch (L.getSubKind()) {

    default:
      assert(false && "EQ not implemented for this LVal.");
      return UnknownVal();
      
    case lval::ConcreteIntKind:

      if (isa<lval::ConcreteInt>(R)) {
        bool b = cast<lval::ConcreteInt>(L).getValue() ==
                 cast<lval::ConcreteInt>(R).getValue();
        
        return NonLVal::MakeIntTruthVal(BasicVals, b);
      }
      else if (isa<lval::SymbolVal>(R)) {
        
        const SymIntConstraint& C =
          BasicVals.getConstraint(cast<lval::SymbolVal>(R).getSymbol(),
                               BinaryOperator::EQ,
                               cast<lval::ConcreteInt>(L).getValue());
        
        return nonlval::SymIntConstraintVal(C);
      }
      
      break;
      
    case lval::SymbolValKind: {

      if (isa<lval::ConcreteInt>(R)) {          
        const SymIntConstraint& C =
          BasicVals.getConstraint(cast<lval::SymbolVal>(L).getSymbol(),
                               BinaryOperator::EQ,
                               cast<lval::ConcreteInt>(R).getValue());
        
        return nonlval::SymIntConstraintVal(C);
      }
      
      // FIXME: Implement == for lval Symbols.  This is mainly useful
      //  in iterator loops when traversing a buffer, e.g. while(z != zTerm).
      //  Since this is not useful for many checkers we'll punt on this for 
      //  now.
       
      return UnknownVal();      
    }
      
    case lval::DeclValKind:
    case lval::FuncValKind:
    case lval::GotoLabelKind:
      return NonLVal::MakeIntTruthVal(BasicVals, L == R);
  }
  
  return NonLVal::MakeIntTruthVal(BasicVals, false);
}

RVal GRSimpleVals::EvalNE(GRExprEngine& Eng, LVal L, LVal R) {
  
  BasicValueFactory& BasicVals = Eng.getBasicVals();

  switch (L.getSubKind()) {

    default:
      assert(false && "NE not implemented for this LVal.");
      return UnknownVal();
      
    case lval::ConcreteIntKind:
      
      if (isa<lval::ConcreteInt>(R)) {
        bool b = cast<lval::ConcreteInt>(L).getValue() !=
                 cast<lval::ConcreteInt>(R).getValue();
        
        return NonLVal::MakeIntTruthVal(BasicVals, b);
      }
      else if (isa<lval::SymbolVal>(R)) {        
        const SymIntConstraint& C =
          BasicVals.getConstraint(cast<lval::SymbolVal>(R).getSymbol(),
                                  BinaryOperator::NE,
                                  cast<lval::ConcreteInt>(L).getValue());
        
        return nonlval::SymIntConstraintVal(C);
      }
      
      break;
      
    case lval::SymbolValKind: {
      if (isa<lval::ConcreteInt>(R)) {          
        const SymIntConstraint& C =
          BasicVals.getConstraint(cast<lval::SymbolVal>(L).getSymbol(),
                                  BinaryOperator::NE,
                                  cast<lval::ConcreteInt>(R).getValue());
        
        return nonlval::SymIntConstraintVal(C);
      }
      
      // FIXME: Implement != for lval Symbols.  This is mainly useful
      //  in iterator loops when traversing a buffer, e.g. while(z != zTerm).
      //  Since this is not useful for many checkers we'll punt on this for 
      //  now.
      
      return UnknownVal();
      
      break;
    }
      
    case lval::DeclValKind:
    case lval::FuncValKind:
    case lval::GotoLabelKind:
      return NonLVal::MakeIntTruthVal(BasicVals, L != R);
  }
  
  return NonLVal::MakeIntTruthVal(BasicVals, true);
}

//===----------------------------------------------------------------------===//
// Transfer function for Function Calls.
//===----------------------------------------------------------------------===//

void GRSimpleVals::EvalCall(ExplodedNodeSet<ValueState>& Dst,
                            GRExprEngine& Eng,
                            GRStmtNodeBuilder<ValueState>& Builder,
                            CallExpr* CE, LVal L,
                            ExplodedNode<ValueState>* Pred) {
  
  ValueStateManager& StateMgr = Eng.getStateManager();
  ValueState* St = Builder.GetState(Pred);
  
  // Invalidate all arguments passed in by reference (LVals).

  for (CallExpr::arg_iterator I = CE->arg_begin(), E = CE->arg_end();
        I != E; ++I) {

    RVal V = StateMgr.GetRVal(St, *I);
    
    if (isa<LVal>(V))
      St = StateMgr.SetRVal(St, cast<LVal>(V), UnknownVal());
  }
  
  // Make up a symbol for the return value of this function.
  
  if (CE->getType() != Eng.getContext().VoidTy) {    
    unsigned Count = Builder.getCurrentBlockCount();
    SymbolID Sym = Eng.getSymbolManager().getConjuredSymbol(CE, Count);
        
    RVal X = CE->getType()->isPointerType() 
             ? cast<RVal>(lval::SymbolVal(Sym)) 
             : cast<RVal>(nonlval::SymbolVal(Sym));
    
    St = StateMgr.SetRVal(St, CE, X, Eng.getCFG().isBlkExpr(CE), false);
  }  
    
  Builder.Nodify(Dst, CE, Pred, St);
}
