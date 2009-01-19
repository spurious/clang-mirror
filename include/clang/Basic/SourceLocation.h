//===--- SourceLocation.h - Compact identifier for Source Files -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the SourceLocation class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_SOURCELOCATION_H
#define LLVM_CLANG_SOURCELOCATION_H

#include <cassert>
#include "llvm/Bitcode/SerializationFwd.h"

namespace llvm {
  class MemoryBuffer;
  template <typename T> struct DenseMapInfo;
}

namespace clang {
  
class SourceManager;
class FileEntry;
  
/// FileID - This is an opaque identifier used by SourceManager which refers to
/// a source file (MemoryBuffer) along with its #include path and #line data.
///
class FileID {
  /// ID - Opaque identifier, 0 is "invalid".
  unsigned ID;
public:
  FileID() : ID(0) {}
  
  bool isInvalid() const { return ID == 0; }
  
  bool operator==(const FileID &RHS) const { return ID == RHS.ID; }
  bool operator<(const FileID &RHS) const { return ID < RHS.ID; }
  bool operator<=(const FileID &RHS) const { return ID <= RHS.ID; }
  bool operator!=(const FileID &RHS) const { return !(*this == RHS); }
  bool operator>(const FileID &RHS) const { return RHS < *this; }
  bool operator>=(const FileID &RHS) const { return RHS <= *this; }
  
  static FileID getSentinel() { return Create(~0U); }
  unsigned getHashValue() const { return ID; }
  
private:
  friend class SourceManager;
  static FileID Create(unsigned V) {
    FileID F;
    F.ID = V;
    return F;
  }
  unsigned getOpaqueValue() const { return ID; }
};
  
    
/// SourceLocation - This is a carefully crafted 32-bit identifier that encodes
/// a full include stack, line and column number information for a position in
/// an input translation unit.
class SourceLocation {
  unsigned ID;
  friend class SourceManager;
  enum {
    // FileID Layout:
    // bit 31: 0 -> FileID, 1 -> MacroID (invalid for FileID)
    //     30...17 -> ChunkID of location, index into SourceManager table.
    ChunkIDBits  = 14,
    //      0...16 -> Index into the chunk of the specified ChunkID.
    FilePosBits = 32-1-ChunkIDBits,
    
    // MacroID Layout:
    // bit 31: 1 -> MacroID, 0 -> FileID (invalid for MacroID)

    // bit 29,30: unused.
    
    // bits 28...9 -> MacroID number.
    MacroIDBits       = 20,
    // bits 8...0  -> Macro spelling offset
    MacroSpellingOffsBits = 9,
    
    
    // Useful constants.
    ChunkSize = (1 << FilePosBits)
  };
public:

  SourceLocation() : ID(0) {}  // 0 is an invalid FileID.
  
  bool isFileID() const { return (ID >> 31) == 0; }
  bool isMacroID() const { return (ID >> 31) != 0; }
  
  /// isValid - Return true if this is a valid SourceLocation object.  Invalid
  /// SourceLocations are often used when events have no corresponding location
  /// in the source (e.g. a diagnostic is required for a command line option).
  ///
  bool isValid() const { return ID != 0; }
  bool isInvalid() const { return ID == 0; }
  
private:
  /// getChunkID - Return the chunk identifier for this SourceLocation.  This
  /// ChunkID can be used with the SourceManager object to obtain an entire
  /// include stack for a file position reference.
  unsigned getChunkID() const {
    assert(isFileID() && "can't get the file id of a non-file sloc!");
    return ID >> FilePosBits;
  }

  unsigned getMacroID() const {
    assert(isMacroID() && "Is not a macro id!");
    return (ID >> MacroSpellingOffsBits) & ((1 << MacroIDBits)-1);
  }
  
  static SourceLocation getFileLoc(unsigned ChunkID, unsigned FilePos) {
    SourceLocation L;
    // If a FilePos is larger than (1<<FilePosBits), the SourceManager makes
    // enough consequtive ChunkIDs that we have one for each chunk.
    if (FilePos >= ChunkSize) {
      ChunkID += FilePos >> FilePosBits;
      FilePos &= ChunkSize-1;
    }
    
    // FIXME: Find a way to handle out of ChunkID bits!  Maybe MaxFileID is an
    // escape of some sort?
    assert(ChunkID < (1 << ChunkIDBits) && "Out of ChunkID's");
    
    L.ID = (ChunkID << FilePosBits) | FilePos;
    return L;
  }
  
  static bool isValidMacroSpellingOffs(int Val) {
    if (Val >= 0)
      return Val < (1 << (MacroSpellingOffsBits-1));
    return -Val <= (1 << (MacroSpellingOffsBits-1));
  }
  
  static SourceLocation getMacroLoc(unsigned MacroID, int SpellingOffs) {
    assert(MacroID < (1 << MacroIDBits) && "Too many macros!");
    assert(isValidMacroSpellingOffs(SpellingOffs) &&"spelling offs too large!");
    
    // Mask off sign bits.
    SpellingOffs &= (1 << MacroSpellingOffsBits)-1;
    
    SourceLocation L;
    L.ID = (1 << 31) |
           (MacroID << MacroSpellingOffsBits) |
           SpellingOffs;
    return L;
  }

  /// getRawFilePos - Return the byte offset from the start of the file-chunk
  /// referred to by ChunkID.  This method should not be used to get the offset
  /// from the start of the file, instead you should use
  /// SourceManager::getDecomposedFileLoc.  This method will be 
  //  incorrect for large files.
  unsigned getRawFilePos() const { 
    assert(isFileID() && "can't get the file id of a non-file sloc!");
    return ID & (ChunkSize-1);
  }

