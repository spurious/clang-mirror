//=- ExprDeclBitVector.h - Dataflow types for Bitvector Analysis --*- C++ --*-//
//
//                     The LLVM Compiler Infrastructure
//
// This file was developed by Ted Kremenek and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file provides definition of dataflow types used by analyses such
// as LiveVariables and UninitializedValues.  The underlying dataflow values
// are implemented as bitvectors, but the definitions in this file include
// the necessary boilerplate to use with our dataflow framework.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_EXPRDECLBVDVAL_H
#define LLVM_CLANG_EXPRDECLBVDVAL_H

#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/DenseMap.h"

namespace clang {
  
  class Expr;
  class ScopedDecl;
  
struct DeclBitVector_Types {
  
  //===--------------------------------------------------------------------===//
  // AnalysisDataTy - Whole-function meta data.
  //===--------------------------------------------------------------------===//
  
  class AnalysisDataTy {
  public:
    typedef llvm::DenseMap<const ScopedDecl*, unsigned > DMapTy;
    typedef DMapTy::const_iterator decl_iterator;
    
  protected:
    DMapTy DMap;    
    unsigned NDecls;
    
  public:
    
    AnalysisDataTy() : NDecls(0) {}
    virtual ~AnalysisDataTy() {}
    
    bool isTracked(const ScopedDecl* SD) { return DMap.find(SD) != DMap.end(); }
    
    unsigned getIdx(const ScopedDecl* SD) const {
      DMapTy::const_iterator I = DMap.find(SD);
      assert (I != DMap.end());
      return I->second;
    }

    unsigned getNumDecls() const { return NDecls; }
    
    void Register(const ScopedDecl* SD) {
      if (!isTracked(SD)) DMap[SD] = NDecls++;
    }

    decl_iterator begin_decl() const { return DMap.begin(); }
    decl_iterator end_decl() const { return DMap.end(); }
  };
  
  //===--------------------------------------------------------------------===//
  // ValTy - Dataflow value.
  //===--------------------------------------------------------------------===//
  
  class ValTy {
    llvm::BitVector DeclBV;
  public:
    
    void resetValues(AnalysisDataTy& AD) {
      DeclBV.resize(AD.getNumDecls()); 
      DeclBV.reset();
    }
    
    bool operator==(const ValTy& RHS) const { 
      assert (sizesEqual(RHS));
      return DeclBV == RHS.DeclBV;
    }
    
    void copyValues(const ValTy& RHS) { DeclBV = RHS.DeclBV; }
    
    llvm::BitVector::reference
    operator()(const ScopedDecl* SD, const AnalysisDataTy& AD) {
      return DeclBV[AD.getIdx(SD)];      
    }
    const llvm::BitVector::reference
    operator()(const ScopedDecl* SD, const AnalysisDataTy& AD) const {
      return const_cast<ValTy&>(*this)(SD,AD);
    }
    
    llvm::BitVector::reference getDeclBit(unsigned i) { return DeclBV[i]; }    
    const llvm::BitVector::reference getDeclBit(unsigned i) const {
      return const_cast<llvm::BitVector&>(DeclBV)[i];
    }
    
    ValTy& operator|=(const ValTy& RHS) {
      assert (sizesEqual(RHS));
      DeclBV |= RHS.DeclBV;
      return *this;
    }
    
    ValTy& operator&=(const ValTy& RHS) {
      assert (sizesEqual(RHS));
      DeclBV &= RHS.DeclBV;
      return *this;
    }
    
    bool sizesEqual(const ValTy& RHS) const {
      return DeclBV.size() == RHS.DeclBV.size();
    }
  };
  
  //===--------------------------------------------------------------------===//
  // Some useful merge operations.
  //===--------------------------------------------------------------------===//
    
  struct Union { void operator()(ValTy& Dst, ValTy& Src) { Dst |= Src; } };
  struct Intersect { void operator()(ValTy& Dst, ValTy& Src) { Dst &= Src; } };
};


struct ExprDeclBitVector_Types {
  
  //===--------------------------------------------------------------------===//
  // AnalysisDataTy - Whole-function meta data.
  //===--------------------------------------------------------------------===//

