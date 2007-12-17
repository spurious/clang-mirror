//===--- HeaderMap.h - A file that acts like dir of symlinks ----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file was developed by Chris Lattner and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the HeaderMap interface.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LEX_HEADERMAP_H
#define LLVM_CLANG_LEX_HEADERMAP_H

#include <string>

namespace clang {
  class FileEntry;
  class FileManager;

/// This class represents an Apple concept known as a 'header map'.  To the
/// #include file resolution process, it basically acts like a directory of
/// symlinks to files.  Its advantages are that it is dense and more efficient
/// to create and process than a directory of symlinks.
class HeaderMap {
  HeaderMap(const HeaderMap&); // DO NOT IMPLEMENT
  void operator=(const HeaderMap&); // DO NOT IMPLEMENT
public:
  /// HeaderMap::Create - This attempts to load the specified file as a header
  /// map.  If it doesn't look like a HeaderMap, it gives up and returns null.
  static const HeaderMap *Create(const FileEntry *FE);
  
  /// LookupFile - Check to see if the specified relative filename is located in
  /// this HeaderMap.  If so, open it and return its FileEntry.
  const FileEntry *LookupFile(const char *FilenameStart,const char *FilenameEnd,
                              FileManager &FM) const;
  
  /// getFileName - Return the filename of the headermap.
  const char *getFileName() const {
    return ""; 
  }
    
};

} // end namespace clang.

#endif