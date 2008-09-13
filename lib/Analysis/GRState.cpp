//= GRState*cpp - Path-Sens. "State" for tracking valuues -----*- C++ -*--=//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines SymbolID, ExprBindKey, and GRState*
//
//===----------------------------------------------------------------------===//

#include "clang/Analysis/PathSensitive/GRStateTrait.h"
#include "clang/Analysis/PathSensitive/GRState.h"
#include "clang/Analysis/PathSensitive/GRTransferFuncs.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/Support/raw_ostream.h"

using namespace clang;

// Give the vtable for ConstraintManager somewhere to live.
ConstraintManager::~ConstraintManager() {}

GRStateManager::~GRStateManager() {
  for (std::vector<GRState::Printer*>::iterator I=Printers.begin(),
        E=Printers.end(); I!=E; ++I)
    delete *I;
  
  for (GDMContextsTy::iterator I=GDMContexts.begin(), E=GDMContexts.end();
       I!=E; ++I)
    I->second.second(I->second.first);
}

const GRState*
GRStateManager::RemoveDeadBindings(const GRState* St, Stmt* Loc,
                                   const LiveVariables& Liveness,
                                   DeadSymbolsTy& DSymbols) {  
  
  // This code essentially performs a "mark-and-sweep" of the VariableBindings.
  // The roots are any Block-level exprs and Decls that our liveness algorithm
  // tells us are live.  We then see what Decls they may reference, and keep
  // those around.  This code more than likely can be made faster, and the
  // frequency of which this method is called should be experimented with
  // for optimum performance.  
  DRoots.clear();
  StoreManager::LiveSymbolsTy LSymbols;
  
  GRState NewSt = *St;

  NewSt.Env = EnvMgr.RemoveDeadBindings(NewSt.Env, Loc, Liveness, 
                                        DRoots, LSymbols);

  // Clean up the store.
  DSymbols.clear();
  NewSt.St = StMgr->RemoveDeadBindings(St->getStore(), Loc, Liveness, DRoots,
                                       LSymbols, DSymbols);

  return ConstraintMgr->RemoveDeadBindings(getPersistentState(NewSt), 
                                           LSymbols, DSymbols);
}

const GRState* GRStateManager::SetRVal(const GRState* St, LVal LV,
                                             RVal V) {
  
  Store OldStore = St->getStore();
  Store NewStore = StMgr->SetRVal(OldStore, LV, V);
  
  if (NewStore == OldStore)
    return St;
  
  GRState NewSt = *St;
  NewSt.St = NewStore;
  return getPersistentState(NewSt);    
}

const GRState* GRStateManager::AddDecl(const GRState* St, const VarDecl* VD, 
                                       Expr* Ex, unsigned Count) {
  Store OldStore = St->getStore();
  Store NewStore;

  if (Ex)
    NewStore = StMgr->AddDecl(OldStore, *this, VD, Ex, 
                              GetRVal(St, Ex), Count);
  else
    NewStore = StMgr->AddDecl(OldStore, *this, VD, Ex);
                              
  if (NewStore == OldStore)
    return St;
  
  GRState NewSt = *St;
  NewSt.St = NewStore;
  return getPersistentState(NewSt);
}

const GRState* GRStateManager::Unbind(const GRState* St, LVal LV) {
  Store OldStore = St->getStore();
  Store NewStore = StMgr->Remove(OldStore, LV);
  
  if (NewStore == OldStore)
    return St;
  
  GRState NewSt = *St;
  NewSt.St = NewStore;
  return getPersistentState(NewSt);    
}

const GRState* GRStateManager::getInitialState() {

  GRState StateImpl(EnvMgr.getInitialEnvironment(), 
                    StMgr->getInitialStore(*this),
                    GDMFactory.GetEmptyMap());

  return getPersistentState(StateImpl);
}

const GRState* GRStateManager::getPersistentState(GRState& State) {
  
  llvm::FoldingSetNodeID ID;
  State.Profile(ID);  
  void* InsertPos;
  
  if (GRState* I = StateSet.FindNodeOrInsertPos(ID, InsertPos))
    return I;
  
  GRState* I = (GRState*) Alloc.Allocate<GRState>();
  new (I) GRState(State);  
  StateSet.InsertNode(I, InsertPos);
  return I;
}


//===----------------------------------------------------------------------===//
//  State pretty-printing.
//===----------------------------------------------------------------------===//

