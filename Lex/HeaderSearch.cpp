//===--- HeaderSearch.cpp - Resolve Header File Locations ---===//
//
//                     The LLVM Compiler Infrastructure
//
// This file was developed by Chris Lattner and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file implements the DirectoryLookup and HeaderSearch interfaces.
//
//===----------------------------------------------------------------------===//

#include "clang/Basic/FileManager.h"
#include "clang/Lex/HeaderSearch.h"
#include "clang/Lex/IdentifierTable.h"
#include "llvm/System/Path.h"
#include "llvm/ADT/SmallString.h"
using namespace clang;

HeaderSearch::HeaderSearch(FileManager &FM) : FileMgr(FM), FrameworkMap(64) {
  SystemDirIdx = 0;
  NoCurDirSearch = false;
  
  NumIncluded = 0;
  NumMultiIncludeFileOptzn = 0;
  NumFrameworkLookups = NumSubFrameworkLookups = 0;
}

void HeaderSearch::PrintStats() {
  fprintf(stderr, "\n*** HeaderSearch Stats:\n");
  fprintf(stderr, "%d files tracked.\n", (int)FileInfo.size());
  unsigned NumOnceOnlyFiles = 0, MaxNumIncludes = 0, NumSingleIncludedFiles = 0;
  for (unsigned i = 0, e = FileInfo.size(); i != e; ++i) {
    NumOnceOnlyFiles += FileInfo[i].isImport;
    if (MaxNumIncludes < FileInfo[i].NumIncludes)
      MaxNumIncludes = FileInfo[i].NumIncludes;
    NumSingleIncludedFiles += FileInfo[i].NumIncludes == 1;
  }
  fprintf(stderr, "  %d #import/#pragma once files.\n", NumOnceOnlyFiles);
  fprintf(stderr, "  %d included exactly once.\n", NumSingleIncludedFiles);
  fprintf(stderr, "  %d max times a file is included.\n", MaxNumIncludes);
  
  fprintf(stderr, "  %d #include/#include_next/#import.\n", NumIncluded);
  fprintf(stderr, "    %d #includes skipped due to"
          " the multi-include optimization.\n", NumMultiIncludeFileOptzn);
  
  fprintf(stderr, "%d framework lookups.\n", NumFrameworkLookups);
  fprintf(stderr, "%d subframework lookups.\n", NumSubFrameworkLookups);
}

//===----------------------------------------------------------------------===//
// Header File Location.
//===----------------------------------------------------------------------===//

const FileEntry *HeaderSearch::DoFrameworkLookup(const DirectoryEntry *Dir,
                                                 const char *FilenameStart,
                                                 const char *FilenameEnd) {
  // Framework names must have a '/' in the filename.
  const char *SlashPos = std::find(FilenameStart, FilenameEnd, '/');
  if (SlashPos == FilenameEnd) return 0;
  
  llvm::StringMapEntry<const DirectoryEntry *> &CacheLookup =
    FrameworkMap.GetOrCreateValue(FilenameStart, SlashPos);
  
  // If it is some other directory, fail.
  if (CacheLookup.getValue() && CacheLookup.getValue() != Dir)
    return 0;

  // FrameworkName = "/System/Library/Frameworks/"
  llvm::SmallString<1024> FrameworkName;
  FrameworkName += Dir->getName();
  if (FrameworkName.empty() || FrameworkName.back() != '/')
    FrameworkName.push_back('/');
  
  // FrameworkName = "/System/Library/Frameworks/Cocoa"
  FrameworkName.append(FilenameStart, SlashPos);
  
  // FrameworkName = "/System/Library/Frameworks/Cocoa.framework/"
  FrameworkName += ".framework/";
 
  if (CacheLookup.getValue() == 0) {
    ++NumFrameworkLookups;
    
    // If the framework dir doesn't exist, we fail.
    if (!llvm::sys::Path(std::string(FrameworkName.begin(), 
                                     FrameworkName.end())).exists())
      return 0;
    
    // Otherwise, if it does, remember that this is the right direntry for this
    // framework.
    CacheLookup.setValue(Dir);
  }
  
  // Check "/System/Library/Frameworks/Cocoa.framework/Headers/file.h"
  unsigned OrigSize = FrameworkName.size();
  
  FrameworkName += "Headers/";
  FrameworkName.append(SlashPos+1, FilenameEnd);
  if (const FileEntry *FE = FileMgr.getFile(FrameworkName.begin(),
                                            FrameworkName.end())) {
    return FE;
  }
  
  // Check "/System/Library/Frameworks/Cocoa.framework/PrivateHeaders/file.h"
  const char *Private = "Private";
  FrameworkName.insert(FrameworkName.begin()+OrigSize, Private, 
                       Private+strlen(Private));
  return FileMgr.getFile(FrameworkName.begin(), FrameworkName.end());
}

