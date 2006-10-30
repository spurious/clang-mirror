//===--- FileManager.cpp - File System Probing and Caching ----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file was developed by Chris Lattner and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file implements the FileManager interface.
//
//===----------------------------------------------------------------------===//
//
// TODO: This should index all interesting directories with dirent calls.
//  getdirentries ?
//  opendir/readdir_r/closedir ?
//
//===----------------------------------------------------------------------===//

#include "clang/Basic/FileManager.h"
#include "llvm/ADT/SmallString.h"
#include <iostream>
using namespace llvm;
using namespace clang;

// FIXME: Enhance libsystem to support inode and other fields.
#include <sys/stat.h>


/// NON_EXISTANT_DIR - A special value distinct from null that is used to
/// represent a dir name that doesn't exist on the disk.
#define NON_EXISTANT_DIR reinterpret_cast<DirectoryEntry*>((intptr_t)-1)

/// getDirectory - Lookup, cache, and verify the specified directory.  This
/// returns null if the directory doesn't exist.
/// 
const DirectoryEntry *FileManager::getDirectory(const char *NameStart,
                                                const char *NameEnd) {
  ++NumDirLookups;
  DirectoryEntry *&NamedDirEnt =DirEntries.GetOrCreateValue(NameStart, NameEnd);
  
  // See if there is already an entry in the map.
  if (NamedDirEnt)
    return NamedDirEnt == NON_EXISTANT_DIR ? 0 : NamedDirEnt;
  
  ++NumDirCacheMisses;
  
  // By default, initialize it to invalid.
  NamedDirEnt = NON_EXISTANT_DIR;
  
  // Get the null-terminated directory name as stored as the key of the
  // DirEntries map.
  const char *InterndDirName = DirEntries.GetKeyForValueInMap(NamedDirEnt);
  
  // Check to see if the directory exists.
  struct stat StatBuf;
  if (stat(InterndDirName, &StatBuf) ||   // Error stat'ing.
      !S_ISDIR(StatBuf.st_mode))          // Not a directory?
    return 0;
  
  // It exists.  See if we have already opened a directory with the same inode.
  // This occurs when one dir is symlinked to another, for example.
  DirectoryEntry &UDE = 
    UniqueDirs[std::make_pair(StatBuf.st_dev, StatBuf.st_ino)];
  
  if (UDE.getName())  // Already have an entry with this inode, return it.
    return NamedDirEnt = &UDE;
  
  // Otherwise, we don't have this directory yet, add it.  We use the string
  // key from the DirEntries map as the string.
  UDE.Name  = InterndDirName;
  return NamedDirEnt = &UDE;
}

/// NON_EXISTANT_FILE - A special value distinct from null that is used to
/// represent a filename that doesn't exist on the disk.
#define NON_EXISTANT_FILE reinterpret_cast<FileEntry*>((intptr_t)-1)

/// getFile - Lookup, cache, and verify the specified file.  This returns null
/// if the file doesn't exist.
/// 
const FileEntry *FileManager::getFile(const char *NameStart,
                                      const char *NameEnd) {
  ++NumFileLookups;
  
  // See if there is already an entry in the map.
  FileEntry *&NamedFileEnt = FileEntries.GetOrCreateValue(NameStart, NameEnd);

  // See if there is already an entry in the map.
  if (NamedFileEnt)
    return NamedFileEnt == NON_EXISTANT_FILE ? 0 : NamedFileEnt;
  
  ++NumFileCacheMisses;

  // By default, initialize it to invalid.
  NamedFileEnt = NON_EXISTANT_FILE;

  // Figure out what directory it is in.
  SmallString<1024> DirName;
  
  // If the string contains a / in it, strip off everything after it.
  // FIXME: this logic should be in sys::Path.
  const char *SlashPos = NameEnd-1;
  while (SlashPos >= NameStart && SlashPos[0] != '/')
    --SlashPos;
  
  if (SlashPos < NameStart) {
    // Use the current directory if file has no path component.
    DirName.push_back('.');
  } else if (SlashPos == NameEnd-1)
    return 0;       // If filename ends with a /, it's a directory.
  else
    DirName.append(NameStart, SlashPos);

  const DirectoryEntry *DirInfo = getDirectory(DirName.begin(), DirName.end());
  if (DirInfo == 0)  // Directory doesn't exist, file can't exist.
    return 0;
  
  // Get the null-terminated file name as stored as the key of the
  // FileEntries map.
  const char *InterndFileName = FileEntries.GetKeyForValueInMap(NamedFileEnt);
  
  // FIXME: Use the directory info to prune this, before doing the stat syscall.
  // FIXME: This will reduce the # syscalls.
  
  // Nope, there isn't.  Check to see if the file exists.
  struct stat StatBuf;
  //std::cerr << "STATING: " << Filename;
  if (stat(InterndFileName, &StatBuf) ||   // Error stat'ing.
      S_ISDIR(StatBuf.st_mode)) {           // A directory?
    // If this file doesn't exist, we leave a null in FileEntries for this path.
    //std::cerr << ": Not existing\n";
    return 0;
  }
  //std::cerr << ": exists\n";
  
  // It exists.  See if we have already opened a directory with the same inode.
  // This occurs when one dir is symlinked to another, for example.
  FileEntry &UFE = UniqueFiles[std::make_pair(StatBuf.st_dev, StatBuf.st_ino)];
  
  if (UFE.getName())  // Already have an entry with this inode, return it.
    return NamedFileEnt = &UFE;

  // Otherwise, we don't have this directory yet, add it.
  // FIXME: Change the name to be a char* that points back to the 'FileEntries'
  // key.
  UFE.Name    = InterndFileName;
  UFE.Size    = StatBuf.st_size;
  UFE.ModTime = StatBuf.st_mtime;
  UFE.Dir     = DirInfo;
  UFE.UID     = NextFileUID++;
  return NamedFileEnt = &UFE;
}

void FileManager::PrintStats() const {
  std::cerr << "\n*** File Manager Stats:\n";
  std::cerr << UniqueFiles.size() << " files found, "
            << UniqueDirs.size() << " dirs found.\n";
  std::cerr << NumDirLookups << " dir lookups, "
            << NumDirCacheMisses << " dir cache misses.\n";
  std::cerr << NumFileLookups << " file lookups, "
            << NumFileCacheMisses << " file cache misses.\n";
  
  //std::cerr << PagesMapped << BytesOfPagesMapped << FSLookups;
}
