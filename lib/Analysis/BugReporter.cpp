// BugReporter.cpp - Generate PathDiagnostics for Bugs ------------*- C++ -*--//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines BugReporter, a utility class for generating
//  PathDiagnostics for analyses based on GRSimpleVals.
//
//===----------------------------------------------------------------------===//

#include "clang/Analysis/PathSensitive/BugReporter.h"
#include "clang/Analysis/PathSensitive/GRExprEngine.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/CFG.h"
#include "clang/AST/Expr.h"
#include "clang/Analysis/ProgramPoint.h"
#include "clang/Analysis/PathDiagnostic.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"

using namespace clang;

//===----------------------------------------------------------------------===//
// static functions.
//===----------------------------------------------------------------------===//

static inline Stmt* GetStmt(ProgramPoint P) {
  if (const PostStmt* PS = dyn_cast<PostStmt>(&P))
    return PS->getStmt();
  else if (const BlockEdge* BE = dyn_cast<BlockEdge>(&P))
    return BE->getSrc()->getTerminator();
  
  return 0;
}

static inline const ExplodedNode<GRState>*
GetPredecessorNode(const ExplodedNode<GRState>* N) {
  return N->pred_empty() ? NULL : *(N->pred_begin());
}

static inline const ExplodedNode<GRState>*
GetSuccessorNode(const ExplodedNode<GRState>* N) {
  return N->succ_empty() ? NULL : *(N->succ_begin());
}

static Stmt* GetPreviousStmt(const ExplodedNode<GRState>* N) {
  for (N = GetPredecessorNode(N); N; N = GetPredecessorNode(N))
    if (Stmt *S = GetStmt(N->getLocation()))
      return S;
  
  return 0;
}

static Stmt* GetNextStmt(const ExplodedNode<GRState>* N) {
  for (N = GetSuccessorNode(N); N; N = GetSuccessorNode(N))
    if (Stmt *S = GetStmt(N->getLocation()))
      return S;
  
  return 0;
}

static inline Stmt* GetCurrentOrPreviousStmt(const ExplodedNode<GRState>* N) {  
  if (Stmt *S = GetStmt(N->getLocation()))
    return S;
  
  return GetPreviousStmt(N);
}
        
static inline Stmt* GetCurrentOrNextStmt(const ExplodedNode<GRState>* N) {  
  if (Stmt *S = GetStmt(N->getLocation()))
    return S;
          
  return GetNextStmt(N);
}

//===----------------------------------------------------------------------===//
// Diagnostics for 'execution continues on line XXX'.
//===----------------------------------------------------------------------===//

static inline void ExecutionContinues(llvm::raw_string_ostream& os,
                                      SourceManager& SMgr,
                                      const ExplodedNode<GRState>* N,
                                      const Decl& D) {
  
  // Slow, but probably doesn't matter.
  if (os.str().empty())
    os << ' ';
  
  if (Stmt *S = GetNextStmt(N))
    os << "Execution continues on line "
    << SMgr.getInstantiationLineNumber(S->getLocStart()) << '.';
  else
    os << "Execution jumps to the end of the "
       << (isa<ObjCMethodDecl>(D) ? "method" : "function") << '.';
}

//===----------------------------------------------------------------------===//
// Methods for BugType and subclasses.
//===----------------------------------------------------------------------===//
BugType::~BugType() {}
void BugType::FlushReports(BugReporter &BR) {}

//===----------------------------------------------------------------------===//
// Methods for BugReport and subclasses.
//===----------------------------------------------------------------------===//
BugReport::~BugReport() {}
RangedBugReport::~RangedBugReport() {}

Stmt* BugReport::getStmt(BugReporter& BR) const {  
  ProgramPoint ProgP = EndNode->getLocation();  
  Stmt *S = NULL;
  
  if (BlockEntrance* BE = dyn_cast<BlockEntrance>(&ProgP)) {
    if (BE->getBlock() == &BR.getCFG()->getExit()) S = GetPreviousStmt(EndNode);
  }
  if (!S) S = GetStmt(ProgP);  
  
  return S;  
}

