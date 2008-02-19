//=-- GRExprEngine.cpp - Path-Sensitive Expression-Level Dataflow ---*- C++ -*-=
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines a meta-engine for path-sensitive dataflow analysis that
//  is built on GREngine, but provides the boilerplate to execute transfer
//  functions and build the ExplodedGraph at the expression level.
//
//===----------------------------------------------------------------------===//

#include "clang/Analysis/PathSensitive/GRExprEngine.h"
#include "clang/Analysis/PathSensitive/GRTransferFuncs.h"

#include "llvm/Support/Streams.h"

using namespace clang;
using llvm::dyn_cast;
using llvm::cast;
using llvm::APSInt;

GRExprEngine::StateTy
GRExprEngine::SetValue(StateTy St, Expr* S, const RValue& V) {

  if (!StateCleaned) {
    St = RemoveDeadBindings(CurrentStmt, St);
    StateCleaned = true;
  }

  bool isBlkExpr = false;
    
  if (S == CurrentStmt) {
    isBlkExpr = getCFG().isBlkExpr(S);
    
    if (!isBlkExpr)
      return St;
  }

  return StateMgr.SetValue(St, S, isBlkExpr, V);
}

const GRExprEngine::StateTy::BufferTy&
GRExprEngine::SetValue(StateTy St, Expr* S, const RValue::BufferTy& RB,
                      StateTy::BufferTy& RetBuf) {
  
  assert (RetBuf.empty());
  
  for (RValue::BufferTy::const_iterator I=RB.begin(), E=RB.end(); I!=E; ++I)
    RetBuf.push_back(SetValue(St, S, *I));
                     
  return RetBuf;
}

GRExprEngine::StateTy
GRExprEngine::SetValue(StateTy St, const LValue& LV, const RValue& V) {
  
  if (LV.isUnknown())
    return St;
  
  if (!StateCleaned) {
    St = RemoveDeadBindings(CurrentStmt, St);
    StateCleaned = true;
  }
  
  return StateMgr.SetValue(St, LV, V);
}

void GRExprEngine::ProcessBranch(Expr* Condition, Stmt* Term,
                                BranchNodeBuilder& builder) {

  // Remove old bindings for subexpressions.
  StateTy PrevState = StateMgr.RemoveSubExprBindings(builder.getState());
  
  // Check for NULL conditions; e.g. "for(;;)"
  if (!Condition) { 
    builder.markInfeasible(false);
    
    // Get the current block counter.
    GRBlockCounter BC = builder.getBlockCounter();
    unsigned BlockID = builder.getTargetBlock(true)->getBlockID();
    unsigned NumVisited = BC.getNumVisited(BlockID);
        
    if (NumVisited < 1) builder.generateNode(PrevState, true);
    else builder.markInfeasible(true);

    return;
  }
  
  RValue V = GetValue(PrevState, Condition);
  
  switch (V.getBaseKind()) {
    default:
      break;

    case RValue::UnknownKind:
      builder.generateNode(PrevState, true);
      builder.generateNode(PrevState, false);
      return;
      
    case RValue::UninitializedKind: {      
      NodeTy* N = builder.generateNode(PrevState, true);

      if (N) {
        N->markAsSink();
        UninitBranches.insert(N);
      }
      
      builder.markInfeasible(false);
      return;
    }      
  }
  
  // Get the current block counter.
  GRBlockCounter BC = builder.getBlockCounter();
  unsigned BlockID = builder.getTargetBlock(true)->getBlockID();
  unsigned NumVisited = BC.getNumVisited(BlockID);
  
  if (isa<nonlval::ConcreteInt>(V) || 
      BC.getNumVisited(builder.getTargetBlock(true)->getBlockID()) < 1) {
    
    // Process the true branch.

    bool isFeasible = true;
    
    StateTy St = Assume(PrevState, V, true, isFeasible);

    if (isFeasible)
      builder.generateNode(St, true);
    else
      builder.markInfeasible(true);
  }
  else
    builder.markInfeasible(true);
  
  BlockID = builder.getTargetBlock(false)->getBlockID();
  NumVisited = BC.getNumVisited(BlockID);
  
  if (isa<nonlval::ConcreteInt>(V) || 
      BC.getNumVisited(builder.getTargetBlock(false)->getBlockID()) < 1) {
    
    // Process the false branch.  
    
    bool isFeasible = false;
    
    StateTy St = Assume(PrevState, V, false, isFeasible);
    
    if (isFeasible)
      builder.generateNode(St, false);
    else
      builder.markInfeasible(false);
  }
  else
    builder.markInfeasible(false);
}

/// ProcessIndirectGoto - Called by GRCoreEngine.  Used to generate successor
///  nodes by processing the 'effects' of a computed goto jump.
void GRExprEngine::ProcessIndirectGoto(IndirectGotoNodeBuilder& builder) {

  StateTy St = builder.getState();  
  LValue V = cast<LValue>(GetValue(St, builder.getTarget()));
  
  // Three possibilities:
  //
  //   (1) We know the computed label.
  //   (2) The label is NULL (or some other constant), or Uninitialized.
  //   (3) We have no clue about the label.  Dispatch to all targets.
  //
  
  typedef IndirectGotoNodeBuilder::iterator iterator;

  if (isa<lval::GotoLabel>(V)) {
    LabelStmt* L = cast<lval::GotoLabel>(V).getLabel();
    
    for (iterator I=builder.begin(), E=builder.end(); I != E; ++I) {
      if (I.getLabel() == L) {
        builder.generateNode(I, St);
        return;
      }
    }
    
    assert (false && "No block with label.");
    return;
  }

  if (isa<lval::ConcreteInt>(V) || isa<UninitializedVal>(V)) {
    // Dispatch to the first target and mark it as a sink.
    NodeTy* N = builder.generateNode(builder.begin(), St, true);
    UninitBranches.insert(N);
    return;
  }
  
  // This is really a catch-all.  We don't support symbolics yet.
  
  assert (isa<UnknownVal>(V));
  
  for (iterator I=builder.begin(), E=builder.end(); I != E; ++I)
    builder.generateNode(I, St);
}