/// LookupFile - Given a "foo" or <foo> reference, look up the indicated file,
/// return null on failure.  isAngled indicates whether the file reference is
/// for system #include's or not (i.e. using <> instead of "").  CurFileEnt, if
/// non-null, indicates where the #including file is, in case a relative search
/// is needed.
const FileEntry *HeaderSearch::LookupFile(const char *FilenameStart,
                                          const char *FilenameEnd, 
                                          bool isAngled,
                                          const DirectoryLookup *FromDir,
                                          const DirectoryLookup *&CurDir,
                                          const FileEntry *CurFileEnt) {
  // If 'Filename' is absolute, check to see if it exists and no searching.
  // FIXME: Portability.  This should be a sys::Path interface, this doesn't
  // handle things like C:\foo.txt right, nor win32 \\network\device\blah.
  if (FilenameStart[0] == '/') {
    CurDir = 0;

    // If this was an #include_next "/absolute/file", fail.
    if (FromDir) return 0;
    
    // Otherwise, just return the file.
    return FileMgr.getFile(FilenameStart, FilenameEnd);
  }
  
  llvm::SmallString<1024> TmpDir;
  
  // Step #0, unless disabled, check to see if the file is in the #includer's
  // directory.  This search is not done for <> headers.
  if (CurFileEnt && !isAngled && !NoCurDirSearch) {
    // Concatenate the requested file onto the directory.
    // FIXME: Portability.  Filename concatenation should be in sys::Path.
    TmpDir += CurFileEnt->getDir()->getName();
    TmpDir.push_back('/');
    TmpDir.append(FilenameStart, FilenameEnd);
    if (const FileEntry *FE = FileMgr.getFile(TmpDir.begin(), TmpDir.end())) {
      // Leave CurDir unset.
      
      // This file is a system header or C++ unfriendly if the old file is.
      getFileInfo(FE).DirInfo = getFileInfo(CurFileEnt).DirInfo;
      return FE;
    }
    TmpDir.clear();
  }
  
  CurDir = 0;

  // If this is a system #include, ignore the user #include locs.
  unsigned i = isAngled ? SystemDirIdx : 0;
  
  // If this is a #include_next request, start searching after the directory the
  // file was found in.
  if (FromDir)
    i = FromDir-&SearchDirs[0];
  
  // Cache all of the lookups performed by this method.  Many headers are
  // multiply included, and the "pragma once" optimization prevents them from
  // being relex/pp'd, but they would still have to search through a
  // (potentially huge) series of SearchDirs to find it.
  std::pair<unsigned, unsigned> &CacheLookup =
    LookupFileCache.GetOrCreateValue(FilenameStart, FilenameEnd).getValue();

  // If the entry has been previously looked up, the first value will be
  // non-zero.  If the value is equal to i (the start point of our search), then
  // this is a matching hit.
  if (CacheLookup.first == i+1) {
    // Skip querying potentially lots of directories for this lookup.
    i = CacheLookup.second;
  } else {
    // Otherwise, this is the first query, or the previous query didn't match
    // our search start.  We will fill in our found location below, so prime the
    // start point value.
    CacheLookup.first = i+1;
  }
    
  // Check each directory in sequence to see if it contains this file.
  for (; i != SearchDirs.size(); ++i) {
    const FileEntry *FE = 0;
    if (!SearchDirs[i].isFramework()) {
      // FIXME: Portability.  Adding file to dir should be in sys::Path.
      // Concatenate the requested file onto the directory.
      TmpDir.clear();
      TmpDir += SearchDirs[i].getDir()->getName();
      TmpDir.push_back('/');
      TmpDir.append(FilenameStart, FilenameEnd);
      FE = FileMgr.getFile(TmpDir.begin(), TmpDir.end());
    } else {
      FE = DoFrameworkLookup(SearchDirs[i].getDir(), FilenameStart,FilenameEnd);
    }
    
    if (FE) {
      CurDir = &SearchDirs[i];
      
      // This file is a system header or C++ unfriendly if the dir is.
      getFileInfo(FE).DirInfo = CurDir->getDirCharacteristic();
      
      // Remember this location for the next lookup we do.
      CacheLookup.second = i;
      return FE;
    }
  }
  
  // Otherwise, didn't find it. Remember we didn't find this.
  CacheLookup.second = SearchDirs.size();
  return 0;
}

