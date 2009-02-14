//===--- SourceManager.cpp - Track and cache source files -----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file implements the SourceManager interface.
//
//===----------------------------------------------------------------------===//

#include "clang/Basic/SourceManager.h"
#include "clang/Basic/FileManager.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/System/Path.h"
#include "llvm/Bitcode/Serialize.h"
#include "llvm/Bitcode/Deserialize.h"
#include "llvm/Support/Streams.h"
#include <algorithm>
using namespace clang;
using namespace SrcMgr;
using llvm::MemoryBuffer;

//===----------------------------------------------------------------------===//
// SourceManager Helper Classes
//===----------------------------------------------------------------------===//

ContentCache::~ContentCache() {
  delete Buffer;
}

/// getSizeBytesMapped - Returns the number of bytes actually mapped for
///  this ContentCache.  This can be 0 if the MemBuffer was not actually
///  instantiated.
unsigned ContentCache::getSizeBytesMapped() const {
  return Buffer ? Buffer->getBufferSize() : 0;
}

/// getSize - Returns the size of the content encapsulated by this ContentCache.
///  This can be the size of the source file or the size of an arbitrary
///  scratch buffer.  If the ContentCache encapsulates a source file, that
///  file is not lazily brought in from disk to satisfy this query.
unsigned ContentCache::getSize() const {
  return Entry ? Entry->getSize() : Buffer->getBufferSize();
}

const llvm::MemoryBuffer *ContentCache::getBuffer() const {  
  // Lazily create the Buffer for ContentCaches that wrap files.
  if (!Buffer && Entry) {
    // FIXME: Should we support a way to not have to do this check over
    //   and over if we cannot open the file?
    Buffer = MemoryBuffer::getFile(Entry->getName(), 0, Entry->getSize());
  }
  return Buffer;
}

//===----------------------------------------------------------------------===//
// Line Table Implementation
//===----------------------------------------------------------------------===//

namespace clang {
struct LineEntry {
  /// FileOffset - The offset in this file that the line entry occurs at.
  unsigned FileOffset;
  
  /// LineNo - The presumed line number of this line entry: #line 4.
  unsigned LineNo;
  
  /// FilenameID - The ID of the filename identified by this line entry:
  /// #line 4 "foo.c".  This is -1 if not specified.
  int FilenameID;
  
  /// Flags - Set the 0 if no flags, 1 if a system header, 
  SrcMgr::CharacteristicKind FileKind;
  
  /// IncludeOffset - This is the offset of the virtual include stack location,
  /// which is manipulated by GNU linemarker directives.  If this is 0 then
  /// there is no virtual #includer.
  unsigned IncludeOffset;
  
  static LineEntry get(unsigned Offs, unsigned Line, int Filename,
                       SrcMgr::CharacteristicKind FileKind,
                       unsigned IncludeOffset) {
    LineEntry E;
    E.FileOffset = Offs;
    E.LineNo = Line;
    E.FilenameID = Filename;
    E.FileKind = FileKind;
    E.IncludeOffset = IncludeOffset;
    return E;
  }
};

// needed for FindNearestLineEntry (upper_bound of LineEntry)
inline bool operator<(const LineEntry &lhs, const LineEntry &rhs) {
  // FIXME: should check the other field?
  return lhs.FileOffset < rhs.FileOffset;
}

inline bool operator<(const LineEntry &E, unsigned Offset) {
  return E.FileOffset < Offset;
}

inline bool operator<(unsigned Offset, const LineEntry &E) {
  return Offset < E.FileOffset;
}
  
/// LineTableInfo - This class is used to hold and unique data used to
/// represent #line information.
class LineTableInfo {
  /// FilenameIDs - This map is used to assign unique IDs to filenames in
  /// #line directives.  This allows us to unique the filenames that
  /// frequently reoccur and reference them with indices.  FilenameIDs holds
  /// the mapping from string -> ID, and FilenamesByID holds the mapping of ID
  /// to string.
  llvm::StringMap<unsigned, llvm::BumpPtrAllocator> FilenameIDs;
  std::vector<llvm::StringMapEntry<unsigned>*> FilenamesByID;
  
  /// LineEntries - This is a map from FileIDs to a list of line entries (sorted
  /// by the offset they occur in the file.
  std::map<unsigned, std::vector<LineEntry> > LineEntries;
public:
  LineTableInfo() {
  }
  
  void clear() {
    FilenameIDs.clear();
    FilenamesByID.clear();
  }
  
  ~LineTableInfo() {}
  
  unsigned getLineTableFilenameID(const char *Ptr, unsigned Len);
  const char *getFilename(unsigned ID) const {
    assert(ID < FilenamesByID.size() && "Invalid FilenameID");
    return FilenamesByID[ID]->getKeyData();
  }
  
  void AddLineNote(unsigned FID, unsigned Offset,
                   unsigned LineNo, int FilenameID);
  void AddLineNote(unsigned FID, unsigned Offset,
                   unsigned LineNo, int FilenameID,
                   unsigned EntryExit, SrcMgr::CharacteristicKind FileKind);

  
  /// FindNearestLineEntry - Find the line entry nearest to FID that is before
  /// it.  If there is no line entry before Offset in FID, return null.
  const LineEntry *FindNearestLineEntry(unsigned FID, unsigned Offset);
};
} // namespace clang