  class AnalysisDataTy : public DeclBitVector_Types::AnalysisDataTy {
  public:
    typedef llvm::DenseMap<const Expr*, unsigned > EMapTy;    
    typedef EMapTy::const_iterator expr_iterator;

  protected:
    EMapTy EMap;
    unsigned NExprs;

  public:
    
    AnalysisDataTy() : NExprs(0) {}
    virtual ~AnalysisDataTy() {}
    
    bool isTracked(const Expr* E) { return EMap.find(E) != EMap.end(); }
    using DeclBitVector_Types::AnalysisDataTy::isTracked;

    unsigned getIdx(const Expr* E) const {
      EMapTy::const_iterator I = EMap.find(E);
      assert (I != EMap.end());
      return I->second;
    }    
    using DeclBitVector_Types::AnalysisDataTy::getIdx;
    
    unsigned getNumExprs() const { return NExprs; }
    
    void Register(const Expr* E) { if (!isTracked(E)) EMap[E] = NExprs++; }    
    using DeclBitVector_Types::AnalysisDataTy::Register;
    
    expr_iterator begin_expr() const { return EMap.begin(); }
    expr_iterator end_expr() const { return EMap.end(); }
  };

  //===--------------------------------------------------------------------===//
  // ValTy - Dataflow value.
  //===--------------------------------------------------------------------===//

  class ValTy : public DeclBitVector_Types::ValTy {
    llvm::BitVector ExprBV;
    typedef DeclBitVector_Types::ValTy ParentTy;
    
    static inline ParentTy& ParentRef(ValTy& X) {
      return static_cast<ParentTy&>(X);
    }
    
    static inline const ParentTy& ParentRef(const ValTy& X) {
      return static_cast<const ParentTy&>(X);
    }
    
  public:
    
    void resetValues(AnalysisDataTy& AD) {
      ParentRef(*this).resetValues(AD);
      ExprBV.resize(AD.getNumExprs());
      ExprBV.reset();
    }
    
    bool operator==(const ValTy& RHS) const { 
      return ParentRef(*this) == ParentRef(RHS) 
          && ExprBV == RHS.ExprBV;
    }
    
    void copyValues(const ValTy& RHS) {
      ParentRef(*this).copyValues(ParentRef(RHS));
      ExprBV = RHS.ExprBV;
    }
        
    llvm::BitVector::reference
    operator()(const Expr* E, const AnalysisDataTy& AD) {
      return ExprBV[AD.getIdx(E)];      
    }    
    const llvm::BitVector::reference
    operator()(const Expr* E, const AnalysisDataTy& AD) const {
      return const_cast<ValTy&>(*this)(E,AD);
    }
    
    using DeclBitVector_Types::ValTy::operator();

    
    llvm::BitVector::reference getExprBit(unsigned i) { return ExprBV[i]; }    
    const llvm::BitVector::reference getExprBit(unsigned i) const {
      return const_cast<llvm::BitVector&>(ExprBV)[i];
    }
    
    ValTy& operator|=(const ValTy& RHS) {
      assert (sizesEqual(RHS));
      ParentRef(*this) |= ParentRef(RHS);
      ExprBV |= RHS.ExprBV;
      return *this;
    }
    
    ValTy& operator&=(const ValTy& RHS) {
      assert (sizesEqual(RHS));
      ParentRef(*this) &= ParentRef(RHS);
      ExprBV &= RHS.ExprBV;
      return *this;
    }
    
    bool sizesEqual(const ValTy& RHS) const {
      return ParentRef(*this).sizesEqual(ParentRef(RHS))
          && ExprBV.size() == RHS.ExprBV.size();
    }
  };
  
  //===--------------------------------------------------------------------===//
  // Some useful merge operations.
  //===--------------------------------------------------------------------===//
  
  struct Union { void operator()(ValTy& Dst, ValTy& Src) { Dst |= Src; } };
  struct Intersect { void operator()(ValTy& Dst, ValTy& Src) { Dst &= Src; } };
  
};
} // end namespace clang

#endif
