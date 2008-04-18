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
#include <sstream>

using namespace clang;

BugReporter::~BugReporter() {}
BugType::~BugType() {}
BugReport::~BugReport() {}
RangedBugReport::~RangedBugReport() {}

ExplodedGraph<ValueState>& BugReporter::getGraph() { return Eng.getGraph(); }

static inline Stmt* GetStmt(const ProgramPoint& P) {
  if (const PostStmt* PS = dyn_cast<PostStmt>(&P)) {
    return PS->getStmt();
  }
  else if (const BlockEdge* BE = dyn_cast<BlockEdge>(&P)) {
    return BE->getSrc()->getTerminator();
  }
  else if (const BlockEntrance* BE = dyn_cast<BlockEntrance>(&P)) {
    return BE->getFirstStmt();
  }
  
  assert (false && "Unsupported ProgramPoint.");
  return NULL;
}

static inline Stmt* GetStmt(const CFGBlock* B) {
  assert (!B->empty());
  return (*B)[0];
}

Stmt* BugReport::getStmt() const {
  return N ? GetStmt(N->getLocation()) : NULL;
}

static inline ExplodedNode<ValueState>*
GetNextNode(ExplodedNode<ValueState>* N) {
  return N->pred_empty() ? NULL : *(N->pred_begin());
}
  
  
static Stmt* GetLastStmt(ExplodedNode<ValueState>* N) {
  assert (isa<BlockEntrance>(N->getLocation()));
  
  for (N = GetNextNode(N); N; N = GetNextNode(N)) {
    
    ProgramPoint P = N->getLocation();
    
    if (PostStmt* PS = dyn_cast<PostStmt>(&P))
      return PS->getStmt();
  }
  
  return NULL;
}

PathDiagnosticPiece*
BugReport::getEndPath(BugReporter& BR,
                      ExplodedNode<ValueState>* EndPathNode) const {
  
  ProgramPoint ProgP = EndPathNode->getLocation();  
  Stmt *S = NULL;
  
  if (BlockEntrance* BE = dyn_cast<BlockEntrance>(&ProgP))
    if (BE->getBlock() == &BR.getCFG().getExit())
      S = GetLastStmt(EndPathNode);
  if (!S)
    S = GetStmt(ProgP);      
  
  if (!S)
    return NULL;
  
  FullSourceLoc L(S->getLocStart(), BR.getContext().getSourceManager());  

  PathDiagnosticPiece* P =
    new PathDiagnosticPiece(L, getDescription());
  
  const SourceRange *Beg, *End;
  getRanges(Beg, End);
  
  if (Beg == End) {
    if (Expr* E = dyn_cast<Expr>(S))
      P->addRange(E->getSourceRange());
  }
  else {
    assert (Beg < End);
    for (; Beg != End; ++Beg)
      P->addRange(*Beg);
  }
  
  return P;
}

void BugReport::getRanges(const SourceRange*& beg,
                          const SourceRange*& end) const {  
  beg = NULL;
  end = NULL;
}

FullSourceLoc BugReport::getLocation(SourceManager& Mgr) {
  
  if (!N)
    return FullSourceLoc();
  
  Stmt* S = GetStmt(N->getLocation());
  
  if (!S)
    return FullSourceLoc();
  
  return FullSourceLoc(S->getLocStart(), Mgr);
}

PathDiagnosticPiece* BugReport::VisitNode(ExplodedNode<ValueState>* N,
                                          ExplodedNode<ValueState>* PrevN,
                                          ExplodedGraph<ValueState>& G,
                                          BugReporter& BR) {
  return NULL;
}

