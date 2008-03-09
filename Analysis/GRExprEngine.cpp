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
#include "clang/Basic/SourceManager.h"
#include "llvm/Support/Streams.h"

#ifndef NDEBUG
#include "llvm/Support/GraphWriter.h"
#include <sstream>
#endif

// SaveAndRestore - A utility class that uses RIIA to save and restore
//  the value of a variable.
template<typename T>
struct VISIBILITY_HIDDEN SaveAndRestore {
  SaveAndRestore(T& x) : X(x), old_value(x) {}
  ~SaveAndRestore() { X = old_value; }
  T get() { return old_value; }
  
  T& X;
  T old_value;
};

using namespace clang;
using llvm::dyn_cast;
using llvm::cast;
using llvm::APSInt;

ValueState* GRExprEngine::RemoveDeadBindings(ValueState* St) {
  
  if (StateCleaned || !CurrentStmt)
    return St;
  
  StateCleaned = true;
  
  return StateMgr.RemoveDeadBindings(St, CurrentStmt, Liveness);
}


ValueState* GRExprEngine::getInitialState() {

  // The LiveVariables information already has a compilation of all VarDecls
  // used in the function.  Iterate through this set, and "symbolicate"
  // any VarDecl whose value originally comes from outside the function.
  
  typedef LiveVariables::AnalysisDataTy LVDataTy;
  LVDataTy& D = Liveness.getAnalysisData();
  
  ValueState StateImpl = *StateMgr.getInitialState();
  
  for (LVDataTy::decl_iterator I=D.begin_decl(), E=D.end_decl(); I != E; ++I) {
    
    VarDecl* VD = cast<VarDecl>(const_cast<ScopedDecl*>(I->first));
    
    if (VD->hasGlobalStorage() || isa<ParmVarDecl>(VD)) {
      RVal X = RVal::GetSymbolValue(SymMgr, VD);
      StateMgr.BindVar(StateImpl, VD, X);
    }
  }
  
  return StateMgr.getPersistentState(StateImpl);
}      
      
ValueState* GRExprEngine::SetRVal(ValueState* St, Expr* Ex, RVal V) {

  St = RemoveDeadBindings(St);

  bool isBlkExpr = false;
    
  if (Ex == CurrentStmt) {
    isBlkExpr = getCFG().isBlkExpr(Ex);
    
    if (!isBlkExpr)
      return St;
  }

  return StateMgr.SetRVal(St, Ex, V, isBlkExpr, false);
}

ValueState* GRExprEngine::SetRVal(ValueState* St, LVal LV, RVal RV) {
  St = RemoveDeadBindings(St);  
  return StateMgr.SetRVal(St, LV, RV);
}

ValueState* GRExprEngine::SetBlkExprRVal(ValueState* St, Expr* Ex, RVal V) {
  St = RemoveDeadBindings(St);  
  return StateMgr.SetRVal(St, Ex, V, true, false);
}

ValueState* GRExprEngine::MarkBranch(ValueState* St, Stmt* Terminator,
                                     bool branchTaken) {
  
  switch (Terminator->getStmtClass()) {
    default:
      return St;
      
    case Stmt::BinaryOperatorClass: { // '&&' and '||'
      
      BinaryOperator* B = cast<BinaryOperator>(Terminator);
      BinaryOperator::Opcode Op = B->getOpcode();
      
      assert (Op == BinaryOperator::LAnd || Op == BinaryOperator::LOr);
      
      // For &&, if we take the true branch, then the value of the whole
      // expression is that of the RHS expression.
      //
      // For ||, if we take the false branch, then the value of the whole
      // expression is that of the RHS expression.
      
      Expr* Ex = (Op == BinaryOperator::LAnd && branchTaken) ||
                 (Op == BinaryOperator::LOr && !branchTaken)  
               ? B->getRHS() : B->getLHS();
        
      return SetBlkExprRVal(St, B, UndefinedVal(Ex));
    }
      
    case Stmt::ConditionalOperatorClass: { // ?:
      
      ConditionalOperator* C = cast<ConditionalOperator>(Terminator);
      
      // For ?, if branchTaken == true then the value is either the LHS or
      // the condition itself. (GNU extension).
      
      Expr* Ex;      
      
      if (branchTaken)
        Ex = C->getLHS() ? C->getLHS() : C->getCond();        
      else
        Ex = C->getRHS();
      
      return SetBlkExprRVal(St, C, UndefinedVal(Ex));
    }
      
    case Stmt::ChooseExprClass: { // ?:
      
      ChooseExpr* C = cast<ChooseExpr>(Terminator);
      
      Expr* Ex = branchTaken ? C->getLHS() : C->getRHS();      
      return SetBlkExprRVal(St, C, UndefinedVal(Ex));
    }
  }
}

bool GRExprEngine::ProcessBlockEntrance(CFGBlock* B, ValueState*,
                                        GRBlockCounter BC) {
  
  return BC.getNumVisited(B->getBlockID()) < 3;
}

void GRExprEngine::ProcessBranch(Expr* Condition, Stmt* Term,
                                 BranchNodeBuilder& builder) {

  // Remove old bindings for subexpressions.
  ValueState* PrevState = StateMgr.RemoveSubExprBindings(builder.getState());
  
  // Check for NULL conditions; e.g. "for(;;)"
  if (!Condition) { 
    builder.markInfeasible(false);
    return;
  }
  
  RVal V = GetRVal(PrevState, Condition);
  
  switch (V.getBaseKind()) {
    default:
      break;

    case RVal::UnknownKind:
      builder.generateNode(MarkBranch(PrevState, Term, true), true);
      builder.generateNode(MarkBranch(PrevState, Term, false), false);
      return;
      
    case RVal::UndefinedKind: {      
      NodeTy* N = builder.generateNode(PrevState, true);

      if (N) {
        N->markAsSink();
        UndefBranches.insert(N);
      }
      
      builder.markInfeasible(false);
      return;
    }      
  }
    

  // Process the true branch.

  bool isFeasible = true;  
  ValueState* St = Assume(PrevState, V, true, isFeasible);

  if (isFeasible)
    builder.generateNode(MarkBranch(St, Term, true), true);
  else
    builder.markInfeasible(true);
      
  // Process the false branch.  
  
  isFeasible = false;
  St = Assume(PrevState, V, false, isFeasible);
  
  if (isFeasible)
    builder.generateNode(MarkBranch(St, Term, false), false);
  else
    builder.markInfeasible(false);
}

