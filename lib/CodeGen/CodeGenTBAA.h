//===--- CodeGenTBAA.h - TBAA information for LLVM CodeGen ------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This is the code that manages TBAA information.
//
//===----------------------------------------------------------------------===//

#ifndef CLANG_CODEGEN_CODEGENTBAA_H
#define CLANG_CODEGEN_CODEGENTBAA_H

#include "llvm/LLVMContext.h"
#include "llvm/ADT/DenseMap.h"

namespace llvm {
  class LLVMContext;
  class MDNode;
}

namespace clang {
  class ASTContext;
  class LangOptions;
  class QualType;
  class Type;

namespace CodeGen {
  class MangleContext;
  class CGRecordLayout;

/// CodeGenTBAA - This class organizes the cross-module state that is used
/// while lowering AST types to LLVM types.
class CodeGenTBAA {
  ASTContext &Context;
  llvm::LLVMContext& VMContext;
  const LangOptions &Features;
  MangleContext &MContext;

  /// MetadataCache - This maps clang::Types to llvm::MDNodes describing them.
  llvm::DenseMap<const Type *, llvm::MDNode *> MetadataCache;

  /// Root - This is the mdnode for the root of the metadata type graph
  /// for this translation unit.
  llvm::MDNode *Root;

  /// Char - This is the mdnode for "char", which is special, and any types
  /// considered to be equivalent to it.
  llvm::MDNode *Char;

  llvm::MDNode *getTBAAInfoForNamedType(llvm::StringRef NameStr,
                                        llvm::MDNode *Parent);

public:
  CodeGenTBAA(ASTContext &Ctx, llvm::LLVMContext &VMContext,
              const LangOptions &Features,
              MangleContext &MContext);
  ~CodeGenTBAA();

  llvm::MDNode *getTBAAInfo(QualType QTy);
};

}  // end namespace CodeGen
}  // end namespace clang

#endif