void BugReporter::GeneratePathDiagnostic(PathDiagnostic& PD,
                                         BugReport& R) {

  ExplodedNode<ValueState>* N = R.getEndNode();

  if (!N)
    return;
  
  llvm::OwningPtr<ExplodedGraph<ValueState> > GTrim(getGraph().Trim(&N, &N+1));

  // Find the sink in the trimmed graph.
  // FIXME: Should we eventually have a sink iterator?
  
  ExplodedNode<ValueState>* NewN = 0;
  
  for (ExplodedGraph<ValueState>::node_iterator
       I = GTrim->nodes_begin(), E = GTrim->nodes_end(); I != E; ++I) {
    
    if (I->isSink()) {
      NewN = &*I;
      break;
    }    
  }
  
  assert (NewN);
  assert (NewN->getLocation() == N->getLocation());
  
  N = NewN;
    
  if (PathDiagnosticPiece* Piece = R.getEndPath(*this, N))
    PD.push_back(Piece);
  else
    return;
  
  ExplodedNode<ValueState>* NextNode = N->pred_empty() 
                                       ? NULL : *(N->pred_begin());
  
  SourceManager& SMgr = Ctx.getSourceManager();
  
  while (NextNode) {
    
    ExplodedNode<ValueState>* LastNode = N;
    N = NextNode;    
    NextNode = GetNextNode(N);
    
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
          
          Stmt* S = GetStmt(LastNode->getLocation());
          
          if (!S)
            continue;
          
          std::ostringstream os;
          
          os << "Control jumps to line "
             << SMgr.getLogicalLineNumber(S->getLocStart()) << ".\n";
          
          PD.push_front(new PathDiagnosticPiece(L, os.str()));
          break;
        }
          
        case Stmt::SwitchStmtClass: {
          
          // Figure out what case arm we took.
          
          Stmt* S = Dst->getLabel();
          
          if (!S)
            continue;
          
          std::ostringstream os;
          
          switch (S->getStmtClass()) {
            default:
              continue;
              
            case Stmt::DefaultStmtClass: {
              
              os << "Control jumps to the 'default' case at line "
                 << SMgr.getLogicalLineNumber(S->getLocStart()) << ".\n";
              
              break;
            }
              
            case Stmt::CaseStmtClass: {
              
              os << "Control jumps to 'case ";
              
              Expr* CondE = cast<SwitchStmt>(T)->getCond();
              unsigned bits = Ctx.getTypeSize(CondE->getType());
              
              llvm::APSInt V1(bits, false);
              
              CaseStmt* Case = cast<CaseStmt>(S);
              
              if (!Case->getLHS()->isIntegerConstantExpr(V1, Ctx, 0, true)) {
                assert (false &&
                        "Case condition must evaluate to an integer constant.");
                continue;
              }
              
              os << V1.toString();
              
              // Get the RHS of the case, if it exists.
              
              if (Expr* E = Case->getRHS()) {
                
                llvm::APSInt V2(bits, false);
                
                if (!E->isIntegerConstantExpr(V2, Ctx, 0, true)) {
                  assert (false &&
                  "Case condition (RHS) must evaluate to an integer constant.");
                  continue;
                }
                
                os << " .. " << V2.toString();
              }
              
              os << ":'  at line " 
                << SMgr.getLogicalLineNumber(S->getLocStart()) << ".\n";
              
              break;
              
            }
          }
          
          PD.push_front(new PathDiagnosticPiece(L, os.str()));
          break;
        }

        case Stmt::ConditionalOperatorClass: {
          
          std::ostringstream os;
          os << "'?' condition evaluates to ";

          if (*(Src->succ_begin()+1) == Dst)
            os << "false.";
          else
            os << "true.";
          
          PD.push_front(new PathDiagnosticPiece(L, os.str()));
          
          break;
        }
          
        case Stmt::DoStmtClass:  {
          
          if (*(Src->succ_begin()) == Dst) {
            
            std::ostringstream os;          
            
            os << "Loop condition is true. Execution continues on line "
               << SMgr.getLogicalLineNumber(GetStmt(Dst)->getLocStart()) << '.';
            
            PD.push_front(new PathDiagnosticPiece(L, os.str()));
          }
          else
            PD.push_front(new PathDiagnosticPiece(L,
                              "Loop condition is false.  Exiting loop."));
          
          break;
        }
          
        case Stmt::WhileStmtClass:
        case Stmt::ForStmtClass: {
          
          if (*(Src->succ_begin()+1) == Dst) {
            
            std::ostringstream os;          

            os << "Loop condition is false. Execution continues on line "
               << SMgr.getLogicalLineNumber(GetStmt(Dst)->getLocStart()) << '.';
          
            PD.push_front(new PathDiagnosticPiece(L, os.str()));
          }
          else
            PD.push_front(new PathDiagnosticPiece(L,
                            "Loop condition is true.  Entering loop body."));
          
          break;
        }
          
        case Stmt::IfStmtClass: {
          
          if (*(Src->succ_begin()+1) == Dst)
            PD.push_front(new PathDiagnosticPiece(L, "Taking false branch."));
          else 
            PD.push_front(new PathDiagnosticPiece(L, "Taking true branch."));
          
          break;
        }
      }
    }
    else
      if (PathDiagnosticPiece* piece = R.VisitNode(N, NextNode, *GTrim, *this))
        PD.push_front(piece);
  }
}

bool BugTypeCacheLocation::isCached(BugReport& R) {
  
  ExplodedNode<ValueState>* N = R.getEndNode();
  
  if (!N)
    return false;

  // Cache the location of the error.  Don't emit the same
  // warning for the same error type that occurs at the same program
  // location but along a different path.
  
  void* p = N->getLocation().getRawData();
  
  if (CachedErrors.count(p))
    return true;
  
  CachedErrors.insert(p);  
  return false;
}

void BugReporter::EmitWarning(BugReport& R) {

  if (R.getBugType().isCached(R))
    return;

  PathDiagnostic D(R.getName());  
  GeneratePathDiagnostic(D, R);

  // Emit a full diagnostic for the path if we have a PathDiagnosticClient.
  
  if (PD && !D.empty()) { 
    PD->HandlePathDiagnostic(D);
    return;    
  }
  
  // We don't have a PathDiagnosticClient, but we can still emit a single
  // line diagnostic.  Determine the location.
  
  FullSourceLoc L = D.empty() ? R.getLocation(Ctx.getSourceManager())
                               : D.back()->getLocation();
  
  
  // Determine the range.
  
  const SourceRange *Beg, *End;
  
  if (!D.empty()) {
    Beg = D.back()->ranges_begin();
    End = D.back()->ranges_end();
  }
  else  
    R.getRanges(Beg, End);

  // Compute the message.
  
  std::ostringstream os;  
  os << "[CHECKER] ";
  
  if (D.empty())
    os << R.getDescription();
  else
    os << D.back()->getString();
  
  unsigned ErrorDiag = Diag.getCustomDiagID(Diagnostic::Warning,
                                            os.str().c_str());
  
  if (PD)
    Diag.Report(PD, L, ErrorDiag, NULL, 0, Beg, End - Beg);    
  else
    Diag.Report(L, ErrorDiag, NULL, 0, Beg, End - Beg);    
}