PathDiagnosticPiece*
BugReport::getEndPath(BugReporter& BR,
                      const ExplodedNode<GRState>* EndPathNode) {
  
  Stmt* S = getStmt(BR);
  
  if (!S)
    return NULL;
  
  FullSourceLoc L(S->getLocStart(), BR.getContext().getSourceManager());
  PathDiagnosticPiece* P = new PathDiagnosticPiece(L, getDescription());
  
  const SourceRange *Beg, *End;
  getRanges(BR, Beg, End);  
  
  for (; Beg != End; ++Beg)
    P->addRange(*Beg);
  
  return P;
}

void BugReport::getRanges(BugReporter& BR, const SourceRange*& beg,
                          const SourceRange*& end) {  
  
  if (Expr* E = dyn_cast_or_null<Expr>(getStmt(BR))) {
    R = E->getSourceRange();
    assert(R.isValid());
    beg = &R;
    end = beg+1;
  }
  else
    beg = end = 0;
}

SourceLocation BugReport::getLocation() const {  
  if (EndNode)
    if (Stmt* S = GetCurrentOrPreviousStmt(EndNode)) {
      // For member expressions, return the location of the '.' or '->'.
      if (MemberExpr* ME = dyn_cast<MemberExpr>(S))
        return ME->getMemberLoc();

      return S->getLocStart();
    }

  return FullSourceLoc();
}

PathDiagnosticPiece* BugReport::VisitNode(const ExplodedNode<GRState>* N,
                                          const ExplodedNode<GRState>* PrevN,
                                          const ExplodedGraph<GRState>& G,
                                          BugReporter& BR,
                                          NodeResolver &NR) {
  return NULL;
}

//===----------------------------------------------------------------------===//
// Methods for BugReporter and subclasses.
//===----------------------------------------------------------------------===//

BugReportEquivClass::~BugReportEquivClass() {
  for (iterator I=begin(), E=end(); I!=E; ++I) delete *I;  
}

GRBugReporter::~GRBugReporter() { FlushReports(); }
BugReporterData::~BugReporterData() {}

ExplodedGraph<GRState>&
GRBugReporter::getGraph() { return Eng.getGraph(); }

GRStateManager&
GRBugReporter::getStateManager() { return Eng.getStateManager(); }

BugReporter::~BugReporter() { FlushReports(); }

void BugReporter::FlushReports() {
  if (BugTypes.isEmpty())
    return;

  // First flush the warnings for each BugType.  This may end up creating new
  // warnings and new BugTypes.  Because ImmutableSet is a functional data
  // structure, we do not need to worry about the iterators being invalidated.
  for (BugTypesTy::iterator I=BugTypes.begin(), E=BugTypes.end(); I!=E; ++I)
    const_cast<BugType*>(*I)->FlushReports(*this);

  // Iterate through BugTypes a second time.  BugTypes may have been updated
  // with new BugType objects and new warnings.
  for (BugTypesTy::iterator I=BugTypes.begin(), E=BugTypes.end(); I!=E; ++I) {
    BugType *BT = const_cast<BugType*>(*I);

    typedef llvm::FoldingSet<BugReportEquivClass> SetTy;
    SetTy& EQClasses = BT->EQClasses;

    for (SetTy::iterator EI=EQClasses.begin(), EE=EQClasses.end(); EI!=EE;++EI){
      BugReportEquivClass& EQ = *EI;
      FlushReport(EQ);
    }
    
    // Delete the BugType object.  This will also delete the equivalence
    // classes.
    delete BT;
  }

  // Remove all references to the BugType objects.
  BugTypes = F.GetEmptySet();
}

//===----------------------------------------------------------------------===//
// PathDiagnostics generation.
//===----------------------------------------------------------------------===//

typedef llvm::DenseMap<const ExplodedNode<GRState>*,
                       const ExplodedNode<GRState>*> NodeBackMap;

