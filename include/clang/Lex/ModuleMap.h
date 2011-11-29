//===--- ModuleMap.h - Describe the layout of modules -----------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the ModuleMap interface, which describes the layout of a
// module as it relates to headers.
//
//===----------------------------------------------------------------------===//


#ifndef LLVM_CLANG_LEX_MODULEMAP_H
#define LLVM_CLANG_LEX_MODULEMAP_H

#include "clang/Basic/LangOptions.h"
#include "clang/Basic/SourceManager.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringMap.h"
#include <string>

namespace clang {
  
class DirectoryEntry;
class FileEntry;
class FileManager;
class DiagnosticConsumer;
class DiagnosticsEngine;
class ModuleMapParser;
  
class ModuleMap {
public:
  /// \brief Describes a module or submodule.
  struct Module {
    /// \brief The name of this module.
    std::string Name;
    
    /// \brief The location of the module definition.
    SourceLocation DefinitionLoc;
    
    /// \brief The parent of this module. This will be NULL for the top-level
    /// module.
    Module *Parent;

    /// \brief The umbrella header, if any.
    ///
    /// Only the top-level module can have an umbrella header.
    const FileEntry *UmbrellaHeader;

    /// \brief The submodules of this module, indexed by name.
    llvm::StringMap<Module *> SubModules;

    /// \brief The headers that are part of this module.
    llvm::SmallVector<const FileEntry *, 2> Headers;
    
    /// \brief Whether this is a framework module.
    bool IsFramework;
    
    /// \brief Whether this is an explicit submodule.
    bool IsExplicit;
    
    /// \brief Construct a top-level module.
    explicit Module(StringRef Name, SourceLocation DefinitionLoc,
                    bool IsFramework)
      : Name(Name), DefinitionLoc(DefinitionLoc), Parent(0), UmbrellaHeader(0),
        IsFramework(IsFramework), IsExplicit(false) { }
    
    /// \brief Construct  a new module or submodule.
    Module(StringRef Name, SourceLocation DefinitionLoc, Module *Parent, 
           bool IsFramework, bool IsExplicit)
      : Name(Name), DefinitionLoc(DefinitionLoc), Parent(Parent), 
        UmbrellaHeader(0), IsFramework(IsFramework), IsExplicit(IsExplicit) {
    }
     
    ~Module();
    
    /// \brief Determine whether this module is a submodule.
    bool isSubModule() const { return Parent != 0; }
    
    /// \brief Determine whether this module is a part of a framework,
    /// either because it is a framework module or because it is a submodule
    /// of a framework module.
    bool isPartOfFramework() const {
      for (const Module *Mod = this; Mod; Mod = Mod->Parent) 
        if (Mod->IsFramework)
          return true;
      
      return false;
    }
    
    /// \brief Retrieve the full name of this module, including the path from
    /// its top-level module.
    std::string getFullModuleName() const;
    
    /// \brief Retrieve the name of the top-level module.
    ///
    StringRef getTopLevelModuleName() const;
    
    /// \brief Print the module map for this module to the given stream. 
    ///
    void print(llvm::raw_ostream &OS, unsigned Indent = 0) const;
    
    /// \brief Dump the contents of this module to the given output stream.
    void dump() const;
  };
  
private:
  SourceManager *SourceMgr;
  llvm::IntrusiveRefCntPtr<DiagnosticsEngine> Diags;
  LangOptions LangOpts;
  
  /// \brief The top-level modules that are known.
  llvm::StringMap<Module *> Modules;
  
  /// \brief Mapping from each header to the module that owns the contents of the
  /// that header.
  llvm::DenseMap<const FileEntry *, Module *> Headers;
  
  /// \brief Mapping from directories with umbrella headers to the module
  /// that is generated from the umbrella header.
  ///
  /// This mapping is used to map headers that haven't explicitly been named
  /// in the module map over to the module that includes them via its umbrella
  /// header.
  llvm::DenseMap<const DirectoryEntry *, Module *> UmbrellaDirs;
  
  friend class ModuleMapParser;
  
public:
  /// \brief Construct a new module map.
  ///
  /// \param FileMgr The file manager used to find module files and headers.
  /// This file manager should be shared with the header-search mechanism, since
  /// they will refer to the same headers.
  ///
  /// \param DC A diagnostic consumer that will be cloned for use in generating
  /// diagnostics.
  ModuleMap(FileManager &FileMgr, const DiagnosticConsumer &DC);

  /// \brief Destroy the module map.
  ///
  ~ModuleMap();
  
  /// \brief Retrieve the module that owns the given header file, if any.
  ///
  /// \param File The header file that is likely to be included.
  ///
  /// \returns The module that owns the given header file, or null to indicate
  /// that no module owns this header file.
  Module *findModuleForHeader(const FileEntry *File);

  /// \brief Retrieve a module with the given name.
  ///
  /// \param The name of the module to look up.
  ///
  /// \returns The named module, if known; otherwise, returns null.
  Module *findModule(StringRef Name);
  
  /// \brief Infer the contents of a framework module map from the given
  /// framework directory.
  Module *inferFrameworkModule(StringRef ModuleName, 
                               const DirectoryEntry *FrameworkDir);
  
  /// \brief Retrieve the module map file containing the definition of the given
  /// module.
  ///
  /// \param Module The module whose module map file will be returned, if known.
  ///
  /// \returns The file entry for the module map file containing the given
  /// module, or NULL if the module definition was inferred.
  const FileEntry *getContainingModuleMapFile(ModuleMap::Module *Module);
  
  /// \brief Parse the given module map file, and record any modules we 
  /// encounter.
  ///
  /// \param File The file to be parsed.
  ///
  /// \returns true if an error occurred, false otherwise.
  bool parseModuleMapFile(const FileEntry *File);
    
  /// \brief Dump the contents of the module map, for debugging purposes.
  void dump();
};
  
}
#endif