/// ProcessSwitch - Called by GRCoreEngine.  Used to generate successor
///  nodes by processing the 'effects' of a switch statement.
void GRExprEngine::ProcessSwitch(SwitchNodeBuilder& builder) {
  
  typedef SwitchNodeBuilder::iterator iterator;
  
  StateTy St = builder.getState();  
  Expr* CondE = builder.getCondition();
  NonLValue CondV = cast<NonLValue>(GetValue(St, CondE));

  if (isa<UninitializedVal>(CondV)) {
    NodeTy* N = builder.generateDefaultCaseNode(St, true);
    UninitBranches.insert(N);
    return;
  }
  
  StateTy  DefaultSt = St;
  
  // While most of this can be assumed (such as the signedness), having it
  // just computed makes sure everything makes the same assumptions end-to-end.
  
  unsigned bits = getContext().getTypeSize(CondE->getType(),
                                           CondE->getExprLoc());

  APSInt V1(bits, false);
  APSInt V2 = V1;
  
  for (iterator I=builder.begin(), E=builder.end(); I!=E; ++I) {

    CaseStmt* Case = cast<CaseStmt>(I.getCase());
    
    // Evaluate the case.
    if (!Case->getLHS()->isIntegerConstantExpr(V1, getContext(), 0, true)) {
      assert (false && "Case condition must evaluate to an integer constant.");
      return;
    }
    
    // Get the RHS of the case, if it exists.
    
    if (Expr* E = Case->getRHS()) {
      if (!E->isIntegerConstantExpr(V2, getContext(), 0, true)) {
        assert (false &&
                "Case condition (RHS) must evaluate to an integer constant.");
        return ;
      }
      
      assert (V1 <= V2);
    }
    else V2 = V1;
    
    // FIXME: Eventually we should replace the logic below with a range
    //  comparison, rather than concretize the values within the range.
    //  This should be easy once we have "ranges" for NonLValues.
        
    do {      
      nonlval::ConcreteInt CaseVal(ValMgr.getValue(V1));
      
      NonLValue Res = EvalBinaryOp(BinaryOperator::EQ, CondV, CaseVal);
      
      // Now "assume" that the case matches.
      bool isFeasible = false;
      
      StateTy StNew = Assume(St, Res, true, isFeasible);
      
      if (isFeasible) {
        builder.generateCaseStmtNode(I, StNew);
       
        // If CondV evaluates to a constant, then we know that this
        // is the *only* case that we can take, so stop evaluating the
        // others.
        if (isa<nonlval::ConcreteInt>(CondV))
          return;
      }
      
      // Now "assume" that the case doesn't match.  Add this state
      // to the default state (if it is feasible).
      
      StNew = Assume(DefaultSt, Res, false, isFeasible);
      
      if (isFeasible)
        DefaultSt = StNew;

      // Concretize the next value in the range.      
      ++V1;
      
    } while (V1 < V2);
  }
  
  // If we reach here, than we know that the default branch is
  // possible.  
  builder.generateDefaultCaseNode(DefaultSt);
}


void GRExprEngine::VisitLogicalExpr(BinaryOperator* B, NodeTy* Pred,
                                   NodeSet& Dst) {

  bool hasR2;
  StateTy PrevState = Pred->getState();

  RValue R1 = GetValue(PrevState, B->getLHS());
  RValue R2 = GetValue(PrevState, B->getRHS(), hasR2);
  
  if (hasR2) {
    if (isa<UninitializedVal>(R2) || isa<UnknownVal>(R2)) {
      Nodify(Dst, B, Pred, SetValue(PrevState, B, R2));
      return;
    }
  }
  else if (isa<UninitializedVal>(R1) || isa<UnknownVal>(R1)) {
    Nodify(Dst, B, Pred, SetValue(PrevState, B, R1));
    return;
  }

  // R1 is an expression that can evaluate to either 'true' or 'false'.
  if (B->getOpcode() == BinaryOperator::LAnd) {
    // hasR2 == 'false' means that LHS evaluated to 'false' and that
    // we short-circuited, leading to a value of '0' for the '&&' expression.
    if (hasR2 == false) { 
      Nodify(Dst, B, Pred, SetValue(PrevState, B, GetRValueConstant(0U, B)));
      return;
    }
  }
  else {
    assert (B->getOpcode() == BinaryOperator::LOr);
    // hasR2 == 'false' means that the LHS evaluate to 'true' and that
    //  we short-circuited, leading to a value of '1' for the '||' expression.
    if (hasR2 == false) {
      Nodify(Dst, B, Pred, SetValue(PrevState, B, GetRValueConstant(1U, B)));
      return;      
    }
  }
    
  // If we reach here we did not short-circuit.  Assume R2 == true and
  // R2 == false.
    
  bool isFeasible;
  StateTy St = Assume(PrevState, R2, true, isFeasible);
  
  if (isFeasible)
    Nodify(Dst, B, Pred, SetValue(PrevState, B, GetRValueConstant(1U, B)));

  St = Assume(PrevState, R2, false, isFeasible);
  
  if (isFeasible)
    Nodify(Dst, B, Pred, SetValue(PrevState, B, GetRValueConstant(0U, B)));  
}