static std::pair<std::pair<ExplodedGraph<GRState>*, NodeBackMap*>,
                 std::pair<ExplodedNode<GRState>*, unsigned> >
MakeReportGraph(const ExplodedGraph<GRState>* G,
                const ExplodedNode<GRState>** NStart,
                const ExplodedNode<GRState>** NEnd) {
  
  // Create the trimmed graph.  It will contain the shortest paths from the
  // error nodes to the root.  In the new graph we should only have one 
  // error node unless there are two or more error nodes with the same minimum
  // path length.
  ExplodedGraph<GRState>* GTrim;
  InterExplodedGraphMap<GRState>* NMap;

  llvm::DenseMap<const void*, const void*> InverseMap;
  llvm::tie(GTrim, NMap) = G->Trim(NStart, NEnd, &InverseMap);
  
  // Create owning pointers for GTrim and NMap just to ensure that they are
  // released when this function exists.
  llvm::OwningPtr<ExplodedGraph<GRState> > AutoReleaseGTrim(GTrim);
  llvm::OwningPtr<InterExplodedGraphMap<GRState> > AutoReleaseNMap(NMap);
  
  // Find the (first) error node in the trimmed graph.  We just need to consult
  // the node map (NMap) which maps from nodes in the original graph to nodes
  // in the new graph.
  const ExplodedNode<GRState>* N = 0;
  unsigned NodeIndex = 0;

  for (const ExplodedNode<GRState>** I = NStart; I != NEnd; ++I)
    if ((N = NMap->getMappedNode(*I))) {
      NodeIndex = (I - NStart) / sizeof(*I);
      break;
    }
  
  assert(N && "No error node found in the trimmed graph.");

  // Create a new (third!) graph with a single path.  This is the graph
  // that will be returned to the caller.
  ExplodedGraph<GRState> *GNew =
    new ExplodedGraph<GRState>(GTrim->getCFG(), GTrim->getCodeDecl(),
                               GTrim->getContext());
  
  // Sometimes the trimmed graph can contain a cycle.  Perform a reverse DFS
  // to the root node, and then construct a new graph that contains only
  // a single path.
  llvm::DenseMap<const void*,unsigned> Visited;
  llvm::SmallVector<const ExplodedNode<GRState>*, 10> WS;
  WS.push_back(N);
  unsigned cnt = 0;
  const ExplodedNode<GRState>* Root = 0;
  
  while (!WS.empty()) {
    const ExplodedNode<GRState>* Node = WS.back();
    WS.pop_back();
    
    if (Visited.find(Node) != Visited.end())
      continue;
    
    Visited[Node] = cnt++;
    
    if (Node->pred_empty()) {
      Root = Node;
      break;
    }
    
    for (ExplodedNode<GRState>::const_pred_iterator I=Node->pred_begin(),
         E=Node->pred_end(); I!=E; ++I)
      WS.push_back(*I);
  }
  
  assert (Root);
  
  // Now walk from the root down the DFS path, always taking the successor
  // with the lowest number.
  ExplodedNode<GRState> *Last = 0, *First = 0;  
  NodeBackMap *BM = new NodeBackMap();
  
  for ( N = Root ;;) {
    // Lookup the number associated with the current node.
    llvm::DenseMap<const void*,unsigned>::iterator I = Visited.find(N);
    assert (I != Visited.end());
    
    // Create the equivalent node in the new graph with the same state
    // and location.
    ExplodedNode<GRState>* NewN =
      GNew->getNode(N->getLocation(), N->getState());
    
    // Store the mapping to the original node.
    llvm::DenseMap<const void*, const void*>::iterator IMitr=InverseMap.find(N);
    assert(IMitr != InverseMap.end() && "No mapping to original node.");
    (*BM)[NewN] = (const ExplodedNode<GRState>*) IMitr->second;
    
    // Link up the new node with the previous node.
    if (Last)
      NewN->addPredecessor(Last);
    
    Last = NewN;
    
    // Are we at the final node?
    if (I->second == 0) {
      First = NewN;
      break;
    }
    
    // Find the next successor node.  We choose the node that is marked
    // with the lowest DFS number.
    ExplodedNode<GRState>::const_succ_iterator SI = N->succ_begin();
    ExplodedNode<GRState>::const_succ_iterator SE = N->succ_end();
    N = 0;
    
    for (unsigned MinVal = 0; SI != SE; ++SI) {
      
      I = Visited.find(*SI);
      
      if (I == Visited.end())
        continue;
      
      if (!N || I->second < MinVal) {
        N = *SI;
        MinVal = I->second;
      }
    }
    
    assert (N);
  }
  
  assert (First);
  return std::make_pair(std::make_pair(GNew, BM),
                        std::make_pair(First, NodeIndex));
}

