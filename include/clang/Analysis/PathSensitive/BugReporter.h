// BugReporter.h - Generate PathDiagnostics  ----------*- C++ -*--//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines BugReporter, a utility class for generating
//  PathDiagnostics for analyses based on ValueState.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_ANALYSIS_BUGREPORTER
#define LLVM_CLANG_ANALYSIS_BUGREPORTER

#include "clang/Basic/SourceLocation.h"
#include "clang/Analysis/PathSensitive/ValueState.h"
#include "clang/Analysis/PathSensitive/ExplodedGraph.h"
#include "llvm/ADT/SmallPtrSet.h"

namespace clang {
  
class PathDiagnostic;
class PathDiagnosticPiece;
class PathDiagnosticClient;
class ASTContext;
class Diagnostic;
class BugReporter;
class GRExprEngine;
class ValueState;
class Stmt;
  
class BugType {
public:
  BugType() {}
  virtual ~BugType();
  
  virtual const char* getName() const = 0;
  virtual const char* getDescription() const { return getName(); }
      
  virtual void EmitWarnings(BugReporter& BR) {}
};
  
class BugReport {
  const BugType& Desc;
  ExplodedNode<ValueState> *N;
  
public:
  BugReport(const BugType& D, ExplodedNode<ValueState> *n) : Desc(D), N(n) {}
  virtual ~BugReport();
  
  const BugType& getBugType() const { return Desc; }
  
  ExplodedNode<ValueState>* getEndNode() const { return N; }
  
  Stmt* getStmt() const;
    
  const char* getName() const { return getBugType().getName(); }

  virtual const char* getDescription() const {
    return getBugType().getDescription();
  }
  
  virtual PathDiagnosticPiece* getEndPath(BugReporter& BR,
                                          ExplodedNode<ValueState>* N) const;
  
  virtual FullSourceLoc getLocation(SourceManager& Mgr);
  
  virtual void getRanges(const SourceRange*& beg,
                         const SourceRange*& end) const;
  
  virtual PathDiagnosticPiece* VisitNode(ExplodedNode<ValueState>* N,
                                         ExplodedNode<ValueState>* PrevN,
                                         ExplodedGraph<ValueState>& G,
                                         ASTContext& Ctx);
};
  
class RangedBugReport : public BugReport {
  std::vector<SourceRange> Ranges;
public:
  RangedBugReport(const BugType& D, ExplodedNode<ValueState> *n)
    : BugReport(D, n) {}
  
  virtual ~RangedBugReport();
  
  void addRange(SourceRange R) { Ranges.push_back(R); }
  
  virtual void getRanges(const SourceRange*& beg,           
                         const SourceRange*& end) const {
    
    if (Ranges.empty()) {
      beg = NULL;
      end = NULL;
    }
    else {
      beg = &Ranges[0];
      end = beg + Ranges.size();
    }
  }
};
  
class BugReporter {
  llvm::SmallPtrSet<void*,10> CachedErrors;
  Diagnostic& Diag;
  PathDiagnosticClient* PD;
  ASTContext& Ctx;
  GRExprEngine& Eng;
  
public:
  BugReporter(Diagnostic& diag, PathDiagnosticClient* pd,
              ASTContext& ctx, GRExprEngine& eng)
  : Diag(diag), PD(pd), Ctx(ctx), Eng(eng) {}
  
  ~BugReporter();
  
  Diagnostic& getDiagnostic() { return Diag; }
  
  PathDiagnosticClient* getDiagnosticClient() { return PD; }
  
  ASTContext& getContext() { return Ctx; }
  
  ExplodedGraph<ValueState>& getGraph();

  GRExprEngine& getEngine() { return Eng; }
  
  CFG& getCFG() { return getGraph().getCFG(); }
  
  void EmitPathWarning(BugReport& R);
  
  void EmitWarning(BugReport& R);
  
  void clearCache() { CachedErrors.clear(); }
  
  bool IsCached(ExplodedNode<ValueState>* N);
  
  void GeneratePathDiagnostic(PathDiagnostic& PD, BugReport& R);
};
  
} // end clang namespace

#endif
