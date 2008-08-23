//==- Regions.h - Abstract memory locations ------------------------*- C++ -*-//
//             
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines Region and its subclasses.  Regions represent abstract
//  memory locations.
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/DataTypes.h"
#include "llvm/Support/Casting.h"
#include "llvm/ADT/FoldingSet.h"

#ifndef LLVM_CLANG_ANALYSIS_REGIONS_H
#define LLVM_CLANG_ANALYSIS_REGIONS_H

namespace clang {
  
class Region {
public:
  enum Kind { Var = 0x0, Anon = 0x1 };
  
private:
  uintptr_t Raw;
  
protected:
  Region(const void* data, Kind kind)
    : Raw((uintptr_t) data | (uintptr_t) kind) {
      assert ((reinterpret_cast<uintptr_t>(const_cast<void*>(data)) & 0x1) == 0
              && "Address must have at least a 2-byte alignment."); 
    }
  
  const void* getData() const { return (const void*) (Raw & ~0x1); }
  
public:  
  // Folding-set profiling.
  void Profile(llvm::FoldingSetNodeID& ID) const { ID.AddPointer((void*) Raw); }

  // Comparing regions.
  bool operator==(const Region& R) const { return Raw == R.Raw; }
  bool operator!=(const Region& R) const { return Raw != R.Raw; }

  // Implement isa<T> support.
  Kind getKind() const { return Kind (Raw & 0x1); }
  static inline bool classof(const Region*) { return true; }
};
  
//===----------------------------------------------------------------------===//
// Region Types.
//===----------------------------------------------------------------------===//
  
class VarRegion : public Region {
public:
  VarRegion(VarDecl* VD) : Region(VD, Region::Var) {}
  
  /// getDecl - Return the declaration of the variable the region represents.
  const VarDecl* getDecl() const { return (const VarDecl*) getData(); }  
  operator const VarDecl*() const { return getDecl(); }
  
  // Implement isa<T> support.
  static inline bool classof(const Region* R) {
    return R->getKind() == Region::Var;
  }
  
  static inline bool classof(const VarRegion*) {
    return true;
  }
};

class AnonRegion : public Region {
protected:
  friend class Region;
  
  AnonRegion(uintptr_t RegionID) : Region((void*) (RegionID<<1), Region::Anon) {
    assert (((RegionID << 1) >> 1) == RegionID);
  }
  
public:
  
  uintptr_t getID() const { return ((uintptr_t) getData()) >> 1; }
  
  // Implement isa<T> support.
  static inline bool classof(const Region* R) {
    return R->getKind() == Region::Anon;
  }
  
  static inline bool classof(const AnonRegion*) {
    return true;
  }
};
  
} // end clang namespace

#endif