static const VarDecl*
GetMostRecentVarDeclBinding(const ExplodedNode<GRState>* N,
                            GRStateManager& VMgr, SVal X) {
  
  for ( ; N ; N = N->pred_empty() ? 0 : *N->pred_begin()) {
    
    ProgramPoint P = N->getLocation();
    
    if (!isa<PostStmt>(P))
      continue;
    
    DeclRefExpr* DR = dyn_cast<DeclRefExpr>(cast<PostStmt>(P).getStmt());
    
    if (!DR)
      continue;
    
    SVal Y = VMgr.GetSVal(N->getState(), DR);
    
    if (X != Y)
      continue;
    
    VarDecl* VD = dyn_cast<VarDecl>(DR->getDecl());
    
    if (!VD)
      continue;
    
    return VD;
  }
  
  return 0;
}

namespace {
class VISIBILITY_HIDDEN NotableSymbolHandler 
  : public StoreManager::BindingsHandler {
    
  SymbolRef Sym;
  const GRState* PrevSt;
  const Stmt* S;
  GRStateManager& VMgr;
  const ExplodedNode<GRState>* Pred;
  PathDiagnostic& PD; 
  BugReporter& BR;
    
public:
  
  NotableSymbolHandler(SymbolRef sym, const GRState* prevst, const Stmt* s,
                       GRStateManager& vmgr, const ExplodedNode<GRState>* pred,
                       PathDiagnostic& pd, BugReporter& br)
    : Sym(sym), PrevSt(prevst), S(s), VMgr(vmgr), Pred(pred), PD(pd), BR(br) {}
                        
  bool HandleBinding(StoreManager& SMgr, Store store,
                     const MemRegion* R, SVal V) {

    SymbolRef ScanSym;
    
    if (loc::SymbolVal* SV = dyn_cast<loc::SymbolVal>(&V))
      ScanSym = SV->getSymbol();
    else if (nonloc::SymbolVal* SV = dyn_cast<nonloc::SymbolVal>(&V))
      ScanSym = SV->getSymbol();
    else
      return true;
    
    if (ScanSym != Sym)
      return true;
    
    // Check if the previous state has this binding.    
    SVal X = VMgr.GetSVal(PrevSt, loc::MemRegionVal(R));
    
    if (X == V) // Same binding?
      return true;
    
    // Different binding.  Only handle assignments for now.  We don't pull
    // this check out of the loop because we will eventually handle other 
    // cases.
    
    VarDecl *VD = 0;
    
    if (const BinaryOperator* B = dyn_cast<BinaryOperator>(S)) {
      if (!B->isAssignmentOp())
        return true;
      
      // What variable did we assign to?
      DeclRefExpr* DR = dyn_cast<DeclRefExpr>(B->getLHS()->IgnoreParenCasts());
      
      if (!DR)
        return true;
      
      VD = dyn_cast<VarDecl>(DR->getDecl());
    }
    else if (const DeclStmt* DS = dyn_cast<DeclStmt>(S)) {
      // FIXME: Eventually CFGs won't have DeclStmts.  Right now we
      //  assume that each DeclStmt has a single Decl.  This invariant
      //  holds by contruction in the CFG.
      VD = dyn_cast<VarDecl>(*DS->decl_begin());
    }
    
    if (!VD)
      return true;
    
    // What is the most recently referenced variable with this binding?
    const VarDecl* MostRecent = GetMostRecentVarDeclBinding(Pred, VMgr, V);
    
    if (!MostRecent)
      return true;
    
    // Create the diagnostic.
    FullSourceLoc L(S->getLocStart(), BR.getSourceManager());
    
    if (Loc::IsLocType(VD->getType())) {
      std::string msg = "'" + std::string(VD->getNameAsString()) +
      "' now aliases '" + MostRecent->getNameAsString() + "'";
      
      PD.push_front(new PathDiagnosticPiece(L, msg));
    }
    
    return true;
  }  
};
}

