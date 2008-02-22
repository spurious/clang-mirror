// GRSimpleVals.cpp - Transfer functions for tracking simple values -*- C++ -*--
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This files defines GRSimpleVals, a sub-class of GRTransferFuncs that
//  provides transfer functions for performing simple value tracking with
//  limited support for symbolics.
//
//===----------------------------------------------------------------------===//

#include "GRSimpleVals.h"
#include "clang/Basic/Diagnostic.h"

using namespace clang;

namespace clang {
  
unsigned RunGRSimpleVals(CFG& cfg, FunctionDecl& FD, ASTContext& Ctx,
                         Diagnostic& Diag, bool Visualize) {
  
  if (Diag.hasErrorOccurred())
    return 0;
  
  GRCoreEngine<GRExprEngine> Engine(cfg, FD, Ctx);
  GRExprEngine* CheckerState = &Engine.getCheckerState();
  GRSimpleVals GRSV;
  CheckerState->setTransferFunctions(GRSV);
  
  // Execute the worklist algorithm.
  Engine.ExecuteWorkList(10000);
  
  // Look for explicit-Null dereferences and warn about them.
  for (GRExprEngine::null_iterator I=CheckerState->null_begin(),
       E=CheckerState->null_end(); I!=E; ++I) {
    
    const PostStmt& L = cast<PostStmt>((*I)->getLocation());
    Expr* Exp = cast<Expr>(L.getStmt());
    
    Diag.Report(FullSourceLoc(Exp->getExprLoc(), Ctx.getSourceManager()),
                diag::chkr_null_deref_after_check);
  }
      
#ifndef NDEBUG
  if (Visualize) CheckerState->ViewGraph();
#endif
  
  return Engine.getGraph().size();
}
  
} // end clang namespace

//===----------------------------------------------------------------------===//
// Transfer function for Casts.
//===----------------------------------------------------------------------===//

RVal GRSimpleVals::EvalCast(ValueManager& ValMgr, NonLVal X, QualType T) {
  
  if (!isa<nonlval::ConcreteInt>(X))
    return UnknownVal();
  
  llvm::APSInt V = cast<nonlval::ConcreteInt>(X).getValue();
  V.setIsUnsigned(T->isUnsignedIntegerType() || T->isPointerType());
  V.extOrTrunc(ValMgr.getContext().getTypeSize(T, SourceLocation()));
  
  if (T->isPointerType())
    return lval::ConcreteInt(ValMgr.getValue(V));
  else
    return nonlval::ConcreteInt(ValMgr.getValue(V));
}

// Casts.

RVal GRSimpleVals::EvalCast(ValueManager& ValMgr, LVal X, QualType T) {
  
  if (T->isPointerType())
    return X;
  
  assert (T->isIntegerType());
  
  if (!isa<lval::ConcreteInt>(X))
    return UnknownVal();
  
  llvm::APSInt V = cast<lval::ConcreteInt>(X).getValue();
  V.setIsUnsigned(T->isUnsignedIntegerType() || T->isPointerType());
  V.extOrTrunc(ValMgr.getContext().getTypeSize(T, SourceLocation()));

  return nonlval::ConcreteInt(ValMgr.getValue(V));
}

// Unary operators.

RVal GRSimpleVals::EvalMinus(ValueManager& ValMgr, UnaryOperator* U, NonLVal X){
  
  switch (X.getSubKind()) {
      
    case nonlval::ConcreteIntKind:
      return cast<nonlval::ConcreteInt>(X).EvalMinus(ValMgr, U);
      
    default:
      return UnknownVal();
  }
}

RVal GRSimpleVals::EvalComplement(ValueManager& ValMgr, NonLVal X) {

  switch (X.getSubKind()) {
      
    case nonlval::ConcreteIntKind:
      return cast<nonlval::ConcreteInt>(X).EvalComplement(ValMgr);
      
    default:
      return UnknownVal();
  }
}

// Binary operators.

