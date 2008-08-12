//== ValueState*h - Path-Sens. "State" for tracking valuues -----*- C++ -*--==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines SymbolID, ExprBindKey, and ValueState*
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_ANALYSIS_VALUESTATE_H
#define LLVM_CLANG_ANALYSIS_VALUESTATE_H

// FIXME: Reduce the number of includes.

#include "clang/Analysis/PathSensitive/Environment.h"
#include "clang/Analysis/PathSensitive/Store.h"
#include "clang/Analysis/PathSensitive/RValues.h"
#include "clang/Analysis/PathSensitive/GRCoreEngine.h"
#include "clang/AST/Expr.h"
#include "clang/AST/Decl.h"
#include "clang/AST/ASTContext.h"
#include "clang/Analysis/Analyses/LiveVariables.h"

#include "llvm/Support/Casting.h"
#include "llvm/Support/DataTypes.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/ImmutableMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Streams.h"

#include <functional>

namespace clang {

class ValueStateManager;
class GRTransferFuncs;
  
//===----------------------------------------------------------------------===//
// ValueState- An ImmutableMap type Stmt*/Decl*/Symbols to RVals.
//===----------------------------------------------------------------------===//

/// ValueState - This class encapsulates the actual data values for
///  for a "state" in our symbolic value tracking.  It is intended to be
///  used as a functional object; that is once it is created and made
///  "persistent" in a FoldingSet its values will never change.
class ValueState : public llvm::FoldingSetNode {
public:  
  // Typedefs.  
  typedef llvm::ImmutableSet<llvm::APSInt*>                IntSetTy;
  typedef llvm::ImmutableMap<void*, void*>                 GenericDataMap;  
  typedef llvm::ImmutableMap<SymbolID,IntSetTy>            ConstNotEqTy;
  typedef llvm::ImmutableMap<SymbolID,const llvm::APSInt*> ConstEqTy;
  
  typedef ValueStateManager ManagerTy;
  
private:
  void operator=(const ValueState& R) const;
  
  friend class ValueStateManager;
  
  Environment Env;
  Store St;

  // FIXME: Make these private.
public:
  GenericDataMap   GDM;
  ConstNotEqTy     ConstNotEq;
  ConstEqTy        ConstEq;
  void*            CheckerState;
  
public:
  
  /// This ctor is used when creating the first ValueState object.
  ValueState(const Environment& env,  Store st, GenericDataMap gdm,
             ConstNotEqTy CNE, ConstEqTy  CE)
    : Env(env),
      St(st),
      GDM(gdm),
      ConstNotEq(CNE),
      ConstEq(CE),
      CheckerState(NULL) {}
  
  /// Copy ctor - We must explicitly define this or else the "Next" ptr
  ///  in FoldingSetNode will also get copied.
  ValueState(const ValueState& RHS)
    : llvm::FoldingSetNode(),
      Env(RHS.Env),
      St(RHS.St),
      GDM(RHS.GDM),
      ConstNotEq(RHS.ConstNotEq),
      ConstEq(RHS.ConstEq),
      CheckerState(RHS.CheckerState) {} 
  
  /// getEnvironment - Return the environment associated with this state.
  ///  The environment is the mapping from expressions to values.
  const Environment& getEnvironment() const { return Env; }
  
  /// getStore - Return the store associated with this state.  The store
  ///  is a mapping from locations to values.
  Store getStore() const { return St; }
  
  /// getGDM - Return the generic data map associated with this state.
  GenericDataMap getGDM() const { return GDM; }
  
  /// Profile - Profile the contents of a ValueState object for use
  ///  in a FoldingSet.
  static void Profile(llvm::FoldingSetNodeID& ID, const ValueState* V) {
    V->Env.Profile(ID);
    ID.AddPointer(V->St);
    V->GDM.Profile(ID);
    V->ConstNotEq.Profile(ID);
    V->ConstEq.Profile(ID);
    ID.AddPointer(V->CheckerState);
  }

