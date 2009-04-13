//===--- PCHReader.cpp - Precompiled Headers Reader -------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the PCHReader class, which reads a precompiled header.
//
//===----------------------------------------------------------------------===//
#include "clang/Frontend/PCHReader.h"
#include "clang/Frontend/FrontendDiagnostic.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Type.h"
#include "clang/Lex/MacroInfo.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/SourceManagerInternals.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/TargetInfo.h"
#include "llvm/Bitcode/BitstreamReader.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/MemoryBuffer.h"
#include <algorithm>
#include <cstdio>

using namespace clang;

//===----------------------------------------------------------------------===//
// Declaration deserialization
//===----------------------------------------------------------------------===//
namespace {
  class VISIBILITY_HIDDEN PCHDeclReader {
    PCHReader &Reader;
    const PCHReader::RecordData &Record;
    unsigned &Idx;

  public:
    PCHDeclReader(PCHReader &Reader, const PCHReader::RecordData &Record,
                  unsigned &Idx)
      : Reader(Reader), Record(Record), Idx(Idx) { }

    void VisitDecl(Decl *D);
    void VisitTranslationUnitDecl(TranslationUnitDecl *TU);
    void VisitNamedDecl(NamedDecl *ND);
    void VisitTypeDecl(TypeDecl *TD);
    void VisitTypedefDecl(TypedefDecl *TD);
    void VisitTagDecl(TagDecl *TD);
    void VisitEnumDecl(EnumDecl *ED);
    void VisitValueDecl(ValueDecl *VD);
    void VisitEnumConstantDecl(EnumConstantDecl *ECD);
    void VisitVarDecl(VarDecl *VD);

    std::pair<uint64_t, uint64_t> VisitDeclContext(DeclContext *DC);
  };
}

void PCHDeclReader::VisitDecl(Decl *D) {
  D->setDeclContext(cast_or_null<DeclContext>(Reader.GetDecl(Record[Idx++])));
  D->setLexicalDeclContext(
                     cast_or_null<DeclContext>(Reader.GetDecl(Record[Idx++])));
  D->setLocation(SourceLocation::getFromRawEncoding(Record[Idx++]));
  D->setInvalidDecl(Record[Idx++]);
  // FIXME: hasAttrs
  D->setImplicit(Record[Idx++]);
  D->setAccess((AccessSpecifier)Record[Idx++]);
}

void PCHDeclReader::VisitTranslationUnitDecl(TranslationUnitDecl *TU) {
  VisitDecl(TU);
}

void PCHDeclReader::VisitNamedDecl(NamedDecl *ND) {
  VisitDecl(ND);
  ND->setDeclName(Reader.ReadDeclarationName(Record, Idx));  
}

void PCHDeclReader::VisitTypeDecl(TypeDecl *TD) {
  VisitNamedDecl(TD);
  // FIXME: circular dependencies here?
  TD->setTypeForDecl(Reader.GetType(Record[Idx++]).getTypePtr());
}

void PCHDeclReader::VisitTypedefDecl(TypedefDecl *TD) {
  VisitTypeDecl(TD);
  TD->setUnderlyingType(Reader.GetType(Record[Idx++]));
}

void PCHDeclReader::VisitTagDecl(TagDecl *TD) {
  VisitTypeDecl(TD);
  TD->setTagKind((TagDecl::TagKind)Record[Idx++]);
  TD->setDefinition(Record[Idx++]);
  TD->setTypedefForAnonDecl(
                    cast_or_null<TypedefDecl>(Reader.GetDecl(Record[Idx++])));
}

void PCHDeclReader::VisitEnumDecl(EnumDecl *ED) {
  VisitTagDecl(ED);
  ED->setIntegerType(Reader.GetType(Record[Idx++]));
}

void PCHDeclReader::VisitValueDecl(ValueDecl *VD) {
  VisitNamedDecl(VD);
  VD->setType(Reader.GetType(Record[Idx++]));
}

void PCHDeclReader::VisitEnumConstantDecl(EnumConstantDecl *ECD) {
  VisitValueDecl(ECD);
  // FIXME: initialization expression
  ECD->setInitVal(Reader.ReadAPSInt(Record, Idx));
}

void PCHDeclReader::VisitVarDecl(VarDecl *VD) {
  VisitValueDecl(VD);
  VD->setStorageClass((VarDecl::StorageClass)Record[Idx++]);
  VD->setThreadSpecified(Record[Idx++]);
  VD->setCXXDirectInitializer(Record[Idx++]);
  VD->setDeclaredInCondition(Record[Idx++]);
  VD->setPreviousDeclaration(
                         cast_or_null<VarDecl>(Reader.GetDecl(Record[Idx++])));
  VD->setTypeSpecStartLoc(SourceLocation::getFromRawEncoding(Record[Idx++]));
}

std::pair<uint64_t, uint64_t> 
PCHDeclReader::VisitDeclContext(DeclContext *DC) {
  uint64_t LexicalOffset = Record[Idx++];
  uint64_t VisibleOffset = 0;
  if (DC->getPrimaryContext() == DC)
    VisibleOffset = Record[Idx++];
  return std::make_pair(LexicalOffset, VisibleOffset);
}

// FIXME: use the diagnostics machinery
static bool Error(const char *Str) {
  std::fprintf(stderr, "%s\n", Str);
  return true;
}