/// ProcessIndirectGoto - Called by GRCoreEngine.  Used to generate successor
///  nodes by processing the 'effects' of a computed goto jump.
void GRExprEngine::ProcessIndirectGoto(IndirectGotoNodeBuilder& builder) {

  ValueState* St = builder.getState();  
  RVal V = GetRVal(St, builder.getTarget());
  
  // Three possibilities:
  //
  //   (1) We know the computed label.
  //   (2) The label is NULL (or some other constant), or Undefined.
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

  if (isa<lval::ConcreteInt>(V) || isa<UndefinedVal>(V)) {
    // Dispatch to the first target and mark it as a sink.
    NodeTy* N = builder.generateNode(builder.begin(), St, true);
    UndefBranches.insert(N);
    return;
  }
  
  // This is really a catch-all.  We don't support symbolics yet.
  
  assert (V.isUnknown());
  
  for (iterator I=builder.begin(), E=builder.end(); I != E; ++I)
    builder.generateNode(I, St);
}

/// ProcessSwitch - Called by GRCoreEngine.  Used to generate successor
///  nodes by processing the 'effects' of a switch statement.
void GRExprEngine::ProcessSwitch(SwitchNodeBuilder& builder) {
  
  typedef SwitchNodeBuilder::iterator iterator;
  
  ValueState* St = builder.getState();  
  Expr* CondE = builder.getCondition();
  RVal  CondV = GetRVal(St, CondE);

  if (CondV.isUndef()) {
    NodeTy* N = builder.generateDefaultCaseNode(St, true);
    UndefBranches.insert(N);
    return;
  }
  
  ValueState*  DefaultSt = St;
  
  // While most of this can be assumed (such as the signedness), having it
  // just computed makes sure everything makes the same assumptions end-to-end.
  
  unsigned bits = getContext().getTypeSize(CondE->getType());

  APSInt V1(bits, false);
  APSInt V2 = V1;
  
  for (iterator I = builder.begin(), EI = builder.end(); I != EI; ++I) {

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
    //  This should be easy once we have "ranges" for NonLVals.
        
    do {      
      nonlval::ConcreteInt CaseVal(BasicVals.getValue(V1));
      
      RVal Res = EvalBinOp(BinaryOperator::EQ, CondV, CaseVal);
      
      // Now "assume" that the case matches.
      bool isFeasible = false;
      
      ValueState* StNew = Assume(St, Res, true, isFeasible);
      
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
  
  assert (B->getOpcode() == BinaryOperator::LAnd ||
          B->getOpcode() == BinaryOperator::LOr);
  
  assert (B == CurrentStmt && getCFG().isBlkExpr(B));
  
  ValueState* St = Pred->getState();
  RVal X = GetBlkExprRVal(St, B);
  
  assert (X.isUndef());
  
  Expr* Ex = (Expr*) cast<UndefinedVal>(X).getData();
  
  assert (Ex);
  
  if (Ex == B->getRHS()) {
    
    X = GetBlkExprRVal(St, Ex);
    
    // Handle undefined values.
    
    if (X.isUndef()) {
      Nodify(Dst, B, Pred, SetBlkExprRVal(St, B, X));
      return;
    }
    
    // We took the RHS.  Because the value of the '&&' or '||' expression must
    // evaluate to 0 or 1, we must assume the value of the RHS evaluates to 0
    // or 1.  Alternatively, we could take a lazy approach, and calculate this
    // value later when necessary.  We don't have the machinery in place for
    // this right now, and since most logical expressions are used for branches,
    // the payoff is not likely to be large.  Instead, we do eager evaluation.
        
    bool isFeasible = false;
    ValueState* NewState = Assume(St, X, true, isFeasible);
    
    if (isFeasible)
      Nodify(Dst, B, Pred, SetBlkExprRVal(NewState, B, MakeConstantVal(1U, B)));
      
    isFeasible = false;
    NewState = Assume(St, X, false, isFeasible);
    
    if (isFeasible)
      Nodify(Dst, B, Pred, SetBlkExprRVal(NewState, B, MakeConstantVal(0U, B)));
  }
  else {
    // We took the LHS expression.  Depending on whether we are '&&' or
    // '||' we know what the value of the expression is via properties of
    // the short-circuiting.
    
    X = MakeConstantVal( B->getOpcode() == BinaryOperator::LAnd ? 0U : 1U, B);
    Nodify(Dst, B, Pred, SetBlkExprRVal(St, B, X));
  }
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
    ValueState* St =
      StateCleaned ? StmtEntryNode->getState() : 
                     RemoveDeadBindings(StmtEntryNode->getState());
    
    builder.generateNode(S, St, StmtEntryNode);
  }
  
  // For safety, NULL out these variables.
  
  CurrentStmt = NULL;
  StmtEntryNode = NULL;
  Builder = NULL;
}

void GRExprEngine::VisitDeclRefExpr(DeclRefExpr* D, NodeTy* Pred, NodeSet& Dst){

  if (D != CurrentStmt) {
    Dst.Add(Pred); // No-op. Simply propagate the current state unchanged.
    return;
  }
  
  // If we are here, we are loading the value of the decl and binding
  // it to the block-level expression.
  
  ValueState* St = Pred->getState();  
  RVal X = RVal::MakeVal(BasicVals, D);
  RVal Y = isa<lval::DeclVal>(X) ? GetRVal(St, cast<lval::DeclVal>(X)) : X;
  Nodify(Dst, D, Pred, SetBlkExprRVal(St, D, Y));
}

void GRExprEngine::VisitCall(CallExpr* CE, NodeTy* Pred,
                             CallExpr::arg_iterator AI,
                             CallExpr::arg_iterator AE,
                             NodeSet& Dst) {
  
  // Process the arguments.
  
  if (AI != AE) {
    
    NodeSet DstTmp;      
    Visit(*AI, Pred, DstTmp);    
    ++AI;
    
    for (NodeSet::iterator DI=DstTmp.begin(), DE=DstTmp.end(); DI != DE; ++DI)
      VisitCall(CE, *DI, AI, AE, Dst);
    
    return;
  }

  // If we reach here we have processed all of the arguments.  Evaluate
  // the callee expression.
  
  NodeSet DstTmp;    
  Expr* Callee = CE->getCallee()->IgnoreParenCasts();

  VisitLVal(Callee, Pred, DstTmp);
  
  if (DstTmp.empty())
    DstTmp.Add(Pred);
  
  // Finally, evaluate the function call.
  for (NodeSet::iterator DI = DstTmp.begin(), DE = DstTmp.end(); DI!=DE; ++DI) {

    ValueState* St = (*DI)->getState();    
    RVal L = GetLVal(St, Callee);

    // FIXME: Add support for symbolic function calls (calls involving
    //  function pointer values that are symbolic).
    
    // Check for undefined control-flow or calls to NULL.
    
    if (L.isUndef() || isa<lval::ConcreteInt>(L)) {      
      NodeTy* N = Builder->generateNode(CE, St, *DI);
      
      if (N) {
        N->markAsSink();
        BadCalls.insert(N);
      }
      
      continue;
    }
    
    // Check for the "noreturn" attribute.
    
    SaveAndRestore<bool> OldSink(Builder->BuildSinks);
    
    if (isa<lval::FuncVal>(L))
      if (cast<lval::FuncVal>(L).getDecl()->getAttr<NoReturnAttr>())
        Builder->BuildSinks = true;
    
    // Evaluate the call.
    
    
    bool invalidateArgs = false;
    
    if (L.isUnknown()) {
      // Check for an "unknown" callee.      
      invalidateArgs = true;
    }
    else if (isa<lval::FuncVal>(L)) {
      
      IdentifierInfo* Info = cast<lval::FuncVal>(L).getDecl()->getIdentifier();
      
      if (unsigned id = Info->getBuiltinID()) {
        switch (id) {
          case Builtin::BI__builtin_expect: {
            // For __builtin_expect, just return the value of the subexpression.
            assert (CE->arg_begin() != CE->arg_end());            
            RVal X = GetRVal(St, *(CE->arg_begin()));
            Nodify(Dst, CE, *DI, SetRVal(St, CE, X));
            continue;            
          }
            
          default:
            invalidateArgs = true;
            break;
        }
      }
    }
        
    if (invalidateArgs) {
      // Invalidate all arguments passed in by reference (LVals).
      for (CallExpr::arg_iterator I = CE->arg_begin(), E = CE->arg_end();
                                                       I != E; ++I) {
        RVal V = GetRVal(St, *I);

        if (isa<LVal>(V))
          St = SetRVal(St, cast<LVal>(V), UnknownVal());
      }
      
      Nodify(Dst, CE, *DI, St);
    }
    else {

      // Check any arguments passed-by-value against being undefined.

      bool badArg = false;
      
      for (CallExpr::arg_iterator I = CE->arg_begin(), E = CE->arg_end();
           I != E; ++I) {

        if (GetRVal((*DI)->getState(), *I).isUndef()) {        
          NodeTy* N = Builder->generateNode(CE, (*DI)->getState(), *DI);
        
          if (N) {
            N->markAsSink();
            UndefArgs[N] = *I;
          }
          
          badArg = true;
          break;
        }
      }
        
      if (badArg)
        continue;        
      
      // Dispatch to the plug-in transfer function.      
      
      unsigned size = Dst.size();
      
      EvalCall(Dst, CE, cast<LVal>(L), *DI);
      
      if (!Builder->BuildSinks && Dst.size() == size)
        Nodify(Dst, CE, *DI, St);
    }
  }
}

void GRExprEngine::VisitCast(Expr* CastE, Expr* Ex, NodeTy* Pred, NodeSet& Dst){
  
  NodeSet S1;
  QualType T = CastE->getType();
  
  if (T->isReferenceType())
    VisitLVal(Ex, Pred, S1);
  else
    Visit(Ex, Pred, S1);
  
  // Check for redundant casts or casting to "void"
  if (T->isVoidType() ||
      Ex->getType() == T || 
      (T->isPointerType() && Ex->getType()->isFunctionType())) {
    
    for (NodeSet::iterator I1 = S1.begin(), E1 = S1.end(); I1 != E1; ++I1)
      Dst.Add(*I1);

    return;
  }
  
  for (NodeSet::iterator I1 = S1.begin(), E1 = S1.end(); I1 != E1; ++I1) {
    NodeTy* N = *I1;
    ValueState* St = N->getState();
    
    RVal V = T->isReferenceType() ? GetLVal(St, Ex) : GetRVal(St, Ex);
    
    Nodify(Dst, CastE, N, SetRVal(St, CastE, EvalCast(V, CastE->getType())));
  }
}

void GRExprEngine::VisitDeclStmt(DeclStmt* DS, GRExprEngine::NodeTy* Pred,
                                 GRExprEngine::NodeSet& Dst) {
  
  ValueState* St = Pred->getState();
  
  for (const ScopedDecl* D = DS->getDecl(); D; D = D->getNextDeclarator())
    if (const VarDecl* VD = dyn_cast<VarDecl>(D)) {
      
      // FIXME: Add support for local arrays.
      if (VD->getType()->isArrayType())
        continue;
      
      const Expr* Ex = VD->getInit();
      
      if (!VD->hasGlobalStorage() || VD->getStorageClass() == VarDecl::Static) {
        
        // In this context, Static => Local variable.
        
        assert (!VD->getStorageClass() == VarDecl::Static ||
                !isa<FileVarDecl>(VD));
        
        // If there is no initializer, set the value of the
        // variable to "Undefined".
        //
        // FIXME: static variables may have an initializer, but the second
        //  time a function is called those values may not be current.

        QualType T = VD->getType();
        
        if ( VD->getStorageClass() == VarDecl::Static) {
          
          // C99: 6.7.8 Initialization
          //  If an object that has static storage duration is not initialized
          //  explicitly, then: 
          //   —if it has pointer type, it is initialized to a null pointer; 
          //   —if it has arithmetic type, it is initialized to (positive or 
          //     unsigned) zero; 
          
          // FIXME: Handle structs.  Now we treat their values as unknown.
          
          if (T->isPointerType()) {
            
            St = SetRVal(St, lval::DeclVal(VD),
                         lval::ConcreteInt(BasicVals.getValue(0, T)));
          }
          else if (T->isIntegerType()) {
            
            St = SetRVal(St, lval::DeclVal(VD),
                         nonlval::ConcreteInt(BasicVals.getValue(0, T)));
          }
          

        }
        else {
          
          // FIXME: Handle structs.  Now we treat them as unknown.  What
          //  we need to do is treat their members as unknown.
          
          if (T->isPointerType() || T->isIntegerType())
            St = SetRVal(St, lval::DeclVal(VD),
                         Ex ? GetRVal(St, Ex) : UndefinedVal());
        }
      }
    }

  Nodify(Dst, DS, Pred, St);
}


void GRExprEngine::VisitGuardedExpr(Expr* Ex, Expr* L, Expr* R,
                                   NodeTy* Pred, NodeSet& Dst) {
  
  assert (Ex == CurrentStmt && getCFG().isBlkExpr(Ex));

  ValueState* St = Pred->getState();
  RVal X = GetBlkExprRVal(St, Ex);
  
  assert (X.isUndef());
  
  Expr* SE = (Expr*) cast<UndefinedVal>(X).getData();
  
  assert (SE);
    
  X = GetBlkExprRVal(St, SE);
  
  // Make sure that we invalidate the previous binding.
  Nodify(Dst, Ex, Pred, StateMgr.SetRVal(St, Ex, X, true, true));
}

/// VisitSizeOfAlignOfTypeExpr - Transfer function for sizeof(type).
void GRExprEngine::VisitSizeOfAlignOfTypeExpr(SizeOfAlignOfTypeExpr* Ex,
                                              NodeTy* Pred,
                                              NodeSet& Dst) {

  assert (Ex->isSizeOf() && "FIXME: AlignOf(Expr) not yet implemented.");
  
  // 6.5.3.4 sizeof: "The result type is an integer."
  
  QualType T = Ex->getArgumentType();


  // FIXME: Add support for VLAs.
  if (!T.getTypePtr()->isConstantSizeType())
    return;
  
  
  uint64_t size = 1;  // Handle sizeof(void)
  
  if (T != getContext().VoidTy)
    size = getContext().getTypeSize(T) / 8;
  
  Nodify(Dst, Ex, Pred,
         SetRVal(Pred->getState(), Ex,
                  NonLVal::MakeVal(BasicVals, size, Ex->getType())));
  
}

void GRExprEngine::VisitDeref(UnaryOperator* U, NodeTy* Pred,
                              NodeSet& Dst, bool GetLVal) {

  Expr* Ex = U->getSubExpr()->IgnoreParens();
    
  NodeSet DstTmp;
  
  if (isa<DeclRefExpr>(Ex))
    DstTmp.Add(Pred);
  else
    Visit(Ex, Pred, DstTmp);
  
  for (NodeSet::iterator I = DstTmp.begin(), DE = DstTmp.end(); I != DE; ++I) {

    NodeTy* N = *I;
    ValueState* St = N->getState();
    
    // FIXME: Bifurcate when dereferencing a symbolic with no constraints?
    
    RVal V = GetRVal(St, Ex);
    
    // Check for dereferences of undefined values.
    
    if (V.isUndef()) {
      
      NodeTy* Succ = Builder->generateNode(U, St, N);
      
      if (Succ) {
        Succ->markAsSink();
        UndefDeref.insert(Succ);
      }
      
      continue;
    }
    
    // Check for dereferences of unknown values.  Treat as No-Ops.
    
    if (V.isUnknown()) {
      Dst.Add(N);
      continue;
    }
    
    // After a dereference, one of two possible situations arise:
    //  (1) A crash, because the pointer was NULL.
    //  (2) The pointer is not NULL, and the dereference works.
    // 
    // We add these assumptions.
    
    LVal LV = cast<LVal>(V);    
    bool isFeasibleNotNull;
    
    // "Assume" that the pointer is Not-NULL.
    
    ValueState* StNotNull = Assume(St, LV, true, isFeasibleNotNull);
    
    if (isFeasibleNotNull) {

      if (GetLVal) Nodify(Dst, U, N, SetRVal(StNotNull, U, LV));
      else {
        
        // FIXME: Currently symbolic analysis "generates" new symbols
        //  for the contents of values.  We need a better approach.
      
        Nodify(Dst, U, N, SetRVal(StNotNull, U,
                                  GetRVal(StNotNull, LV, U->getType())));
      }
    }
    
    bool isFeasibleNull;
    
    // Now "assume" that the pointer is NULL.
    
    ValueState* StNull = Assume(St, LV, false, isFeasibleNull);
    
    if (isFeasibleNull) {
      
      // We don't use "Nodify" here because the node will be a sink
      // and we have no intention of processing it later.

      NodeTy* NullNode = Builder->generateNode(U, StNull, N);
      
      if (NullNode) {

        NullNode->markAsSink();
        
        if (isFeasibleNotNull) ImplicitNullDeref.insert(NullNode);
        else ExplicitNullDeref.insert(NullNode);
      }
    }    
  }
}

void GRExprEngine::VisitUnaryOperator(UnaryOperator* U, NodeTy* Pred,
                                      NodeSet& Dst) {
  
  NodeSet S1;
  
  assert (U->getOpcode() != UnaryOperator::Deref);
  assert (U->getOpcode() != UnaryOperator::SizeOf);
  assert (U->getOpcode() != UnaryOperator::AlignOf);
  
  bool use_GetLVal = false;
  
  switch (U->getOpcode()) {
    case UnaryOperator::PostInc:
    case UnaryOperator::PostDec:
    case UnaryOperator::PreInc:
    case UnaryOperator::PreDec:
    case UnaryOperator::AddrOf:
      // Evalue subexpression as an LVal.
      use_GetLVal = true;
      VisitLVal(U->getSubExpr(), Pred, S1);
      break;
      
    default:
      Visit(U->getSubExpr(), Pred, S1);
      break;
  }

  for (NodeSet::iterator I1 = S1.begin(), E1 = S1.end(); I1 != E1; ++I1) {

    NodeTy* N1 = *I1;
    ValueState* St = N1->getState();
        
    RVal SubV = use_GetLVal ? GetLVal(St, U->getSubExpr()) : 
                              GetRVal(St, U->getSubExpr());
    
    if (SubV.isUnknown()) {
      Dst.Add(N1);
      continue;
    }

    if (SubV.isUndef()) {
      Nodify(Dst, U, N1, SetRVal(St, U, SubV));
      continue;
    }
    
    if (U->isIncrementDecrementOp()) {
      
      // Handle ++ and -- (both pre- and post-increment).
      
      LVal SubLV = cast<LVal>(SubV); 
      RVal V  = GetRVal(St, SubLV, U->getType());
      
      if (V.isUnknown()) {
        Dst.Add(N1);
        continue;
      }

      // Propagate undefined values.      
      if (V.isUndef()) {
        Nodify(Dst, U, N1, SetRVal(St, U, V));
        continue;
      }
      
      // Handle all other values.
      
      BinaryOperator::Opcode Op = U->isIncrementOp() ? BinaryOperator::Add
                                                     : BinaryOperator::Sub;
      
      RVal Result = EvalBinOp(Op, V, MakeConstantVal(1U, U));
      
      if (U->isPostfix())
        St = SetRVal(SetRVal(St, U, V), SubLV, Result);
      else
        St = SetRVal(SetRVal(St, U, Result), SubLV, Result);
        
      Nodify(Dst, U, N1, St);
      continue;
    }    
    
    // Handle all other unary operators.
    
    switch (U->getOpcode()) {

      case UnaryOperator::Minus:
        St = SetRVal(St, U, EvalMinus(U, cast<NonLVal>(SubV)));
        break;
        
      case UnaryOperator::Not:
        St = SetRVal(St, U, EvalComplement(cast<NonLVal>(SubV)));
        break;
        
      case UnaryOperator::LNot:   
        
        // C99 6.5.3.3: "The expression !E is equivalent to (0==E)."
        //
        //  Note: technically we do "E == 0", but this is the same in the
        //    transfer functions as "0 == E".

        if (isa<LVal>(SubV)) {
          lval::ConcreteInt V(BasicVals.getZeroWithPtrWidth());
          RVal Result = EvalBinOp(BinaryOperator::EQ, cast<LVal>(SubV), V);
          St = SetRVal(St, U, Result);
        }
        else {
          Expr* Ex = U->getSubExpr();
          nonlval::ConcreteInt V(BasicVals.getValue(0, Ex->getType()));
          RVal Result = EvalBinOp(BinaryOperator::EQ, cast<NonLVal>(SubV), V);
          St = SetRVal(St, U, Result);
        }
        
        break;
        
      case UnaryOperator::AddrOf: {
        assert (isa<LVal>(SubV));
        St = SetRVal(St, U, SubV);
        break;
      }
                
      default: ;
        assert (false && "Not implemented.");
    } 
    
    Nodify(Dst, U, N1, St);
  }
}

void GRExprEngine::VisitSizeOfExpr(UnaryOperator* U, NodeTy* Pred,
                                   NodeSet& Dst) {
  
  QualType T = U->getSubExpr()->getType();
  
  // FIXME: Add support for VLAs.
  if (!T.getTypePtr()->isConstantSizeType())
    return;
  
  uint64_t size = getContext().getTypeSize(T) / 8;                
  ValueState* St = Pred->getState();
  St = SetRVal(St, U, NonLVal::MakeVal(BasicVals, size, U->getType()));

  Nodify(Dst, U, Pred, St);
}

void GRExprEngine::VisitLVal(Expr* Ex, NodeTy* Pred, NodeSet& Dst) {

  if (Ex != CurrentStmt && getCFG().isBlkExpr(Ex)) {
    Dst.Add(Pred);
    return;
  }
  
  Ex = Ex->IgnoreParens();
  
  if (isa<DeclRefExpr>(Ex)) {
    Dst.Add(Pred);
    return;
  }
  
  if (UnaryOperator* U = dyn_cast<UnaryOperator>(Ex))
    if (U->getOpcode() == UnaryOperator::Deref) {
      VisitDeref(U, Pred, Dst, true);
      return;
    }
  
  Visit(Ex, Pred, Dst);
}

void GRExprEngine::VisitBinaryOperator(BinaryOperator* B,
                                       GRExprEngine::NodeTy* Pred,
                                       GRExprEngine::NodeSet& Dst) {
  NodeSet S1;
  
  if (B->isAssignmentOp())
    VisitLVal(B->getLHS(), Pred, S1);
  else
    Visit(B->getLHS(), Pred, S1);

  for (NodeSet::iterator I1=S1.begin(), E1=S1.end(); I1 != E1; ++I1) {

    NodeTy* N1 = *I1;
    
    // When getting the value for the LHS, check if we are in an assignment.
    // In such cases, we want to (initially) treat the LHS as an LVal,
    // so we use GetLVal instead of GetRVal so that DeclRefExpr's are
    // evaluated to LValDecl's instead of to an NonLVal.

    RVal LeftV = B->isAssignmentOp() ? GetLVal(N1->getState(), B->getLHS())
                                     : GetRVal(N1->getState(), B->getLHS());
    
    // Visit the RHS...
    
    NodeSet S2;    
    Visit(B->getRHS(), N1, S2);
    
    // Process the binary operator.
  
    for (NodeSet::iterator I2 = S2.begin(), E2 = S2.end(); I2 != E2; ++I2) {

      NodeTy* N2 = *I2;
      ValueState* St = N2->getState();      
      Expr* RHS = B->getRHS();
      RVal RightV = GetRVal(St, RHS);

      BinaryOperator::Opcode Op = B->getOpcode();
      
      if ((Op == BinaryOperator::Div || Op == BinaryOperator::Rem)
          && RHS->getType()->isIntegerType()) {

        // Check if the denominator is undefined.
        
        if (!RightV.isUnknown()) {
        
          if (RightV.isUndef()) {
            NodeTy* DivUndef = Builder->generateNode(B, St, N2);
            
            if (DivUndef) {
              DivUndef->markAsSink();
              ExplicitBadDivides.insert(DivUndef);
            }
            
            continue;
          }
            
          // Check for divide/remainder-by-zero.
          //
          // First, "assume" that the denominator is 0 or undefined.
          
          bool isFeasibleZero = false;
          ValueState* ZeroSt =  Assume(St, RightV, false, isFeasibleZero);
          
          // Second, "assume" that the denominator cannot be 0.
          
          bool isFeasibleNotZero = false;
          St = Assume(St, RightV, true, isFeasibleNotZero);
          
          // Create the node for the divide-by-zero (if it occurred).
          
          if (isFeasibleZero)
            if (NodeTy* DivZeroNode = Builder->generateNode(B, ZeroSt, N2)) {
              DivZeroNode->markAsSink();
              
              if (isFeasibleNotZero)
                ImplicitBadDivides.insert(DivZeroNode);
              else
                ExplicitBadDivides.insert(DivZeroNode);

            }
          
          if (!isFeasibleNotZero)
            continue;
        }
        
        // Fall-through.  The logic below processes the divide.
      }
      
      
      if (Op <= BinaryOperator::Or) {
        
        // Process non-assignements except commas or short-circuited
        // logical expressions (LAnd and LOr).
        
        RVal Result = EvalBinOp(Op, LeftV, RightV);
        
        if (Result.isUnknown()) {
          Dst.Add(N2);
          continue;
        }
        
        if (Result.isUndef() && !LeftV.isUndef() && !RightV.isUndef()) {
          
          // The operands were not undefined, but the result is undefined.
          
          if (NodeTy* UndefNode = Builder->generateNode(B, St, N2)) {
            UndefNode->markAsSink();            
            UndefResults.insert(UndefNode);
          }
          
          continue;
        }
        
        Nodify(Dst, B, N2, SetRVal(St, B, Result));
        continue;
      }
        
      // Process assignments.
    
      switch (Op) {        
          
        case BinaryOperator::Assign: {
          
          // Simple assignments.

          if (LeftV.isUndef()) {
            HandleUndefinedStore(B, N2);
            continue;
          }
          
          if (LeftV.isUnknown()) {
            St = SetRVal(St, B, RightV);
            break;
          }

          St = SetRVal(SetRVal(St, B, RightV), cast<LVal>(LeftV), RightV);
          break;
        }

          // Compound assignment operators.
          
        default: { 
          
          assert (B->isCompoundAssignmentOp());
          
          if (Op >= BinaryOperator::AndAssign)
            ((int&) Op) -= (BinaryOperator::AndAssign - BinaryOperator::And);
          else
            ((int&) Op) -= BinaryOperator::MulAssign;  
          
          // Check if the LHS is undefined.
          
          if (LeftV.isUndef()) {
            HandleUndefinedStore(B, N2);
            continue;
          }
          
          if (LeftV.isUnknown()) {
            assert (isa<UnknownVal>(GetRVal(St, B)));
            Dst.Add(N2);
            continue;
          }
          
          // At this pointer we know that the LHS evaluates to an LVal
          // that is neither "Unknown" or "Undefined."
          
          LVal LeftLV = cast<LVal>(LeftV);
          
          // Fetch the value of the LHS (the value of the variable, etc.).
          
          RVal V = GetRVal(N1->getState(), LeftLV, B->getLHS()->getType());
          
          // Propagate undefined value (left-side).  We
          // propogate undefined values for the RHS below when
          // we also check for divide-by-zero.
          
          if (V.isUndef()) {
            St = SetRVal(St, B, V);
            break;
          }
          
          // Propagate unknown values.
          
          if (V.isUnknown()) {
            // The value bound to LeftV is unknown.  Thus we just
            // propagate the current node (as "B" is already bound to nothing).
            assert (isa<UnknownVal>(GetRVal(St, B)));
            Dst.Add(N2);
            continue;
          }
          
          if (RightV.isUnknown()) {
            assert (isa<UnknownVal>(GetRVal(St, B)));
            St = SetRVal(St, LeftLV, UnknownVal());
            break;
          }
            
          // At this point:
          //
          //  The LHS is not Undef/Unknown.
          //  The RHS is not Unknown.
          
          // Get the computation type.
          QualType CTy = cast<CompoundAssignOperator>(B)->getComputationType();
          
          // Perform promotions.
          V = EvalCast(V, CTy);
          RightV = EvalCast(RightV, CTy);
          
          // Evaluate operands and promote to result type.

          if ((Op == BinaryOperator::Div || Op == BinaryOperator::Rem)
              && RHS->getType()->isIntegerType()) {
            
            // Check if the denominator is undefined.
                
            if (RightV.isUndef()) {
              NodeTy* DivUndef = Builder->generateNode(B, St, N2);
              
              if (DivUndef) {
                DivUndef->markAsSink();
                ExplicitBadDivides.insert(DivUndef);
              }
              
              continue;
            }

            // First, "assume" that the denominator is 0.
            
            bool isFeasibleZero = false;
            ValueState* ZeroSt = Assume(St, RightV, false, isFeasibleZero);
            
            // Second, "assume" that the denominator cannot be 0.
            
            bool isFeasibleNotZero = false;
            St = Assume(St, RightV, true, isFeasibleNotZero);
            
            // Create the node for the divide-by-zero error (if it occurred).
            
            if (isFeasibleZero) {
              NodeTy* DivZeroNode = Builder->generateNode(B, ZeroSt, N2);
              
              if (DivZeroNode) {
                DivZeroNode->markAsSink();
                
                if (isFeasibleNotZero)
                  ImplicitBadDivides.insert(DivZeroNode);
                else
                  ExplicitBadDivides.insert(DivZeroNode);
              }
            }
            
            if (!isFeasibleNotZero)
              continue;
            
            // Fall-through.  The logic below processes the divide.
          }
          else {
            
            // Propagate undefined values (right-side).
            
            if (RightV.isUndef()) {
              St = SetRVal(SetRVal(St, B, RightV), LeftLV, RightV);
              break;
            }
            
          }

          RVal Result = EvalCast(EvalBinOp(Op, V, RightV), B->getType());
          
          if (Result.isUndef()) {
            
            // The operands were not undefined, but the result is undefined.
            
            if (NodeTy* UndefNode = Builder->generateNode(B, St, N2)) {
              UndefNode->markAsSink();            
              UndefResults.insert(UndefNode);
            }
            
            continue;
          }
          
          St = SetRVal(SetRVal(St, B, Result), LeftLV, Result);
        }
      }
    
      Nodify(Dst, B, N2, St);
    }
  }
}

void GRExprEngine::HandleUndefinedStore(Stmt* S, NodeTy* Pred) {  
  NodeTy* N = Builder->generateNode(S, Pred->getState(), Pred);
  N->markAsSink();
  UndefStores.insert(N);
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
      //   AddrLabelExpr, IntegerLiteral, CharacterLiteral
      
      Dst.Add(Pred); // No-op. Simply propagate the current state unchanged.
      break;
                                                       
    case Stmt::BinaryOperatorClass: {
      BinaryOperator* B = cast<BinaryOperator>(S);
 
      if (B->isLogicalOp()) {
        VisitLogicalExpr(B, Pred, Dst);
        break;
      }
      else if (B->getOpcode() == BinaryOperator::Comma) {
        ValueState* St = Pred->getState();
        Nodify(Dst, B, Pred, SetRVal(St, B, GetRVal(St, B->getRHS())));
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

      // FIXME: ChooseExpr is really a constant.  We need to fix
      //        the CFG do not model them as explicit control-flow.
      
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
      
      ValueState* St = Pred->getState();
      Expr* LastExpr = cast<Expr>(*SE->getSubStmt()->body_rbegin());
      Nodify(Dst, SE, Pred, SetRVal(St, SE, GetRVal(St, LastExpr)));
      break;      
    }
      
      // FIXME: We may wish to always bind state to ReturnStmts so
      //  that users can quickly query what was the state at the
      //  exit points of a function.
      
    case Stmt::ReturnStmtClass: {
      if (Expr* R = cast<ReturnStmt>(S)->getRetValue())
        Visit(R, Pred, Dst);
      else
        Dst.Add(Pred);
      
      break;
    }
      
    case Stmt::UnaryOperatorClass: {
      UnaryOperator* U = cast<UnaryOperator>(S);
      
      switch (U->getOpcode()) {
        case UnaryOperator::Deref: VisitDeref(U, Pred, Dst); break;
        case UnaryOperator::Plus:  Visit(U->getSubExpr(), Pred, Dst); break;
        case UnaryOperator::SizeOf: VisitSizeOfExpr(U, Pred, Dst); break;
        default: VisitUnaryOperator(U, Pred, Dst); break;
      }
      
      break;
    }
  }
}

//===----------------------------------------------------------------------===//
// "Assume" logic.
//===----------------------------------------------------------------------===//

ValueState* GRExprEngine::Assume(ValueState* St, LVal Cond,
                                           bool Assumption, 
                                           bool& isFeasible) {
  switch (Cond.getSubKind()) {
    default:
      assert (false && "'Assume' not implemented for this LVal.");
      return St;
      
    case lval::SymbolValKind:
      if (Assumption)
        return AssumeSymNE(St, cast<lval::SymbolVal>(Cond).getSymbol(),
                           BasicVals.getZeroWithPtrWidth(), isFeasible);
      else
        return AssumeSymEQ(St, cast<lval::SymbolVal>(Cond).getSymbol(),
                           BasicVals.getZeroWithPtrWidth(), isFeasible);
      
      
    case lval::DeclValKind:
    case lval::FuncValKind:
    case lval::GotoLabelKind:
      isFeasible = Assumption;
      return St;

    case lval::ConcreteIntKind: {
      bool b = cast<lval::ConcreteInt>(Cond).getValue() != 0;
      isFeasible = b ? Assumption : !Assumption;      
      return St;
    }
  }
}

ValueState* GRExprEngine::Assume(ValueState* St, NonLVal Cond,
                                         bool Assumption, 
                                         bool& isFeasible) {  
  switch (Cond.getSubKind()) {
    default:
      assert (false && "'Assume' not implemented for this NonLVal.");
      return St;
      
      
    case nonlval::SymbolValKind: {
      nonlval::SymbolVal& SV = cast<nonlval::SymbolVal>(Cond);
      SymbolID sym = SV.getSymbol();
      
      if (Assumption)
        return AssumeSymNE(St, sym, BasicVals.getValue(0, SymMgr.getType(sym)),
                           isFeasible);
      else
        return AssumeSymEQ(St, sym, BasicVals.getValue(0, SymMgr.getType(sym)),
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

ValueState*
GRExprEngine::AssumeSymNE(ValueState* St, SymbolID sym,
                         const llvm::APSInt& V, bool& isFeasible) {
  
  // First, determine if sym == X, where X != V.
  if (const llvm::APSInt* X = St->getSymVal(sym)) {
    isFeasible = *X != V;
    return St;
  }
  
  // Second, determine if sym != V.
  if (St->isNotEqual(sym, V)) {
    isFeasible = true;
    return St;
  }
      
  // If we reach here, sym is not a constant and we don't know if it is != V.
  // Make that assumption.
  
  isFeasible = true;
  return StateMgr.AddNE(St, sym, V);
}

ValueState*
GRExprEngine::AssumeSymEQ(ValueState* St, SymbolID sym,
                         const llvm::APSInt& V, bool& isFeasible) {
  
  // First, determine if sym == X, where X != V.
  if (const llvm::APSInt* X = St->getSymVal(sym)) {
    isFeasible = *X == V;
    return St;
  }
  
  // Second, determine if sym != V.
  if (St->isNotEqual(sym, V)) {
    isFeasible = false;
    return St;
  }
  
  // If we reach here, sym is not a constant and we don't know if it is == V.
  // Make that assumption.
  
  isFeasible = true;
  return StateMgr.AddEQ(St, sym, V);
}

ValueState*
GRExprEngine::AssumeSymInt(ValueState* St, bool Assumption,
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
static SourceManager* GraphPrintSourceManager;

namespace llvm {
template<>
struct VISIBILITY_HIDDEN DOTGraphTraits<GRExprEngine::NodeTy*> :
  public DefaultDOTGraphTraits {
    
  static void PrintVarBindings(std::ostream& Out, ValueState* St) {

    Out << "Variables:\\l";
    
    bool isFirst = true;
    
    for (ValueState::vb_iterator I=St->vb_begin(), E=St->vb_end(); I!=E;++I) {        

      if (isFirst)
        isFirst = false;
      else
        Out << "\\l";
      
      Out << ' ' << I.getKey()->getName() << " : ";
      I.getData().print(Out);
    }
    
  }
    
    
  static void PrintSubExprBindings(std::ostream& Out, ValueState* St){
    
    bool isFirst = true;
    
    for (ValueState::seb_iterator I=St->seb_begin(), E=St->seb_end();I!=E;++I) {        
      
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
    
  static void PrintBlkExprBindings(std::ostream& Out, ValueState* St){
        
    bool isFirst = true;

    for (ValueState::beb_iterator I=St->beb_begin(), E=St->beb_end(); I!=E;++I){      
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
    
  static void PrintEQ(std::ostream& Out, ValueState* St) {
    ValueState::ConstEqTy CE = St->ConstEq;
    
    if (CE.isEmpty())
      return;
    
    Out << "\\l\\|'==' constraints:";

    for (ValueState::ConstEqTy::iterator I=CE.begin(), E=CE.end(); I!=E;++I)
      Out << "\\l $" << I.getKey() << " : " << I.getData()->toString();
  }
    
  static void PrintNE(std::ostream& Out, ValueState* St) {
    ValueState::ConstNotEqTy NE = St->ConstNotEq;
    
    if (NE.isEmpty())
      return;
    
    Out << "\\l\\|'!=' constraints:";
    
    for (ValueState::ConstNotEqTy::iterator I=NE.begin(), EI=NE.end();
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
        GraphPrintCheckerState->isUndefDeref(N) ||
        GraphPrintCheckerState->isUndefStore(N) ||
        GraphPrintCheckerState->isUndefControlFlow(N) ||
        GraphPrintCheckerState->isExplicitBadDivide(N) ||
        GraphPrintCheckerState->isImplicitBadDivide(N) ||
        GraphPrintCheckerState->isUndefResult(N) ||
        GraphPrintCheckerState->isBadCall(N) ||
        GraphPrintCheckerState->isUndefArg(N))
      return "color=\"red\",style=\"filled\"";
    
    if (GraphPrintCheckerState->isNoReturnCall(N))
      return "color=\"blue\",style=\"filled\"";
    
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
        Stmt* S = L.getStmt();
        SourceLocation SLoc = S->getLocStart();

        Out << S->getStmtClassName() << ' ' << (void*) S << ' ';        
        S->printPretty(Out);
        
        if (SLoc.isFileID()) {        
          Out << "\\lline="
            << GraphPrintSourceManager->getLineNumber(SLoc) << " col="
            << GraphPrintSourceManager->getColumnNumber(SLoc) << "\\l";          
        }
        
        if (GraphPrintCheckerState->isImplicitNullDeref(N))
          Out << "\\|Implicit-Null Dereference.\\l";
        else if (GraphPrintCheckerState->isExplicitNullDeref(N))
          Out << "\\|Explicit-Null Dereference.\\l";
        else if (GraphPrintCheckerState->isUndefDeref(N))
          Out << "\\|Dereference of undefialied value.\\l";
        else if (GraphPrintCheckerState->isUndefStore(N))
          Out << "\\|Store to Undefined LVal.";
        else if (GraphPrintCheckerState->isExplicitBadDivide(N))
          Out << "\\|Explicit divide-by zero or undefined value.";
        else if (GraphPrintCheckerState->isImplicitBadDivide(N))
          Out << "\\|Implicit divide-by zero or undefined value.";
        else if (GraphPrintCheckerState->isUndefResult(N))
          Out << "\\|Result of operation is undefined.";
        else if (GraphPrintCheckerState->isNoReturnCall(N))
          Out << "\\|Call to function marked \"noreturn\".";
        else if (GraphPrintCheckerState->isBadCall(N))
          Out << "\\|Call to NULL/Undefined.";
        else if (GraphPrintCheckerState->isUndefArg(N))
          Out << "\\|Argument in call is undefined";
        
        break;
      }
    
      default: {
        const BlockEdge& E = cast<BlockEdge>(Loc);
        Out << "Edge: (B" << E.getSrc()->getBlockID() << ", B"
            << E.getDst()->getBlockID()  << ')';
        
        if (Stmt* T = E.getSrc()->getTerminator()) {
          
          SourceLocation SLoc = T->getLocStart();
         
          Out << "\\|Terminator: ";
          
          E.getSrc()->printTerminator(Out);
          
          if (SLoc.isFileID()) {
            Out << "\\lline="
              << GraphPrintSourceManager->getLineNumber(SLoc) << " col="
              << GraphPrintSourceManager->getColumnNumber(SLoc);
          }
            
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
        
        if (GraphPrintCheckerState->isUndefControlFlow(N)) {
          Out << "\\|Control-flow based on\\lUndefined value.\\l";
        }
      }
    }
    
    Out << "\\|StateID: " << (void*) N->getState() << "\\|";

    N->getState()->printDOT(Out);
      
    Out << "\\l";
    return Out.str();
  }
};
} // end llvm namespace    
#endif

#ifndef NDEBUG
template <typename ITERATOR>
static void AddSources(llvm::SmallVector<GRExprEngine::NodeTy*, 10>& Sources,
                              ITERATOR I, ITERATOR E) {
  
  for ( ; I != E; ++I )
    Sources.push_back(*I);
}
#endif

void GRExprEngine::ViewGraph(bool trim) {
#ifndef NDEBUG
  GraphPrintCheckerState = this;
  GraphPrintSourceManager = &getContext().getSourceManager();
  
  if (trim) {
    llvm::SmallVector<NodeTy*, 10> Sources;
    AddSources(Sources, null_derefs_begin(), null_derefs_end());
    AddSources(Sources, undef_derefs_begin(), undef_derefs_end());
    
    GRExprEngine::GraphTy* TrimmedG = G.Trim(&Sources[0],
                                             &Sources[0]+Sources.size());
    
    if (!TrimmedG)
      llvm::cerr << "warning: Trimmed ExplodedGraph is empty.\n";
    else {
      llvm::ViewGraph(*TrimmedG->roots_begin(), "TrimmedGRExprEngine");    
      delete TrimmedG;
    }
  }
  else
    llvm::ViewGraph(*G.roots_begin(), "GRExprEngine");
  
  
  GraphPrintCheckerState = NULL;
  GraphPrintSourceManager = NULL;
#endif
}