  /// Profile - Used to profile the contents of this object for inclusion
  ///  in a FoldingSet.
  void Profile(llvm::FoldingSetNodeID& ID) const {
    Profile(ID, this);
  }
  
  // Queries.
  
  bool isNotEqual(SymbolID sym, const llvm::APSInt& V) const;
  bool isEqual(SymbolID sym, const llvm::APSInt& V) const;
  
  const llvm::APSInt* getSymVal(SymbolID sym) const;
 
  RVal LookupExpr(Expr* E) const {
    return Env.LookupExpr(E);
  }
  
  // Iterators.


  // FIXME: We'll be removing the VarBindings iterator very soon.  Right now
  //  it assumes that Store is a VarBindingsTy.
  typedef llvm::ImmutableMap<VarDecl*,RVal> VarBindingsTy;
  typedef VarBindingsTy::iterator vb_iterator;
  vb_iterator vb_begin() const {
    VarBindingsTy B(static_cast<const VarBindingsTy::TreeTy*>(St));
    return B.begin();
  }
  vb_iterator vb_end() const {
    VarBindingsTy B(static_cast<const VarBindingsTy::TreeTy*>(St));
    return B.end();
  }

    
  typedef Environment::seb_iterator seb_iterator;
  seb_iterator seb_begin() const { return Env.seb_begin(); }
  seb_iterator seb_end() const { return Env.beb_end(); }
  
  typedef Environment::beb_iterator beb_iterator;
  beb_iterator beb_begin() const { return Env.beb_begin(); }
  beb_iterator beb_end() const { return Env.beb_end(); }
  
  typedef ConstNotEqTy::iterator cne_iterator;
  cne_iterator cne_begin() const { return ConstNotEq.begin(); }
  cne_iterator cne_end() const { return ConstNotEq.end(); }
  
  typedef ConstEqTy::iterator ce_iterator;
  ce_iterator ce_begin() const { return ConstEq.begin(); }
  ce_iterator ce_end() const { return ConstEq.end(); }
  
  class CheckerStatePrinter {
  public:
    virtual ~CheckerStatePrinter() {}
    virtual void PrintCheckerState(std::ostream& Out, void* State,
                                   const char* nl, const char* sep) = 0;
  };

  void print(std::ostream& Out, CheckerStatePrinter* P = NULL,
             const char* nl = "\n", const char* sep = "") const;
  
  void printStdErr(CheckerStatePrinter* P = NULL) const;
  
  void printDOT(std::ostream& Out, CheckerStatePrinter*P = NULL) const;
};  
  
template<> struct GRTrait<ValueState*> {
  static inline void* toPtr(ValueState* St)  { return (void*) St; }
  static inline ValueState* toState(void* P) { return (ValueState*) P; }
  static inline void Profile(llvm::FoldingSetNodeID& profile, ValueState* St) {    
    // At this point states have already been uniqued.  Just
    // add the pointer.
    profile.AddPointer(St);
  }
};
  
class ValueStateSet {
  typedef llvm::SmallPtrSet<const ValueState*,5> ImplTy;
  ImplTy Impl;  
public:
  ValueStateSet() {}

  inline void Add(const ValueState* St) {
    Impl.insert(St);
  }
  
  typedef ImplTy::const_iterator iterator;
  
  inline unsigned size() const { return Impl.size();  }
  inline bool empty()    const { return Impl.empty(); }
  
  inline iterator begin() const { return Impl.begin(); }
  inline iterator end() const { return Impl.end();   }
  
  class AutoPopulate {
    ValueStateSet& S;
    unsigned StartSize;
    const ValueState* St;
  public:
    AutoPopulate(ValueStateSet& s, const ValueState* st) 
      : S(s), StartSize(S.size()), St(st) {}
    
