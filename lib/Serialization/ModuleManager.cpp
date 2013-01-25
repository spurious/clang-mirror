//===--- ModuleManager.cpp - Module Manager ---------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the ModuleManager class, which manages a set of loaded
//  modules for the ASTReader.
//
//===----------------------------------------------------------------------===//
#include "clang/Serialization/ModuleManager.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/system_error.h"

#ifndef NDEBUG
#include "llvm/Support/GraphWriter.h"
#endif

using namespace clang;
using namespace serialization;

ModuleFile *ModuleManager::lookup(StringRef Name) {
  const FileEntry *Entry = FileMgr.getFile(Name);
  return Modules[Entry];
}

llvm::MemoryBuffer *ModuleManager::lookupBuffer(StringRef Name) {
  const FileEntry *Entry = FileMgr.getFile(Name);
  return InMemoryBuffers[Entry];
}

std::pair<ModuleFile *, bool>
ModuleManager::addModule(StringRef FileName, ModuleKind Type,
                         SourceLocation ImportLoc, ModuleFile *ImportedBy,
                         unsigned Generation, std::string &ErrorStr) {
  const FileEntry *Entry = FileMgr.getFile(FileName);
  if (!Entry && FileName != "-") {
    ErrorStr = "file not found";
    return std::make_pair(static_cast<ModuleFile*>(0), false);
  }
  
  // Check whether we already loaded this module, before 
  ModuleFile *&ModuleEntry = Modules[Entry];
  bool NewModule = false;
  if (!ModuleEntry) {
    // Allocate a new module.
    ModuleFile *New = new ModuleFile(Type, Generation);
    New->Index = Chain.size();
    New->FileName = FileName.str();
    New->File = Entry;
    New->ImportLoc = ImportLoc;
    Chain.push_back(New);
    NewModule = true;
    ModuleEntry = New;

    // Load the contents of the module
    if (llvm::MemoryBuffer *Buffer = lookupBuffer(FileName)) {
      // The buffer was already provided for us.
      assert(Buffer && "Passed null buffer");
      New->Buffer.reset(Buffer);
    } else {
      // Open the AST file.
      llvm::error_code ec;
      if (FileName == "-") {
        ec = llvm::MemoryBuffer::getSTDIN(New->Buffer);
        if (ec)
          ErrorStr = ec.message();
      } else
        New->Buffer.reset(FileMgr.getBufferForFile(FileName, &ErrorStr));
      
      if (!New->Buffer)
        return std::make_pair(static_cast<ModuleFile*>(0), false);
    }
    
    // Initialize the stream
    New->StreamFile.init((const unsigned char *)New->Buffer->getBufferStart(),
                         (const unsigned char *)New->Buffer->getBufferEnd());     }
  
  if (ImportedBy) {
    ModuleEntry->ImportedBy.insert(ImportedBy);
    ImportedBy->Imports.insert(ModuleEntry);
  } else {
    if (!ModuleEntry->DirectlyImported)
      ModuleEntry->ImportLoc = ImportLoc;
    
    ModuleEntry->DirectlyImported = true;
  }
  
  return std::make_pair(ModuleEntry, NewModule);
}

namespace {
  /// \brief Predicate that checks whether a module file occurs within
  /// the given set.
  class IsInModuleFileSet : public std::unary_function<ModuleFile *, bool> {
    llvm::SmallPtrSet<ModuleFile *, 4> &Removed;

  public:
    IsInModuleFileSet(llvm::SmallPtrSet<ModuleFile *, 4> &Removed)
    : Removed(Removed) { }

    bool operator()(ModuleFile *MF) const {
      return Removed.count(MF);
    }
  };
}

void ModuleManager::removeModules(ModuleIterator first, ModuleIterator last) {
  if (first == last)
    return;

  // Collect the set of module file pointers that we'll be removing.
  llvm::SmallPtrSet<ModuleFile *, 4> victimSet(first, last);

  // Remove any references to the now-destroyed modules.
  IsInModuleFileSet checkInSet(victimSet);
  for (unsigned i = 0, n = Chain.size(); i != n; ++i) {
    Chain[i]->ImportedBy.remove_if(checkInSet);
  }

  // Delete the modules and erase them from the various structures.
  for (ModuleIterator victim = first; victim != last; ++victim) {
    Modules.erase((*victim)->File);
    delete *victim;
  }

  // Remove the modules from the chain.
  Chain.erase(first, last);
}

void ModuleManager::addInMemoryBuffer(StringRef FileName, 
                                      llvm::MemoryBuffer *Buffer) {
  
  const FileEntry *Entry = FileMgr.getVirtualFile(FileName, 
                                                  Buffer->getBufferSize(), 0);
  InMemoryBuffers[Entry] = Buffer;
}

ModuleManager::ModuleManager(FileManager &FileMgr) : FileMgr(FileMgr) { }

ModuleManager::~ModuleManager() {
  for (unsigned i = 0, e = Chain.size(); i != e; ++i)
    delete Chain[e - i - 1];
}

