//===--- FileManager.h - File System Probing and Caching --------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file was developed by Chris Lattner and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the FileManager interface.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_FILEMANAGER_H
#define LLVM_CLANG_FILEMANAGER_H

#include "llvm/ADT/CStringMap.h"
#include <map>
#include <string>
// FIXME: Enhance libsystem to support inode and other fields in stat.
#include <sys/types.h>

namespace llvm {
namespace clang {
class FileManager;
  
/// DirectoryEntry - Cached information about one directory on the disk.
///
class DirectoryEntry {
  std::string Name;   // Name of the directory.
  friend class FileManager;
public:
  DirectoryEntry() {}
  const char *getName() const { return Name.c_str(); }
};

/// FileEntry - Cached information about one file on the disk.
///
class FileEntry {
  std::string Name;           // Name of the directory.
  off_t Size;                 // File size in bytes.
  time_t ModTime;             // Modification time of file.
  const DirectoryEntry *Dir;  // Directory file lives in.
  unsigned UID;               // A unique (small) ID for the file.
  friend class FileManager;
public:
  FileEntry() : UID(~0U) {}
  
  const char *getName() const { return Name.c_str(); }
  off_t getSize() const { return Size; }
  unsigned getUID() const { return UID; }
  time_t getModificationTime() const { return ModTime; }
  
  /// getDir - Return the directory the file lives in.
  ///
  const DirectoryEntry *getDir() const { return Dir; }
};

 
/// FileManager - Implements support for file system lookup, file system
/// caching, and directory search management.  This also handles more advanced
/// properties, such as uniquing files based on "inode", so that a file with two
/// names (e.g. symlinked) will be treated as a single file.
///
class FileManager {
  /// UniqueDirs/UniqueFiles - Cache from ID's to existing directories/files.
  ///
  std::map<std::pair<dev_t, ino_t>, DirectoryEntry> UniqueDirs;
  std::map<std::pair<dev_t, ino_t>, FileEntry> UniqueFiles;
  
  /// DirEntries/FileEntries - This is a cache of directory/file entries we have
  /// looked up.  The actual Entry is owned by UniqueFiles/UniqueDirs above.
  ///
  CStringMap<DirectoryEntry*> DirEntries;
  std::map<std::string, FileEntry*> FileEntries;
  
  /// NextFileUID - Each FileEntry we create is assigned a unique ID #.
  ///
  unsigned NextFileUID;
  
  // Statistics.
  unsigned NumDirLookups, NumFileLookups;
  unsigned NumDirCacheMisses, NumFileCacheMisses;
public:
  FileManager() : NextFileUID(0) {
    NumDirLookups = NumFileLookups = 0;
    NumDirCacheMisses = NumFileCacheMisses = 0;
  }

  /// getDirectory - Lookup, cache, and verify the specified directory.  This
  /// returns null if the directory doesn't exist.
  /// 
  const DirectoryEntry *getDirectory(const std::string &Filename);
  
  /// getFile - Lookup, cache, and verify the specified file.  This returns null
  /// if the file doesn't exist.
  /// 
  const FileEntry *getFile(const std::string &Filename);
  
  void PrintStats() const;
};

}  // end namespace clang
}  // end namespace llvm

#endif
