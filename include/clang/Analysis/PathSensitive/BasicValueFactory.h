//=== BasicValueFactory.h - Basic values for Path Sens analysis --*- C++ -*---//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines BasicValueFactory, a class that manages the lifetime
//  of APSInt objects and symbolic constraints used by GRExprEngine 
//  and related classes.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_ANALYSIS_BASICVALUEFACTORY_H
#define LLVM_CLANG_ANALYSIS_BASICVALUEFACTORY_H

#include "clang/Analysis/PathSensitive/SymbolManager.h"
#include "clang/Analysis/PathSensitive/SVals.h"
#include "clang/AST/ASTContext.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/ImmutableList.h"

namespace clang {
  
class CompoundValData : public llvm::FoldingSetNode {
  QualType T;
  llvm::ImmutableList<SVal> L;

public:
  CompoundValData(QualType t, llvm::ImmutableList<SVal> l) 
    : T(t), L(l) {}

  typedef llvm::ImmutableList<SVal>::iterator iterator;
  iterator begin() const { return L.begin(); }
  iterator end() const { return L.end(); }  
  
  static void Profile(llvm::FoldingSetNodeID& ID, QualType T,
                      llvm::ImmutableList<SVal> L);

  void Profile(llvm::FoldingSetNodeID& ID) { Profile(ID, T, L); }
};

class BasicValueFactory {
  typedef llvm::FoldingSet<llvm::FoldingSetNodeWrapper<llvm::APSInt> >
          APSIntSetTy;

  typedef llvm::FoldingSet<SymIntConstraint>
          SymIntCSetTy;
  

  ASTContext& Ctx;
  llvm::BumpPtrAllocator& BPAlloc;

  APSIntSetTy   APSIntSet;
  SymIntCSetTy  SymIntCSet;
  void*         PersistentSVals;
  void*         PersistentSValPairs;

  llvm::ImmutableList<SVal>::Factory SValListFactory;
  llvm::FoldingSet<CompoundValData>  CompoundValDataSet;

public:
  BasicValueFactory(ASTContext& ctx, llvm::BumpPtrAllocator& Alloc) 
  : Ctx(ctx), BPAlloc(Alloc), PersistentSVals(0), PersistentSValPairs(0),
    SValListFactory(Alloc) {}

  ~BasicValueFactory();

  ASTContext& getContext() const { return Ctx; }  

  const llvm::APSInt& getValue(const llvm::APSInt& X);
  const llvm::APSInt& getValue(const llvm::APInt& X, bool isUnsigned);
  const llvm::APSInt& getValue(uint64_t X, unsigned BitWidth, bool isUnsigned);
  const llvm::APSInt& getValue(uint64_t X, QualType T);
  
  /// Convert - Create a new persistent APSInt with the same value as 'From'
  ///  but with the bitwidth and signeness of 'To'.
  const llvm::APSInt& Convert(const llvm::APSInt& To,
                              const llvm::APSInt& From) {
    
    if (To.isUnsigned() == From.isUnsigned() &&
        To.getBitWidth() == From.getBitWidth())
      return From;
    
    return getValue(From.getSExtValue(),
                    To.getBitWidth(),
                    To.isUnsigned());
  }

  const llvm::APSInt& getIntValue(uint64_t X, bool isUnsigned) {
    QualType T = isUnsigned ? Ctx.UnsignedIntTy : Ctx.IntTy;
    return getValue(X, T);
  }
  
  inline const llvm::APSInt& getMaxValue(const llvm::APSInt &v) {
    return getValue(llvm::APSInt::getMaxValue(v.getBitWidth(), v.isUnsigned()));
  }
  
  inline const llvm::APSInt& getMinValue(const llvm::APSInt &v) {
    return getValue(llvm::APSInt::getMinValue(v.getBitWidth(), v.isUnsigned()));
  }

  inline const llvm::APSInt& getMaxValue(QualType T) {
    assert(T->isIntegerType() || Loc::IsLocType(T));
    bool isUnsigned = T->isUnsignedIntegerType() || Loc::IsLocType(T);
    return getValue(llvm::APSInt::getMaxValue(Ctx.getTypeSize(T), isUnsigned));
  }
  
  inline const llvm::APSInt& getMinValue(QualType T) {
    assert(T->isIntegerType() || Loc::IsLocType(T));
    bool isUnsigned = T->isUnsignedIntegerType() || Loc::IsLocType(T);
    return getValue(llvm::APSInt::getMinValue(Ctx.getTypeSize(T), isUnsigned));
  }
  
  inline const llvm::APSInt& Add1(const llvm::APSInt& V) {
    llvm::APSInt X = V;
    ++X;
    return getValue(X);
  }
  
  inline const llvm::APSInt& Sub1(const llvm::APSInt& V) {
    llvm::APSInt X = V;
    --X;
    return getValue(X);
  }
  
  inline const llvm::APSInt& getZeroWithPtrWidth(bool isUnsigned = true) {
    return getValue(0, Ctx.getTypeSize(Ctx.VoidPtrTy), isUnsigned);
  }

  inline const llvm::APSInt& getTruthValue(bool b) {
    return getValue(b ? 1 : 0, Ctx.getTypeSize(Ctx.IntTy), false);
  }
  
  const SymIntConstraint& getConstraint(SymbolRef sym, BinaryOperator::Opcode Op,
                                        const llvm::APSInt& V);

  const CompoundValData* getCompoundValData(QualType T, 
                                            llvm::ImmutableList<SVal> Vals);
  
  llvm::ImmutableList<SVal> getEmptySValList() {
    return SValListFactory.GetEmptyList();
  }
  
  llvm::ImmutableList<SVal> consVals(SVal X, llvm::ImmutableList<SVal> L) {
    return SValListFactory.Add(X, L);
  }

  const llvm::APSInt* EvaluateAPSInt(BinaryOperator::Opcode Op,
                                     const llvm::APSInt& V1,
                                     const llvm::APSInt& V2);
  
  const std::pair<SVal, uintptr_t>&
  getPersistentSValWithData(const SVal& V, uintptr_t Data);
  
  const std::pair<SVal, SVal>&
  getPersistentSValPair(const SVal& V1, const SVal& V2);  
  
  const SVal* getPersistentSVal(SVal X);
};

} // end clang namespace

#endif