void GRExprEngine::ProcessStmt(Stmt* S, StmtNodeBuilder& builder) {
  Builder = &builder;

  StmtEntryNode = builder.getLastNode();
  CurrentStmt = S;
  NodeSet Dst;
  StateCleaned = false;

  Visit(S, StmtEntryNode, Dst);

  // If no nodes were generated, generate a new node that has all the
  // dead mappings removed.
  if (Dst.size() == 1 && *Dst.begin() == StmtEntryNode) {
    StateTy St = RemoveDeadBindings(S, StmtEntryNode->getState());
    builder.generateNode(S, St, StmtEntryNode);
  }
  
  CurrentStmt = NULL;
  StmtEntryNode = NULL;
  Builder = NULL;
}

GRExprEngine::NodeTy*
GRExprEngine::Nodify(NodeSet& Dst, Stmt* S, NodeTy* Pred, StateTy St) {
 
  // If the state hasn't changed, don't generate a new node.
  if (St == Pred->getState())
    return NULL;
  
  NodeTy* N = Builder->generateNode(S, St, Pred);
  Dst.Add(N);
  return N;
}

void GRExprEngine::Nodify(NodeSet& Dst, Stmt* S, NodeTy* Pred,
                         const StateTy::BufferTy& SB) {
  
  for (StateTy::BufferTy::const_iterator I=SB.begin(), E=SB.end(); I!=E; ++I)
    Nodify(Dst, S, Pred, *I);
}

void GRExprEngine::VisitDeclRefExpr(DeclRefExpr* D, NodeTy* Pred, NodeSet& Dst){
  if (D != CurrentStmt) {
    Dst.Add(Pred); // No-op. Simply propagate the current state unchanged.
    return;
  }
  
  // If we are here, we are loading the value of the decl and binding
  // it to the block-level expression.
  
  StateTy St = Pred->getState();
  
  Nodify(Dst, D, Pred, SetValue(St, D, GetValue(St, D)));
}

void GRExprEngine::VisitCall(CallExpr* CE, NodeTy* Pred,
                             CallExpr::arg_iterator I, CallExpr::arg_iterator E,
                             NodeSet& Dst) {
  
  if (I != E) {
    NodeSet DstTmp;  
    Visit(*I, Pred, DstTmp);
    ++I;
    
    for (NodeSet::iterator DI=DstTmp.begin(), DE=DstTmp.end(); DI!=DE; ++DI)
      VisitCall(CE, *DI, I, E, Dst);
    
    return;
  }

  // If we reach here we have processed all of the arguments.  Evaluate
  // the callee expression.
  NodeSet DstTmp;
  Visit(CE->getCallee(), Pred, DstTmp);
  
  // Finally, evaluate the function call.
  for (NodeSet::iterator DI=DstTmp.begin(), DE=DstTmp.end(); DI!=DE; ++DI) {
    StateTy St = (*DI)->getState();    
    LValue L = GetLValue(St, CE->getCallee());

    // Check for uninitialized control-flow.
    if (isa<UninitializedVal>(L)) {
      NodeTy* N = Builder->generateNode(CE, St, *DI);
      N->markAsSink();
      UninitBranches.insert(N);
      continue;
    }
    
    // Note: EvalCall must handle the case where the callee is "UnknownVal."
    Nodify(Dst, CE, *DI, EvalCall(CE, (*DI)->getState()));
  }
}

void GRExprEngine::VisitCast(Expr* CastE, Expr* E, NodeTy* Pred, NodeSet& Dst) {
  
  NodeSet S1;
  Visit(E, Pred, S1);

  QualType T = CastE->getType();
  
  // Check for redundant casts or casting to "void"
  if (T->isVoidType() ||
      E->getType() == T || 
      (T->isPointerType() && E->getType()->isFunctionType())) {
    
    for (NodeSet::iterator I1=S1.begin(), E1=S1.end(); I1 != E1; ++I1)
      Dst.Add(*I1);

    return;
  }
  
  for (NodeSet::iterator I1=S1.begin(), E1=S1.end(); I1 != E1; ++I1) {
    NodeTy* N = *I1;
    StateTy St = N->getState();
    const RValue& V = GetValue(St, E);
    Nodify(Dst, CastE, N, SetValue(St, CastE, EvalCast(ValMgr, V, CastE)));
  }
}

void GRExprEngine::VisitDeclStmt(DeclStmt* DS, GRExprEngine::NodeTy* Pred,
                                GRExprEngine::NodeSet& Dst) {
  
  StateTy St = Pred->getState();
  
  for (const ScopedDecl* D = DS->getDecl(); D; D = D->getNextDeclarator())
    if (const VarDecl* VD = dyn_cast<VarDecl>(D)) {
      
      // FIXME: Add support for local arrays.
      if (VD->getType()->isArrayType())
        continue;
      
      const Expr* E = VD->getInit();      
      St = SetValue(St, lval::DeclVal(VD),
                    E ? GetValue(St, E) : UninitializedVal());
    }

  Nodify(Dst, DS, Pred, St);
  
  if (Dst.empty())
    Dst.Add(Pred);  
}


void GRExprEngine::VisitGuardedExpr(Expr* S, Expr* LHS, Expr* RHS,
                                   NodeTy* Pred, NodeSet& Dst) {
  
  StateTy St = Pred->getState();
  
  RValue R = GetValue(St, LHS);
  if (isa<UnknownVal>(R)) R = GetValue(St, RHS);
  
  Nodify(Dst, S, Pred, SetValue(St, S, R));
}

