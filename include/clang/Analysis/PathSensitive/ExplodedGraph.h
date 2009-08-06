//=-- ExplodedGraph.h - Local, Path-Sens. "Exploded Graph" -*- C++ -*-------==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the template classes ExplodedNode and ExplodedGraph,
//  which represent a path-sensitive, intra-procedural "exploded graph."
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_ANALYSIS_EXPLODEDGRAPH
#define LLVM_CLANG_ANALYSIS_EXPLODEDGRAPH

#include "clang/Analysis/ProgramPoint.h"
#include "clang/AST/Decl.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Support/Allocator.h"
#include "llvm/ADT/OwningPtr.h"
#include "llvm/ADT/GraphTraits.h"
#include "llvm/ADT/DepthFirstIterator.h"
#include "llvm/Support/Casting.h"

namespace clang {

class GRState;
class GRCoreEngineImpl;
class ExplodedNode;
class CFG;
class ASTContext;

class GRStmtNodeBuilderImpl;
class GRBranchNodeBuilderImpl;
class GRIndirectGotoNodeBuilderImpl;
class GRSwitchNodeBuilderImpl;
class GREndPathNodebuilderImpl;  

//===----------------------------------------------------------------------===//
// ExplodedGraph "implementation" classes.  These classes are not typed to
// contain a specific kind of state.  Typed-specialized versions are defined
// on top of these classes.
//===----------------------------------------------------------------------===//
  
class ExplodedNode : public llvm::FoldingSetNode {
protected:
  friend class ExplodedGraphImpl;
  friend class GRCoreEngineImpl;
  friend class GRStmtNodeBuilderImpl;
  friend class GRBranchNodeBuilderImpl;
  friend class GRIndirectGotoNodeBuilderImpl;
  friend class GRSwitchNodeBuilderImpl;
  friend class GREndPathNodeBuilderImpl;  
  
  class NodeGroup {
    enum { Size1 = 0x0, SizeOther = 0x1, AuxFlag = 0x2, Mask = 0x3 };
    uintptr_t P;
    
    unsigned getKind() const {
      return P & 0x1;
    }
    
    void* getPtr() const {
      assert (!getFlag());
      return reinterpret_cast<void*>(P & ~Mask);
    }

    ExplodedNode *getNode() const {
      return reinterpret_cast<ExplodedNode*>(getPtr());
    }
    
  public:
    NodeGroup() : P(0) {}
    
    ~NodeGroup();
    
    ExplodedNode** begin() const;
    
    ExplodedNode** end() const;
    
    unsigned size() const;
    
    bool empty() const { return size() == 0; }
    
    void addNode(ExplodedNode* N);
    
    void setFlag() {
      assert (P == 0);
      P = AuxFlag;
    }
    
    bool getFlag() const {
      return P & AuxFlag ? true : false;
    }
  };  
  
  /// Location - The program location (within a function body) associated
  ///  with this node.
  const ProgramPoint Location;
  
  /// State - The state associated with this node.
  const GRState* State;
  
  /// Preds - The predecessors of this node.
  NodeGroup Preds;
  
  /// Succs - The successors of this node.
  NodeGroup Succs;

public:

  explicit ExplodedNode(const ProgramPoint& loc, const GRState* state) 
    : Location(loc), State(state) {}

  /// getLocation - Returns the edge associated with the given node.
  ProgramPoint getLocation() const { return Location; }

  const GRState* getState() const {
    return State;
  }

  template <typename T>
  const T* getLocationAs() const { return llvm::dyn_cast<T>(&Location); }

  static void Profile(llvm::FoldingSetNodeID &ID, 
                      const ProgramPoint& Loc, const GRState* state);

  void Profile(llvm::FoldingSetNodeID& ID) const {
    Profile(ID, getLocation(), getState());
  }

  /// addPredeccessor - Adds a predecessor to the current node, and 
  ///  in tandem add this node as a successor of the other node.
  void addPredecessor(ExplodedNode* V);

  unsigned succ_size() const { return Succs.size(); }
  unsigned pred_size() const { return Preds.size(); }
  bool succ_empty() const { return Succs.empty(); }
  bool pred_empty() const { return Preds.empty(); }
  
  bool isSink() const { return Succs.getFlag(); }
  void markAsSink() { Succs.setFlag(); } 

  ExplodedNode* getFirstPred() {
    return pred_empty() ? NULL : *(pred_begin());
  }
  
  const ExplodedNode* getFirstPred() const {
    return const_cast<ExplodedNode*>(this)->getFirstPred();
  }
  