unsigned LineTableInfo::getLineTableFilenameID(const char *Ptr, unsigned Len) {
  // Look up the filename in the string table, returning the pre-existing value
  // if it exists.
  llvm::StringMapEntry<unsigned> &Entry = 
    FilenameIDs.GetOrCreateValue(Ptr, Ptr+Len, ~0U);
  if (Entry.getValue() != ~0U)
    return Entry.getValue();
  
  // Otherwise, assign this the next available ID.
  Entry.setValue(FilenamesByID.size());
  FilenamesByID.push_back(&Entry);
  return FilenamesByID.size()-1;
}

/// AddLineNote - Add a line note to the line table that indicates that there
/// is a #line at the specified FID/Offset location which changes the presumed
/// location to LineNo/FilenameID.
void LineTableInfo::AddLineNote(unsigned FID, unsigned Offset,
                                unsigned LineNo, int FilenameID) {
  std::vector<LineEntry> &Entries = LineEntries[FID];
  
  assert((Entries.empty() || Entries.back().FileOffset < Offset) &&
         "Adding line entries out of order!");
  
  SrcMgr::CharacteristicKind Kind = SrcMgr::C_User;
  unsigned IncludeOffset = 0;
  
  if (!Entries.empty()) {
    // If this is a '#line 4' after '#line 42 "foo.h"', make sure to remember
    // that we are still in "foo.h".
    if (FilenameID == -1)
      FilenameID = Entries.back().FilenameID;
    
    // If we are after a line marker that switched us to system header mode, or
    // that set #include information, preserve it.
    Kind = Entries.back().FileKind;
    IncludeOffset = Entries.back().IncludeOffset;
  }
  
  Entries.push_back(LineEntry::get(Offset, LineNo, FilenameID, Kind,
                                   IncludeOffset));
}

/// AddLineNote This is the same as the previous version of AddLineNote, but is
/// used for GNU line markers.  If EntryExit is 0, then this doesn't change the
/// presumed #include stack.  If it is 1, this is a file entry, if it is 2 then
/// this is a file exit.  FileKind specifies whether this is a system header or
/// extern C system header.
void LineTableInfo::AddLineNote(unsigned FID, unsigned Offset,
                                unsigned LineNo, int FilenameID,
                                unsigned EntryExit,
                                SrcMgr::CharacteristicKind FileKind) {
  assert(FilenameID != -1 && "Unspecified filename should use other accessor");
  
  std::vector<LineEntry> &Entries = LineEntries[FID];
  
  assert((Entries.empty() || Entries.back().FileOffset < Offset) &&
         "Adding line entries out of order!");

  unsigned IncludeOffset = 0;
  if (EntryExit == 0) {  // No #include stack change.
    IncludeOffset = Entries.empty() ? 0 : Entries.back().IncludeOffset;
  } else if (EntryExit == 1) {
    IncludeOffset = Offset-1;
  } else if (EntryExit == 2) {
    assert(!Entries.empty() && Entries.back().IncludeOffset &&
       "PPDirectives should have caught case when popping empty include stack");
    
    // Get the include loc of the last entries' include loc as our include loc.
    IncludeOffset = 0;
    if (const LineEntry *PrevEntry =
          FindNearestLineEntry(FID, Entries.back().IncludeOffset))
      IncludeOffset = PrevEntry->IncludeOffset;
  }
  
  Entries.push_back(LineEntry::get(Offset, LineNo, FilenameID, FileKind,
                                   IncludeOffset));
}


/// FindNearestLineEntry - Find the line entry nearest to FID that is before
/// it.  If there is no line entry before Offset in FID, return null.
const LineEntry *LineTableInfo::FindNearestLineEntry(unsigned FID, 
                                                     unsigned Offset) {
  const std::vector<LineEntry> &Entries = LineEntries[FID];
  assert(!Entries.empty() && "No #line entries for this FID after all!");

  // It is very common for the query to be after the last #line, check this
  // first.
  if (Entries.back().FileOffset <= Offset)
    return &Entries.back();

  // Do a binary search to find the maximal element that is still before Offset.
  std::vector<LineEntry>::const_iterator I =
    std::upper_bound(Entries.begin(), Entries.end(), Offset);
  if (I == Entries.begin()) return 0;
  return &*--I;
}


/// getLineTableFilenameID - Return the uniqued ID for the specified filename.
/// 
unsigned SourceManager::getLineTableFilenameID(const char *Ptr, unsigned Len) {
  if (LineTable == 0)
    LineTable = new LineTableInfo();
  return LineTable->getLineTableFilenameID(Ptr, Len);
}


/// AddLineNote - Add a line note to the line table for the FileID and offset
/// specified by Loc.  If FilenameID is -1, it is considered to be
/// unspecified.
void SourceManager::AddLineNote(SourceLocation Loc, unsigned LineNo,
                                int FilenameID) {
  std::pair<FileID, unsigned> LocInfo = getDecomposedInstantiationLoc(Loc);
  
  const SrcMgr::FileInfo &FileInfo = getSLocEntry(LocInfo.first).getFile();

  // Remember that this file has #line directives now if it doesn't already.
  const_cast<SrcMgr::FileInfo&>(FileInfo).setHasLineDirectives();
  
  if (LineTable == 0)
    LineTable = new LineTableInfo();
  LineTable->AddLineNote(LocInfo.first.ID, LocInfo.second, LineNo, FilenameID);
}