/// VisitSizeOfAlignOfTypeExpr - Transfer function for sizeof(type).
void GRExprEngine::VisitSizeOfAlignOfTypeExpr(SizeOfAlignOfTypeExpr* S,
                                             NodeTy* Pred,
                                             NodeSet& Dst) {
  
  // 6.5.3.4 sizeof: "The result type is an integer."
  
  QualType T = S->getArgumentType();
  
  // FIXME: Add support for VLAs.
  if (!T.getTypePtr()->isConstantSizeType())
    return;
  
  SourceLocation L = S->getExprLoc();
  uint64_t size = getContext().getTypeSize(T, L) / 8;
  
  Nodify(Dst, S, Pred,
         SetValue(Pred->getState(), S,
                  NonLValue::GetValue(ValMgr, size, S->getType(), L)));
  
}

void GRExprEngine::VisitUnaryOperator(UnaryOperator* U,
                                     GRExprEngine::NodeTy* Pred,
                                     GRExprEngine::NodeSet& Dst) {
  
  NodeSet S1;
  UnaryOperator::Opcode Op = U->getOpcode();
  
  // FIXME: This is a hack so that for '*' and '&' we don't recurse
  //  on visiting the subexpression if it is a DeclRefExpr.  We should
  //  probably just handle AddrOf and Deref in their own methods to make
  //  this cleaner.
  if ((Op == UnaryOperator::Deref || Op == UnaryOperator::AddrOf) &&
      isa<DeclRefExpr>(U->getSubExpr()))
    S1.Add(Pred);
  else
    Visit(U->getSubExpr(), Pred, S1);
    
  for (NodeSet::iterator I1=S1.begin(), E1=S1.end(); I1 != E1; ++I1) {
    NodeTy* N1 = *I1;
    StateTy St = N1->getState();
    
    // Handle ++ and -- (both pre- and post-increment).
    
    if (U->isIncrementDecrementOp()) {
      const LValue& L1 = GetLValue(St, U->getSubExpr());
      RValue R1 = GetValue(St, L1);
      
      BinaryOperator::Opcode Op = U->isIncrementOp() ? BinaryOperator::Add
                                                     : BinaryOperator::Sub;
      
      RValue Result = EvalBinaryOp(Op, R1, GetRValueConstant(1U, U));
      
      if (U->isPostfix())
        Nodify(Dst, U, N1, SetValue(SetValue(St, U, R1), L1, Result));
      else
        Nodify(Dst, U, N1, SetValue(SetValue(St, U, Result), L1, Result));
        
      continue;
    }    
    
    // Handle all other unary operators.
    
    switch (U->getOpcode()) {

      case UnaryOperator::Minus: {
        const NonLValue& R1 = cast<NonLValue>(GetValue(St, U->getSubExpr()));
        Nodify(Dst, U, N1, SetValue(St, U, EvalMinus(ValMgr, U, R1)));
        break;
      }
        
      case UnaryOperator::Not: {
        const NonLValue& R1 = cast<NonLValue>(GetValue(St, U->getSubExpr()));
        Nodify(Dst, U, N1, SetValue(St, U, EvalComplement(ValMgr, R1)));
        break;
      }
        
      case UnaryOperator::LNot: {
        // C99 6.5.3.3: "The expression !E is equivalent to (0==E)."
        //
        //  Note: technically we do "E == 0", but this is the same in the
        //    transfer functions as "0 == E".
        
        RValue V1 = GetValue(St, U->getSubExpr());
        
        if (isa<LValue>(V1)) {
          const LValue& L1 = cast<LValue>(V1);
          lval::ConcreteInt V2(ValMgr.getZeroWithPtrWidth());
          Nodify(Dst, U, N1,
                 SetValue(St, U, EvalBinaryOp(BinaryOperator::EQ,
                                              L1, V2)));
        }
        else {
          const NonLValue& R1 = cast<NonLValue>(V1);
          nonlval::ConcreteInt V2(ValMgr.getZeroWithPtrWidth());
          Nodify(Dst, U, N1,
                 SetValue(St, U, EvalBinaryOp(BinaryOperator::EQ,
                                              R1, V2)));
        }
        
        break;
      }
      
      case UnaryOperator::SizeOf: {
        // 6.5.3.4 sizeof: "The result type is an integer."
        
        QualType T = U->getSubExpr()->getType();
        
        // FIXME: Add support for VLAs.
        if (!T.getTypePtr()->isConstantSizeType())
          return;
        
        SourceLocation L = U->getExprLoc();
        uint64_t size = getContext().getTypeSize(T, L) / 8;
                
        Nodify(Dst, U, N1,
               SetValue(St, U, NonLValue::GetValue(ValMgr, size,
                                                   U->getType(), L)));
        
        break;
      }
        
      case UnaryOperator::AddrOf: {
        const LValue& L1 = GetLValue(St, U->getSubExpr());
        Nodify(Dst, U, N1, SetValue(St, U, L1));
        break;
      }
        
      case UnaryOperator::Deref: {
        // FIXME: Stop when dereferencing an uninitialized value.
        // FIXME: Bifurcate when dereferencing a symbolic with no constraints?
        
        const RValue& V = GetValue(St, U->getSubExpr());        
        const LValue& L1 = cast<LValue>(V);
        
        // After a dereference, one of two possible situations arise:
        //  (1) A crash, because the pointer was NULL.
        //  (2) The pointer is not NULL, and the dereference works.
        // 
        // We add these assumptions.
                
        bool isFeasibleNotNull;
       
        // "Assume" that the pointer is Not-NULL.
        StateTy StNotNull = Assume(St, L1, true, isFeasibleNotNull);
        
        if (isFeasibleNotNull) {
          QualType T = U->getType();
          Nodify(Dst, U, N1, SetValue(StNotNull, U,
                                      GetValue(StNotNull, L1, &T)));
        }
        
        if (V.isUnknown())
          return;
        
        bool isFeasibleNull;
        
        // "Assume" that the pointer is NULL.
        StateTy StNull = Assume(St, L1, false, isFeasibleNull);
        
        if (isFeasibleNull) {
          // We don't use "Nodify" here because the node will be a sink
          // and we have no intention of processing it later.
          NodeTy* NullNode = Builder->generateNode(U, StNull, N1);

          if (NullNode) {
            NullNode->markAsSink();
            
            if (isFeasibleNotNull)
              ImplicitNullDeref.insert(NullNode);
            else
              ExplicitNullDeref.insert(NullNode);
          }
        }
        
        break;
      }
        
      default: ;
        assert (false && "Not implemented.");
    }    
  }
}