  // Iterators over successor and predecessor vertices.
  typedef ExplodedNode**       succ_iterator;
  typedef const ExplodedNode* const * const_succ_iterator;
  typedef ExplodedNode**       pred_iterator;
  typedef const ExplodedNode* const * const_pred_iterator;

  pred_iterator pred_begin() { return Preds.begin(); }
  pred_iterator pred_end() { return Preds.end(); }

  const_pred_iterator pred_begin() const {
    return const_cast<ExplodedNode*>(this)->pred_begin();
  }  
  const_pred_iterator pred_end() const {
    return const_cast<ExplodedNode*>(this)->pred_end();
  }

  succ_iterator succ_begin() { return Succs.begin(); }
  succ_iterator succ_end() { return Succs.end(); }

  const_succ_iterator succ_begin() const {
    return const_cast<ExplodedNode*>(this)->succ_begin();
  }
  const_succ_iterator succ_end() const {
    return const_cast<ExplodedNode*>(this)->succ_end();
  }

  // For debugging.
  
public:
  
  class Auditor {
  public:
    virtual ~Auditor();
    virtual void AddEdge(ExplodedNode* Src, ExplodedNode* Dst) = 0;
  };
  
  static void SetAuditor(Auditor* A);
};


template <typename StateTy>
struct GRTrait {
  static inline void Profile(llvm::FoldingSetNodeID& ID, const StateTy* St) {
    St->Profile(ID);
  }
};

class InterExplodedGraphMapImpl;

class ExplodedGraphImpl {
protected:
  friend class GRCoreEngineImpl;
  friend class GRStmtNodeBuilderImpl;
  friend class GRBranchNodeBuilderImpl;
  friend class GRIndirectGotoNodeBuilderImpl;
  friend class GRSwitchNodeBuilderImpl;
  friend class GREndPathNodeBuilderImpl;
  
  // Type definitions.
  typedef llvm::SmallVector<ExplodedNode*,2>    RootsTy;
  typedef llvm::SmallVector<ExplodedNode*,10>   EndNodesTy;
    
  /// Roots - The roots of the simulation graph. Usually there will be only
  /// one, but clients are free to establish multiple subgraphs within a single
  /// SimulGraph. Moreover, these subgraphs can often merge when paths from
  /// different roots reach the same state at the same program location.
  RootsTy Roots;

  /// EndNodes - The nodes in the simulation graph which have been
  ///  specially marked as the endpoint of an abstract simulation path.
  EndNodesTy EndNodes;
  
  /// Allocator - BumpPtrAllocator to create nodes.
  llvm::BumpPtrAllocator Allocator;
  
  /// cfg - The CFG associated with this analysis graph.
  CFG& cfg;
  
  /// CodeDecl - The declaration containing the code being analyzed.  This
  ///  can be a FunctionDecl or and ObjCMethodDecl.
  Decl& CodeDecl;
  
  /// Ctx - The ASTContext used to "interpret" CodeDecl.
  ASTContext& Ctx;
  
  /// NumNodes - The number of nodes in the graph.
  unsigned NumNodes;

  /// getNodeImpl - Retrieve the node associated with a (Location,State)
  ///  pair, where 'State' is represented as an opaque void*.  This method
  ///  is intended to be used only by GRCoreEngineImpl.
  virtual ExplodedNode* getNodeImpl(const ProgramPoint& L,
                                        const void* State,
                                        bool* IsNew) = 0;
  
  virtual ExplodedGraphImpl* MakeEmptyGraph() const = 0;

  /// addRoot - Add an untyped node to the set of roots.
  ExplodedNode* addRoot(ExplodedNode* V) {
    Roots.push_back(V);
    return V;
  }

  /// addEndOfPath - Add an untyped node to the set of EOP nodes.
  ExplodedNode* addEndOfPath(ExplodedNode* V) {
    EndNodes.push_back(V);
    return V;
  }
  
  // ctor.
  ExplodedGraphImpl(CFG& c, Decl& cd, ASTContext& ctx)
    : cfg(c), CodeDecl(cd), Ctx(ctx), NumNodes(0) {}

public:
  virtual ~ExplodedGraphImpl() {}

  unsigned num_roots() const { return Roots.size(); }
  unsigned num_eops() const { return EndNodes.size(); }
  
  bool empty() const { return NumNodes == 0; }
  unsigned size() const { return NumNodes; }
  
  llvm::BumpPtrAllocator& getAllocator() { return Allocator; }
  CFG& getCFG() { return cfg; }
  ASTContext& getContext() { return Ctx; }