/// \brief Check the contents of the predefines buffer against the
/// contents of the predefines buffer used to build the PCH file.
///
/// The contents of the two predefines buffers should be the same. If
/// not, then some command-line option changed the preprocessor state
/// and we must reject the PCH file.
///
/// \param PCHPredef The start of the predefines buffer in the PCH
/// file.
///
/// \param PCHPredefLen The length of the predefines buffer in the PCH
/// file.
///
/// \param PCHBufferID The FileID for the PCH predefines buffer.
///
/// \returns true if there was a mismatch (in which case the PCH file
/// should be ignored), or false otherwise.
bool PCHReader::CheckPredefinesBuffer(const char *PCHPredef, 
                                      unsigned PCHPredefLen,
                                      FileID PCHBufferID) {
  const char *Predef = PP.getPredefines().c_str();
  unsigned PredefLen = PP.getPredefines().size();

  // If the two predefines buffers compare equal, we're done!.
  if (PredefLen == PCHPredefLen && 
      strncmp(Predef, PCHPredef, PCHPredefLen) == 0)
    return false;
  
  // The predefines buffers are different. Produce a reasonable
  // diagnostic showing where they are different.

  // The source locations (potentially in the two different predefines
  // buffers)
  SourceLocation Loc1, Loc2;
  SourceManager &SourceMgr = PP.getSourceManager();

  // Create a source buffer for our predefines string, so
  // that we can build a diagnostic that points into that
  // source buffer.
  FileID BufferID;
  if (Predef && Predef[0]) {
    llvm::MemoryBuffer *Buffer
      = llvm::MemoryBuffer::getMemBuffer(Predef, Predef + PredefLen,
                                         "<built-in>");
    BufferID = SourceMgr.createFileIDForMemBuffer(Buffer);
  }

  unsigned MinLen = std::min(PredefLen, PCHPredefLen);
  std::pair<const char *, const char *> Locations
    = std::mismatch(Predef, Predef + MinLen, PCHPredef); 
 
  if (Locations.first != Predef + MinLen) {
    // We found the location in the two buffers where there is a
    // difference. Form source locations to point there (in both
    // buffers).
    unsigned Offset = Locations.first - Predef;
    Loc1 = SourceMgr.getLocForStartOfFile(BufferID)
             .getFileLocWithOffset(Offset);
    Loc2 = SourceMgr.getLocForStartOfFile(PCHBufferID)
             .getFileLocWithOffset(Offset);
  } else if (PredefLen > PCHPredefLen) {
    Loc1 = SourceMgr.getLocForStartOfFile(BufferID)
             .getFileLocWithOffset(MinLen);
  } else {
    Loc1 = SourceMgr.getLocForStartOfFile(PCHBufferID)
             .getFileLocWithOffset(MinLen);
  }
  
  Diag(Loc1, diag::warn_pch_preprocessor);
  if (Loc2.isValid())
    Diag(Loc2, diag::note_predef_in_pch);
  Diag(diag::note_ignoring_pch) << FileName;
  return true;
}

/// \brief Read the line table in the source manager block.
/// \returns true if ther was an error.
static bool ParseLineTable(SourceManager &SourceMgr, 
                           llvm::SmallVectorImpl<uint64_t> &Record) {
  unsigned Idx = 0;
  LineTableInfo &LineTable = SourceMgr.getLineTable();

  // Parse the file names
  std::map<int, int> FileIDs;
  for (int I = 0, N = Record[Idx++]; I != N; ++I) {
    // Extract the file name
    unsigned FilenameLen = Record[Idx++];
    std::string Filename(&Record[Idx], &Record[Idx] + FilenameLen);
    Idx += FilenameLen;
    FileIDs[I] = LineTable.getLineTableFilenameID(Filename.c_str(), 
                                                  Filename.size());
  }

  // Parse the line entries
  std::vector<LineEntry> Entries;
  while (Idx < Record.size()) {
    int FID = FileIDs[Record[Idx++]];

    // Extract the line entries
    unsigned NumEntries = Record[Idx++];
    Entries.clear();
    Entries.reserve(NumEntries);
    for (unsigned I = 0; I != NumEntries; ++I) {
      unsigned FileOffset = Record[Idx++];
      unsigned LineNo = Record[Idx++];
      int FilenameID = Record[Idx++];
      SrcMgr::CharacteristicKind FileKind 
        = (SrcMgr::CharacteristicKind)Record[Idx++];
      unsigned IncludeOffset = Record[Idx++];
      Entries.push_back(LineEntry::get(FileOffset, LineNo, FilenameID,
                                       FileKind, IncludeOffset));
    }
    LineTable.AddEntry(FID, Entries);
  }

  return false;
}