void ModuleManager::visit(bool (*Visitor)(ModuleFile &M, void *UserData), 
                          void *UserData) {
  // If the visitation number array is the wrong size, resize it and recompute
  // an order.
  if (VisitOrder.size() != Chain.size()) {
    unsigned N = size();
    VisitOrder.clear();
    VisitOrder.reserve(N);
    
    // Record the number of incoming edges for each module. When we
    // encounter a module with no incoming edges, push it into the queue
    // to seed the queue.
    SmallVector<ModuleFile *, 4> Queue;
    Queue.reserve(N);
    llvm::SmallVector<unsigned, 4> UnusedIncomingEdges;
    UnusedIncomingEdges.reserve(size());
    for (ModuleIterator M = begin(), MEnd = end(); M != MEnd; ++M) {
      if (unsigned Size = (*M)->ImportedBy.size())
        UnusedIncomingEdges.push_back(Size);
      else {
        UnusedIncomingEdges.push_back(0);
        Queue.push_back(*M);
      }
    }

    // Traverse the graph, making sure to visit a module before visiting any
    // of its dependencies.
    unsigned QueueStart = 0;
    while (QueueStart < Queue.size()) {
      ModuleFile *CurrentModule = Queue[QueueStart++];
      VisitOrder.push_back(CurrentModule);

      // For any module that this module depends on, push it on the
      // stack (if it hasn't already been marked as visited).
      for (llvm::SetVector<ModuleFile *>::iterator
             M = CurrentModule->Imports.begin(),
             MEnd = CurrentModule->Imports.end();
           M != MEnd; ++M) {
        // Remove our current module as an impediment to visiting the
        // module we depend on. If we were the last unvisited module
        // that depends on this particular module, push it into the
        // queue to be visited.
        unsigned &NumUnusedEdges = UnusedIncomingEdges[(*M)->Index];
        if (NumUnusedEdges && (--NumUnusedEdges == 0))
          Queue.push_back(*M);
      }
    }

    assert(VisitOrder.size() == N && "Visitation order is wrong?");
  }

  SmallVector<ModuleFile *, 4> Stack;
  SmallVector<bool, 4> Visited(size(), false);

  for (unsigned I = 0, N = VisitOrder.size(); I != N; ++I) {
    ModuleFile *CurrentModule = VisitOrder[I];
    // Should we skip this module file?
    if (Visited[CurrentModule->Index])
      continue;

    // Visit the module.
    Visited[CurrentModule->Index] = true;
    if (!Visitor(*CurrentModule, UserData))
      continue;

    // The visitor has requested that cut off visitation of any
    // module that the current module depends on. To indicate this
    // behavior, we mark all of the reachable modules as having been visited.
    ModuleFile *NextModule = CurrentModule;
    Stack.reserve(size());
    do {
      // For any module that this module depends on, push it on the
      // stack (if it hasn't already been marked as visited).
      for (llvm::SetVector<ModuleFile *>::iterator
             M = NextModule->Imports.begin(),
             MEnd = NextModule->Imports.end();
           M != MEnd; ++M) {
        if (!Visited[(*M)->Index]) {
          Stack.push_back(*M);
          Visited[(*M)->Index] = true;
        }
      }

      if (Stack.empty())
        break;

      // Pop the next module off the stack.
      NextModule = Stack.back();
      Stack.pop_back();
    } while (true);
  }
}

/// \brief Perform a depth-first visit of the current module.
static bool visitDepthFirst(ModuleFile &M, 
                            bool (*Visitor)(ModuleFile &M, bool Preorder, 
                                            void *UserData), 
                            void *UserData,
                            SmallVectorImpl<bool> &Visited) {
  // Preorder visitation
  if (Visitor(M, /*Preorder=*/true, UserData))
    return true;
  
  // Visit children
  for (llvm::SetVector<ModuleFile *>::iterator IM = M.Imports.begin(),
                                            IMEnd = M.Imports.end();
       IM != IMEnd; ++IM) {
    if (Visited[(*IM)->Index])
      continue;
    Visited[(*IM)->Index] = true;

    if (visitDepthFirst(**IM, Visitor, UserData, Visited))
      return true;
  }  
  
  // Postorder visitation
  return Visitor(M, /*Preorder=*/false, UserData);
}

void ModuleManager::visitDepthFirst(bool (*Visitor)(ModuleFile &M, bool Preorder, 
                                                    void *UserData), 
                                    void *UserData) {
  SmallVector<bool, 16> Visited(size(), false);
  for (unsigned I = 0, N = Chain.size(); I != N; ++I) {
    if (Visited[Chain[I]->Index])
      continue;
    Visited[Chain[I]->Index] = true;

    if (::visitDepthFirst(*Chain[I], Visitor, UserData, Visited))
      return;
  }
}

#ifndef NDEBUG
namespace llvm {
  template<>
  struct GraphTraits<ModuleManager> {
    typedef ModuleFile NodeType;
    typedef llvm::SetVector<ModuleFile *>::const_iterator ChildIteratorType;
    typedef ModuleManager::ModuleConstIterator nodes_iterator;
    
    static ChildIteratorType child_begin(NodeType *Node) {
      return Node->Imports.begin();
    }

    static ChildIteratorType child_end(NodeType *Node) {
      return Node->Imports.end();
    }
    
    static nodes_iterator nodes_begin(const ModuleManager &Manager) {
      return Manager.begin();
    }
    
    static nodes_iterator nodes_end(const ModuleManager &Manager) {
      return Manager.end();
    }
  };
  
  template<>
  struct DOTGraphTraits<ModuleManager> : public DefaultDOTGraphTraits {
    explicit DOTGraphTraits(bool IsSimple = false)
      : DefaultDOTGraphTraits(IsSimple) { }
    
    static bool renderGraphFromBottomUp() {
      return true;
    }

    std::string getNodeLabel(ModuleFile *M, const ModuleManager&) {
      return llvm::sys::path::stem(M->FileName);
    }
  };
}

void ModuleManager::viewGraph() {
  llvm::ViewGraph(*this, "Modules");
}
#endif
