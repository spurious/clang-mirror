//== SMTSort.h --------------------------------------------------*- C++ -*--==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines a SMT generic Sort API, which will be the base class
//  for every SMT solver sort specific class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_SMTSORT_H
#define LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_SMTSORT_H

#include "clang/Basic/TargetInfo.h"

namespace clang {
namespace ento {

/// Generic base class for SMT sorts
class SMTSort {
public:
  SMTSort() = default;
  virtual ~SMTSort() = default;

  /// Returns true if the sort is a bitvector, calls isBitvectorSortImpl().
  virtual bool isBitvectorSort() const { return isBitvectorSortImpl(); }

  /// Returns true if the sort is a floating-point, calls isFloatSortImpl().
  virtual bool isFloatSort() const { return isFloatSortImpl(); }

  /// Returns true if the sort is a boolean, calls isBooleanSortImpl().
  virtual bool isBooleanSort() const { return isBooleanSortImpl(); }

  /// Returns the bitvector size, fails if the sort is not a bitvector
  /// Calls getBitvectorSortSizeImpl().
  virtual unsigned getBitvectorSortSize() const {
    assert(isBitvectorSort() && "Not a bitvector sort!");
    unsigned Size = getBitvectorSortSizeImpl();
    assert(Size && "Size is zero!");
    return Size;
  };

  /// Returns the floating-point size, fails if the sort is not a floating-point
  /// Calls getFloatSortSizeImpl().
  virtual unsigned getFloatSortSize() const {
    assert(isFloatSort() && "Not a floating-point sort!");
    unsigned Size = getFloatSortSizeImpl();
    assert(Size && "Size is zero!");
    return Size;
  };

  virtual void Profile(llvm::FoldingSetNodeID &ID) const = 0;

  bool operator<(const SMTSort &Other) const {
    llvm::FoldingSetNodeID ID1, ID2;
    Profile(ID1);
    Other.Profile(ID2);
    return ID1 < ID2;
  }

  friend bool operator==(SMTSort const &LHS, SMTSort const &RHS) {
    return LHS.equal_to(RHS);
  }

  virtual void print(raw_ostream &OS) const = 0;

  LLVM_DUMP_METHOD void dump() const { print(llvm::errs()); }

protected:
  /// Query the SMT solver and returns true if two sorts are equal (same kind
  /// and bit width). This does not check if the two sorts are the same objects.
  virtual bool equal_to(SMTSort const &other) const = 0;

  /// Query the SMT solver and checks if a sort is bitvector.
  virtual bool isBitvectorSortImpl() const = 0;

  /// Query the SMT solver and checks if a sort is floating-point.
  virtual bool isFloatSortImpl() const = 0;

  /// Query the SMT solver and checks if a sort is boolean.
  virtual bool isBooleanSortImpl() const = 0;

  /// Query the SMT solver and returns the sort bit width.
  virtual unsigned getBitvectorSortSizeImpl() const = 0;

  /// Query the SMT solver and returns the sort bit width.
  virtual unsigned getFloatSortSizeImpl() const = 0;
};

/// Shared pointer for SMTSorts, used by SMTSolver API.
using SMTSortRef = const SMTSort *;

} // namespace ento
} // namespace clang

#endif
