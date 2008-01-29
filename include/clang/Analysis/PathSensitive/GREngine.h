//==- GREngine.h - Path-Sensitive Dataflow Engine ------------------*- C++ -*-//
//             
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines a generic engine for intraprocedural, path-sensitive,
//  dataflow analysis via graph reachability engine.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_ANALYSIS_GRENGINE
#define LLVM_CLANG_ANALYSIS_GRENGINE

#include "clang/Analysis/PathSensitive/ExplodedGraph.h"
#include "clang/Analysis/PathSensitive/GRWorkList.h"
#include "llvm/ADT/OwningPtr.h"

namespace clang {
  
class GRStmtNodeBuilderImpl;
class GRBranchNodeBuilderImpl;
class GRWorkList;
  
class GREngineImpl {
protected:
  friend class GRStmtNodeBuilderImpl;
  friend class GRBranchNodeBuilderImpl;
  
  typedef llvm::DenseMap<Stmt*,Stmt*> ParentMapTy;
    
  /// G - The simulation graph.  Each node is a (location,state) pair.
  llvm::OwningPtr<ExplodedGraphImpl> G;
  
  /// ParentMap - A lazily populated map from a Stmt* to its parent Stmt*.
  void* ParentMap;
  
  /// CurrentBlkExpr - The current Block-level expression being processed.
  ///  This is used when lazily populating ParentMap.
  Stmt* CurrentBlkExpr;
  
  /// WList - A set of queued nodes that need to be processed by the
  ///  worklist algorithm.  It is up to the implementation of WList to decide
  ///  the order that nodes are processed.
  GRWorkList* WList;
  
  void GenerateNode(const ProgramPoint& Loc, void* State, 
                    ExplodedNodeImpl* Pred = NULL);
  
  /// getInitialState - Gets the void* representing the initial 'state'
  ///  of the analysis.  This is simply a wrapper (implemented
  ///  in GREngine) that performs type erasure on the initial
  ///  state returned by the checker object.
  virtual void* getInitialState() = 0;
  
  void HandleBlockEdge(const BlockEdge& E, ExplodedNodeImpl* Pred);
  void HandleBlockEntrance(const BlockEntrance& E, ExplodedNodeImpl* Pred);
  void HandleBlockExit(CFGBlock* B, ExplodedNodeImpl* Pred);
  void HandlePostStmt(const PostStmt& S, CFGBlock* B,
                      unsigned StmtIdx, ExplodedNodeImpl *Pred);
  
  void HandleBranch(Stmt* Cond, Stmt* Term, CFGBlock* B,
                    ExplodedNodeImpl* Pred);  
  
  virtual void* ProcessEOP(CFGBlock* Blk, void* State) = 0;  

  virtual void  ProcessStmt(Stmt* S, GRStmtNodeBuilderImpl& Builder) = 0;

  virtual void  ProcessBranch(Stmt* Condition, Stmt* Terminator,
                              GRBranchNodeBuilderImpl& Builder) = 0;


private:
  GREngineImpl(const GREngineImpl&); // Do not implement.
  GREngineImpl& operator=(const GREngineImpl&);
  
protected:  
  GREngineImpl(ExplodedGraphImpl* g, GRWorkList* wl) : G(g), WList(wl) {}
  
public:
  /// ExecuteWorkList - Run the worklist algorithm for a maximum number of
  ///  steps.  Returns true if there is still simulation state on the worklist.
  bool ExecuteWorkList(unsigned Steps = 1000000);
  
  virtual ~GREngineImpl() {}
  
  CFG& getCFG() { return G->getCFG(); }
};
  
class GRStmtNodeBuilderImpl {
  GREngineImpl& Eng;
  CFGBlock& B;
  const unsigned Idx;
  ExplodedNodeImpl* LastNode;  
  bool HasGeneratedNode;
  bool Populated;
  
  typedef llvm::SmallPtrSet<ExplodedNodeImpl*,5> DeferredTy;
  DeferredTy Deferred;
  
  void GenerateAutoTransition(ExplodedNodeImpl* N);
  
public:
  GRStmtNodeBuilderImpl(CFGBlock* b, unsigned idx,
                    ExplodedNodeImpl* N, GREngineImpl* e);      
  
  ~GRStmtNodeBuilderImpl();
  
  const ExplodedGraphImpl& getGraph() const { return *Eng.G; }
  
  inline ExplodedNodeImpl* getLastNode() {
    return LastNode ? (LastNode->isInfeasible() ? NULL : LastNode) : NULL;
  }
  
  ExplodedNodeImpl* generateNodeImpl(Stmt* S, void* State,
                                     ExplodedNodeImpl* Pred);

  inline ExplodedNodeImpl* generateNodeImpl(Stmt* S, void* State) {
    ExplodedNodeImpl* N = getLastNode();
    assert (N && "Predecessor of new node is infeasible.");
    return generateNodeImpl(S, State, N);
  }
  
  Stmt* getStmt() const { return B[Idx]; }
  
  CFGBlock* getBlock() const { return &B; }
};

template<typename CHECKER>
class GRStmtNodeBuilder  {
  typedef CHECKER                                CheckerTy; 
  typedef typename CheckerTy::StateTy            StateTy;
  typedef ExplodedGraph<CheckerTy>               GraphTy;
  typedef typename GraphTy::NodeTy               NodeTy;
  
  GRStmtNodeBuilderImpl& NB;
  
public:
  GRStmtNodeBuilder(GRStmtNodeBuilderImpl& nb) : NB(nb) {}
  