/// \brief Read the source manager block
PCHReader::PCHReadResult PCHReader::ReadSourceManagerBlock() {
  using namespace SrcMgr;
  if (Stream.EnterSubBlock(pch::SOURCE_MANAGER_BLOCK_ID)) {
    Error("Malformed source manager block record");
    return Failure;
  }

  SourceManager &SourceMgr = Context.getSourceManager();
  RecordData Record;
  while (true) {
    unsigned Code = Stream.ReadCode();
    if (Code == llvm::bitc::END_BLOCK) {
      if (Stream.ReadBlockEnd()) {
        Error("Error at end of Source Manager block");
        return Failure;
      }

      return Success;
    }
    
    if (Code == llvm::bitc::ENTER_SUBBLOCK) {
      // No known subblocks, always skip them.
      Stream.ReadSubBlockID();
      if (Stream.SkipBlock()) {
        Error("Malformed block record");
        return Failure;
      }
      continue;
    }
    
    if (Code == llvm::bitc::DEFINE_ABBREV) {
      Stream.ReadAbbrevRecord();
      continue;
    }
    
    // Read a record.
    const char *BlobStart;
    unsigned BlobLen;
    Record.clear();
    switch (Stream.ReadRecord(Code, Record, &BlobStart, &BlobLen)) {
    default:  // Default behavior: ignore.
      break;

    case pch::SM_SLOC_FILE_ENTRY: {
      // FIXME: We would really like to delay the creation of this
      // FileEntry until it is actually required, e.g., when producing
      // a diagnostic with a source location in this file.
      const FileEntry *File 
        = PP.getFileManager().getFile(BlobStart, BlobStart + BlobLen);
      // FIXME: Error recovery if file cannot be found.
      FileID ID = SourceMgr.createFileID(File,
                                SourceLocation::getFromRawEncoding(Record[1]),
                                         (CharacteristicKind)Record[2]);
      if (Record[3])
        const_cast<SrcMgr::FileInfo&>(SourceMgr.getSLocEntry(ID).getFile())
          .setHasLineDirectives();
      break;
    }

    case pch::SM_SLOC_BUFFER_ENTRY: {
      const char *Name = BlobStart;
      unsigned Code = Stream.ReadCode();
      Record.clear();
      unsigned RecCode = Stream.ReadRecord(Code, Record, &BlobStart, &BlobLen);
      assert(RecCode == pch::SM_SLOC_BUFFER_BLOB && "Ill-formed PCH file");
      llvm::MemoryBuffer *Buffer
        = llvm::MemoryBuffer::getMemBuffer(BlobStart, 
                                           BlobStart + BlobLen - 1,
                                           Name);
      FileID BufferID = SourceMgr.createFileIDForMemBuffer(Buffer);

      if (strcmp(Name, "<built-in>") == 0
          && CheckPredefinesBuffer(BlobStart, BlobLen - 1, BufferID))
        return IgnorePCH;
      break;
    }

    case pch::SM_SLOC_INSTANTIATION_ENTRY: {
      SourceLocation SpellingLoc 
        = SourceLocation::getFromRawEncoding(Record[1]);
      SourceMgr.createInstantiationLoc(
                              SpellingLoc,
                              SourceLocation::getFromRawEncoding(Record[2]),
                              SourceLocation::getFromRawEncoding(Record[3]),
                              Lexer::MeasureTokenLength(SpellingLoc, 
                                                        SourceMgr));
      break;
    }

    case pch::SM_LINE_TABLE: {
      if (ParseLineTable(SourceMgr, Record))
        return Failure;
    }
    }
  }
}

bool PCHReader::ReadPreprocessorBlock() {
  if (Stream.EnterSubBlock(pch::PREPROCESSOR_BLOCK_ID))
    return Error("Malformed preprocessor block record");
  
  RecordData Record;
  llvm::SmallVector<IdentifierInfo*, 16> MacroArgs;
  MacroInfo *LastMacro = 0;
  
  while (true) {
    unsigned Code = Stream.ReadCode();
    switch (Code) {
    case llvm::bitc::END_BLOCK:
      if (Stream.ReadBlockEnd())
        return Error("Error at end of preprocessor block");
      return false;
    
    case llvm::bitc::ENTER_SUBBLOCK:
      // No known subblocks, always skip them.
      Stream.ReadSubBlockID();
      if (Stream.SkipBlock())
        return Error("Malformed block record");
      continue;
    
    case llvm::bitc::DEFINE_ABBREV:
      Stream.ReadAbbrevRecord();
      continue;
    default: break;
    }
    
    // Read a record.
    Record.clear();
    pch::PreprocessorRecordTypes RecType =
      (pch::PreprocessorRecordTypes)Stream.ReadRecord(Code, Record);
    switch (RecType) {
    default:  // Default behavior: ignore unknown records.
      break;
    case pch::PP_COUNTER_VALUE:
      if (!Record.empty())
        PP.setCounterValue(Record[0]);
      break;

    case pch::PP_MACRO_OBJECT_LIKE:
    case pch::PP_MACRO_FUNCTION_LIKE: {
      IdentifierInfo *II = DecodeIdentifierInfo(Record[0]);
      if (II == 0)
        return Error("Macro must have a name");
      SourceLocation Loc = SourceLocation::getFromRawEncoding(Record[1]);
      bool isUsed = Record[2];
      
      MacroInfo *MI = PP.AllocateMacroInfo(Loc);
      MI->setIsUsed(isUsed);
      
      if (RecType == pch::PP_MACRO_FUNCTION_LIKE) {
        // Decode function-like macro info.
        bool isC99VarArgs = Record[3];
        bool isGNUVarArgs = Record[4];
        MacroArgs.clear();
        unsigned NumArgs = Record[5];
        for (unsigned i = 0; i != NumArgs; ++i)
          MacroArgs.push_back(DecodeIdentifierInfo(Record[6+i]));

        // Install function-like macro info.
        MI->setIsFunctionLike();
        if (isC99VarArgs) MI->setIsC99Varargs();
        if (isGNUVarArgs) MI->setIsGNUVarargs();
        MI->setArgumentList(&MacroArgs[0], MacroArgs.size(),
                            PP.getPreprocessorAllocator());
      }

      // Finally, install the macro.
      PP.setMacroInfo(II, MI);

      // Remember that we saw this macro last so that we add the tokens that
      // form its body to it.
      LastMacro = MI;
      break;
    }
        
    case pch::PP_TOKEN: {
      // If we see a TOKEN before a PP_MACRO_*, then the file is eroneous, just
      // pretend we didn't see this.
      if (LastMacro == 0) break;
      
      Token Tok;
      Tok.startToken();
      Tok.setLocation(SourceLocation::getFromRawEncoding(Record[0]));
      Tok.setLength(Record[1]);
      if (IdentifierInfo *II = DecodeIdentifierInfo(Record[2]))
        Tok.setIdentifierInfo(II);
      Tok.setKind((tok::TokenKind)Record[3]);
      Tok.setFlag((Token::TokenFlags)Record[4]);
      LastMacro->AddTokenToBody(Tok);
      break;
    }
    }
  }
}

