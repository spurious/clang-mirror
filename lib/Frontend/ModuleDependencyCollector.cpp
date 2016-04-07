//===--- ModuleDependencyCollector.cpp - Collect module dependencies ------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Collect the dependencies of a set of modules.
//
//===----------------------------------------------------------------------===//

#include "clang/Frontend/Utils.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Serialization/ASTReader.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"

using namespace clang;

namespace {
/// Private implementations for ModuleDependencyCollector
class ModuleDependencyListener : public ASTReaderListener {
  ModuleDependencyCollector &Collector;
public:
  ModuleDependencyListener(ModuleDependencyCollector &Collector)
      : Collector(Collector) {}
  bool needsInputFileVisitation() override { return true; }
  bool needsSystemInputFileVisitation() override { return true; }
  bool visitInputFile(StringRef Filename, bool IsSystem, bool IsOverridden,
                      bool IsExplicitModule) override {
    Collector.addFile(Filename);
    return true;
  }
};

struct ModuleDependencyMMCallbacks : public ModuleMapCallbacks {
  ModuleDependencyCollector &Collector;
  ModuleDependencyMMCallbacks(ModuleDependencyCollector &Collector)
      : Collector(Collector) {}

  void moduleMapAddHeader(const FileEntry &File) override {
    StringRef HeaderPath = File.getName();
    if (llvm::sys::path::is_absolute(HeaderPath))
      Collector.addFile(HeaderPath);
  }
};

}

// TODO: move this to Support/Path.h and check for HAVE_REALPATH?
static bool real_path(StringRef SrcPath, SmallVectorImpl<char> &RealPath) {
#ifdef LLVM_ON_UNIX
  char CanonicalPath[PATH_MAX];

  // TODO: emit a warning in case this fails...?
  if (!realpath(SrcPath.str().c_str(), CanonicalPath))
    return false;

  SmallString<256> RPath(CanonicalPath);
  RealPath.swap(RPath);
  return true;
#else
  // FIXME: Add support for systems without realpath.
  return false;
#endif
}

void ModuleDependencyCollector::attachToASTReader(ASTReader &R) {
  R.addListener(llvm::make_unique<ModuleDependencyListener>(*this));
}

void ModuleDependencyCollector::attachToPreprocessor(Preprocessor &PP) {
  PP.getHeaderSearchInfo().getModuleMap().addModuleMapCallbacks(
      llvm::make_unique<ModuleDependencyMMCallbacks>(*this));
}

static bool isCaseSensitivePath(StringRef Path) {
  SmallString<PATH_MAX> TmpDest = Path, UpperDest, RealDest;
  // Remove component traversals, links, etc.
  if (!real_path(Path, TmpDest))
    return true; // Current default value in vfs.yaml
  Path = TmpDest;

  // Change path to all upper case and ask for its real path, if the latter
  // exists and is equal to Path, it's not case sensitive. Default to case
  // sensitive in the absense of realpath, since this is what the VFSWriter
  // already expects when sensitivity isn't setup.
  for (auto &C : Path)
    UpperDest.push_back(::toupper(C));
  if (real_path(UpperDest, RealDest) && Path.equals(RealDest))
    return false;
  return true;
}

void ModuleDependencyCollector::writeFileMap() {
  if (Seen.empty())
    return;

  StringRef VFSDir = getDest();

  // Default to use relative overlay directories in the VFS yaml file. This
  // allows crash reproducer scripts to work across machines.
  VFSWriter.setOverlayDir(VFSDir);

  // Explicitly set case sensitivity for the YAML writer. For that, find out
  // the sensitivity at the path where the headers all collected to.
  VFSWriter.setCaseSensitivity(isCaseSensitivePath(VFSDir));

  std::error_code EC;
  SmallString<256> YAMLPath = VFSDir;
  llvm::sys::path::append(YAMLPath, "vfs.yaml");
  llvm::raw_fd_ostream OS(YAMLPath, EC, llvm::sys::fs::F_Text);
  if (EC) {
    HasErrors = true;
    return;
  }
  VFSWriter.write(OS);
}

bool ModuleDependencyCollector::getRealPath(StringRef SrcPath,
                                            SmallVectorImpl<char> &Result) {
  using namespace llvm::sys;
  SmallString<256> RealPath;
  StringRef FileName = path::filename(SrcPath);
  std::string Dir = path::parent_path(SrcPath).str();
  auto DirWithSymLink = SymLinkMap.find(Dir);

  // Use real_path to fix any symbolic link component present in a path.
  // Computing the real path is expensive, cache the search through the
  // parent path directory.
  if (DirWithSymLink == SymLinkMap.end()) {
    if (!real_path(Dir, RealPath))
      return false;
    SymLinkMap[Dir] = RealPath.str();
  } else {
    RealPath = DirWithSymLink->second;
  }

  path::append(RealPath, FileName);
  Result.swap(RealPath);
  return true;
}

std::error_code ModuleDependencyCollector::copyToRoot(StringRef Src) {
  using namespace llvm::sys;

  // We need an absolute path to append to the root.
  SmallString<256> AbsoluteSrc = Src;
  fs::make_absolute(AbsoluteSrc);
  // Canonicalize to a native path to avoid mixed separator styles.
  path::native(AbsoluteSrc);
  // Remove redundant leading "./" pieces and consecutive separators.
  AbsoluteSrc = path::remove_leading_dotslash(AbsoluteSrc);

  // Canonicalize path by removing "..", "." components.
  SmallString<256> CanonicalPath = AbsoluteSrc;
  path::remove_dots(CanonicalPath, /*remove_dot_dot=*/true);

  // If a ".." component is present after a symlink component, remove_dots may
  // lead to the wrong real destination path. Let the source be canonicalized
  // like that but make sure the destination uses the real path.
  bool HasDotDotInPath =
      std::count(path::begin(AbsoluteSrc), path::end(AbsoluteSrc), "..") > 0;
  SmallString<256> RealPath;
  bool HasRemovedSymlinkComponent = HasDotDotInPath &&
                             getRealPath(AbsoluteSrc, RealPath) &&
                             !StringRef(CanonicalPath).equals(RealPath);

  // Build the destination path.
  SmallString<256> Dest = getDest();
  path::append(Dest, path::relative_path(HasRemovedSymlinkComponent ? RealPath
                                                             : CanonicalPath));

  // Copy the file into place.
  if (std::error_code EC = fs::create_directories(path::parent_path(Dest),
                                                   /*IgnoreExisting=*/true))
    return EC;
  if (std::error_code EC = fs::copy_file(
          HasRemovedSymlinkComponent ? RealPath : CanonicalPath, Dest))
    return EC;

  // Use the canonical path under the root for the file mapping. Also create
  // an additional entry for the real path.
  addFileMapping(CanonicalPath, Dest);
  if (HasRemovedSymlinkComponent)
    addFileMapping(RealPath, Dest);

  return std::error_code();
}

void ModuleDependencyCollector::addFile(StringRef Filename) {
  if (insertSeen(Filename))
    if (copyToRoot(Filename))
      HasErrors = true;
}
