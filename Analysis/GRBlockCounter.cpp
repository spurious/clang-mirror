//==- GRBlockCounter.h - ADT for counting block visits -------------*- C++ -*-//
//             
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines GRBlockCounter, an abstract data type used to count
//  the number of times a given block has been visited along a path
//  analyzed by GREngine.
//
//===----------------------------------------------------------------------===//

#include "clang/Analysis/PathSensitive/GRBlockCounter.h"
#include "llvm/ADT/ImmutableMap.h"

using namespace clang;

typedef llvm::ImmutableMap<unsigned,unsigned> CountMap;

static inline CountMap GetMap(void* D) {
  return CountMap(static_cast<CountMap::TreeTy*>(D));
}

static inline CountMap::Factory& GetFactory(void* F) {
  return *static_cast<CountMap::Factory*>(F);
}

unsigned GRBlockCounter::getNumVisited(unsigned BlockID) const {
  CountMap M = GetMap(Data);
  CountMap::TreeTy* T = M.SlimFind(BlockID);
  return T ? T->getValue().second : 0;
}

GRBlockCounter::Factory::Factory(llvm::BumpPtrAllocator& Alloc) {
  F = new CountMap::Factory(Alloc);
}

GRBlockCounter::Factory::~Factory() {
  delete static_cast<CountMap*>(F);
}

GRBlockCounter
GRBlockCounter::Factory::IncrementCount(GRBlockCounter BC, unsigned BlockID) {
  return GRBlockCounter(GetFactory(F).Add(GetMap(BC.Data), BlockID,
                                        BC.getNumVisited(BlockID)+1).getRoot());
}

GRBlockCounter
GRBlockCounter::Factory::GetEmptyCounter() {
  return GRBlockCounter(GetFactory(F).GetEmptyMap().getRoot());
}