PCHReader::PCHReadResult PCHReader::ReadPCHBlock() {
  if (Stream.EnterSubBlock(pch::PCH_BLOCK_ID)) {
    Error("Malformed block record");
    return Failure;
  }

  uint64_t PreprocessorBlockBit = 0;
  
  // Read all of the records and blocks for the PCH file.
  RecordData Record;
  while (!Stream.AtEndOfStream()) {
    unsigned Code = Stream.ReadCode();
    if (Code == llvm::bitc::END_BLOCK) {
      // If we saw the preprocessor block, read it now.
      if (PreprocessorBlockBit) {
        uint64_t SavedPos = Stream.GetCurrentBitNo();
        Stream.JumpToBit(PreprocessorBlockBit);
        if (ReadPreprocessorBlock()) {
          Error("Malformed preprocessor block");
          return Failure;
        }
        Stream.JumpToBit(SavedPos);
      }        
      
      if (Stream.ReadBlockEnd()) {
        Error("Error at end of module block");
        return Failure;
      }

      return Success;
    }

    if (Code == llvm::bitc::ENTER_SUBBLOCK) {
      switch (Stream.ReadSubBlockID()) {
      case pch::DECLS_BLOCK_ID: // Skip decls block (lazily loaded)
      case pch::TYPES_BLOCK_ID: // Skip types block (lazily loaded)
      default:  // Skip unknown content.
        if (Stream.SkipBlock()) {
          Error("Malformed block record");
          return Failure;
        }
        break;

      case pch::PREPROCESSOR_BLOCK_ID:
        // Skip the preprocessor block for now, but remember where it is.  We
        // want to read it in after the identifier table.
        if (PreprocessorBlockBit) {
          Error("Multiple preprocessor blocks found.");
          return Failure;
        }
        PreprocessorBlockBit = Stream.GetCurrentBitNo();
        if (Stream.SkipBlock()) {
          Error("Malformed block record");
          return Failure;
        }
        break;
          
      case pch::SOURCE_MANAGER_BLOCK_ID:
        switch (ReadSourceManagerBlock()) {
        case Success:
          break;

        case Failure:
          Error("Malformed source manager block");
          return Failure;

        case IgnorePCH:
          return IgnorePCH;
        }
        break;
      }
      continue;
    }

    if (Code == llvm::bitc::DEFINE_ABBREV) {
      Stream.ReadAbbrevRecord();
      continue;
    }

    // Read and process a record.
    Record.clear();
    const char *BlobStart = 0;
    unsigned BlobLen = 0;
    switch ((pch::PCHRecordTypes)Stream.ReadRecord(Code, Record, 
                                                   &BlobStart, &BlobLen)) {
    default:  // Default behavior: ignore.
      break;

    case pch::TYPE_OFFSET:
      if (!TypeOffsets.empty()) {
        Error("Duplicate TYPE_OFFSET record in PCH file");
        return Failure;
      }
      TypeOffsets.swap(Record);
      TypeAlreadyLoaded.resize(TypeOffsets.size(), false);
      break;

    case pch::DECL_OFFSET:
      if (!DeclOffsets.empty()) {
        Error("Duplicate DECL_OFFSET record in PCH file");
        return Failure;
      }
      DeclOffsets.swap(Record);
      DeclAlreadyLoaded.resize(DeclOffsets.size(), false);
      break;

    case pch::LANGUAGE_OPTIONS:
      if (ParseLanguageOptions(Record))
        return IgnorePCH;
      break;

    case pch::TARGET_TRIPLE: {
      std::string TargetTriple(BlobStart, BlobLen);
      if (TargetTriple != Context.Target.getTargetTriple()) {
        Diag(diag::warn_pch_target_triple)
          << TargetTriple << Context.Target.getTargetTriple();
        Diag(diag::note_ignoring_pch) << FileName;
        return IgnorePCH;
      }
      break;
    }

    case pch::IDENTIFIER_TABLE:
      IdentifierTable = BlobStart;
      break;

    case pch::IDENTIFIER_OFFSET:
      if (!IdentifierData.empty()) {
        Error("Duplicate IDENTIFIER_OFFSET record in PCH file");
        return Failure;
      }
      IdentifierData.swap(Record);
#ifndef NDEBUG
      for (unsigned I = 0, N = IdentifierData.size(); I != N; ++I) {
        if ((IdentifierData[I] & 0x01) == 0) {
          Error("Malformed identifier table in the precompiled header");
          return Failure;
        }
      }
#endif
      break;
    }
  }

  Error("Premature end of bitstream");
  return Failure;
}