void GRExprEngine::VisitAssignmentLHS(Expr* E, GRExprEngine::NodeTy* Pred,
                                     GRExprEngine::NodeSet& Dst) {

  if (isa<DeclRefExpr>(E)) {
    Dst.Add(Pred);
    return;
  }
  
  if (UnaryOperator* U = dyn_cast<UnaryOperator>(E)) {
    if (U->getOpcode() == UnaryOperator::Deref) {
      Visit(U->getSubExpr(), Pred, Dst);
      return;
    }
  }
  
  Visit(E, Pred, Dst);
}

void GRExprEngine::VisitBinaryOperator(BinaryOperator* B,
                                       GRExprEngine::NodeTy* Pred,
                                       GRExprEngine::NodeSet& Dst) {
  NodeSet S1;
  
  if (B->isAssignmentOp())
    VisitAssignmentLHS(B->getLHS(), Pred, S1);
  else
    Visit(B->getLHS(), Pred, S1);

  for (NodeSet::iterator I1=S1.begin(), E1=S1.end(); I1 != E1; ++I1) {
    NodeTy* N1 = *I1;
    
    // When getting the value for the LHS, check if we are in an assignment.
    // In such cases, we want to (initially) treat the LHS as an LValue,
    // so we use GetLValue instead of GetValue so that DeclRefExpr's are
    // evaluated to LValueDecl's instead of to an NonLValue.
    const RValue& V1 = 
      B->isAssignmentOp() ? GetLValue(N1->getState(), B->getLHS())
                          : GetValue(N1->getState(), B->getLHS());
    
    NodeSet S2;
    Visit(B->getRHS(), N1, S2);
  
    for (NodeSet::iterator I2=S2.begin(), E2=S2.end(); I2 != E2; ++I2) {

      NodeTy* N2 = *I2;
      StateTy St = N2->getState();
      const RValue& V2 = GetValue(St, B->getRHS());

      BinaryOperator::Opcode Op = B->getOpcode();
      
      if (Op <= BinaryOperator::Or) {
        
        if (isa<UnknownVal>(V1) || isa<UninitializedVal>(V1)) {
          Nodify(Dst, B, N2, SetValue(St, B, V1));
          continue;
        }
        
        Nodify(Dst, B, N2, SetValue(St, B, EvalBinaryOp(Op, V1, V2)));
        continue;
      
      }
      
      switch (Op) {
        case BinaryOperator::Assign: {
          const LValue& L1 = cast<LValue>(V1);

          if (isa<UninitializedVal>(L1))
            HandleUninitializedStore(B, N2);
          else          
            Nodify(Dst, B, N2, SetValue(SetValue(St, B, V2), L1, V2));

          break;
        }

        default: { // Compound assignment operators.
          
          assert (B->isCompoundAssignmentOp());
                          
          const LValue& L1 = cast<LValue>(V1);
          
          if (isa<UninitializedVal>(L1)) {
            HandleUninitializedStore(B, N2);
            break;
          }
          
          RValue Result = cast<NonLValue>(UnknownVal());
          
          if (Op >= BinaryOperator::AndAssign)
            ((int&) Op) -= (BinaryOperator::AndAssign - BinaryOperator::And);
          else
            ((int&) Op) -= BinaryOperator::MulAssign;          
          
          if (B->getType()->isPointerType()) { // Perform pointer arithmetic.
            const NonLValue& R2 = cast<NonLValue>(V2);
            Result = EvalBinaryOp(Op, L1, R2);
          }
          else if (isa<LValue>(V2)) {
            const LValue& L2 = cast<LValue>(V2);
            
            if (B->getRHS()->getType()->isPointerType()) {
              // LValue comparison.
              Result = EvalBinaryOp(Op, L1, L2);
            }
            else {
              QualType T1 = B->getLHS()->getType();
              QualType T2 = B->getRHS()->getType();
              
              // An operation between two variables of a non-lvalue type.
              Result =
                EvalBinaryOp(Op,
                            cast<NonLValue>(GetValue(N1->getState(), L1, &T1)),
                            cast<NonLValue>(GetValue(N2->getState(), L2, &T2)));
            }
          }
          else { // Any other operation between two Non-LValues.
            QualType T = B->getLHS()->getType();
            const NonLValue& R1 = cast<NonLValue>(GetValue(N1->getState(),
                                                           L1, &T));
            const NonLValue& R2 = cast<NonLValue>(V2);
            Result = EvalBinaryOp(Op, R1, R2);
          }
          
          Nodify(Dst, B, N2, SetValue(SetValue(St, B, Result), L1, Result));
          break;
        }
      }
    }
  }
}

void GRExprEngine::HandleUninitializedStore(Stmt* S, NodeTy* Pred) {
  
  NodeTy* N = Builder->generateNode(S, Pred->getState(), Pred);
  N->markAsSink();
  UninitStores.insert(N);
}