static void HandleNotableSymbol(const ExplodedNode<GRState>* N,
                                const Stmt* S,
                                SymbolRef Sym, BugReporter& BR,
                                PathDiagnostic& PD) {
  
  const ExplodedNode<GRState>* Pred = N->pred_empty() ? 0 : *N->pred_begin();
  const GRState* PrevSt = Pred ? Pred->getState() : 0;
  
  if (!PrevSt)
    return;
  
  // Look at the region bindings of the current state that map to the
  // specified symbol.  Are any of them not in the previous state?
  GRStateManager& VMgr = cast<GRBugReporter>(BR).getStateManager();
  NotableSymbolHandler H(Sym, PrevSt, S, VMgr, Pred, PD, BR);
  cast<GRBugReporter>(BR).getStateManager().iterBindings(N->getState(), H);
}

namespace {
class VISIBILITY_HIDDEN ScanNotableSymbols
  : public StoreManager::BindingsHandler {
    
  llvm::SmallSet<SymbolRef, 10> AlreadyProcessed;
  const ExplodedNode<GRState>* N;
  Stmt* S;
  GRBugReporter& BR;
  PathDiagnostic& PD;
    
public:
  ScanNotableSymbols(const ExplodedNode<GRState>* n, Stmt* s, GRBugReporter& br,
                     PathDiagnostic& pd)
    : N(n), S(s), BR(br), PD(pd) {}
  
  bool HandleBinding(StoreManager& SMgr, Store store,
                     const MemRegion* R, SVal V) {
    SymbolRef ScanSym;
  
    if (loc::SymbolVal* SV = dyn_cast<loc::SymbolVal>(&V))
      ScanSym = SV->getSymbol();
    else if (nonloc::SymbolVal* SV = dyn_cast<nonloc::SymbolVal>(&V))
      ScanSym = SV->getSymbol();
    else
      return true;
  
    assert (ScanSym.isValid());
  
    if (!BR.isNotable(ScanSym))
      return true;
  
    if (AlreadyProcessed.count(ScanSym))
      return true;
  
    AlreadyProcessed.insert(ScanSym);
  
    HandleNotableSymbol(N, S, ScanSym, BR, PD);
    return true;
  }
};
} // end anonymous namespace

namespace {
class VISIBILITY_HIDDEN NodeMapClosure : public BugReport::NodeResolver {
  NodeBackMap& M;
public:
  NodeMapClosure(NodeBackMap *m) : M(*m) {}
  ~NodeMapClosure() {}
  
  const ExplodedNode<GRState>* getOriginalNode(const ExplodedNode<GRState>* N) {
    NodeBackMap::iterator I = M.find(N);
    return I == M.end() ? 0 : I->second;
  }
};
}