PCHReader::PCHReadResult PCHReader::ReadPCH(const std::string &FileName) {
  // Set the PCH file name.
  this->FileName = FileName;

  // Open the PCH file.
  std::string ErrStr;
  Buffer.reset(llvm::MemoryBuffer::getFile(FileName.c_str(), &ErrStr));
  if (!Buffer) {
    Error(ErrStr.c_str());
    return IgnorePCH;
  }

  // Initialize the stream
  Stream.init((const unsigned char *)Buffer->getBufferStart(), 
              (const unsigned char *)Buffer->getBufferEnd());

  // Sniff for the signature.
  if (Stream.Read(8) != 'C' ||
      Stream.Read(8) != 'P' ||
      Stream.Read(8) != 'C' ||
      Stream.Read(8) != 'H') {
    Error("Not a PCH file");
    return IgnorePCH;
  }

  // We expect a number of well-defined blocks, though we don't necessarily
  // need to understand them all.
  while (!Stream.AtEndOfStream()) {
    unsigned Code = Stream.ReadCode();
    
    if (Code != llvm::bitc::ENTER_SUBBLOCK) {
      Error("Invalid record at top-level");
      return Failure;
    }

    unsigned BlockID = Stream.ReadSubBlockID();
    
    // We only know the PCH subblock ID.
    switch (BlockID) {
    case llvm::bitc::BLOCKINFO_BLOCK_ID:
      if (Stream.ReadBlockInfoBlock()) {
        Error("Malformed BlockInfoBlock");
        return Failure;
      }
      break;
    case pch::PCH_BLOCK_ID:
      switch (ReadPCHBlock()) {
      case Success:
        break;

      case Failure:
        return Failure;

      case IgnorePCH:
        // FIXME: We could consider reading through to the end of this
        // PCH block, skipping subblocks, to see if there are other
        // PCH blocks elsewhere.
        return IgnorePCH;
      }
      break;
    default:
      if (Stream.SkipBlock()) {
        Error("Malformed block record");
        return Failure;
      }
      break;
    }
  }  

  // Load the translation unit declaration
  ReadDeclRecord(DeclOffsets[0], 0);

  return Success;
}

/// \brief Parse the record that corresponds to a LangOptions data
/// structure.
///
/// This routine compares the language options used to generate the
/// PCH file against the language options set for the current
/// compilation. For each option, we classify differences between the
/// two compiler states as either "benign" or "important". Benign
/// differences don't matter, and we accept them without complaint
/// (and without modifying the language options). Differences between
/// the states for important options cause the PCH file to be
/// unusable, so we emit a warning and return true to indicate that
/// there was an error.
///
/// \returns true if the PCH file is unacceptable, false otherwise.
bool PCHReader::ParseLanguageOptions(
                             const llvm::SmallVectorImpl<uint64_t> &Record) {
  const LangOptions &LangOpts = Context.getLangOptions();
#define PARSE_LANGOPT_BENIGN(Option) ++Idx
#define PARSE_LANGOPT_IMPORTANT(Option, DiagID)                 \
  if (Record[Idx] != LangOpts.Option) {                         \
    Diag(DiagID) << (unsigned)Record[Idx] << LangOpts.Option;   \
    Diag(diag::note_ignoring_pch) << FileName;                  \
    return true;                                                \
  }                                                             \
  ++Idx

  unsigned Idx = 0;
  PARSE_LANGOPT_BENIGN(Trigraphs);
  PARSE_LANGOPT_BENIGN(BCPLComment);
  PARSE_LANGOPT_BENIGN(DollarIdents);
  PARSE_LANGOPT_BENIGN(AsmPreprocessor);
  PARSE_LANGOPT_IMPORTANT(GNUMode, diag::warn_pch_gnu_extensions);
  PARSE_LANGOPT_BENIGN(ImplicitInt);
  PARSE_LANGOPT_BENIGN(Digraphs);
  PARSE_LANGOPT_BENIGN(HexFloats);
  PARSE_LANGOPT_IMPORTANT(C99, diag::warn_pch_c99);
  PARSE_LANGOPT_IMPORTANT(Microsoft, diag::warn_pch_microsoft_extensions);
  PARSE_LANGOPT_IMPORTANT(CPlusPlus, diag::warn_pch_cplusplus);
  PARSE_LANGOPT_IMPORTANT(CPlusPlus0x, diag::warn_pch_cplusplus0x);
  PARSE_LANGOPT_IMPORTANT(NoExtensions, diag::warn_pch_extensions);
  PARSE_LANGOPT_BENIGN(CXXOperatorName);
  PARSE_LANGOPT_IMPORTANT(ObjC1, diag::warn_pch_objective_c);
  PARSE_LANGOPT_IMPORTANT(ObjC2, diag::warn_pch_objective_c2);
  PARSE_LANGOPT_IMPORTANT(ObjCNonFragileABI, diag::warn_pch_nonfragile_abi);
  PARSE_LANGOPT_BENIGN(PascalStrings);
  PARSE_LANGOPT_BENIGN(Boolean);
  PARSE_LANGOPT_BENIGN(WritableStrings);
  PARSE_LANGOPT_IMPORTANT(LaxVectorConversions, 
                          diag::warn_pch_lax_vector_conversions);
  PARSE_LANGOPT_IMPORTANT(Exceptions, diag::warn_pch_exceptions);
  PARSE_LANGOPT_IMPORTANT(NeXTRuntime, diag::warn_pch_objc_runtime);
  PARSE_LANGOPT_IMPORTANT(Freestanding, diag::warn_pch_freestanding);
  PARSE_LANGOPT_IMPORTANT(NoBuiltin, diag::warn_pch_builtins);
  PARSE_LANGOPT_IMPORTANT(ThreadsafeStatics, 
                          diag::warn_pch_thread_safe_statics);
  PARSE_LANGOPT_IMPORTANT(Blocks, diag::warn_pch_blocks);
  PARSE_LANGOPT_BENIGN(EmitAllDecls);
  PARSE_LANGOPT_IMPORTANT(MathErrno, diag::warn_pch_math_errno);
  PARSE_LANGOPT_IMPORTANT(OverflowChecking, diag::warn_pch_overflow_checking);
  PARSE_LANGOPT_IMPORTANT(HeinousExtensions, 
                          diag::warn_pch_heinous_extensions);
  // FIXME: Most of the options below are benign if the macro wasn't
  // used. Unfortunately, this means that a PCH compiled without
  // optimization can't be used with optimization turned on, even
  // though the only thing that changes is whether __OPTIMIZE__ was
  // defined... but if __OPTIMIZE__ never showed up in the header, it
  // doesn't matter. We could consider making this some special kind
  // of check.
  PARSE_LANGOPT_IMPORTANT(Optimize, diag::warn_pch_optimize);
  PARSE_LANGOPT_IMPORTANT(OptimizeSize, diag::warn_pch_optimize_size);
  PARSE_LANGOPT_IMPORTANT(Static, diag::warn_pch_static);
  PARSE_LANGOPT_IMPORTANT(PICLevel, diag::warn_pch_pic_level);
  PARSE_LANGOPT_IMPORTANT(GNUInline, diag::warn_pch_gnu_inline);
  PARSE_LANGOPT_IMPORTANT(NoInline, diag::warn_pch_no_inline);
  if ((LangOpts.getGCMode() != 0) != (Record[Idx] != 0)) {
    Diag(diag::warn_pch_gc_mode) 
      << (unsigned)Record[Idx] << LangOpts.getGCMode();
    Diag(diag::note_ignoring_pch) << FileName;
    return true;
  }
  ++Idx;
  PARSE_LANGOPT_BENIGN(getVisibilityMode());
  PARSE_LANGOPT_BENIGN(InstantiationDepth);
#undef PARSE_LANGOPT_IRRELEVANT
#undef PARSE_LANGOPT_BENIGN

  return false;
}

