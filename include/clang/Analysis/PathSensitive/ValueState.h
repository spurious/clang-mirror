//== ValueState*h - Path-Sens. "State" for tracking valuues -----*- C++ -*--==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This files defines SymbolID, ExprBindKey, and ValueState*
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_ANALYSIS_VALUESTATE_H
#define LLVM_CLANG_ANALYSIS_VALUESTATE_H

// FIXME: Reduce the number of includes.

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
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Streams.h"

#include <functional>

namespace clang {

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
  typedef llvm::ImmutableMap<Expr*,RVal>                   ExprBindingsTy;
  typedef llvm::ImmutableMap<VarDecl*,RVal>                VarBindingsTy;  
  typedef llvm::ImmutableMap<SymbolID,IntSetTy>            ConstNotEqTy;
  typedef llvm::ImmutableMap<SymbolID,const llvm::APSInt*> ConstEqTy;

private:

  void operator=(const ValueState& R) const;
  
  // FIXME: Make these private.

public:
  ExprBindingsTy   SubExprBindings;
  ExprBindingsTy   BlockExprBindings;  
  VarBindingsTy    VarBindings;
  ConstNotEqTy     ConstNotEq;
  ConstEqTy        ConstEq;
  
public:
  
  /// This ctor is used when creating the first ValueState object.
  ValueState(ExprBindingsTy  EB,  VarBindingsTy VB,
                 ConstNotEqTy CNE, ConstEqTy  CE)
    : SubExprBindings(EB), 
      BlockExprBindings(EB),
      VarBindings(VB),
      ConstNotEq(CNE),
      ConstEq(CE) {}
  
  /// Copy ctor - We must explicitly define this or else the "Next" ptr
  ///  in FoldingSetNode will also get copied.
  ValueState(const ValueState& RHS)
    : llvm::FoldingSetNode(),
      SubExprBindings(RHS.SubExprBindings),
      BlockExprBindings(RHS.BlockExprBindings),
      VarBindings(RHS.VarBindings),
      ConstNotEq(RHS.ConstNotEq),
      ConstEq(RHS.ConstEq) {} 
  
  /// Profile - Profile the contents of a ValueState object for use
  ///  in a FoldingSet.
  static void Profile(llvm::FoldingSetNodeID& ID, ValueState* V) {
    V->SubExprBindings.Profile(ID);
    V->BlockExprBindings.Profile(ID);
    V->VarBindings.Profile(ID);
    V->ConstNotEq.Profile(ID);
    V->ConstEq.Profile(ID);
  }

  /// Profile - Used to profile the contents of this object for inclusion
  ///  in a FoldingSet.
  void Profile(llvm::FoldingSetNodeID& ID) {
    Profile(ID, this);
  }
  
  // Queries.
  
  bool isNotEqual(SymbolID sym, const llvm::APSInt& V) const;
  const llvm::APSInt* getSymVal(SymbolID sym) const;

  // Iterators.

  typedef VarBindingsTy::iterator vb_iterator;
  vb_iterator vb_begin() const { return VarBindings.begin(); }
  vb_iterator vb_end() const { return VarBindings.end(); }
    
  typedef ExprBindingsTy::iterator seb_iterator;
  seb_iterator seb_begin() const { return SubExprBindings.begin(); }
  seb_iterator seb_end() const { return SubExprBindings.end(); }
  
  typedef ExprBindingsTy::iterator beb_iterator;
  beb_iterator beb_begin() const { return BlockExprBindings.begin(); }
  beb_iterator beb_end() const { return BlockExprBindings.end(); }
  
  typedef ConstNotEqTy::iterator cne_iterator;
  cne_iterator cne_begin() const { return ConstNotEq.begin(); }
  cne_iterator cne_end() const { return ConstNotEq.end(); }
  
  typedef ConstEqTy::iterator ce_iterator;
  ce_iterator ce_begin() const { return ConstEq.begin(); }
  ce_iterator ce_end() const { return ConstEq.end(); }