/// AddLineNote - Add a GNU line marker to the line table.
void SourceManager::AddLineNote(SourceLocation Loc, unsigned LineNo,
                                int FilenameID, bool IsFileEntry,
                                bool IsFileExit, bool IsSystemHeader,
                                bool IsExternCHeader) {
  // If there is no filename and no flags, this is treated just like a #line,
  // which does not change the flags of the previous line marker.
  if (FilenameID == -1) {
    assert(!IsFileEntry && !IsFileExit && !IsSystemHeader && !IsExternCHeader &&
           "Can't set flags without setting the filename!");
    return AddLineNote(Loc, LineNo, FilenameID);
  }
  
  std::pair<FileID, unsigned> LocInfo = getDecomposedInstantiationLoc(Loc);
  const SrcMgr::FileInfo &FileInfo = getSLocEntry(LocInfo.first).getFile();
  
  // Remember that this file has #line directives now if it doesn't already.
  const_cast<SrcMgr::FileInfo&>(FileInfo).setHasLineDirectives();
  
  if (LineTable == 0)
    LineTable = new LineTableInfo();
  
  SrcMgr::CharacteristicKind FileKind;
  if (IsExternCHeader)
    FileKind = SrcMgr::C_ExternCSystem;
  else if (IsSystemHeader)
    FileKind = SrcMgr::C_System;
  else
    FileKind = SrcMgr::C_User;
  
  unsigned EntryExit = 0;
  if (IsFileEntry)
    EntryExit = 1;
  else if (IsFileExit)
    EntryExit = 2;
  
  LineTable->AddLineNote(LocInfo.first.ID, LocInfo.second, LineNo, FilenameID,
                         EntryExit, FileKind);
}


//===----------------------------------------------------------------------===//
// Private 'Create' methods.
//===----------------------------------------------------------------------===//

SourceManager::~SourceManager() {
  delete LineTable;
  
  // Delete FileEntry objects corresponding to content caches.  Since the actual
  // content cache objects are bump pointer allocated, we just have to run the
  // dtors, but we call the deallocate method for completeness.
  for (unsigned i = 0, e = MemBufferInfos.size(); i != e; ++i) {
    MemBufferInfos[i]->~ContentCache();
    ContentCacheAlloc.Deallocate(MemBufferInfos[i]);
  }
  for (llvm::DenseMap<const FileEntry*, SrcMgr::ContentCache*>::iterator
       I = FileInfos.begin(), E = FileInfos.end(); I != E; ++I) {
    I->second->~ContentCache();
    ContentCacheAlloc.Deallocate(I->second);
  }
}

void SourceManager::clearIDTables() {
  MainFileID = FileID();
  SLocEntryTable.clear();
  LastLineNoFileIDQuery = FileID();
  LastLineNoContentCache = 0;
  LastFileIDLookup = FileID();
  
  if (LineTable)
    LineTable->clear();
  
  // Use up FileID #0 as an invalid instantiation.
  NextOffset = 0;
  createInstantiationLoc(SourceLocation(), SourceLocation(), 1);
}

/// getOrCreateContentCache - Create or return a cached ContentCache for the
/// specified file.
const ContentCache *
SourceManager::getOrCreateContentCache(const FileEntry *FileEnt) {
  assert(FileEnt && "Didn't specify a file entry to use?");
  
  // Do we already have information about this file?
  ContentCache *&Entry = FileInfos[FileEnt];
  if (Entry) return Entry;
  
  // Nope, create a new Cache entry.  Make sure it is at least 8-byte aligned
  // so that FileInfo can use the low 3 bits of the pointer for its own
  // nefarious purposes.
  unsigned EntryAlign = llvm::AlignOf<ContentCache>::Alignment;
  EntryAlign = std::max(8U, EntryAlign);
  Entry = ContentCacheAlloc.Allocate<ContentCache>(1, EntryAlign);
  new (Entry) ContentCache(FileEnt);
  return Entry;
}


/// createMemBufferContentCache - Create a new ContentCache for the specified
///  memory buffer.  This does no caching.
const ContentCache*
SourceManager::createMemBufferContentCache(const MemoryBuffer *Buffer) {
  // Add a new ContentCache to the MemBufferInfos list and return it.  Make sure
  // it is at least 8-byte aligned so that FileInfo can use the low 3 bits of
  // the pointer for its own nefarious purposes.
  unsigned EntryAlign = llvm::AlignOf<ContentCache>::Alignment;
  EntryAlign = std::max(8U, EntryAlign);
  ContentCache *Entry = ContentCacheAlloc.Allocate<ContentCache>(1, EntryAlign);
  new (Entry) ContentCache();
  MemBufferInfos.push_back(Entry);
  Entry->setBuffer(Buffer);
  return Entry;
}

//===----------------------------------------------------------------------===//
// Methods to create new FileID's and instantiations.
//===----------------------------------------------------------------------===//

