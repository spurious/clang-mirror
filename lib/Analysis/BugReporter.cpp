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


PathDiagnosticPiece*
BugDescription::getEndPath(ASTContext& Ctx,
                           ExplodedNode<ValueState> *N) const {
  
  Stmt* S = GetStmt(N->getLocation());
  
  if (!S)
    return NULL;
  
  FullSourceLoc L(S->getLocStart(), Ctx.getSourceManager());  
  PathDiagnosticPiece* P = new PathDiagnosticPiece(L, getDescription());
  
  if (Expr* E = dyn_cast<Expr>(S))
    P->addRange(E->getSourceRange());
  
  return P;
}

void BugReporter::GeneratePathDiagnostic(PathDiagnostic& PD, ASTContext& Ctx,
                                         const BugDescription& B,
                                         ExplodedGraph<GRExprEngine>& G,
                                         ExplodedNode<ValueState>* N) {
  PD.push_back(B.getEndPath(Ctx, N));
  
  SourceManager& SMgr = Ctx.getSourceManager();
  
  llvm::OwningPtr<ExplodedGraph<GRExprEngine> > GTrim(G.Trim(&N, &N+1));
  
  // Find the sink in the trimmed graph.
  // FIXME: Should we eventually have a sink iterator?
  
  ExplodedNode<ValueState>* NewN = 0;
  
  for (ExplodedGraph<GRExprEngine>::node_iterator
        I = GTrim->nodes_begin(), E = GTrim->nodes_end(); I != E; ++I) {
    
    if (I->isSink()) {
      NewN = &*I;
      break;
    }    
  }
  
  assert (NewN);
  assert (NewN->getLocation() == N->getLocation());
  
  N = NewN;

  while (!N->pred_empty()) {
    
    ExplodedNode<ValueState>* LastNode = N;
    N = *(N->pred_begin());
    
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
          
          
        case Stmt::DoStmtClass:
        case Stmt::WhileStmtClass:
        case Stmt::ForStmtClass:
        case Stmt::IfStmtClass: {
          
          if (*(Src->succ_begin()+1) == Dst)
            PD.push_front(new PathDiagnosticPiece(L, "Taking false branch."));
          else 
            PD.push_front(new PathDiagnosticPiece(L, "Taking true branch."));
          
          break;
        }
      }
    }  
  }
}

bool BugReporter::IsCached(ExplodedNode<ValueState>* N) {
  
  // HACK: Cache the location of the error.  Don't emit the same
  // warning for the same error type that occurs at the same program
  // location but along a different path.
  
  void* p = N->getLocation().getRawData();
  
  if (CachedErrors.count(p))
    return true;
  
  CachedErrors.insert(p);
  
  return false;
}

void BugReporter::EmitPathWarning(Diagnostic& Diag,
                                  PathDiagnosticClient* PDC,
                                  ASTContext& Ctx,
                                  const BugDescription& B,
                                  ExplodedGraph<GRExprEngine>& G,
                                  ExplodedNode<ValueState>* N) {
  
  if (!PDC) {
    EmitWarning(Diag, Ctx, B, N);
    return;
  }
  
  if (IsCached(N))
    return;
  
  PathDiagnostic D(B.getName());  
  GeneratePathDiagnostic(D, Ctx, B, G, N);
  PDC->HandlePathDiagnostic(D);
}


void BugReporter::EmitWarning(Diagnostic& Diag, ASTContext& Ctx,
                              const BugDescription& B,
                              ExplodedNode<ValueState>* N) {  
  if (IsCached(N))
    return;
  
  std::ostringstream os;
  os << "[CHECKER] " << B.getName();
  
  unsigned ErrorDiag = Diag.getCustomDiagID(Diagnostic::Warning,
                                            os.str().c_str());

  // FIXME: Add support for multiple ranges.
  
  Stmt* S = GetStmt(N->getLocation());
  
  if (!S)
    return;
  
  SourceRange R = S->getSourceRange();
  
  Diag.Report(FullSourceLoc(S->getLocStart(), Ctx.getSourceManager()),
              ErrorDiag, NULL, 0, &R, 1);   
}