/// \brief Read and return the type at the given offset.
///
/// This routine actually reads the record corresponding to the type
/// at the given offset in the bitstream. It is a helper routine for
/// GetType, which deals with reading type IDs.
QualType PCHReader::ReadTypeRecord(uint64_t Offset) {
  Stream.JumpToBit(Offset);
  RecordData Record;
  unsigned Code = Stream.ReadCode();
  switch ((pch::TypeCode)Stream.ReadRecord(Code, Record)) {
  case pch::TYPE_FIXED_WIDTH_INT: {
    assert(Record.size() == 2 && "Incorrect encoding of fixed-width int type");
    return Context.getFixedWidthIntType(Record[0], Record[1]);
  }

  case pch::TYPE_COMPLEX: {
    assert(Record.size() == 1 && "Incorrect encoding of complex type");
    QualType ElemType = GetType(Record[0]);
    return Context.getComplexType(ElemType);
  }

  case pch::TYPE_POINTER: {
    assert(Record.size() == 1 && "Incorrect encoding of pointer type");
    QualType PointeeType = GetType(Record[0]);
    return Context.getPointerType(PointeeType);
  }

  case pch::TYPE_BLOCK_POINTER: {
    assert(Record.size() == 1 && "Incorrect encoding of block pointer type");
    QualType PointeeType = GetType(Record[0]);
    return Context.getBlockPointerType(PointeeType);
  }

  case pch::TYPE_LVALUE_REFERENCE: {
    assert(Record.size() == 1 && "Incorrect encoding of lvalue reference type");
    QualType PointeeType = GetType(Record[0]);
    return Context.getLValueReferenceType(PointeeType);
  }

  case pch::TYPE_RVALUE_REFERENCE: {
    assert(Record.size() == 1 && "Incorrect encoding of rvalue reference type");
    QualType PointeeType = GetType(Record[0]);
    return Context.getRValueReferenceType(PointeeType);
  }

  case pch::TYPE_MEMBER_POINTER: {
    assert(Record.size() == 1 && "Incorrect encoding of member pointer type");
    QualType PointeeType = GetType(Record[0]);
    QualType ClassType = GetType(Record[1]);
    return Context.getMemberPointerType(PointeeType, ClassType.getTypePtr());
  }

  case pch::TYPE_ENUM:
    assert(Record.size() == 1 && "Incorrect encoding of enum type");
    return Context.getTypeDeclType(cast<EnumDecl>(GetDecl(Record[0])));

    // FIXME: Several other kinds of types to deserialize here!
  default:
    assert(false && "Unable to deserialize this type");
    break;
  }

  // Suppress a GCC warning
  return QualType();
}

/// \brief Note that we have loaded the declaration with the given
/// Index.
/// 
/// This routine notes that this declaration has already been loaded,
/// so that future GetDecl calls will return this declaration rather
/// than trying to load a new declaration.
inline void PCHReader::LoadedDecl(unsigned Index, Decl *D) {
  assert(!DeclAlreadyLoaded[Index] && "Decl loaded twice?");
  DeclAlreadyLoaded[Index] = true;
  DeclOffsets[Index] = reinterpret_cast<uint64_t>(D);
}