void GRBugReporter::GeneratePathDiagnostic(PathDiagnostic& PD,
                                           BugReportEquivClass& EQ) {
  
  std::vector<const ExplodedNode<GRState>*> Nodes;

  for (BugReportEquivClass::iterator I=EQ.begin(), E=EQ.end(); I!=E; ++I) {
    const ExplodedNode<GRState>* N = I->getEndNode();
    if (N) Nodes.push_back(N);
  }
  
  if (Nodes.empty())
    return;
  
  // Construct a new graph that contains only a single path from the error
  // node to a root.  
  const std::pair<std::pair<ExplodedGraph<GRState>*, NodeBackMap*>,
                  std::pair<ExplodedNode<GRState>*, unsigned> >&
    GPair = MakeReportGraph(&getGraph(), &Nodes[0], &Nodes[0] + Nodes.size());
  
  // Find the BugReport with the original location.
  BugReport *R = 0;
  unsigned i = 0;
  for (BugReportEquivClass::iterator I=EQ.begin(), E=EQ.end(); I!=E; ++I, ++i)
    if (i == GPair.second.second) { R = *I; break; }
  
  assert(R && "No original report found for sliced graph.");
  
  llvm::OwningPtr<ExplodedGraph<GRState> > ReportGraph(GPair.first.first);
  llvm::OwningPtr<NodeBackMap> BackMap(GPair.first.second);
  const ExplodedNode<GRState> *N = GPair.second.first;

  // Start building the path diagnostic...  
  if (PathDiagnosticPiece* Piece = R->getEndPath(*this, N))
    PD.push_back(Piece);
  else
    return;
  
  const ExplodedNode<GRState>* NextNode = N->pred_empty() 
                                        ? NULL : *(N->pred_begin());
  
  ASTContext& Ctx = getContext();
  SourceManager& SMgr = Ctx.getSourceManager();
  NodeMapClosure NMC(BackMap.get());
  
  while (NextNode) {
    N = NextNode;    
    NextNode = GetPredecessorNode(N);
    
    ProgramPoint P = N->getLocation();
    
    if (const BlockEdge* BE = dyn_cast<BlockEdge>(&P)) {
      
      CFGBlock* Src = BE->getSrc();
      CFGBlock* Dst = BE->getDst();
      
      Stmt* T = Src->getTerminator();
      
      if (!T)
        continue;
      
      FullSourceLoc L(T->getLocStart(), SMgr);
      
      switch (T->getStmtClass()) {
        default:
          break;
          
        case Stmt::GotoStmtClass:
        case Stmt::IndirectGotoStmtClass: {
          
          Stmt* S = GetNextStmt(N);
          
          if (!S)
            continue;
          
          std::string sbuf;
          llvm::raw_string_ostream os(sbuf);
          
          os << "Control jumps to line "
             << SMgr.getInstantiationLineNumber(S->getLocStart()) << ".\n";
          
          PD.push_front(new PathDiagnosticPiece(L, os.str(), 
                                             PathDiagnosticPiece::ControlFlow));
          break;
        }
          
        case Stmt::SwitchStmtClass: {          
          // Figure out what case arm we took.
          std::string sbuf;
          llvm::raw_string_ostream os(sbuf);

          if (Stmt* S = Dst->getLabel())
            switch (S->getStmtClass()) {
            default:
              os << "No cases match in the switch statement. "
                    "Control jumps to line "
                 << SMgr.getInstantiationLineNumber(S->getLocStart()) << ".\n";
              break;
            case Stmt::DefaultStmtClass:
              os << "Control jumps to the 'default' case at line "
                 << SMgr.getInstantiationLineNumber(S->getLocStart()) << ".\n";
              break;
              
            case Stmt::CaseStmtClass: {
              os << "Control jumps to 'case ";
              
              CaseStmt* Case = cast<CaseStmt>(S);              
              Expr* LHS = Case->getLHS()->IgnoreParenCasts();
              
              // Determine if it is an enum.
              
              bool GetRawInt = true;
              
              if (DeclRefExpr* DR = dyn_cast<DeclRefExpr>(LHS)) {

                // FIXME: Maybe this should be an assertion.  Are there cases
                // were it is not an EnumConstantDecl?
                EnumConstantDecl* D = dyn_cast<EnumConstantDecl>(DR->getDecl());
                if (D) {
                  GetRawInt = false;
                  os << D->getNameAsString();
                }
              }

              if (GetRawInt) {
              
                // Not an enum.
                Expr* CondE = cast<SwitchStmt>(T)->getCond();
                unsigned bits = Ctx.getTypeSize(CondE->getType());
                llvm::APSInt V(bits, false);
                
                if (!LHS->isIntegerConstantExpr(V, Ctx, 0, true)) {
                  assert (false && "Case condition must be constant.");
                  continue;
                }
                
                os << V;
              }       
              
              os << ":'  at line " 
                 << SMgr.getInstantiationLineNumber(S->getLocStart()) << ".\n";
              
              break;
            }
          }
          else {
            os << "'Default' branch taken. ";
            ExecutionContinues(os, SMgr, N, getStateManager().getCodeDecl());
          }
          
          PD.push_front(new PathDiagnosticPiece(L, os.str(),
                                             PathDiagnosticPiece::ControlFlow));
          break;
        }
          
        case Stmt::BreakStmtClass:
        case Stmt::ContinueStmtClass: {
          std::string sbuf;
          llvm::raw_string_ostream os(sbuf);
          ExecutionContinues(os, SMgr, N, getStateManager().getCodeDecl());
          PD.push_front(new PathDiagnosticPiece(L, os.str(),
                                            PathDiagnosticPiece::ControlFlow));
          break;
        }

        case Stmt::ConditionalOperatorClass: {
          std::string sbuf;
          llvm::raw_string_ostream os(sbuf);
          os << "'?' condition evaluates to ";

          if (*(Src->succ_begin()+1) == Dst)
            os << "false.";
          else
            os << "true.";
          
          PD.push_front(new PathDiagnosticPiece(L, os.str(),
                                            PathDiagnosticPiece::ControlFlow));          
          break;
        }
          
        case Stmt::DoStmtClass:  {
          
          if (*(Src->succ_begin()) == Dst) {
            std::string sbuf;
            llvm::raw_string_ostream os(sbuf);
            
            os << "Loop condition is true. ";
            ExecutionContinues(os, SMgr, N, getStateManager().getCodeDecl());
            
            PD.push_front(new PathDiagnosticPiece(L, os.str(),
                                             PathDiagnosticPiece::ControlFlow));
          }
          else
            PD.push_front(new PathDiagnosticPiece(L,
                              "Loop condition is false.  Exiting loop.",
                              PathDiagnosticPiece::ControlFlow));
          
          break;
        }
          
        case Stmt::WhileStmtClass:
        case Stmt::ForStmtClass: {
          
          if (*(Src->succ_begin()+1) == Dst) {
            std::string sbuf;
            llvm::raw_string_ostream os(sbuf);

            os << "Loop condition is false. ";
            ExecutionContinues(os, SMgr, N, getStateManager().getCodeDecl());

            PD.push_front(new PathDiagnosticPiece(L, os.str(),
                                             PathDiagnosticPiece::ControlFlow));
          }
          else
            PD.push_front(new PathDiagnosticPiece(L,
                            "Loop condition is true.  Entering loop body.",
                            PathDiagnosticPiece::ControlFlow));
          
          break;
        }
          
        case Stmt::IfStmtClass: {          
          if (*(Src->succ_begin()+1) == Dst)
            PD.push_front(new PathDiagnosticPiece(L, "Taking false branch.",
                            PathDiagnosticPiece::ControlFlow));
          else  
            PD.push_front(new PathDiagnosticPiece(L, "Taking true branch.",
                            PathDiagnosticPiece::ControlFlow));
          
          break;
        }
      }
    }

    if (PathDiagnosticPiece* p = R->VisitNode(N, NextNode, *ReportGraph, *this,
                                              NMC))
      PD.push_front(p);
    
    if (const PostStmt* PS = dyn_cast<PostStmt>(&P)) {      
      // Scan the region bindings, and see if a "notable" symbol has a new
      // lval binding.
      ScanNotableSymbols SNS(N, PS->getStmt(), *this, PD);
      getStateManager().iterBindings(N->getState(), SNS);
    }
  }
}


