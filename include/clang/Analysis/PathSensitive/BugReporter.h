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
#include <vector>

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
class BugReport;
  
class BugType {
public:
  BugType() {}
  virtual ~BugType();
  
  virtual const char* getName() const = 0;
  virtual const char* getDescription() const { return getName(); }
  
  virtual std::pair<const char**,const char**> getExtraDescriptiveText() {
    return std::pair<const char**, const char**>(0, 0);
  }
      
  virtual void EmitWarnings(BugReporter& BR) {}
  virtual void GetErrorNodes(std::vector<ExplodedNode<ValueState>*>& Nodes) {}
  
  virtual bool isCached(BugReport& R) = 0;
};
  
class BugTypeCacheLocation : public BugType {
  llvm::SmallPtrSet<void*,10> CachedErrors;
public:
  BugTypeCacheLocation() {}
  virtual ~BugTypeCacheLocation() {}  
  virtual bool isCached(BugReport& R);
};
  
  
class BugReport {
  BugType& Desc;
  ExplodedNode<ValueState> *N;
  
public:
  BugReport(BugType& D, ExplodedNode<ValueState> *n) : Desc(D), N(n) {}
  virtual ~BugReport();
  
  const BugType& getBugType() const { return Desc; }
  BugType& getBugType() { return Desc; }
  
  ExplodedNode<ValueState>* getEndNode() const { return N; }
  
  Stmt* getStmt() const;
    
  const char* getName() const { return getBugType().getName(); }

  virtual const char* getDescription() const {
    return getBugType().getDescription();
  }
  
  virtual std::pair<const char**,const char**> getExtraDescriptiveText() {
    return getBugType().getExtraDescriptiveText();
  }
  
  virtual PathDiagnosticPiece* getEndPath(BugReporter& BR,
                                          ExplodedNode<ValueState>* N) const;
  
  virtual FullSourceLoc getLocation(SourceManager& Mgr);
  
  virtual void getRanges(const SourceRange*& beg,
                         const SourceRange*& end) const;
  
  virtual PathDiagnosticPiece* VisitNode(ExplodedNode<ValueState>* N,
                                         ExplodedNode<ValueState>* PrevN,
                                         ExplodedGraph<ValueState>& G,
                                         BugReporter& BR);
};
  
class RangedBugReport : public BugReport {
  std::vector<SourceRange> Ranges;
public:
  RangedBugReport(BugType& D, ExplodedNode<ValueState> *n)
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
  
  void EmitWarning(BugReport& R);
  
  void GeneratePathDiagnostic(PathDiagnostic& PD, BugReport& R);
};
  
} // end clang namespace

#endif
