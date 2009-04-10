//===--- PCHWriter.h - Precompiled Headers Writer ---------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the PCHWriter class, which writes a precompiled
//  header containing a serialized representation of a translation
//  unit.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_CLANG_FRONTEND_PCH_WRITER_H
#define LLVM_CLANG_FRONTEND_PCH_WRITER_H

#include "clang/AST/Decl.h"
#include "clang/AST/DeclarationName.h"
#include "clang/Frontend/PCHBitCodes.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include <queue>

namespace llvm {
  class APInt;
  class BitstreamWriter;
}

namespace clang {

class SourceManager;
class Preprocessor;
class ASTContext;

/// \brief Writes a precompiled header containing the contents of a
/// translation unit.
///
/// The PCHWriter class produces a bitstream containing the serialized
/// representation of a given abstract syntax tree and its supporting
/// data structures. This bitstream can be de-serialized via an
/// instance of the PCHReader class.
class PCHWriter {
  /// \brief The bitstream writer used to emit this precompiled header.
  llvm::BitstreamWriter &S;

  /// \brief Map that provides the ID numbers of each declaration within
  /// the output stream.
  ///
  /// The ID numbers of declarations are consecutive (in order of
  /// discovery) and start at 2. 1 is reserved for the translation
  /// unit, while 0 is reserved for NULL.
  llvm::DenseMap<const Decl *, pch::ID> DeclIDs;

  /// \brief Offset of each declaration in the bitstream, indexed by
  /// the declaration's ID.
  llvm::SmallVector<uint64_t, 16> DeclOffsets;

  /// \brief Queue containing the declarations that we still need to
  /// emit.
  std::queue<Decl *> DeclsToEmit;

  /// \brief Map that provides the ID numbers of each type within the
  /// output stream.
  ///
  /// The ID numbers of types are consecutive (in order of discovery)
  /// and start at 1. 0 is reserved for NULL. When types are actually
  /// stored in the stream, the ID number is shifted by 3 bits to
  /// allow for the const/volatile/restrict qualifiers.
  llvm::DenseMap<const Type *, pch::ID> TypeIDs;

  /// \brief Offset of each type in the bitstream, indexed by
  /// the type's ID.
  llvm::SmallVector<uint64_t, 16> TypeOffsets;

  /// \brief The type ID that will be assigned to the next new type.
  unsigned NextTypeID;

  void WriteSourceManagerBlock(SourceManager &SourceMgr);
  void WritePreprocessor(Preprocessor &PP);
  void WriteType(const Type *T);
  void WriteTypesBlock(ASTContext &Context);
  uint64_t WriteDeclContextLexicalBlock(ASTContext &Context, DeclContext *DC);
  uint64_t WriteDeclContextVisibleBlock(ASTContext &Context, DeclContext *DC);
  void WriteDeclsBlock(ASTContext &Context);

public:
  typedef llvm::SmallVector<uint64_t, 64> RecordData;

  /// \brief Create a new precompiled header writer that outputs to
  /// the given bitstream.
  PCHWriter(llvm::BitstreamWriter &S);
  
  /// \brief Write a precompiled header for the given AST context.
  void WritePCH(ASTContext &Context, Preprocessor &PP);

  /// \brief Emit a source location.
  void AddSourceLocation(SourceLocation Loc, RecordData &Record);

  /// \brief Emit an integral value.
  void AddAPInt(const llvm::APInt &Value, RecordData &Record);

  /// \brief Emit a reference to an identifier
  void AddIdentifierRef(const IdentifierInfo *II, RecordData &Record);

  /// \brief Emit a reference to a type.
  void AddTypeRef(QualType T, RecordData &Record);

  /// \brief Emit a reference to a declaration.
  void AddDeclRef(const Decl *D, RecordData &Record);

  /// \brief Emit a declaration name.
  void AddDeclarationName(DeclarationName Name, RecordData &Record);
};

} // end namespace clang

#endif