RVal GRSimpleVals::EvalBinOp(ValueManager& ValMgr, BinaryOperator::Opcode Op,
                             NonLVal L, NonLVal R)  {  
  while (1) {
    
    switch (L.getSubKind()) {
      default:
        return UnknownVal();
        
      case nonlval::ConcreteIntKind:
        
        if (isa<nonlval::ConcreteInt>(R)) {          
          const nonlval::ConcreteInt& L_CI = cast<nonlval::ConcreteInt>(L);
          const nonlval::ConcreteInt& R_CI = cast<nonlval::ConcreteInt>(R);          
          return L_CI.EvalBinOp(ValMgr, Op, R_CI);          
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
            ValMgr.getConstraint(cast<nonlval::SymbolVal>(L).getSymbol(), Op,
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

RVal GRSimpleVals::EvalBinOp(ValueManager& ValMgr, BinaryOperator::Opcode Op,
                             LVal L, LVal R) {
  
  switch (Op) {

    default:
      return UnknownVal();
      
    case BinaryOperator::EQ:
      return EvalEQ(ValMgr, L, R);
      
    case BinaryOperator::NE:
      return EvalNE(ValMgr, L, R);      
  }
}

// Pointer arithmetic.

RVal GRSimpleVals::EvalBinOp(ValueManager& ValMgr, BinaryOperator::Opcode Op,
                             LVal L, NonLVal R) {  
  return UnknownVal();
}

// Equality operators for LVals.

RVal GRSimpleVals::EvalEQ(ValueManager& ValMgr, LVal L, LVal R) {
  
  switch (L.getSubKind()) {

    default:
      assert(false && "EQ not implemented for this LVal.");
      return UnknownVal();
      
    case lval::ConcreteIntKind:

      if (isa<lval::ConcreteInt>(R)) {
        bool b = cast<lval::ConcreteInt>(L).getValue() ==
                 cast<lval::ConcreteInt>(R).getValue();
        
        return NonLVal::MakeIntTruthVal(ValMgr, b);
      }
      else if (isa<lval::SymbolVal>(R)) {
        
        const SymIntConstraint& C =
          ValMgr.getConstraint(cast<lval::SymbolVal>(R).getSymbol(),
                               BinaryOperator::EQ,
                               cast<lval::ConcreteInt>(L).getValue());
        
        return nonlval::SymIntConstraintVal(C);
      }
      
      break;
      
    case lval::SymbolValKind: {

      if (isa<lval::ConcreteInt>(R)) {          
        const SymIntConstraint& C =
          ValMgr.getConstraint(cast<lval::SymbolVal>(L).getSymbol(),
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
      return NonLVal::MakeIntTruthVal(ValMgr, L == R);
  }
  
  return NonLVal::MakeIntTruthVal(ValMgr, false);
}

RVal GRSimpleVals::EvalNE(ValueManager& ValMgr, LVal L, LVal R) {
  
  switch (L.getSubKind()) {

    default:
      assert(false && "NE not implemented for this LVal.");
      return UnknownVal();
      
    case lval::ConcreteIntKind:
      
      if (isa<lval::ConcreteInt>(R)) {
        bool b = cast<lval::ConcreteInt>(L).getValue() !=
                 cast<lval::ConcreteInt>(R).getValue();
        
        return NonLVal::MakeIntTruthVal(ValMgr, b);
      }
      else if (isa<lval::SymbolVal>(R)) {        
        const SymIntConstraint& C =
          ValMgr.getConstraint(cast<lval::SymbolVal>(R).getSymbol(),
                               BinaryOperator::NE,
                               cast<lval::ConcreteInt>(L).getValue());
        
        return nonlval::SymIntConstraintVal(C);
      }
      
      break;
      
    case lval::SymbolValKind: {
      if (isa<lval::ConcreteInt>(R)) {          
        const SymIntConstraint& C =
          ValMgr.getConstraint(cast<lval::SymbolVal>(L).getSymbol(),
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
      return NonLVal::MakeIntTruthVal(ValMgr, L != R);
  }
  
  return NonLVal::MakeIntTruthVal(ValMgr, true);
}
