// ValueManager.h - Low-level value management for Value Tracking -*- C++ -*--==
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This files defines ValueManager, a class that manages the lifetime of APSInt
//  objects and symbolic constraints used by GRExprEngine and related classes.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_ANALYSIS_VALUEMANAGER_H
#define LLVM_CLANG_ANALYSIS_VALUEMANAGER_H

#include "clang/Analysis/PathSensitive/SymbolManager.h"
#include "clang/AST/ASTContext.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/APSInt.h"

namespace llvm {
  class BumpPtrAllocator;
}

namespace clang {
  
class ValueManager {
  typedef llvm::FoldingSet<llvm::FoldingSetNodeWrapper<llvm::APSInt> >
          APSIntSetTy;

  typedef llvm::FoldingSet<SymIntConstraint>
          SymIntCSetTy;

  ASTContext& Ctx;
  llvm::BumpPtrAllocator& BPAlloc;

  APSIntSetTy   APSIntSet;
  SymIntCSetTy  SymIntCSet;

public:
  ValueManager(ASTContext& ctx, llvm::BumpPtrAllocator& Alloc) 
  : Ctx(ctx), BPAlloc(Alloc) {}

  ~ValueManager();

  ASTContext& getContext() const { return Ctx; }  

  const llvm::APSInt& getValue(const llvm::APSInt& X);
  const llvm::APSInt& getValue(uint64_t X, unsigned BitWidth, bool isUnsigned);
  const llvm::APSInt& getValue(uint64_t X, QualType T,
                               SourceLocation Loc = SourceLocation());

  inline const llvm::APSInt& getZeroWithPtrWidth() {
    return getValue(0, Ctx.getTypeSize(Ctx.VoidPtrTy, SourceLocation()), true);
  }

  inline const llvm::APSInt& getTruthValue(bool b) {
    return getValue(b ? 1 : 0,
                    Ctx.getTypeSize(Ctx.IntTy, SourceLocation()),
                    false);
  }

  const SymIntConstraint& getConstraint(SymbolID sym, BinaryOperator::Opcode Op,
                                        const llvm::APSInt& V);

  const llvm::APSInt* EvaluateAPSInt(BinaryOperator::Opcode Op,
                                     const llvm::APSInt& V1,
                                     const llvm::APSInt& V2);
};

} // end clang namespace

#endif