    ~AutoPopulate() {
      if (StartSize == S.size())
        S.Add(St);
    }
  };
};
  
class ValueStateManager {
  friend class GRExprEngine;
  
private:
  EnvironmentManager                   EnvMgr;
  llvm::OwningPtr<StoreManager>        StMgr;
  ValueState::IntSetTy::Factory        ISetFactory;
  ValueState::GenericDataMap::Factory  GDMFactory;
  ValueState::ConstNotEqTy::Factory    CNEFactory;
  ValueState::ConstEqTy::Factory       CEFactory;
  
  /// StateSet - FoldingSet containing all the states created for analyzing
  ///  a particular function.  This is used to unique states.
  llvm::FoldingSet<ValueState> StateSet;

  /// ValueMgr - Object that manages the data for all created RVals.
  BasicValueFactory BasicVals;

  /// SymMgr - Object that manages the symbol information.
  SymbolManager SymMgr;

  /// Alloc - A BumpPtrAllocator to allocate states.
  llvm::BumpPtrAllocator& Alloc;
  
  /// DRoots - A vector to hold of worklist used by RemoveDeadSymbols.
  ///  This vector is persistent because it is reused over and over.
  StoreManager::DeclRootsTy DRoots;
  
  /// CurrentStmt - The block-level statement currently being visited.  This
  ///  is set by GRExprEngine.
  Stmt* CurrentStmt;
  
  /// cfg - The CFG for the analyzed function/method.
  CFG& cfg;
    
  /// TF - Object that represents a bundle of transfer functions
  ///  for manipulating and creating RVals.
  GRTransferFuncs* TF;
  
private:

  Environment RemoveBlkExpr(const Environment& Env, Expr* E) {
    return EnvMgr.RemoveBlkExpr(Env, E);
  }
   
  // FIXME: Remove when we do lazy initializaton of variable bindings.
  const ValueState* BindVar(const ValueState* St, VarDecl* D, RVal V) {
    return SetRVal(St, lval::DeclVal(D), V);
  }
    
public:  
  ValueStateManager(ASTContext& Ctx, StoreManager* stmgr,
                    llvm::BumpPtrAllocator& alloc, CFG& c) 
  : EnvMgr(alloc),
    StMgr(stmgr),
    ISetFactory(alloc), 
    GDMFactory(alloc),
    CNEFactory(alloc),
    CEFactory(alloc),
    BasicVals(Ctx, alloc),
    SymMgr(alloc),
    Alloc(alloc),
    cfg(c) {}

  const ValueState* getInitialState();
        
  BasicValueFactory& getBasicVals() { return BasicVals; }
  const BasicValueFactory& getBasicVals() const { return BasicVals; }
  SymbolManager& getSymbolManager() { return SymMgr; }
  
  typedef StoreManager::DeadSymbolsTy DeadSymbolsTy;
  
  const ValueState* RemoveDeadBindings(const ValueState* St, Stmt* Loc, 
                                       const LiveVariables& Liveness,
                                       DeadSymbolsTy& DeadSyms);

  const ValueState* RemoveSubExprBindings(const ValueState* St) {
    ValueState NewSt = *St;
    NewSt.Env = EnvMgr.RemoveSubExprBindings(NewSt.Env);
    return getPersistentState(NewSt);
  }

  // Methods that query & manipulate the Environment.
  
  RVal GetRVal(const ValueState* St, Expr* Ex) {
    return St->getEnvironment().GetRVal(Ex, BasicVals);
  }
  
  RVal GetBlkExprRVal(const ValueState* St, Expr* Ex) {
    return St->getEnvironment().GetBlkExprRVal(Ex, BasicVals);
  }
  
  const ValueState* SetRVal(const ValueState* St, Expr* Ex, RVal V,
                            bool isBlkExpr, bool Invalidate) {
    
    const Environment& OldEnv = St->getEnvironment();
    Environment NewEnv = EnvMgr.SetRVal(OldEnv, Ex, V, isBlkExpr, Invalidate);
    
    if (NewEnv == OldEnv)
      return St;
    
    ValueState NewSt = *St;
    NewSt.Env = NewEnv;
    return getPersistentState(NewSt);
  }
  