  Decl& getCodeDecl() { return CodeDecl; }
  const Decl& getCodeDecl() const { return CodeDecl; }

  const FunctionDecl* getFunctionDecl() const {
    return llvm::dyn_cast<FunctionDecl>(&CodeDecl);
  }
  
  typedef llvm::DenseMap<const ExplodedNode*, ExplodedNode*> NodeMap;

  ExplodedGraphImpl* Trim(const ExplodedNode* const * NBeg,
                          const ExplodedNode* const * NEnd,
                          InterExplodedGraphMapImpl *M,
                    llvm::DenseMap<const void*, const void*> *InverseMap) const;
};
  
class InterExplodedGraphMapImpl {
  llvm::DenseMap<const ExplodedNode*, ExplodedNode*> M;
  friend class ExplodedGraphImpl;  
  void add(const ExplodedNode* From, ExplodedNode* To);
  
protected:
  ExplodedNode* getMappedImplNode(const ExplodedNode* N) const;
  
  InterExplodedGraphMapImpl();
public:
  virtual ~InterExplodedGraphMapImpl() {}
};
  
//===----------------------------------------------------------------------===//
// Type-specialized ExplodedGraph classes.
//===----------------------------------------------------------------------===//
  
class InterExplodedGraphMap : public InterExplodedGraphMapImpl {
public:
  InterExplodedGraphMap() {};
  ~InterExplodedGraphMap() {};

  ExplodedNode* getMappedNode(const ExplodedNode* N) const {
    return static_cast<ExplodedNode*>(getMappedImplNode(N));
  }
};
  
template <typename STATE>
class ExplodedGraph : public ExplodedGraphImpl {
public:
  typedef STATE                       StateTy;
  typedef ExplodedNode      NodeTy;  
  typedef llvm::FoldingSet<NodeTy>    AllNodesTy;
  
protected:  
  virtual ExplodedNode* getNodeImpl(const ProgramPoint& L,
                                    const void* State,
                                    bool* IsNew) {
    
    return getNode(L, static_cast<const StateTy*>(State), IsNew);
  }

  /// Nodes - The nodes in the graph.
  AllNodesTy Nodes;
  
protected:
  virtual ExplodedGraphImpl* MakeEmptyGraph() const {
    return new ExplodedGraph(cfg, CodeDecl, Ctx);
  }  
    
public:
  ExplodedGraph(CFG& c, Decl& cd, ASTContext& ctx)
    : ExplodedGraphImpl(c, cd, ctx) {}
  
  /// getNode - Retrieve the node associated with a (Location,State) pair,
  ///  where the 'Location' is a ProgramPoint in the CFG.  If no node for
  ///  this pair exists, it is created.  IsNew is set to true if
  ///  the node was freshly created.
  NodeTy* getNode(const ProgramPoint& L, const GRState* State,
                  bool* IsNew = NULL) {
    
    // Profile 'State' to determine if we already have an existing node.
    llvm::FoldingSetNodeID profile;    
    void* InsertPos = 0;
    
    NodeTy::Profile(profile, L, State);
    NodeTy* V = Nodes.FindNodeOrInsertPos(profile, InsertPos);

    if (!V) {
      // Allocate a new node.
      V = (NodeTy*) Allocator.Allocate<NodeTy>();
      new (V) NodeTy(L, State);
      
      // Insert the node into the node set and return it.
      Nodes.InsertNode(V, InsertPos);
      
      ++NumNodes;
      
      if (IsNew) *IsNew = true;
    }
    else
      if (IsNew) *IsNew = false;

    return V;
  }
  
  // Iterators.
  typedef NodeTy**                            roots_iterator;
  typedef const NodeTy**                      const_roots_iterator;
  typedef NodeTy**                            eop_iterator;
  typedef const NodeTy**                      const_eop_iterator;
  typedef typename AllNodesTy::iterator       node_iterator;
  typedef typename AllNodesTy::const_iterator const_node_iterator;
  
  node_iterator nodes_begin() {
    return Nodes.begin();
  }

  node_iterator nodes_end() {
    return Nodes.end();
  }
  
  const_node_iterator nodes_begin() const {
    return Nodes.begin();
  }
  
  const_node_iterator nodes_end() const {
    return Nodes.end();
  }
  
  roots_iterator roots_begin() {
    return reinterpret_cast<roots_iterator>(Roots.begin());
  }
  
  roots_iterator roots_end() { 
    return reinterpret_cast<roots_iterator>(Roots.end());
  }
  
