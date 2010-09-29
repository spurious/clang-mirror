//===- Version.cpp - Clang Version Number -----------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines several version-related utility functions for Clang.
//
//===----------------------------------------------------------------------===//

#include "clang/Basic/Version.h"
#include "llvm/Support/raw_ostream.h"
#include <cstring>
#include <cstdlib>

using namespace std;

namespace clang {
  
std::string getClangRepositoryPath() {
#ifdef SVN_REPOSITORY
  llvm::StringRef URL(SVN_REPOSITORY);
#else
  llvm::StringRef URL("");
#endif

  // Strip off version from a build from an integration branch.
  URL = URL.slice(0, URL.find("/src/tools/clang"));

  // Trim path prefix off, assuming path came from standard cfe path.
  size_t Start = URL.find("cfe/");
  if (Start != llvm::StringRef::npos)
    URL = URL.substr(Start + 4);

  return URL;
}

std::string getClangRevision() {
#ifdef SVN_REVISION
  return SVN_REVISION;
#else
  return "";
#endif
}

std::string getClangFullRepositoryVersion() {
  std::string buf;
  llvm::raw_string_ostream OS(buf);
  std::string Path = getClangRepositoryPath();
  std::string Revision = getClangRevision();
  if (!Path.empty())
    OS << Path;
  if (!Revision.empty()) {
    if (!Path.empty())
      OS << ' ';
    OS << Revision;
  }
  return OS.str();
}
  
std::string getClangFullVersion() {
  std::string buf;
  llvm::raw_string_ostream OS(buf);
#ifdef CLANG_VENDOR
  OS << CLANG_VENDOR;
#endif
  OS << "clang version " CLANG_VERSION_STRING " ("
     << getClangFullRepositoryVersion() << ')';
  return OS.str();
}

} // end namespace clang