  const GraphTy& getGraph() const {
    return static_cast<const GraphTy&>(NB.getGraph());
  }
  
  NodeTy* getLastNode() const {
    return static_cast<NodeTy*>(NB.getLastNode());
  }
  
  NodeTy* generateNode(Stmt* S, StateTy State, NodeTy* Pred) {
    void *state = GRTrait<StateTy>::toPtr(State);        
    return static_cast<NodeTy*>(NB.generateNodeImpl(S, state, Pred));
  }
  
  NodeTy* generateNode(Stmt* S, StateTy State) {
    void *state = GRTrait<StateTy>::toPtr(State);
    return static_cast<NodeTy*>(NB.generateNodeImpl(S, state));    
  }  
};
  
class GRBranchNodeBuilderImpl {
  GREngineImpl& Eng;
  CFGBlock* Src;
  CFGBlock* DstT;
  CFGBlock* DstF;
  ExplodedNodeImpl* Pred;

  bool GeneratedTrue;
  bool GeneratedFalse;
  
public:
  GRBranchNodeBuilderImpl(CFGBlock* src, CFGBlock* dstT, CFGBlock* dstF,
                          ExplodedNodeImpl* pred, GREngineImpl* e) 
  : Eng(*e), Src(src), DstT(dstT), DstF(dstF), Pred(pred),
    GeneratedTrue(false), GeneratedFalse(false) {}
  
  ~GRBranchNodeBuilderImpl();
  
  const ExplodedGraphImpl& getGraph() const { return *Eng.G; }
    
  void generateNodeImpl(void* State, bool branch);  
  
  void markInfeasible(bool branch) {
    if (branch) GeneratedTrue = true;
    else GeneratedFalse = true;
  }
};

template<typename CHECKER>
class GRBranchNodeBuilder {
  typedef CHECKER                                CheckerTy; 
  typedef typename CheckerTy::StateTy            StateTy;
  typedef ExplodedGraph<CheckerTy>               GraphTy;
  typedef typename GraphTy::NodeTy               NodeTy;
  
  GRBranchNodeBuilderImpl& NB;
  
public:
  GRBranchNodeBuilder(GRBranchNodeBuilderImpl& nb) : NB(nb) {}
  
  const GraphTy& getGraph() const {
    return static_cast<const GraphTy&>(NB.getGraph());
  }

  inline void generateNode(StateTy State, bool branch) {
    void *state = GRTrait<StateTy>::toPtr(State);        
    NB.generateNodeImpl(state, branch);
  }
  
  inline void markInfeasible(bool branch) {
    NB.markInfeasible(branch);
  }
};
  
  
template<typename CHECKER>
class GREngine : public GREngineImpl {
public:
  typedef CHECKER                                CheckerTy; 
  typedef typename CheckerTy::StateTy            StateTy;
  typedef ExplodedGraph<CheckerTy>               GraphTy;
  typedef typename GraphTy::NodeTy               NodeTy;

protected:
  // A local reference to the checker that avoids an indirect access
  // via the Graph.
  CheckerTy* Checker;  
  
  virtual void* getInitialState() {
    return GRTrait<StateTy>::toPtr(getCheckerState().getInitialState());
  }
  
  virtual void* ProcessEOP(CFGBlock* Blk, void* State) {
    // FIXME: Perform dispatch to adjust state.
    return State;
  }
  
  virtual void ProcessStmt(Stmt* S, GRStmtNodeBuilderImpl& BuilderImpl) {
    GRStmtNodeBuilder<CHECKER> Builder(BuilderImpl);
    Checker->ProcessStmt(S, Builder);
  }


  virtual void ProcessBranch(Stmt* Condition, Stmt* Terminator,
                             GRBranchNodeBuilderImpl& BuilderImpl) {
    GRBranchNodeBuilder<CHECKER> Builder(BuilderImpl);
    Checker->ProcessBranch(Condition, Terminator, Builder);    
  }
  
  
public:  
  /// Construct a GREngine object to analyze the provided CFG using
  ///  a DFS exploration of the exploded graph.
  GREngine(CFG& cfg, FunctionDecl& fd, ASTContext& ctx)
    : GREngineImpl(new GraphTy(cfg, fd, ctx), GRWorkList::MakeDFS()),
      Checker(static_cast<GraphTy*>(G.get())->getCheckerState()) {}
  
  /// Construct a GREngine object to analyze the provided CFG and to
  ///  use the provided worklist object to execute the worklist algorithm.
  ///  The GREngine object assumes ownership of 'wlist'.
  GREngine(CFG& cfg, FunctionDecl& fd, ASTContext& ctx, GRWorkList* wlist)
    : GREngineImpl(new GraphTy(cfg, fd, ctx), wlist),
      Checker(static_cast<GraphTy*>(G.get())->getCheckerState()) {}
  
  virtual ~GREngine() {}
  
  /// getGraph - Returns the exploded graph.
  GraphTy& getGraph() {
    return *static_cast<GraphTy*>(G.get());
  }
  
  /// getCheckerState - Returns the internal checker state.
  CheckerTy& getCheckerState() {
    return *Checker;
  }  
  
  /// takeGraph - Returns the exploded graph.  Ownership of the graph is
  ///  transfered to the caller.
  GraphTy* takeGraph() { 
    return static_cast<GraphTy*>(G.take());
  }
};

} // end clang namespace

#endif
