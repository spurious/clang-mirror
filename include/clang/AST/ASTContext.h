//===--- ASTContext.h - Context to hold long-lived AST nodes ----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file was developed by Chris Lattner and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the ASTContext interface.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_ASTCONTEXT_H
#define LLVM_CLANG_AST_ASTCONTEXT_H

#include "clang/AST/Type.h"
#include <vector>

namespace llvm {
namespace clang {
  class Preprocessor;
  class TargetInfo;
  
/// ASTContext - This class holds long-lived AST nodes (such as types and
/// decls) that can be referred to throughout the semantic analysis of a file.
class ASTContext {
  // FIXME: This is a stupid data structure.
  std::vector<TypeRef> Types;
public:
  Preprocessor &PP;
  TargetInfo &Target;

  
  // Builtin Types.
  TypeRef VoidTy;
  TypeRef BoolTy;
  TypeRef CharTy;
  TypeRef SignedCharTy, ShortTy, IntTy, LongTy, LongLongTy;
  TypeRef UnsignedCharTy, UnsignedShortTy, UnsignedIntTy, UnsignedLongTy;
  TypeRef UnsignedLongLongTy;
  TypeRef FloatTy, DoubleTy, LongDoubleTy;
  TypeRef FloatComplexTy, DoubleComplexTy, LongDoubleComplexTy;
  
  ASTContext(Preprocessor &pp);
  
  
  /// getPointerType - Return the uniqued reference to the type for a pointer to
  /// the specified type.
  TypeRef getPointerType(const TypeRef &T);
  
private:
  void InitBuiltinTypes();
};
  
}  // end namespace clang
}  // end namespace llvm

#endif