  const_roots_iterator roots_begin() const { 
    return const_cast<ExplodedGraph>(this)->roots_begin();
  }
  
  const_roots_iterator roots_end() const { 
    return const_cast<ExplodedGraph>(this)->roots_end();
  }  

  eop_iterator eop_begin() {
    return reinterpret_cast<eop_iterator>(EndNodes.begin());
  }
    
  eop_iterator eop_end() { 
    return reinterpret_cast<eop_iterator>(EndNodes.end());
  }
  
  const_eop_iterator eop_begin() const {
    return const_cast<ExplodedGraph>(this)->eop_begin();
  }
  
  const_eop_iterator eop_end() const {
    return const_cast<ExplodedGraph>(this)->eop_end();
  }
  
  std::pair<ExplodedGraph*, InterExplodedGraphMap*>
  Trim(const NodeTy* const* NBeg, const NodeTy* const* NEnd,
       llvm::DenseMap<const void*, const void*> *InverseMap = 0) const {
    
    if (NBeg == NEnd)
      return std::make_pair((ExplodedGraph*) 0,
                            (InterExplodedGraphMap*) 0);
    
    assert (NBeg < NEnd);
    
    const ExplodedNode* const* NBegImpl =
      (const ExplodedNode* const*) NBeg;
    const ExplodedNode* const* NEndImpl =
      (const ExplodedNode* const*) NEnd;
    
    llvm::OwningPtr<InterExplodedGraphMap> M(new InterExplodedGraphMap());

    ExplodedGraphImpl* G = ExplodedGraphImpl::Trim(NBegImpl, NEndImpl, M.get(),
                                                   InverseMap);

    return std::make_pair(static_cast<ExplodedGraph*>(G), M.take());
  }
};

class ExplodedNodeSet {
  typedef llvm::SmallPtrSet<ExplodedNode*,5> ImplTy;
  ImplTy Impl;
  
public:
  ExplodedNodeSet(ExplodedNode* N) {
    assert (N && !static_cast<ExplodedNode*>(N)->isSink());
    Impl.insert(N);
  }
  
  ExplodedNodeSet() {}
  
  inline void Add(ExplodedNode* N) {
    if (N && !static_cast<ExplodedNode*>(N)->isSink()) Impl.insert(N);
  }
  
  ExplodedNodeSet& operator=(const ExplodedNodeSet &X) {
    Impl = X.Impl;
    return *this;
  }
  
  typedef ImplTy::iterator       iterator;
  typedef ImplTy::const_iterator const_iterator;

  inline unsigned size() const { return Impl.size();  }
  inline bool empty()    const { return Impl.empty(); }

  inline void clear() { Impl.clear(); }
  
  inline iterator begin() { return Impl.begin(); }
  inline iterator end()   { return Impl.end();   }
  
  inline const_iterator begin() const { return Impl.begin(); }
  inline const_iterator end()   const { return Impl.end();   }
};  
  
} // end clang namespace

// GraphTraits

namespace llvm {
  template<> struct GraphTraits<clang::ExplodedNode*> {
    typedef clang::ExplodedNode NodeType;
    typedef NodeType::succ_iterator  ChildIteratorType;
    typedef llvm::df_iterator<NodeType*>      nodes_iterator;
    
    static inline NodeType* getEntryNode(NodeType* N) {
      return N;
    }
    
    static inline ChildIteratorType child_begin(NodeType* N) {
      return N->succ_begin();
    }
    
    static inline ChildIteratorType child_end(NodeType* N) {
      return N->succ_end();
    }
    
    static inline nodes_iterator nodes_begin(NodeType* N) {
      return df_begin(N);
    }
    
    static inline nodes_iterator nodes_end(NodeType* N) {
      return df_end(N);
    }
  };
  
  template<> struct GraphTraits<const clang::ExplodedNode*> {
    typedef const clang::ExplodedNode NodeType;
    typedef NodeType::const_succ_iterator   ChildIteratorType;
    typedef llvm::df_iterator<NodeType*>       nodes_iterator;
    
    static inline NodeType* getEntryNode(NodeType* N) {
      return N;
    }
    
    static inline ChildIteratorType child_begin(NodeType* N) {
      return N->succ_begin();
    }
    
    static inline ChildIteratorType child_end(NodeType* N) {
      return N->succ_end();
    }
    
    static inline nodes_iterator nodes_begin(NodeType* N) {
      return df_begin(N);
    }
    
    static inline nodes_iterator nodes_end(NodeType* N) {
      return df_end(N);
    }
  };
  
} // end llvm namespace

#endif