/// createFileID - Create a new fileID for the specified ContentCache and
/// include position.  This works regardless of whether the ContentCache
/// corresponds to a file or some other input source.
FileID SourceManager::createFileID(const ContentCache *File,
                                   SourceLocation IncludePos,
                                   SrcMgr::CharacteristicKind FileCharacter) {
  SLocEntryTable.push_back(SLocEntry::get(NextOffset, 
                                          FileInfo::get(IncludePos, File,
                                                        FileCharacter)));
  unsigned FileSize = File->getSize();
  assert(NextOffset+FileSize+1 > NextOffset && "Ran out of source locations!");
  NextOffset += FileSize+1;
  
  // Set LastFileIDLookup to the newly created file.  The next getFileID call is
  // almost guaranteed to be from that file.
  return LastFileIDLookup = FileID::get(SLocEntryTable.size()-1);
}

/// createInstantiationLoc - Return a new SourceLocation that encodes the fact
/// that a token from SpellingLoc should actually be referenced from
/// InstantiationLoc.
SourceLocation SourceManager::createInstantiationLoc(SourceLocation SpellingLoc,
                                                     SourceLocation InstantLoc,
                                                     unsigned TokLength) {
  SLocEntryTable.push_back(SLocEntry::get(NextOffset, 
                                          InstantiationInfo::get(InstantLoc,
                                                                 SpellingLoc)));
  assert(NextOffset+TokLength+1 > NextOffset && "Ran out of source locations!");
  NextOffset += TokLength+1;
  return SourceLocation::getMacroLoc(NextOffset-(TokLength+1));
}

/// getBufferData - Return a pointer to the start and end of the source buffer
/// data for the specified FileID.
std::pair<const char*, const char*>
SourceManager::getBufferData(FileID FID) const {
  const llvm::MemoryBuffer *Buf = getBuffer(FID);
  return std::make_pair(Buf->getBufferStart(), Buf->getBufferEnd());
}


//===----------------------------------------------------------------------===//
// SourceLocation manipulation methods.
//===----------------------------------------------------------------------===//

/// getFileIDSlow - Return the FileID for a SourceLocation.  This is a very hot
/// method that is used for all SourceManager queries that start with a
/// SourceLocation object.  It is responsible for finding the entry in
/// SLocEntryTable which contains the specified location.
///
FileID SourceManager::getFileIDSlow(unsigned SLocOffset) const {
  assert(SLocOffset && "Invalid FileID");
  
  // After the first and second level caches, I see two common sorts of
  // behavior: 1) a lot of searched FileID's are "near" the cached file location
  // or are "near" the cached instantiation location.  2) others are just
  // completely random and may be a very long way away.
  //
  // To handle this, we do a linear search for up to 8 steps to catch #1 quickly
  // then we fall back to a less cache efficient, but more scalable, binary
  // search to find the location.
  
  // See if this is near the file point - worst case we start scanning from the
  // most newly created FileID.
  std::vector<SrcMgr::SLocEntry>::const_iterator I;
  
  if (SLocEntryTable[LastFileIDLookup.ID].getOffset() < SLocOffset) {
    // Neither loc prunes our search.
    I = SLocEntryTable.end();
  } else {
    // Perhaps it is near the file point.
    I = SLocEntryTable.begin()+LastFileIDLookup.ID;
  }

  // Find the FileID that contains this.  "I" is an iterator that points to a
  // FileID whose offset is known to be larger than SLocOffset.
  unsigned NumProbes = 0;
  while (1) {
    --I;
    if (I->getOffset() <= SLocOffset) {
#if 0
      printf("lin %d -> %d [%s] %d %d\n", SLocOffset,
             I-SLocEntryTable.begin(),
             I->isInstantiation() ? "inst" : "file",
             LastFileIDLookup.ID,  int(SLocEntryTable.end()-I));
#endif
      FileID Res = FileID::get(I-SLocEntryTable.begin());
      
      // If this isn't an instantiation, remember it.  We have good locality
      // across FileID lookups.
      if (!I->isInstantiation())
        LastFileIDLookup = Res;
      NumLinearScans += NumProbes+1;
      return Res;
    }
    if (++NumProbes == 8)
      break;
  }
  
  // Convert "I" back into an index.  We know that it is an entry whose index is
  // larger than the offset we are looking for.
  unsigned GreaterIndex = I-SLocEntryTable.begin();
  // LessIndex - This is the lower bound of the range that we're searching.
  // We know that the offset corresponding to the FileID is is less than
  // SLocOffset.
  unsigned LessIndex = 0;
  NumProbes = 0;
  while (1) {
    unsigned MiddleIndex = (GreaterIndex-LessIndex)/2+LessIndex;
    unsigned MidOffset = SLocEntryTable[MiddleIndex].getOffset();
    
    ++NumProbes;
    
    // If the offset of the midpoint is too large, chop the high side of the
    // range to the midpoint.
    if (MidOffset > SLocOffset) {
      GreaterIndex = MiddleIndex;
      continue;
    }
    
    // If the middle index contains the value, succeed and return.
    if (isOffsetInFileID(FileID::get(MiddleIndex), SLocOffset)) {
#if 0
      printf("bin %d -> %d [%s] %d %d\n", SLocOffset,
             I-SLocEntryTable.begin(),
             I->isInstantiation() ? "inst" : "file",
             LastFileIDLookup.ID, int(SLocEntryTable.end()-I));
#endif
      FileID Res = FileID::get(MiddleIndex);

      // If this isn't an instantiation, remember it.  We have good locality
      // across FileID lookups.
      if (!I->isInstantiation())
        LastFileIDLookup = Res;
      NumBinaryProbes += NumProbes;
      return Res;
    }
    
    // Otherwise, move the low-side up to the middle index.
    LessIndex = MiddleIndex;
  }
}