void GRExprEngine::Visit(Stmt* S, NodeTy* Pred, NodeSet& Dst) {

  // FIXME: add metadata to the CFG so that we can disable
  //  this check when we KNOW that there is no block-level subexpression.
  //  The motivation is that this check requires a hashtable lookup.

  if (S != CurrentStmt && getCFG().isBlkExpr(S)) {
    Dst.Add(Pred);
    return;
  }

  switch (S->getStmtClass()) {
      
    default:
      // Cases we intentionally have "default" handle:
      //   AddrLabelExpr
      
      Dst.Add(Pred); // No-op. Simply propagate the current state unchanged.
      break;
                                                       
    case Stmt::BinaryOperatorClass: {
      BinaryOperator* B = cast<BinaryOperator>(S);
 
      if (B->isLogicalOp()) {
        VisitLogicalExpr(B, Pred, Dst);
        break;
      }
      else if (B->getOpcode() == BinaryOperator::Comma) {
        StateTy St = Pred->getState();
        Nodify(Dst, B, Pred, SetValue(St, B, GetValue(St, B->getRHS())));
        break;
      }
      
      VisitBinaryOperator(cast<BinaryOperator>(S), Pred, Dst);
      break;
    }
      
    case Stmt::CallExprClass: {
      CallExpr* C = cast<CallExpr>(S);
      VisitCall(C, Pred, C->arg_begin(), C->arg_end(), Dst);
      break;      
    }

    case Stmt::CastExprClass: {
      CastExpr* C = cast<CastExpr>(S);
      VisitCast(C, C->getSubExpr(), Pred, Dst);
      break;
    }
      
      // While explicitly creating a node+state for visiting a CharacterLiteral
      // seems wasteful, it also solves a bunch of problems when handling
      // the ?, &&, and ||.
      
    case Stmt::CharacterLiteralClass: {
      CharacterLiteral* C = cast<CharacterLiteral>(S);
      StateTy St = Pred->getState();
      NonLValue X = NonLValue::GetValue(ValMgr, C->getValue(), C->getType(),
                                        C->getLoc());
      Nodify(Dst, C, Pred, SetValue(St, C, X));
      break;      
    }
      
    case Stmt::ChooseExprClass: { // __builtin_choose_expr
      ChooseExpr* C = cast<ChooseExpr>(S);
      VisitGuardedExpr(C, C->getLHS(), C->getRHS(), Pred, Dst);
      break;
    }
      
    case Stmt::CompoundAssignOperatorClass:
      VisitBinaryOperator(cast<BinaryOperator>(S), Pred, Dst);
      break;
      
    case Stmt::ConditionalOperatorClass: { // '?' operator
      ConditionalOperator* C = cast<ConditionalOperator>(S);
      VisitGuardedExpr(C, C->getLHS(), C->getRHS(), Pred, Dst);
      break;
    }
      
    case Stmt::DeclRefExprClass:
      VisitDeclRefExpr(cast<DeclRefExpr>(S), Pred, Dst);
      break;
      
    case Stmt::DeclStmtClass:
      VisitDeclStmt(cast<DeclStmt>(S), Pred, Dst);
      break;
      
      // While explicitly creating a node+state for visiting an IntegerLiteral
      // seems wasteful, it also solves a bunch of problems when handling
      // the ?, &&, and ||.
      
    case Stmt::IntegerLiteralClass: {      
      StateTy St = Pred->getState();
      IntegerLiteral* I = cast<IntegerLiteral>(S);
      NonLValue X = NonLValue::GetValue(ValMgr, I);
      Nodify(Dst, I, Pred, SetValue(St, I, X));
      break;      
    }
      
    case Stmt::ImplicitCastExprClass: {
      ImplicitCastExpr* C = cast<ImplicitCastExpr>(S);
      VisitCast(C, C->getSubExpr(), Pred, Dst);
      break;
    }

    case Stmt::ParenExprClass:
      Visit(cast<ParenExpr>(S)->getSubExpr(), Pred, Dst);
      break;
      
    case Stmt::SizeOfAlignOfTypeExprClass:
      VisitSizeOfAlignOfTypeExpr(cast<SizeOfAlignOfTypeExpr>(S), Pred, Dst);
      break;
      
    case Stmt::StmtExprClass: {
      StmtExpr* SE = cast<StmtExpr>(S);
      
      StateTy St = Pred->getState();
      Expr* LastExpr = cast<Expr>(*SE->getSubStmt()->body_rbegin());
      Nodify(Dst, SE, Pred, SetValue(St, SE, GetValue(St, LastExpr)));
      break;      
    }
      
    case Stmt::ReturnStmtClass: {
      if (Expr* R = cast<ReturnStmt>(S)->getRetValue())
        Visit(R, Pred, Dst);
      else
        Dst.Add(Pred);
      
      break;
    }
      
    case Stmt::UnaryOperatorClass:
      VisitUnaryOperator(cast<UnaryOperator>(S), Pred, Dst);
      break;
  }
}

//===----------------------------------------------------------------------===//
// "Assume" logic.
//===----------------------------------------------------------------------===//