  const ValueState* SetRVal(const ValueState* St, Expr* Ex, RVal V) {
    
    bool isBlkExpr = false;
    
    if (Ex == CurrentStmt) {
      isBlkExpr = cfg.isBlkExpr(Ex);
      
      if (!isBlkExpr)
        return St;
    }
    
    return SetRVal(St, Ex, V, isBlkExpr, true);
  }
  
  // Methods that manipulate the GDM.
  const ValueState* addGDM(const ValueState* St, void* Key, void* Data) {
    ValueState::GenericDataMap M1 = St->getGDM();
    ValueState::GenericDataMap M2 = GDMFactory.Add(M2, Key, Data);    
    
    if (M1 == M2)
      return St;
    
    ValueState NewSt = *St;
    NewSt.GDM = M2;
    return getPersistentState(NewSt);
  }

  // Methods that query & manipulate the Store.
  RVal GetRVal(const ValueState* St, LVal LV, QualType T = QualType()) {
    return StMgr->GetRVal(St->getStore(), LV, T);
  }
  
  void SetRVal(ValueState& St, LVal LV, RVal V) {
    St.St = StMgr->SetRVal(St.St, LV, V);
  }
  
  const ValueState* SetRVal(const ValueState* St, LVal LV, RVal V);  

  void Unbind(ValueState& St, LVal LV) {
    St.St = StMgr->Remove(St.St, LV);
  }
  
  const ValueState* Unbind(const ValueState* St, LVal LV);
  
  const ValueState* getPersistentState(ValueState& Impl);
  
  const ValueState* AddEQ(const ValueState* St, SymbolID sym,
                          const llvm::APSInt& V);

  const ValueState* AddNE(const ValueState* St, SymbolID sym,
                          const llvm::APSInt& V);
  
  bool isEqual(const ValueState* state, Expr* Ex, const llvm::APSInt& V);
  bool isEqual(const ValueState* state, Expr* Ex, uint64_t);
  
  // Assumption logic.
  const ValueState* Assume(const ValueState* St, RVal Cond, bool Assumption,
                           bool& isFeasible) {
    
    if (Cond.isUnknown()) {
      isFeasible = true;
      return St;
    }
    
    if (isa<LVal>(Cond))
      return Assume(St, cast<LVal>(Cond), Assumption, isFeasible);
    else
      return Assume(St, cast<NonLVal>(Cond), Assumption, isFeasible);
  }
  
  const ValueState* Assume(const ValueState* St, LVal Cond, bool Assumption,
                           bool& isFeasible);

  const ValueState* Assume(const ValueState* St, NonLVal Cond, bool Assumption,
                           bool& isFeasible);

private:  
  const ValueState* AssumeAux(const ValueState* St, LVal Cond, bool Assumption,
                              bool& isFeasible);
  
  
  const ValueState* AssumeAux(const ValueState* St, NonLVal Cond,
                              bool Assumption, bool& isFeasible);
    
  const ValueState* AssumeSymInt(const ValueState* St, bool Assumption,                                 
                                 const SymIntConstraint& C, bool& isFeasible);
  
  const ValueState* AssumeSymNE(const ValueState* St, SymbolID sym,
                                const llvm::APSInt& V, bool& isFeasible);
  
  const ValueState* AssumeSymEQ(const ValueState* St, SymbolID sym,
                                const llvm::APSInt& V, bool& isFeasible);
  
  const ValueState* AssumeSymLT(const ValueState* St, SymbolID sym,
                                const llvm::APSInt& V, bool& isFeasible);
  
  const ValueState* AssumeSymLE(const ValueState* St, SymbolID sym,
                                const llvm::APSInt& V, bool& isFeasible);
  
  const ValueState* AssumeSymGT(const ValueState* St, SymbolID sym,
                                const llvm::APSInt& V, bool& isFeasible);
  
  const ValueState* AssumeSymGE(const ValueState* St, SymbolID sym,
                                const llvm::APSInt& V, bool& isFeasible);
};
  
} // end clang namespace

#endif