SourceLocation SourceManager::
getInstantiationLocSlowCase(SourceLocation Loc) const {
  do {
    std::pair<FileID, unsigned> LocInfo = getDecomposedLoc(Loc);
    Loc =getSLocEntry(LocInfo.first).getInstantiation().getInstantiationLoc();
    Loc = Loc.getFileLocWithOffset(LocInfo.second);
  } while (!Loc.isFileID());

  return Loc;
}

SourceLocation SourceManager::getSpellingLocSlowCase(SourceLocation Loc) const {
  do {
    std::pair<FileID, unsigned> LocInfo = getDecomposedLoc(Loc);
    Loc = getSLocEntry(LocInfo.first).getInstantiation().getSpellingLoc();
    Loc = Loc.getFileLocWithOffset(LocInfo.second);
  } while (!Loc.isFileID());
  return Loc;
}


std::pair<FileID, unsigned>
SourceManager::getDecomposedInstantiationLocSlowCase(const SrcMgr::SLocEntry *E,
                                                     unsigned Offset) const {
  // If this is an instantiation record, walk through all the instantiation
  // points.
  FileID FID;
  SourceLocation Loc;
  do {
    Loc = E->getInstantiation().getInstantiationLoc();
    
    FID = getFileID(Loc);
    E = &getSLocEntry(FID);
    Offset += Loc.getOffset()-E->getOffset();
  } while (!Loc.isFileID());
  
  return std::make_pair(FID, Offset);
}

std::pair<FileID, unsigned>
SourceManager::getDecomposedSpellingLocSlowCase(const SrcMgr::SLocEntry *E,
                                                unsigned Offset) const {
  // If this is an instantiation record, walk through all the instantiation
  // points.
  FileID FID;
  SourceLocation Loc;
  do {
    Loc = E->getInstantiation().getSpellingLoc();
    
    FID = getFileID(Loc);
    E = &getSLocEntry(FID);
    Offset += Loc.getOffset()-E->getOffset();
  } while (!Loc.isFileID());
  
  return std::make_pair(FID, Offset);
}


//===----------------------------------------------------------------------===//
// Queries about the code at a SourceLocation.
//===----------------------------------------------------------------------===//

/// getCharacterData - Return a pointer to the start of the specified location
/// in the appropriate MemoryBuffer.
const char *SourceManager::getCharacterData(SourceLocation SL) const {
  // Note that this is a hot function in the getSpelling() path, which is
  // heavily used by -E mode.
  std::pair<FileID, unsigned> LocInfo = getDecomposedSpellingLoc(SL);
  
  // Note that calling 'getBuffer()' may lazily page in a source file.
  return getSLocEntry(LocInfo.first).getFile().getContentCache()
              ->getBuffer()->getBufferStart() + LocInfo.second;
}


/// getColumnNumber - Return the column # for the specified file position.
/// this is significantly cheaper to compute than the line number.
unsigned SourceManager::getColumnNumber(FileID FID, unsigned FilePos) const {
  const char *Buf = getBuffer(FID)->getBufferStart();
  
  unsigned LineStart = FilePos;
  while (LineStart && Buf[LineStart-1] != '\n' && Buf[LineStart-1] != '\r')
    --LineStart;
  return FilePos-LineStart+1;
}

unsigned SourceManager::getSpellingColumnNumber(SourceLocation Loc) const {
  if (Loc.isInvalid()) return 0;
  std::pair<FileID, unsigned> LocInfo = getDecomposedSpellingLoc(Loc);
  return getColumnNumber(LocInfo.first, LocInfo.second);
}

unsigned SourceManager::getInstantiationColumnNumber(SourceLocation Loc) const {
  if (Loc.isInvalid()) return 0;
  std::pair<FileID, unsigned> LocInfo = getDecomposedInstantiationLoc(Loc);
  return getColumnNumber(LocInfo.first, LocInfo.second);
}



static void ComputeLineNumbers(ContentCache* FI,
                               llvm::BumpPtrAllocator &Alloc) DISABLE_INLINE;