/// \brief Read the declaration at the given offset from the PCH file.
Decl *PCHReader::ReadDeclRecord(uint64_t Offset, unsigned Index) {
  Decl *D = 0;
  Stream.JumpToBit(Offset);
  RecordData Record;
  unsigned Code = Stream.ReadCode();
  unsigned Idx = 0;
  PCHDeclReader Reader(*this, Record, Idx);
  switch ((pch::DeclCode)Stream.ReadRecord(Code, Record)) {
  case pch::DECL_TRANSLATION_UNIT:
    assert(Index == 0 && "Translation unit must be at index 0");
    Reader.VisitTranslationUnitDecl(Context.getTranslationUnitDecl());
    D = Context.getTranslationUnitDecl();
    LoadedDecl(Index, D);
    break;

  case pch::DECL_TYPEDEF: {
    TypedefDecl *Typedef = TypedefDecl::Create(Context, 0, SourceLocation(),
                                               0, QualType());
    LoadedDecl(Index, Typedef);
    Reader.VisitTypedefDecl(Typedef);
    D = Typedef;
    break;
  }

  case pch::DECL_ENUM: {
    EnumDecl *Enum = EnumDecl::Create(Context, 0, SourceLocation(), 0, 0);
    LoadedDecl(Index, Enum);
    Reader.VisitEnumDecl(Enum);
    D = Enum;
    break;
  }

  case pch::DECL_ENUM_CONSTANT: {
    EnumConstantDecl *ECD = EnumConstantDecl::Create(Context, 0, 
                                                     SourceLocation(), 0,
                                                     QualType(), 0,
                                                     llvm::APSInt());
    LoadedDecl(Index, ECD);
    Reader.VisitEnumConstantDecl(ECD);
    D = ECD;
    break;
  }

  case pch::DECL_VAR: {
    VarDecl *Var = VarDecl::Create(Context, 0, SourceLocation(), 0, QualType(),
                                   VarDecl::None, SourceLocation());
    LoadedDecl(Index, Var);
    Reader.VisitVarDecl(Var);
    D = Var;
    break;
  }

  default:
    assert(false && "Cannot de-serialize this kind of declaration");
    break;
  }

  // If this declaration is also a declaration context, get the
  // offsets for its tables of lexical and visible declarations.
  if (DeclContext *DC = dyn_cast<DeclContext>(D)) {
    std::pair<uint64_t, uint64_t> Offsets = Reader.VisitDeclContext(DC);
    if (Offsets.first || Offsets.second) {
      DC->setHasExternalLexicalStorage(Offsets.first != 0);
      DC->setHasExternalVisibleStorage(Offsets.second != 0);
      DeclContextOffsets[DC] = Offsets;
    }
  }
  assert(Idx == Record.size());

  return D;
}

QualType PCHReader::GetType(pch::TypeID ID) {
  unsigned Quals = ID & 0x07; 
  unsigned Index = ID >> 3;

  if (Index < pch::NUM_PREDEF_TYPE_IDS) {
    QualType T;
    switch ((pch::PredefinedTypeIDs)Index) {
    case pch::PREDEF_TYPE_NULL_ID: return QualType();
    case pch::PREDEF_TYPE_VOID_ID: T = Context.VoidTy; break;
    case pch::PREDEF_TYPE_BOOL_ID: T = Context.BoolTy; break;

    case pch::PREDEF_TYPE_CHAR_U_ID:
    case pch::PREDEF_TYPE_CHAR_S_ID:
      // FIXME: Check that the signedness of CharTy is correct!
      T = Context.CharTy;
      break;

    case pch::PREDEF_TYPE_UCHAR_ID:      T = Context.UnsignedCharTy;     break;
    case pch::PREDEF_TYPE_USHORT_ID:     T = Context.UnsignedShortTy;    break;
    case pch::PREDEF_TYPE_UINT_ID:       T = Context.UnsignedIntTy;      break;
    case pch::PREDEF_TYPE_ULONG_ID:      T = Context.UnsignedLongTy;     break;
    case pch::PREDEF_TYPE_ULONGLONG_ID:  T = Context.UnsignedLongLongTy; break;
    case pch::PREDEF_TYPE_SCHAR_ID:      T = Context.SignedCharTy;       break;
    case pch::PREDEF_TYPE_WCHAR_ID:      T = Context.WCharTy;            break;
    case pch::PREDEF_TYPE_SHORT_ID:      T = Context.ShortTy;            break;
    case pch::PREDEF_TYPE_INT_ID:        T = Context.IntTy;              break;
    case pch::PREDEF_TYPE_LONG_ID:       T = Context.LongTy;             break;
    case pch::PREDEF_TYPE_LONGLONG_ID:   T = Context.LongLongTy;         break;
    case pch::PREDEF_TYPE_FLOAT_ID:      T = Context.FloatTy;            break;
    case pch::PREDEF_TYPE_DOUBLE_ID:     T = Context.DoubleTy;           break;
    case pch::PREDEF_TYPE_LONGDOUBLE_ID: T = Context.LongDoubleTy;       break;
    case pch::PREDEF_TYPE_OVERLOAD_ID:   T = Context.OverloadTy;         break;
    case pch::PREDEF_TYPE_DEPENDENT_ID:  T = Context.DependentTy;        break;
    }

    assert(!T.isNull() && "Unknown predefined type");
    return T.getQualifiedType(Quals);
  }

  Index -= pch::NUM_PREDEF_TYPE_IDS;
  if (!TypeAlreadyLoaded[Index]) {
    // Load the type from the PCH file.
    TypeOffsets[Index] = reinterpret_cast<uint64_t>(
                             ReadTypeRecord(TypeOffsets[Index]).getTypePtr());
    TypeAlreadyLoaded[Index] = true;
  }
    
  return QualType(reinterpret_cast<Type *>(TypeOffsets[Index]), Quals);
}

Decl *PCHReader::GetDecl(pch::DeclID ID) {
  if (ID == 0)
    return 0;

  unsigned Index = ID - 1;
  if (DeclAlreadyLoaded[Index])
    return reinterpret_cast<Decl *>(DeclOffsets[Index]);

  // Load the declaration from the PCH file.
  return ReadDeclRecord(DeclOffsets[Index], Index);
}

bool PCHReader::ReadDeclsLexicallyInContext(DeclContext *DC,
                                  llvm::SmallVectorImpl<pch::DeclID> &Decls) {
  assert(DC->hasExternalLexicalStorage() && 
         "DeclContext has no lexical decls in storage");
  uint64_t Offset = DeclContextOffsets[DC].first;
  assert(Offset && "DeclContext has no lexical decls in storage");

  // Load the record containing all of the declarations lexically in
  // this context.
  Stream.JumpToBit(Offset);
  RecordData Record;
  unsigned Code = Stream.ReadCode();
  unsigned RecCode = Stream.ReadRecord(Code, Record);
  assert(RecCode == pch::DECL_CONTEXT_LEXICAL && "Expected lexical block");

  // Load all of the declaration IDs
  Decls.clear();
  Decls.insert(Decls.end(), Record.begin(), Record.end());
  return false;
}