/// LookupSubframeworkHeader - Look up a subframework for the specified
/// #include file.  For example, if #include'ing <HIToolbox/HIToolbox.h> from
/// within ".../Carbon.framework/Headers/Carbon.h", check to see if HIToolbox
/// is a subframework within Carbon.framework.  If so, return the FileEntry
/// for the designated file, otherwise return null.
const FileEntry *HeaderSearch::
LookupSubframeworkHeader(const char *FilenameStart,
                         const char *FilenameEnd,
                         const FileEntry *ContextFileEnt) {
  // Framework names must have a '/' in the filename.  Find it.
  const char *SlashPos = std::find(FilenameStart, FilenameEnd, '/');
  if (SlashPos == FilenameEnd) return 0;
  
  // Look up the base framework name of the ContextFileEnt.
  const char *ContextName = ContextFileEnt->getName();
    
  // If the context info wasn't a framework, couldn't be a subframework.
  const char *FrameworkPos = strstr(ContextName, ".framework/");
  if (FrameworkPos == 0)
    return 0;
  
  llvm::SmallString<1024> FrameworkName(ContextName, 
                                        FrameworkPos+strlen(".framework/"));

  // Append Frameworks/HIToolbox.framework/
  FrameworkName += "Frameworks/";
  FrameworkName.append(FilenameStart, SlashPos);
  FrameworkName += ".framework/";

  llvm::StringMapEntry<const DirectoryEntry *> &CacheLookup =
    FrameworkMap.GetOrCreateValue(FilenameStart, SlashPos);
  
  // Some other location?
  if (CacheLookup.getValue() &&
      CacheLookup.getKeyLength() == FrameworkName.size() &&
      memcmp(CacheLookup.getKeyData(), &FrameworkName[0],
             CacheLookup.getKeyLength()) != 0)
    return 0;
  
  // Cache subframework.
  if (CacheLookup.getValue() == 0) {
    ++NumSubFrameworkLookups;
    
    // If the framework dir doesn't exist, we fail.
    const DirectoryEntry *Dir = FileMgr.getDirectory(FrameworkName.begin(),
                                                     FrameworkName.end());
    if (Dir == 0) return 0;
    
    // Otherwise, if it does, remember that this is the right direntry for this
    // framework.
    CacheLookup.setValue(Dir);
  }
  
  const FileEntry *FE = 0;

  // Check ".../Frameworks/HIToolbox.framework/Headers/HIToolbox.h"
  llvm::SmallString<1024> HeadersFilename(FrameworkName);
  HeadersFilename += "Headers/";
  HeadersFilename.append(SlashPos+1, FilenameEnd);
  if (!(FE = FileMgr.getFile(HeadersFilename.begin(),
                             HeadersFilename.end()))) {
    
    // Check ".../Frameworks/HIToolbox.framework/PrivateHeaders/HIToolbox.h"
    HeadersFilename = FrameworkName;
    HeadersFilename += "PrivateHeaders/";
    HeadersFilename.append(SlashPos+1, FilenameEnd);
    if (!(FE = FileMgr.getFile(HeadersFilename.begin(), HeadersFilename.end())))
      return 0;
  }
  
  // This file is a system header or C++ unfriendly if the old file is.
  getFileInfo(FE).DirInfo = getFileInfo(ContextFileEnt).DirInfo;
  return FE;
}

//===----------------------------------------------------------------------===//
// File Info Management.
//===----------------------------------------------------------------------===//


/// getFileInfo - Return the PerFileInfo structure for the specified
/// FileEntry.
HeaderSearch::PerFileInfo &HeaderSearch::getFileInfo(const FileEntry *FE) {
  if (FE->getUID() >= FileInfo.size())
    FileInfo.resize(FE->getUID()+1);
  return FileInfo[FE->getUID()];
}  

/// ShouldEnterIncludeFile - Mark the specified file as a target of of a
/// #include, #include_next, or #import directive.  Return false if #including
/// the file will have no effect or true if we should include it.
bool HeaderSearch::ShouldEnterIncludeFile(const FileEntry *File, bool isImport){
  ++NumIncluded; // Count # of attempted #includes.

  // Get information about this file.
  PerFileInfo &FileInfo = getFileInfo(File);
  
  // If this is a #import directive, check that we have not already imported
  // this header.
  if (isImport) {
    // If this has already been imported, don't import it again.
    FileInfo.isImport = true;
    
    // Has this already been #import'ed or #include'd?
    if (FileInfo.NumIncludes) return false;
  } else {
    // Otherwise, if this is a #include of a file that was previously #import'd
    // or if this is the second #include of a #pragma once file, ignore it.
    if (FileInfo.isImport)
      return false;
  }
  
  // Next, check to see if the file is wrapped with #ifndef guards.  If so, and
  // if the macro that guards it is defined, we know the #include has no effect.
  if (FileInfo.ControllingMacro && FileInfo.ControllingMacro->getMacroInfo()) {
    ++NumMultiIncludeFileOptzn;
    return false;
  }
  
  // Increment the number of times this file has been included.
  ++FileInfo.NumIncludes;
  
  return true;
}