static void ComputeLineNumbers(ContentCache* FI, llvm::BumpPtrAllocator &Alloc){ 
  // Note that calling 'getBuffer()' may lazily page in the file.
  const MemoryBuffer *Buffer = FI->getBuffer();
  
  // Find the file offsets of all of the *physical* source lines.  This does
  // not look at trigraphs, escaped newlines, or anything else tricky.
  std::vector<unsigned> LineOffsets;
  
  // Line #1 starts at char 0.
  LineOffsets.push_back(0);
  
  const unsigned char *Buf = (const unsigned char *)Buffer->getBufferStart();
  const unsigned char *End = (const unsigned char *)Buffer->getBufferEnd();
  unsigned Offs = 0;
  while (1) {
    // Skip over the contents of the line.
    // TODO: Vectorize this?  This is very performance sensitive for programs
    // with lots of diagnostics and in -E mode.
    const unsigned char *NextBuf = (const unsigned char *)Buf;
    while (*NextBuf != '\n' && *NextBuf != '\r' && *NextBuf != '\0')
      ++NextBuf;
    Offs += NextBuf-Buf;
    Buf = NextBuf;
    
    if (Buf[0] == '\n' || Buf[0] == '\r') {
      // If this is \n\r or \r\n, skip both characters.
      if ((Buf[1] == '\n' || Buf[1] == '\r') && Buf[0] != Buf[1])
        ++Offs, ++Buf;
      ++Offs, ++Buf;
      LineOffsets.push_back(Offs);
    } else {
      // Otherwise, this is a null.  If end of file, exit.
      if (Buf == End) break;
      // Otherwise, skip the null.
      ++Offs, ++Buf;
    }
  }
  
  // Copy the offsets into the FileInfo structure.
  FI->NumLines = LineOffsets.size();
  FI->SourceLineCache = Alloc.Allocate<unsigned>(LineOffsets.size());
  std::copy(LineOffsets.begin(), LineOffsets.end(), FI->SourceLineCache);
}

/// getLineNumber - Given a SourceLocation, return the spelling line number
/// for the position indicated.  This requires building and caching a table of
/// line offsets for the MemoryBuffer, so this is not cheap: use only when
/// about to emit a diagnostic.
unsigned SourceManager::getLineNumber(FileID FID, unsigned FilePos) const {
  ContentCache *Content;
  if (LastLineNoFileIDQuery == FID)
    Content = LastLineNoContentCache;
  else
    Content = const_cast<ContentCache*>(getSLocEntry(FID)
                                        .getFile().getContentCache());
  
  // If this is the first use of line information for this buffer, compute the
  /// SourceLineCache for it on demand.
  if (Content->SourceLineCache == 0)
    ComputeLineNumbers(Content, ContentCacheAlloc);

  // Okay, we know we have a line number table.  Do a binary search to find the
  // line number that this character position lands on.
  unsigned *SourceLineCache = Content->SourceLineCache;
  unsigned *SourceLineCacheStart = SourceLineCache;
  unsigned *SourceLineCacheEnd = SourceLineCache + Content->NumLines;
  
  unsigned QueriedFilePos = FilePos+1;

  // If the previous query was to the same file, we know both the file pos from
  // that query and the line number returned.  This allows us to narrow the
  // search space from the entire file to something near the match.
  if (LastLineNoFileIDQuery == FID) {
    if (QueriedFilePos >= LastLineNoFilePos) {
      SourceLineCache = SourceLineCache+LastLineNoResult-1;
      
      // The query is likely to be nearby the previous one.  Here we check to
      // see if it is within 5, 10 or 20 lines.  It can be far away in cases
      // where big comment blocks and vertical whitespace eat up lines but
      // contribute no tokens.
      if (SourceLineCache+5 < SourceLineCacheEnd) {
        if (SourceLineCache[5] > QueriedFilePos)
          SourceLineCacheEnd = SourceLineCache+5;
        else if (SourceLineCache+10 < SourceLineCacheEnd) {
          if (SourceLineCache[10] > QueriedFilePos)
            SourceLineCacheEnd = SourceLineCache+10;
          else if (SourceLineCache+20 < SourceLineCacheEnd) {
            if (SourceLineCache[20] > QueriedFilePos)
              SourceLineCacheEnd = SourceLineCache+20;
          }
        }
      }
    } else {
      SourceLineCacheEnd = SourceLineCache+LastLineNoResult+1;
    }
  }
  
  // If the spread is large, do a "radix" test as our initial guess, based on
  // the assumption that lines average to approximately the same length.
  // NOTE: This is currently disabled, as it does not appear to be profitable in
  // initial measurements.
  if (0 && SourceLineCacheEnd-SourceLineCache > 20) {
    unsigned FileLen = Content->SourceLineCache[Content->NumLines-1];
    
    // Take a stab at guessing where it is.
    unsigned ApproxPos = Content->NumLines*QueriedFilePos / FileLen;
    
    // Check for -10 and +10 lines.
    unsigned LowerBound = std::max(int(ApproxPos-10), 0);
    unsigned UpperBound = std::min(ApproxPos+10, FileLen);

    // If the computed lower bound is less than the query location, move it in.
    if (SourceLineCache < SourceLineCacheStart+LowerBound &&
        SourceLineCacheStart[LowerBound] < QueriedFilePos)
      SourceLineCache = SourceLineCacheStart+LowerBound;
    
    // If the computed upper bound is greater than the query location, move it.
    if (SourceLineCacheEnd > SourceLineCacheStart+UpperBound &&
        SourceLineCacheStart[UpperBound] >= QueriedFilePos)
      SourceLineCacheEnd = SourceLineCacheStart+UpperBound;
  }
  
  unsigned *Pos
    = std::lower_bound(SourceLineCache, SourceLineCacheEnd, QueriedFilePos);
  unsigned LineNo = Pos-SourceLineCacheStart;
  
  LastLineNoFileIDQuery = FID;
  LastLineNoContentCache = Content;
  LastLineNoFilePos = QueriedFilePos;
  LastLineNoResult = LineNo;
  return LineNo;
}