void GRState::print(std::ostream& Out, StoreManager& StoreMgr,
                    ConstraintManager& ConstraintMgr,
                    Printer** Beg, Printer** End,
                    const char* nl, const char* sep) const {
  
  // Print the store.
  StoreMgr.print(getStore(), Out, nl, sep);
  
  // Print Subexpression bindings.
  bool isFirst = true;
  
  for (seb_iterator I = seb_begin(), E = seb_end(); I != E; ++I) {        
    
    if (isFirst) {
      Out << nl << nl << "Sub-Expressions:" << nl;
      isFirst = false;
    }
    else { Out << nl; }
    
    Out << " (" << (void*) I.getKey() << ") ";
    llvm::raw_os_ostream OutS(Out);
    I.getKey()->printPretty(OutS);
    OutS.flush();
    Out << " : ";
    I.getData().print(Out);
  }
  
  // Print block-expression bindings.
  isFirst = true;
  
  for (beb_iterator I = beb_begin(), E = beb_end(); I != E; ++I) {      

    if (isFirst) {
      Out << nl << nl << "Block-level Expressions:" << nl;
      isFirst = false;
    }
    else { Out << nl; }
    
    Out << " (" << (void*) I.getKey() << ") ";
    llvm::raw_os_ostream OutS(Out);
    I.getKey()->printPretty(OutS);
    OutS.flush();
    Out << " : ";
    I.getData().print(Out);
  }
  
  ConstraintMgr.print(this, Out, nl, sep);
  
  // Print checker-specific data. 
  for ( ; Beg != End ; ++Beg) (*Beg)->Print(Out, this, nl, sep);
}

void GRStateRef::printDOT(std::ostream& Out) const {
  print(Out, "\\l", "\\|");
}

void GRStateRef::printStdErr() const {
  print(*llvm::cerr);
}  

void GRStateRef::print(std::ostream& Out, const char* nl, const char* sep)const{
  GRState::Printer **beg = Mgr->Printers.empty() ? 0 : &Mgr->Printers[0];
  GRState::Printer **end = !beg ? 0 : beg + Mgr->Printers.size();  
  St->print(Out, *Mgr->StMgr, *Mgr->ConstraintMgr, beg, end, nl, sep);
}

//===----------------------------------------------------------------------===//
// Generic Data Map.
//===----------------------------------------------------------------------===//

void* const* GRState::FindGDM(void* K) const {
  return GDM.lookup(K);
}

void*
GRStateManager::FindGDMContext(void* K,
                               void* (*CreateContext)(llvm::BumpPtrAllocator&),
                               void (*DeleteContext)(void*)) {
  
  std::pair<void*, void (*)(void*)>& p = GDMContexts[K];
  if (!p.first) {
    p.first = CreateContext(Alloc);
    p.second = DeleteContext;
  }
  
  return p.first;
}

const GRState* GRStateManager::addGDM(const GRState* St, void* Key, void* Data){  
  GRState::GenericDataMap M1 = St->getGDM();
  GRState::GenericDataMap M2 = GDMFactory.Add(M1, Key, Data);
  
  if (M1 == M2)
    return St;
  
  GRState NewSt = *St;
  NewSt.GDM = M2;
  return getPersistentState(NewSt);
}

//===----------------------------------------------------------------------===//
// Queries.
//===----------------------------------------------------------------------===//

bool GRStateManager::isEqual(const GRState* state, Expr* Ex,
                             const llvm::APSInt& Y) {
  
  RVal V = GetRVal(state, Ex);
  
  if (lval::ConcreteInt* X = dyn_cast<lval::ConcreteInt>(&V))
    return X->getValue() == Y;

  if (nonlval::ConcreteInt* X = dyn_cast<nonlval::ConcreteInt>(&V))
    return X->getValue() == Y;
    
  if (nonlval::SymbolVal* X = dyn_cast<nonlval::SymbolVal>(&V))
    return ConstraintMgr->isEqual(state, X->getSymbol(), Y);
  
  if (lval::SymbolVal* X = dyn_cast<lval::SymbolVal>(&V))
    return ConstraintMgr->isEqual(state, X->getSymbol(), Y);
  
  return false;
}
  
bool GRStateManager::isEqual(const GRState* state, Expr* Ex, uint64_t x) {
  return isEqual(state, Ex, BasicVals.getValue(x, Ex->getType()));
}
