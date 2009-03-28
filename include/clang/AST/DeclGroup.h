//===--- DeclGroup.h - Classes for representing groups of Decls -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the DeclGroup, DeclGroupRef, and OwningDeclGroup classes.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_DECLGROUP_H
#define LLVM_CLANG_AST_DECLGROUP_H

#include "llvm/Support/DataTypes.h"
#include "llvm/Bitcode/SerializationFwd.h"
#include <cassert>

namespace clang {
  
class ASTContext;
class Decl;
class DeclGroup;
class DeclGroupIterator;

class DeclGroup {
  // FIXME: Include a TypeSpecifier object.
  unsigned NumDecls;
  
private:
  DeclGroup() : NumDecls(0) {}
  DeclGroup(unsigned numdecls, Decl** decls);

public:
  static DeclGroup* Create(ASTContext& C, unsigned numdecls, Decl** decls);
  void Destroy(ASTContext& C);

  unsigned size() const { return NumDecls; }

  Decl*& operator[](unsigned i) { 
    assert (i < NumDecls && "Out-of-bounds access.");
    return *((Decl**) (this+1));
  }
  
  Decl* const& operator[](unsigned i) const { 
    assert (i < NumDecls && "Out-of-bounds access.");
    return *((Decl* const*) (this+1));
  }
  
  /// Emit - Serialize a DeclGroup to Bitcode.
  void Emit(llvm::Serializer& S) const;
  
  /// Read - Deserialize a DeclGroup from Bitcode.
  static DeclGroup* Read(llvm::Deserializer& D, ASTContext& C);
};
    
class DeclGroupRef {
protected:
  enum Kind { SingleDeclKind=0x0, DeclGroupKind=0x1, Mask=0x1 };  
  Decl* D;

  Kind getKind() const {
    return (Kind) (reinterpret_cast<uintptr_t>(D) & Mask);
  }  
  
public:    
  DeclGroupRef() : D(0) {}
  
  explicit DeclGroupRef(Decl* d) : D(d) {}
  explicit DeclGroupRef(DeclGroup* dg)
    : D((Decl*) (reinterpret_cast<uintptr_t>(dg) | DeclGroupKind)) {}
  
  typedef Decl** iterator;
  typedef Decl* const * const_iterator;
  
  bool isSingleDecl() const { return getKind() == SingleDeclKind; }
  bool isDeclGroup() const { return getKind() == DeclGroupKind; }

  Decl *getSingleDecl() {
    assert(isSingleDecl() && "Isn't a declgroup");
    return D;
  }
  const Decl *getSingleDecl() const {
    return const_cast<DeclGroupRef*>(this)->getSingleDecl();
  }
  
  DeclGroup &getDeclGroup() {
    assert(isDeclGroup() && "Isn't a declgroup");
    return *((DeclGroup*)(reinterpret_cast<uintptr_t>(D) & ~Mask));
  }
  const DeclGroup &getDeclGroup() const {
    return const_cast<DeclGroupRef*>(this)->getDeclGroup();
  }
  

  iterator begin() {
    if (isSingleDecl())
      return D ? &D : 0;
    return &getDeclGroup()[0];
  }

  iterator end() {
    if (isSingleDecl())
      return D ? &D+1 : 0;
    DeclGroup &G = getDeclGroup();
    return &G[0] + G.size();
  }
  
  const_iterator begin() const {
    if (isSingleDecl())
      return D ? &D : 0;
    return &getDeclGroup()[0];
  }
  
  const_iterator end() const {
    if (isSingleDecl())
      return D ? &D+1 : 0;
    const DeclGroup &G = getDeclGroup();
    return &G[0] + G.size();
  }

  /// Emit - Serialize a DeclGroupRef to Bitcode.
  void Emit(llvm::Serializer& S) const;
  
  /// Read - Deserialize a DeclGroupRef from Bitcode.
  static DeclGroupRef ReadVal(llvm::Deserializer& D);
};
  
} // end clang namespace
#endif