unsigned SourceManager::getInstantiationLineNumber(SourceLocation Loc) const {
  if (Loc.isInvalid()) return 0;
  std::pair<FileID, unsigned> LocInfo = getDecomposedInstantiationLoc(Loc);
  return getLineNumber(LocInfo.first, LocInfo.second);
}
unsigned SourceManager::getSpellingLineNumber(SourceLocation Loc) const {
  if (Loc.isInvalid()) return 0;
  std::pair<FileID, unsigned> LocInfo = getDecomposedSpellingLoc(Loc);
  return getLineNumber(LocInfo.first, LocInfo.second);
}

/// getFileCharacteristic - return the file characteristic of the specified
/// source location, indicating whether this is a normal file, a system 
/// header, or an "implicit extern C" system header.
///
/// This state can be modified with flags on GNU linemarker directives like:
///   # 4 "foo.h" 3
/// which changes all source locations in the current file after that to be
/// considered to be from a system header.
SrcMgr::CharacteristicKind 
SourceManager::getFileCharacteristic(SourceLocation Loc) const {
  assert(!Loc.isInvalid() && "Can't get file characteristic of invalid loc!");
  std::pair<FileID, unsigned> LocInfo = getDecomposedInstantiationLoc(Loc);
  const SrcMgr::FileInfo &FI = getSLocEntry(LocInfo.first).getFile();

  // If there are no #line directives in this file, just return the whole-file
  // state.
  if (!FI.hasLineDirectives())
    return FI.getFileCharacteristic();
  
  assert(LineTable && "Can't have linetable entries without a LineTable!");
  // See if there is a #line directive before the location.
  const LineEntry *Entry =
    LineTable->FindNearestLineEntry(LocInfo.first.ID, LocInfo.second);
  
  // If this is before the first line marker, use the file characteristic.
  if (!Entry)
    return FI.getFileCharacteristic();

  return Entry->FileKind;
}


/// getPresumedLoc - This method returns the "presumed" location of a
/// SourceLocation specifies.  A "presumed location" can be modified by #line
/// or GNU line marker directives.  This provides a view on the data that a
/// user should see in diagnostics, for example.
///
/// Note that a presumed location is always given as the instantiation point
/// of an instantiation location, not at the spelling location.
PresumedLoc SourceManager::getPresumedLoc(SourceLocation Loc) const {
  if (Loc.isInvalid()) return PresumedLoc();
  
  // Presumed locations are always for instantiation points.
  std::pair<FileID, unsigned> LocInfo = getDecomposedInstantiationLoc(Loc);
  
  const SrcMgr::FileInfo &FI = getSLocEntry(LocInfo.first).getFile();
  const SrcMgr::ContentCache *C = FI.getContentCache();
  
  // To get the source name, first consult the FileEntry (if one exists)
  // before the MemBuffer as this will avoid unnecessarily paging in the
  // MemBuffer.
  const char *Filename = 
    C->Entry ? C->Entry->getName() : C->getBuffer()->getBufferIdentifier();
  unsigned LineNo = getLineNumber(LocInfo.first, LocInfo.second);
  unsigned ColNo  = getColumnNumber(LocInfo.first, LocInfo.second);
  SourceLocation IncludeLoc = FI.getIncludeLoc();
  
  // If we have #line directives in this file, update and overwrite the physical
  // location info if appropriate.
  if (FI.hasLineDirectives()) {
    assert(LineTable && "Can't have linetable entries without a LineTable!");
    // See if there is a #line directive before this.  If so, get it.
    if (const LineEntry *Entry =
          LineTable->FindNearestLineEntry(LocInfo.first.ID, LocInfo.second)) {
      // If the LineEntry indicates a filename, use it.
      if (Entry->FilenameID != -1)
        Filename = LineTable->getFilename(Entry->FilenameID);

      // Use the line number specified by the LineEntry.  This line number may
      // be multiple lines down from the line entry.  Add the difference in
      // physical line numbers from the query point and the line marker to the
      // total.
      unsigned MarkerLineNo = getLineNumber(LocInfo.first, Entry->FileOffset);
      LineNo = Entry->LineNo + (LineNo-MarkerLineNo-1);
      
      // Note that column numbers are not molested by line markers.
      
      // Handle virtual #include manipulation.
      if (Entry->IncludeOffset) {
        IncludeLoc = getLocForStartOfFile(LocInfo.first);
        IncludeLoc = IncludeLoc.getFileLocWithOffset(Entry->IncludeOffset);
      }
    }
  }

  return PresumedLoc(Filename, LineNo, ColNo, IncludeLoc);
}

//===----------------------------------------------------------------------===//
// Other miscellaneous methods.
//===----------------------------------------------------------------------===//