bool PCHReader::ReadDeclsVisibleInContext(DeclContext *DC,
                           llvm::SmallVectorImpl<VisibleDeclaration> & Decls) {
  assert(DC->hasExternalVisibleStorage() && 
         "DeclContext has no visible decls in storage");
  uint64_t Offset = DeclContextOffsets[DC].second;
  assert(Offset && "DeclContext has no visible decls in storage");

  // Load the record containing all of the declarations visible in
  // this context.
  Stream.JumpToBit(Offset);
  RecordData Record;
  unsigned Code = Stream.ReadCode();
  unsigned RecCode = Stream.ReadRecord(Code, Record);
  assert(RecCode == pch::DECL_CONTEXT_VISIBLE && "Expected visible block");
  if (Record.size() == 0)
    return false;  

  Decls.clear();

  unsigned Idx = 0;
  //  llvm::SmallVector<uintptr_t, 16> DeclIDs;
  while (Idx < Record.size()) {
    Decls.push_back(VisibleDeclaration());
    Decls.back().Name = ReadDeclarationName(Record, Idx);

    // FIXME: Don't actually read anything here!
    unsigned Size = Record[Idx++];
    llvm::SmallVector<unsigned, 4> & LoadedDecls
      = Decls.back().Declarations;
    LoadedDecls.reserve(Size);
    for (unsigned I = 0; I < Size; ++I)
      LoadedDecls.push_back(Record[Idx++]);
  }

  return false;
}

void PCHReader::PrintStats() {
  std::fprintf(stderr, "*** PCH Statistics:\n");

  unsigned NumTypesLoaded = std::count(TypeAlreadyLoaded.begin(),
                                       TypeAlreadyLoaded.end(),
                                       true);
  unsigned NumDeclsLoaded = std::count(DeclAlreadyLoaded.begin(),
                                       DeclAlreadyLoaded.end(),
                                       true);
  std::fprintf(stderr, "  %u/%u types read (%f%%)\n",
               NumTypesLoaded, (unsigned)TypeAlreadyLoaded.size(),
               ((float)NumTypesLoaded/(float)TypeAlreadyLoaded.size() * 100));
  std::fprintf(stderr, "  %u/%u declarations read (%f%%)\n",
               NumDeclsLoaded, (unsigned)DeclAlreadyLoaded.size(),
               ((float)NumDeclsLoaded/(float)DeclAlreadyLoaded.size() * 100));
  std::fprintf(stderr, "\n");
}

IdentifierInfo *PCHReader::DecodeIdentifierInfo(unsigned ID) {
  if (ID == 0)
    return 0;
  
  if (!IdentifierTable || IdentifierData.empty()) {
    Error("No identifier table in PCH file");
    return 0;
  }
  
  if (IdentifierData[ID - 1] & 0x01) {
    uint64_t Offset = IdentifierData[ID - 1];
    IdentifierData[ID - 1] = reinterpret_cast<uint64_t>(
                                                        &Context.Idents.get(IdentifierTable + Offset));
  }
  
  return reinterpret_cast<IdentifierInfo *>(IdentifierData[ID - 1]);
}

DeclarationName 
PCHReader::ReadDeclarationName(const RecordData &Record, unsigned &Idx) {
  DeclarationName::NameKind Kind = (DeclarationName::NameKind)Record[Idx++];
  switch (Kind) {
  case DeclarationName::Identifier:
    return DeclarationName(GetIdentifierInfo(Record, Idx));

  case DeclarationName::ObjCZeroArgSelector:
  case DeclarationName::ObjCOneArgSelector:
  case DeclarationName::ObjCMultiArgSelector:
    assert(false && "Unable to de-serialize Objective-C selectors");
    break;

  case DeclarationName::CXXConstructorName:
    return Context.DeclarationNames.getCXXConstructorName(
                                                      GetType(Record[Idx++]));

  case DeclarationName::CXXDestructorName:
    return Context.DeclarationNames.getCXXDestructorName(
                                                      GetType(Record[Idx++]));

  case DeclarationName::CXXConversionFunctionName:
    return Context.DeclarationNames.getCXXConversionFunctionName(
                                                      GetType(Record[Idx++]));

  case DeclarationName::CXXOperatorName:
    return Context.DeclarationNames.getCXXOperatorName(
                                       (OverloadedOperatorKind)Record[Idx++]);

  case DeclarationName::CXXUsingDirective:
    return DeclarationName::getUsingDirectiveName();
  }

  // Required to silence GCC warning
  return DeclarationName();
}

/// \brief Read an integral value
llvm::APInt PCHReader::ReadAPInt(const RecordData &Record, unsigned &Idx) {
  unsigned BitWidth = Record[Idx++];
  unsigned NumWords = llvm::APInt::getNumWords(BitWidth);
  llvm::APInt Result(BitWidth, NumWords, &Record[Idx]);
  Idx += NumWords;
  return Result;
}

/// \brief Read a signed integral value
llvm::APSInt PCHReader::ReadAPSInt(const RecordData &Record, unsigned &Idx) {
  bool isUnsigned = Record[Idx++];
  return llvm::APSInt(ReadAPInt(Record, Idx), isUnsigned);
}

DiagnosticBuilder PCHReader::Diag(unsigned DiagID) {
  return Diag(SourceLocation(), DiagID);
}

DiagnosticBuilder PCHReader::Diag(SourceLocation Loc, unsigned DiagID) {
  return PP.getDiagnostics().Report(FullSourceLoc(Loc,
                                                  Context.getSourceManager()),
                                    DiagID);
}
