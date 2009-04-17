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
#include <map>
#include <queue>

namespace llvm {
  class APFloat;
  class APInt;
  class BitstreamWriter;
}

namespace clang {

class ASTContext;
class Preprocessor;
class SourceManager;
class SwitchCase;
class TargetInfo;

/// \brief Writes a precompiled header containing the contents of a
/// translation unit.
///
/// The PCHWriter class produces a bitstream containing the serialized
/// representation of a given abstract syntax tree and its supporting
/// data structures. This bitstream can be de-serialized via an
/// instance of the PCHReader class.
class PCHWriter {
public:
  typedef llvm::SmallVector<uint64_t, 64> RecordData;

private:
  /// \brief The bitstream writer used to emit this precompiled header.
  llvm::BitstreamWriter &Stream;

  /// \brief Map that provides the ID numbers of each declaration within
  /// the output stream.
  ///
  /// The ID numbers of declarations are consecutive (in order of
  /// discovery) and start at 2. 1 is reserved for the translation
  /// unit, while 0 is reserved for NULL.
  llvm::DenseMap<const Decl *, pch::DeclID> DeclIDs;

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
  llvm::DenseMap<const Type *, pch::TypeID> TypeIDs;

  /// \brief Offset of each type in the bitstream, indexed by
  /// the type's ID.
  llvm::SmallVector<uint64_t, 16> TypeOffsets;

  /// \brief The type ID that will be assigned to the next new type.
  pch::TypeID NextTypeID;

  /// \brief Map that provides the ID numbers of each identifier in
  /// the output stream.
  ///
  /// The ID numbers for identifiers are consecutive (in order of
  /// discovery), starting at 1. An ID of zero refers to a NULL
  /// IdentifierInfo.
  llvm::DenseMap<const IdentifierInfo *, pch::IdentID> IdentifierIDs;

  /// \brief Declarations encountered that might be external
  /// definitions.
  ///
  /// We keep track of external definitions (as well as tentative
  /// definitions) as we are emitting declarations to the PCH
  /// file. The PCH file contains a separate record for these external
  /// definitions, which are provided to the AST consumer by the PCH
  /// reader. This is behavior is required to properly cope with,
  /// e.g., tentative variable definitions that occur within
  /// headers. The declarations themselves are stored as declaration
  /// IDs, since they will be written out to an EXTERNAL_DEFINITIONS
  /// record.
  llvm::SmallVector<uint64_t, 16> ExternalDefinitions;

  /// \brief Statements that we've encountered while serializing a
  /// declaration or type.
  llvm::SmallVector<Stmt *, 8> StmtsToEmit;

  /// \brief Mapping from SwitchCase statements to IDs.
  std::map<SwitchCase *, unsigned> SwitchCaseIDs;
  
  void WriteTargetTriple(const TargetInfo &Target);
  void WriteLanguageOptions(const LangOptions &LangOpts);
  void WriteSourceManagerBlock(SourceManager &SourceMgr);
  void WritePreprocessor(const Preprocessor &PP);
  void WriteType(const Type *T);
  void WriteTypesBlock(ASTContext &Context);
  uint64_t WriteDeclContextLexicalBlock(ASTContext &Context, DeclContext *DC);
  uint64_t WriteDeclContextVisibleBlock(ASTContext &Context, DeclContext *DC);
  void WriteDeclsBlock(ASTContext &Context);
  void WriteIdentifierTable();
  void WriteAttributeRecord(const Attr *Attr);

  void AddString(const std::string &Str, RecordData &Record);

public:
  /// \brief Create a new precompiled header writer that outputs to
  /// the given bitstream.
  PCHWriter(llvm::BitstreamWriter &Stream);
  
  /// \brief Write a precompiled header for the given AST context.
  void WritePCH(ASTContext &Context, const Preprocessor &PP);

  /// \brief Emit a source location.
  void AddSourceLocation(SourceLocation Loc, RecordData &Record);

  /// \brief Emit an integral value.
  void AddAPInt(const llvm::APInt &Value, RecordData &Record);

  /// \brief Emit a signed integral value.
  void AddAPSInt(const llvm::APSInt &Value, RecordData &Record);

  /// \brief Emit a floating-point value.
  void AddAPFloat(const llvm::APFloat &Value, RecordData &Record);

  /// \brief Emit a reference to an identifier
  void AddIdentifierRef(const IdentifierInfo *II, RecordData &Record);

  /// \brief Emit a reference to a type.
  void AddTypeRef(QualType T, RecordData &Record);

  /// \brief Emit a reference to a declaration.
  void AddDeclRef(const Decl *D, RecordData &Record);

  /// \brief Emit a declaration name.
  void AddDeclarationName(DeclarationName Name, RecordData &Record);

  /// \brief Add the given statement or expression to the queue of statements to
  /// emit.
  ///
  /// This routine should be used when emitting types and declarations
  /// that have expressions as part of their formulation. Once the
  /// type or declaration has been written, call FlushStmts() to write
  /// the corresponding statements just after the type or
  /// declaration.
  void AddStmt(Stmt *S) { StmtsToEmit.push_back(S); }

  /// \brief Write the given subexpression to the bitstream.
  void WriteSubStmt(Stmt *S);

  /// \brief Flush all of the statements and expressions that have
  /// been added to the queue via AddStmt().
  void FlushStmts();

  /// \brief Record an ID for the given switch-case statement.
  unsigned RecordSwitchCaseID(SwitchCase *S);

  /// \brief Retrieve the ID for the given switch-case statement.
  unsigned getSwitchCaseID(SwitchCase *S);

};

} // end namespace clang

#endif