/// PrintStats - Print statistics to stderr.
///
void SourceManager::PrintStats() const {
  llvm::cerr << "\n*** Source Manager Stats:\n";
  llvm::cerr << FileInfos.size() << " files mapped, " << MemBufferInfos.size()
             << " mem buffers mapped.\n";
  llvm::cerr << SLocEntryTable.size() << " SLocEntry's allocated, "
             << NextOffset << "B of Sloc address space used.\n";
    
  unsigned NumLineNumsComputed = 0;
  unsigned NumFileBytesMapped = 0;
  for (fileinfo_iterator I = fileinfo_begin(), E = fileinfo_end(); I != E; ++I){
    NumLineNumsComputed += I->second->SourceLineCache != 0;
    NumFileBytesMapped  += I->second->getSizeBytesMapped();
  }
  
  llvm::cerr << NumFileBytesMapped << " bytes of files mapped, "
             << NumLineNumsComputed << " files with line #'s computed.\n";
  llvm::cerr << "FileID scans: " << NumLinearScans << " linear, "
             << NumBinaryProbes << " binary.\n";
}

//===----------------------------------------------------------------------===//
// Serialization.
//===----------------------------------------------------------------------===//
  
void ContentCache::Emit(llvm::Serializer& S) const {
  S.FlushRecord();
  S.EmitPtr(this);

  if (Entry) {
    llvm::sys::Path Fname(Buffer->getBufferIdentifier());

    if (Fname.isAbsolute())
      S.EmitCStr(Fname.c_str());
    else {
      // Create an absolute path.
      // FIXME: This will potentially contain ".." and "." in the path.
      llvm::sys::Path path = llvm::sys::Path::GetCurrentDirectory();
      path.appendComponent(Fname.c_str());      
      S.EmitCStr(path.c_str());
    }
  }
  else {
    const char* p = Buffer->getBufferStart();
    const char* e = Buffer->getBufferEnd();
    
    S.EmitInt(e-p);
    
    for ( ; p != e; ++p)
      S.EmitInt(*p);    
  }
  
  S.FlushRecord();  
}

void ContentCache::ReadToSourceManager(llvm::Deserializer& D,
                                       SourceManager& SMgr,
                                       FileManager* FMgr,
                                       std::vector<char>& Buf) {
  if (FMgr) {
    llvm::SerializedPtrID PtrID = D.ReadPtrID();    
    D.ReadCStr(Buf,false);
    
    // Create/fetch the FileEntry.
    const char* start = &Buf[0];
    const FileEntry* E = FMgr->getFile(start,start+Buf.size());
    
    // FIXME: Ideally we want a lazy materialization of the ContentCache
    //  anyway, because we don't want to read in source files unless this
    //  is absolutely needed.
    if (!E)
      D.RegisterPtr(PtrID,NULL);
    else
      // Get the ContextCache object and register it with the deserializer.
      D.RegisterPtr(PtrID, SMgr.getOrCreateContentCache(E));
    return;
  }
  
  // Register the ContextCache object with the deserializer.
  /* FIXME:
  ContentCache *Entry
  SMgr.MemBufferInfos.push_back(ContentCache());
   = const_cast<ContentCache&>(SMgr.MemBufferInfos.back());
  D.RegisterPtr(&Entry);
  
  // Create the buffer.
  unsigned Size = D.ReadInt();
  Entry.Buffer = MemoryBuffer::getNewUninitMemBuffer(Size);
  
  // Read the contents of the buffer.
  char* p = const_cast<char*>(Entry.Buffer->getBufferStart());
  for (unsigned i = 0; i < Size ; ++i)
    p[i] = D.ReadInt();    
   */
}

void SourceManager::Emit(llvm::Serializer& S) const {
  S.EnterBlock();
  S.EmitPtr(this);
  S.EmitInt(MainFileID.getOpaqueValue());
  
  // Emit: FileInfos.  Just emit the file name.
  S.EnterBlock();    

  // FIXME: Emit FileInfos.
  //std::for_each(FileInfos.begin(), FileInfos.end(),
  //              S.MakeEmitter<ContentCache>());
  
  S.ExitBlock();
  
  // Emit: MemBufferInfos
  S.EnterBlock();

  /* FIXME: EMIT.
  std::for_each(MemBufferInfos.begin(), MemBufferInfos.end(),
                S.MakeEmitter<ContentCache>());
   */
  
  S.ExitBlock();
  
  // FIXME: Emit SLocEntryTable.
  
  S.ExitBlock();
}

SourceManager*
SourceManager::CreateAndRegister(llvm::Deserializer &D, FileManager &FMgr) {
  SourceManager *M = new SourceManager();
  D.RegisterPtr(M);
  
  // Read: the FileID of the main source file of the translation unit.
  M->MainFileID = FileID::get(D.ReadInt());
  
  std::vector<char> Buf;
    
  /*{ // FIXME Read: FileInfos.
    llvm::Deserializer::Location BLoc = D.getCurrentBlockLocation();
    while (!D.FinishedBlock(BLoc))
    ContentCache::ReadToSourceManager(D,*M,&FMgr,Buf);
  }*/
    
  { // Read: MemBufferInfos.
    llvm::Deserializer::Location BLoc = D.getCurrentBlockLocation();
    while (!D.FinishedBlock(BLoc))
    ContentCache::ReadToSourceManager(D,*M,NULL,Buf);
  }
  
  // FIXME: Read SLocEntryTable.
  
  return M;
}