GRExprEngine::StateTy GRExprEngine::Assume(StateTy St, LValue Cond,
                                           bool Assumption, 
                                           bool& isFeasible) {    
  
  assert (!isa<UninitializedVal>(Cond));

  if (isa<UnknownVal>(Cond)) {
    isFeasible = true;
    return St;  
  }
  
  switch (Cond.getSubKind()) {
    default:
      assert (false && "'Assume' not implemented for this LValue.");
      return St;
      
    case lval::SymbolValKind:
      if (Assumption)
        return AssumeSymNE(St, cast<lval::SymbolVal>(Cond).getSymbol(),
                           ValMgr.getZeroWithPtrWidth(), isFeasible);
      else
        return AssumeSymEQ(St, cast<lval::SymbolVal>(Cond).getSymbol(),
                           ValMgr.getZeroWithPtrWidth(), isFeasible);
      
      
    case lval::DeclValKind:
      isFeasible = Assumption;
      return St;

    case lval::ConcreteIntKind: {
      bool b = cast<lval::ConcreteInt>(Cond).getValue() != 0;
      isFeasible = b ? Assumption : !Assumption;      
      return St;
    }
  }
}

GRExprEngine::StateTy GRExprEngine::Assume(StateTy St, NonLValue Cond,
                                         bool Assumption, 
                                         bool& isFeasible) {
  
  assert (!isa<UninitializedVal>(Cond));
  
  if (isa<UnknownVal>(Cond)) {
    isFeasible = true;
    return St;  
  }
  
  switch (Cond.getSubKind()) {
    default:
      assert (false && "'Assume' not implemented for this NonLValue.");
      return St;
      
      
    case nonlval::SymbolValKind: {
      nonlval::SymbolVal& SV = cast<nonlval::SymbolVal>(Cond);
      SymbolID sym = SV.getSymbol();
      
      if (Assumption)
        return AssumeSymNE(St, sym, ValMgr.getValue(0, SymMgr.getType(sym)),
                           isFeasible);
      else
        return AssumeSymEQ(St, sym, ValMgr.getValue(0, SymMgr.getType(sym)),
                           isFeasible);
    }
      
    case nonlval::SymIntConstraintValKind:
      return
        AssumeSymInt(St, Assumption,
                     cast<nonlval::SymIntConstraintVal>(Cond).getConstraint(),
                     isFeasible);
      
    case nonlval::ConcreteIntKind: {
      bool b = cast<nonlval::ConcreteInt>(Cond).getValue() != 0;
      isFeasible = b ? Assumption : !Assumption;      
      return St;
    }
  }
}

GRExprEngine::StateTy
GRExprEngine::AssumeSymNE(StateTy St, SymbolID sym,
                         const llvm::APSInt& V, bool& isFeasible) {
  
  // First, determine if sym == X, where X != V.
  if (const llvm::APSInt* X = St.getSymVal(sym)) {
    isFeasible = *X != V;
    return St;
  }
  
  // Second, determine if sym != V.
  if (St.isNotEqual(sym, V)) {
    isFeasible = true;
    return St;
  }
      
  // If we reach here, sym is not a constant and we don't know if it is != V.
  // Make that assumption.
  
  isFeasible = true;
  return StateMgr.AddNE(St, sym, V);
}

GRExprEngine::StateTy
GRExprEngine::AssumeSymEQ(StateTy St, SymbolID sym,
                         const llvm::APSInt& V, bool& isFeasible) {
  
  // First, determine if sym == X, where X != V.
  if (const llvm::APSInt* X = St.getSymVal(sym)) {
    isFeasible = *X == V;
    return St;
  }
  
  // Second, determine if sym != V.
  if (St.isNotEqual(sym, V)) {
    isFeasible = false;
    return St;
  }
  
  // If we reach here, sym is not a constant and we don't know if it is == V.
  // Make that assumption.
  
  isFeasible = true;
  return StateMgr.AddEQ(St, sym, V);
}

GRExprEngine::StateTy
GRExprEngine::AssumeSymInt(StateTy St, bool Assumption,
                          const SymIntConstraint& C, bool& isFeasible) {
  
  switch (C.getOpcode()) {
    default:
      // No logic yet for other operators.
      return St;
      
    case BinaryOperator::EQ:
      if (Assumption)
        return AssumeSymEQ(St, C.getSymbol(), C.getInt(), isFeasible);
      else
        return AssumeSymNE(St, C.getSymbol(), C.getInt(), isFeasible);
      
    case BinaryOperator::NE:
      if (Assumption)
        return AssumeSymNE(St, C.getSymbol(), C.getInt(), isFeasible);
      else
        return AssumeSymEQ(St, C.getSymbol(), C.getInt(), isFeasible);
  }
}

//===----------------------------------------------------------------------===//
// Visualization.
//===----------------------------------------------------------------------===//

#ifndef NDEBUG
static GRExprEngine* GraphPrintCheckerState;