  void print(std::ostream& Out,
             const char* nl = "\n",
             const char* sep = "") const;
  
  void printStdErr() const { print(*llvm::cerr); }  
  void printDOT(std::ostream& Out) const;
};  
  
template<> struct GRTrait<ValueState*> {
  static inline void* toPtr(ValueState* St)  { return (void*) St; }
  static inline ValueState* toState(void* P) { return (ValueState*) P; }
  static inline void Profile(llvm::FoldingSetNodeID& profile, ValueState* St) {
    ValueState::Profile(profile, St);
  }
};    
  
class ValueStateManager {
private:
  ValueState::IntSetTy::Factory        ISetFactory;
  ValueState::ExprBindingsTy::Factory  EXFactory;
  ValueState::VarBindingsTy::Factory   VBFactory;
  ValueState::ConstNotEqTy::Factory    CNEFactory;
  ValueState::ConstEqTy::Factory       CEFactory;
  
  /// StateSet - FoldingSet containing all the states created for analyzing
  ///  a particular function.  This is used to unique states.
  llvm::FoldingSet<ValueState> StateSet;

  /// ValueMgr - Object that manages the data for all created RVals.
  ValueManager ValMgr;

  /// SymMgr - Object that manages the symbol information.
  SymbolManager SymMgr;

  /// Alloc - A BumpPtrAllocator to allocate states.
  llvm::BumpPtrAllocator& Alloc;
  
private:
  
  ValueState::ExprBindingsTy Remove(ValueState::ExprBindingsTy B, Expr* E) {
    return EXFactory.Remove(B, E);
  }    
    
  ValueState::VarBindingsTy  Remove(ValueState::VarBindingsTy B, VarDecl* V) {
    return VBFactory.Remove(B, V);
  }

  inline ValueState::ExprBindingsTy Remove(const ValueState& V, Expr* E) {
    return Remove(V.BlockExprBindings, E);
  }
  
  inline ValueState::VarBindingsTy Remove(const ValueState& V, VarDecl* D) {
    return Remove(V.VarBindings, D);
  }
                  
  ValueState* BindVar(ValueState* St, VarDecl* D, RVal V);
  ValueState* UnbindVar(ValueState* St, VarDecl* D);  
  
public:  
  ValueStateManager(ASTContext& Ctx, llvm::BumpPtrAllocator& alloc) 
    : ISetFactory(alloc), 
      EXFactory(alloc),
      VBFactory(alloc),
      CNEFactory(alloc),
      CEFactory(alloc),
      ValMgr(Ctx, alloc),
      Alloc(alloc) {}
  
  ValueState* getInitialState();
        
  ValueManager& getValueManager() { return ValMgr; }
  SymbolManager& getSymbolManager() { return SymMgr; }
  
  ValueState* RemoveDeadBindings(ValueState* St, Stmt* Loc, 
                                 const LiveVariables& Liveness);
  
  ValueState* RemoveSubExprBindings(ValueState* St) {
    ValueState NewSt = *St;
    NewSt.SubExprBindings = EXFactory.GetEmptyMap();
    return getPersistentState(NewSt);    
  }
  
  ValueState* SetRVal(ValueState* St, Expr* E, RVal V, bool isBlkExpr,
                      bool Invalidate);
  
  ValueState* SetRVal(ValueState* St, LVal LV, RVal V);

  RVal GetRVal(ValueState* St, Expr* E);
  RVal GetRVal(ValueState* St, LVal LV, QualType T = QualType());    
  RVal GetLVal(ValueState* St, Expr* E);
  
  RVal GetBlkExprRVal(ValueState* St, Expr* Ex);
  
  void BindVar(ValueState& StImpl, VarDecl* D, RVal V);
  ValueState* getPersistentState(ValueState& Impl);
  
  ValueState* AddEQ(ValueState* St, SymbolID sym, const llvm::APSInt& V);
  ValueState* AddNE(ValueState* St, SymbolID sym, const llvm::APSInt& V);
};
  
} // end clang namespace

#endif