void BugReporter::Register(BugType *BT) {
  BugTypes = F.Add(BugTypes, BT);
}

void BugReporter::EmitReport(BugReport* R) {  
  // Compute the bug report's hash to determine its equivalence class.
  llvm::FoldingSetNodeID ID;
  R->Profile(ID);
  
  // Lookup the equivance class.  If there isn't one, create it.  
  BugType& BT = R->getBugType();
  Register(&BT);
  void *InsertPos;
  BugReportEquivClass* EQ = BT.EQClasses.FindNodeOrInsertPos(ID, InsertPos);  
  
  if (!EQ) {
    EQ = new BugReportEquivClass(R);
    BT.EQClasses.InsertNode(EQ, InsertPos);
  }
  else
    EQ->AddReport(R);
}

void BugReporter::FlushReport(BugReportEquivClass& EQ) {
  assert(!EQ.Reports.empty());
  BugReport &R = **EQ.begin();
  
  // FIXME: Make sure we use the 'R' for the path that was actually used.
  // Probably doesn't make a difference in practice.  
  BugType& BT = R.getBugType();
  
  llvm::OwningPtr<PathDiagnostic> D(new PathDiagnostic(R.getBugType().getName(),
                                                R.getDescription(),
                                                BT.getCategory()));
  GeneratePathDiagnostic(*D.get(), EQ);
  
  // Get the meta data.
  std::pair<const char**, const char**> Meta = R.getExtraDescriptiveText();
  for (const char** s = Meta.first; s != Meta.second; ++s) D->addMeta(*s);

  // Emit a summary diagnostic to the regular Diagnostics engine.
  PathDiagnosticClient* PD = getPathDiagnosticClient();
  const SourceRange *Beg = 0, *End = 0;
  R.getRanges(*this, Beg, End);    
  Diagnostic& Diag = getDiagnostic();
  FullSourceLoc L(R.getLocation(), getSourceManager());  
  unsigned ErrorDiag = Diag.getCustomDiagID(Diagnostic::Warning,
                                            R.getDescription().c_str());

  switch (End-Beg) {
    default: assert(0 && "Don't handle this many ranges yet!");
    case 0: Diag.Report(L, ErrorDiag); break;
    case 1: Diag.Report(L, ErrorDiag) << Beg[0]; break;
    case 2: Diag.Report(L, ErrorDiag) << Beg[0] << Beg[1]; break;
    case 3: Diag.Report(L, ErrorDiag) << Beg[0] << Beg[1] << Beg[2]; break;
  }

  // Emit a full diagnostic for the path if we have a PathDiagnosticClient.
  if (!PD)
    return;
  
  if (D->empty()) { 
    PathDiagnosticPiece* piece = new PathDiagnosticPiece(L, R.getDescription());
    for ( ; Beg != End; ++Beg) piece->addRange(*Beg);
    D->push_back(piece);
  }
  
  PD->HandlePathDiagnostic(D.take());
}

void BugReporter::EmitBasicReport(const char* name, const char* str,
                                  SourceLocation Loc,
                                  SourceRange* RBeg, unsigned NumRanges) {
  EmitBasicReport(name, "", str, Loc, RBeg, NumRanges);
}

void BugReporter::EmitBasicReport(const char* name, const char* category,
                                  const char* str, SourceLocation Loc,
                                  SourceRange* RBeg, unsigned NumRanges) {
  
  // 'BT' will be owned by BugReporter as soon as we call 'EmitReport'.
  BugType *BT = new BugType(name, category);
  FullSourceLoc L = getContext().getFullLoc(Loc);
  RangedBugReport *R = new DiagBugReport(*BT, str, L);
  for ( ; NumRanges > 0 ; --NumRanges, ++RBeg) R->addRange(*RBeg);
  EmitReport(R);
}