namespace llvm {
template<>
struct VISIBILITY_HIDDEN DOTGraphTraits<GRExprEngine::NodeTy*> :
  public DefaultDOTGraphTraits {
    
  static void PrintVarBindings(std::ostream& Out, GRExprEngine::StateTy St) {

    Out << "Variables:\\l";
    
    bool isFirst = true;
    
    for (GRExprEngine::StateTy::vb_iterator I=St.vb_begin(),
                                           E=St.vb_end(); I!=E;++I) {        

      if (isFirst)
        isFirst = false;
      else
        Out << "\\l";
      
      Out << ' ' << I.getKey()->getName() << " : ";
      I.getData().print(Out);
    }
    
  }
    
    
  static void PrintSubExprBindings(std::ostream& Out, GRExprEngine::StateTy St){
    
    bool isFirst = true;
    
    for (GRExprEngine::StateTy::seb_iterator I=St.seb_begin(), E=St.seb_end();
                                            I != E;++I) {        
      
      if (isFirst) {
        Out << "\\l\\lSub-Expressions:\\l";
        isFirst = false;
      }
      else
        Out << "\\l";
      
      Out << " (" << (void*) I.getKey() << ") ";
      I.getKey()->printPretty(Out);
      Out << " : ";
      I.getData().print(Out);
    }
  }
    
  static void PrintBlkExprBindings(std::ostream& Out, GRExprEngine::StateTy St){
        
    bool isFirst = true;

    for (GRExprEngine::StateTy::beb_iterator I=St.beb_begin(), E=St.beb_end();
                                            I != E; ++I) {      
      if (isFirst) {
        Out << "\\l\\lBlock-level Expressions:\\l";
        isFirst = false;
      }
      else
        Out << "\\l";

      Out << " (" << (void*) I.getKey() << ") ";
      I.getKey()->printPretty(Out);
      Out << " : ";
      I.getData().print(Out);
    }
  }
    
  static void PrintEQ(std::ostream& Out, GRExprEngine::StateTy St) {
    ValueState::ConstantEqTy CE = St.getImpl()->ConstantEq;
    
    if (CE.isEmpty())
      return;
    
    Out << "\\l\\|'==' constraints:";

    for (ValueState::ConstantEqTy::iterator I=CE.begin(), E=CE.end(); I!=E;++I)
      Out << "\\l $" << I.getKey() << " : " << I.getData()->toString();
  }
    
  static void PrintNE(std::ostream& Out, GRExprEngine::StateTy St) {
    ValueState::ConstantNotEqTy NE = St.getImpl()->ConstantNotEq;
    
    if (NE.isEmpty())
      return;
    
    Out << "\\l\\|'!=' constraints:";
    
    for (ValueState::ConstantNotEqTy::iterator I=NE.begin(), EI=NE.end();
         I != EI; ++I){
      
      Out << "\\l $" << I.getKey() << " : ";
      bool isFirst = true;
      
      ValueState::IntSetTy::iterator J=I.getData().begin(),
                                    EJ=I.getData().end();      
      for ( ; J != EJ; ++J) {        
        if (isFirst) isFirst = false;
        else Out << ", ";
        
        Out << (*J)->toString();
      }    
    }
  }
    
  static std::string getNodeAttributes(const GRExprEngine::NodeTy* N, void*) {
    
    if (GraphPrintCheckerState->isImplicitNullDeref(N) ||
        GraphPrintCheckerState->isExplicitNullDeref(N) ||
        GraphPrintCheckerState->isUninitStore(N) ||
        GraphPrintCheckerState->isUninitControlFlow(N))
      return "color=\"red\",style=\"filled\"";
    
    return "";
  }
    
  static std::string getNodeLabel(const GRExprEngine::NodeTy* N, void*) {
    std::ostringstream Out;

    // Program Location.
    ProgramPoint Loc = N->getLocation();
    
    switch (Loc.getKind()) {
      case ProgramPoint::BlockEntranceKind:
        Out << "Block Entrance: B" 
            << cast<BlockEntrance>(Loc).getBlock()->getBlockID();
        break;
      
      case ProgramPoint::BlockExitKind:
        assert (false);
        break;
        
      case ProgramPoint::PostStmtKind: {
        const PostStmt& L = cast<PostStmt>(Loc);
        Out << L.getStmt()->getStmtClassName() << ':' 
            << (void*) L.getStmt() << ' ';
        
        L.getStmt()->printPretty(Out);
        
        if (GraphPrintCheckerState->isImplicitNullDeref(N)) {
          Out << "\\|Implicit-Null Dereference.\\l";
        }
        else if (GraphPrintCheckerState->isExplicitNullDeref(N)) {
          Out << "\\|Explicit-Null Dereference.\\l";
        }
        else if (GraphPrintCheckerState->isUninitStore(N)) {
          Out << "\\|Store to Uninitialized LValue.";
        }
        
        break;
      }
    
      default: {
        const BlockEdge& E = cast<BlockEdge>(Loc);
        Out << "Edge: (B" << E.getSrc()->getBlockID() << ", B"
            << E.getDst()->getBlockID()  << ')';
        
        if (Stmt* T = E.getSrc()->getTerminator()) {
          Out << "\\|Terminator: ";
          E.getSrc()->printTerminator(Out);
          
          if (isa<SwitchStmt>(T)) {
            Stmt* Label = E.getDst()->getLabel();
            
            if (Label) {                        
              if (CaseStmt* C = dyn_cast<CaseStmt>(Label)) {
                Out << "\\lcase ";
                C->getLHS()->printPretty(Out);
                
                if (Stmt* RHS = C->getRHS()) {
                  Out << " .. ";
                  RHS->printPretty(Out);
                }
                
                Out << ":";
              }
              else {
                assert (isa<DefaultStmt>(Label));
                Out << "\\ldefault:";
              }
            }
            else 
              Out << "\\l(implicit) default:";
          }
          else if (isa<IndirectGotoStmt>(T)) {
            // FIXME
          }
          else {
            Out << "\\lCondition: ";
            if (*E.getSrc()->succ_begin() == E.getDst())
              Out << "true";
            else
              Out << "false";                        
          }
          
          Out << "\\l";
        }
        
        if (GraphPrintCheckerState->isUninitControlFlow(N)) {
          Out << "\\|Control-flow based on\\lUninitialized value.\\l";
        }
      }
    }
    
    Out << "\\|StateID: " << (void*) N->getState().getImpl() << "\\|";

    N->getState().printDOT(Out);
      
    Out << "\\l";
    return Out.str();
  }
};
} // end llvm namespace    
#endif

void GRExprEngine::ViewGraph() {
#ifndef NDEBUG
  GraphPrintCheckerState = this;
  llvm::ViewGraph(*G.roots_begin(), "GRExprEngine");
  GraphPrintCheckerState = NULL;
#endif
}