  int getMacroSpellingOffs() const {
    assert(isMacroID() && "Is not a macro id!");
    int Val = ID & ((1 << MacroSpellingOffsBits)-1);
    // Sign extend it properly.
    unsigned ShAmt = sizeof(int)*8 - MacroSpellingOffsBits;
    return (Val << ShAmt) >> ShAmt;
  }
public:
  
  /// getFileLocWithOffset - Return a source location with the specified offset
  /// from this file SourceLocation.
  SourceLocation getFileLocWithOffset(int Offset) const {
    unsigned ChunkID = getChunkID();
    Offset += getRawFilePos();
    // Handle negative offsets correctly.
    while (Offset < 0) {
      --ChunkID;
      Offset += ChunkSize;
    }
    return getFileLoc(ChunkID, Offset);
  }
  
  /// getRawEncoding - When a SourceLocation itself cannot be used, this returns
  /// an (opaque) 32-bit integer encoding for it.  This should only be passed
  /// to SourceLocation::getFromRawEncoding, it should not be inspected
  /// directly.
  unsigned getRawEncoding() const { return ID; }
  
  
  /// getFromRawEncoding - Turn a raw encoding of a SourceLocation object into
  /// a real SourceLocation.
  static SourceLocation getFromRawEncoding(unsigned Encoding) {
    SourceLocation X;
    X.ID = Encoding;
    return X;
  }
  
  /// Emit - Emit this SourceLocation object to Bitcode.
  void Emit(llvm::Serializer& S) const;
  
  /// ReadVal - Read a SourceLocation object from Bitcode.
  static SourceLocation ReadVal(llvm::Deserializer& D);
};

inline bool operator==(const SourceLocation &LHS, const SourceLocation &RHS) {
  return LHS.getRawEncoding() == RHS.getRawEncoding();
}

inline bool operator!=(const SourceLocation &LHS, const SourceLocation &RHS) {
  return !(LHS == RHS);
}
  
inline bool operator<(const SourceLocation &LHS, const SourceLocation &RHS) {
  return LHS.getRawEncoding() < RHS.getRawEncoding();
}

/// SourceRange - a trival tuple used to represent a source range.
class SourceRange {
  SourceLocation B;
  SourceLocation E;
public:
  SourceRange(): B(SourceLocation()), E(SourceLocation()) {}
  SourceRange(SourceLocation loc) : B(loc), E(loc) {}
  SourceRange(SourceLocation begin, SourceLocation end) : B(begin), E(end) {}
    
  SourceLocation getBegin() const { return B; }
  SourceLocation getEnd() const { return E; }
  
  void setBegin(SourceLocation b) { B = b; }
  void setEnd(SourceLocation e) { E = e; }
  
  bool isValid() const { return B.isValid() && E.isValid(); }
  
  /// Emit - Emit this SourceRange object to Bitcode.
  void Emit(llvm::Serializer& S) const;    

  /// ReadVal - Read a SourceRange object from Bitcode.
  static SourceRange ReadVal(llvm::Deserializer& D);
};
  
/// FullSourceLoc - A SourceLocation and its associated SourceManager.  Useful
/// for argument passing to functions that expect both objects.
class FullSourceLoc : public SourceLocation {
  SourceManager* SrcMgr;
public:
  // Creates a FullSourceLoc where isValid() returns false.
  explicit FullSourceLoc() : SrcMgr((SourceManager*) 0) {}

  explicit FullSourceLoc(SourceLocation Loc, SourceManager &SM) 
    : SourceLocation(Loc), SrcMgr(&SM) {}
    
  SourceManager& getManager() {
    assert (SrcMgr && "SourceManager is NULL.");
    return *SrcMgr;
  }
  
  const SourceManager& getManager() const {
    assert (SrcMgr && "SourceManager is NULL.");
    return *SrcMgr;
  }
  
  FileID getFileID() const;
  
  FullSourceLoc getInstantiationLoc() const;
  FullSourceLoc getSpellingLoc() const;
  FullSourceLoc getIncludeLoc() const;

  unsigned getLineNumber() const;
  unsigned getColumnNumber() const;
  
  unsigned getInstantiationLineNumber() const;
  unsigned getInstantiationColumnNumber() const;

  unsigned getSpellingLineNumber() const;
  unsigned getSpellingColumnNumber() const;

  const char *getCharacterData() const;
  
  const llvm::MemoryBuffer* getBuffer() const;
  
  const char* getSourceName() const;

  bool isInSystemHeader() const;
  
  /// Prints information about this FullSourceLoc to stderr. Useful for
  ///  debugging.
  void dump() const;

  friend inline bool 
  operator==(const FullSourceLoc &LHS, const FullSourceLoc &RHS) {
    return LHS.getRawEncoding() == RHS.getRawEncoding() &&
          LHS.SrcMgr == RHS.SrcMgr;
  }

  friend inline bool 
  operator!=(const FullSourceLoc &LHS, const FullSourceLoc &RHS) {
    return !(LHS == RHS);
  }

};
 
}  // end namespace clang

namespace llvm {
  /// Define DenseMapInfo so that FileID's can be used as keys in DenseMap and
  /// DenseSets.
  template <>
  struct DenseMapInfo<clang::FileID> {
    static inline clang::FileID getEmptyKey() {
      return clang::FileID();
    }
    static inline clang::FileID getTombstoneKey() {
      return clang::FileID::getSentinel(); 
    }
    
    static unsigned getHashValue(clang::FileID S) {
      return S.getHashValue();
    }
    
    static bool isEqual(clang::FileID LHS, clang::FileID RHS) {
      return LHS == RHS;
    }
    
    static bool isPod() { return true; }
  };
  
}  // end namespace llvm

#endif
