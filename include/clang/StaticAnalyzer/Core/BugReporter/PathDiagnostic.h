//===--- PathDiagnostic.h - Path-Specific Diagnostic Handling ---*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the PathDiagnostic-related interfaces.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_PATH_DIAGNOSTIC_H
#define LLVM_CLANG_PATH_DIAGNOSTIC_H

#include "clang/Basic/SourceLocation.h"
#include "clang/Analysis/ProgramPoint.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/ADT/PointerUnion.h"
#include <deque>
#include <iterator>
#include <string>
#include <vector>

namespace clang {

class AnalysisDeclContext;
class BinaryOperator;
class CompoundStmt;
class Decl;
class LocationContext;
class MemberExpr;
class ParentMap;
class ProgramPoint;
class SourceManager;
class Stmt;

namespace ento {

class ExplodedNode;

//===----------------------------------------------------------------------===//
// High-level interface for handlers of path-sensitive diagnostics.
//===----------------------------------------------------------------------===//

class PathDiagnostic;

class PathDiagnosticConsumer {
  virtual void anchor();
public:
  PathDiagnosticConsumer() : flushed(false) {}
  virtual ~PathDiagnosticConsumer();

  void FlushDiagnostics(SmallVectorImpl<std::string> *FilesMade);

  virtual void FlushDiagnosticsImpl(std::vector<const PathDiagnostic *> &Diags,
                                    SmallVectorImpl<std::string> *FilesMade)
                                    = 0;

  virtual StringRef getName() const = 0;
  
  void HandlePathDiagnostic(PathDiagnostic *D);

  enum PathGenerationScheme { Minimal, Extensive };
  virtual PathGenerationScheme getGenerationScheme() const { return Minimal; }
  virtual bool supportsLogicalOpControlFlow() const { return false; }
  virtual bool supportsAllBlockEdges() const { return false; }
  virtual bool useVerboseDescription() const { return true; }

protected:
  bool flushed;
  llvm::FoldingSet<PathDiagnostic> Diags;
};

//===----------------------------------------------------------------------===//
// Path-sensitive diagnostics.
//===----------------------------------------------------------------------===//

class PathDiagnosticRange : public SourceRange {
public:
  bool isPoint;

  PathDiagnosticRange(const SourceRange &R, bool isP = false)
    : SourceRange(R), isPoint(isP) {}

  PathDiagnosticRange() : isPoint(false) {}
};

typedef llvm::PointerUnion<const LocationContext*, AnalysisDeclContext*>
                                                   LocationOrAnalysisDeclContext;

class PathDiagnosticLocation {
private:
  enum Kind { RangeK, SingleLocK, StmtK, DeclK } K;
  const Stmt *S;
  const Decl *D;
  const SourceManager *SM;
  FullSourceLoc Loc;
  PathDiagnosticRange Range;

  PathDiagnosticLocation(SourceLocation L, const SourceManager &sm,
                         Kind kind)
    : K(kind), S(0), D(0), SM(&sm),
      Loc(genLocation(L)), Range(genRange()) {
    assert(Loc.isValid());
    assert(Range.isValid());
  }

  FullSourceLoc
    genLocation(SourceLocation L = SourceLocation(),
                LocationOrAnalysisDeclContext LAC = (AnalysisDeclContext*)0) const;

  PathDiagnosticRange
    genRange(LocationOrAnalysisDeclContext LAC = (AnalysisDeclContext*)0) const;

public:
  /// Create an invalid location.
  PathDiagnosticLocation()
    : K(SingleLocK), S(0), D(0), SM(0) {}

  /// Create a location corresponding to the given statement.
  PathDiagnosticLocation(const Stmt *s,
                         const SourceManager &sm,
                         LocationOrAnalysisDeclContext lac)
    : K(StmtK), S(s), D(0), SM(&sm),
      Loc(genLocation(SourceLocation(), lac)),
      Range(genRange(lac)) {
    assert(S);
    assert(Loc.isValid());
    assert(Range.isValid());
  }

  /// Create a location corresponding to the given declaration.
  PathDiagnosticLocation(const Decl *d, const SourceManager &sm)
    : K(DeclK), S(0), D(d), SM(&sm),
      Loc(genLocation()), Range(genRange()) {
    assert(D);
    assert(Loc.isValid());
    assert(Range.isValid());
  }

  /// Create a location corresponding to the given declaration.
  static PathDiagnosticLocation create(const Decl *D,
                                       const SourceManager &SM) {
    return PathDiagnosticLocation(D, SM);
  }

  /// Create a location for the beginning of the declaration.
  static PathDiagnosticLocation createBegin(const Decl *D,
                                            const SourceManager &SM);

  /// Create a location for the beginning of the statement.
  static PathDiagnosticLocation createBegin(const Stmt *S,
                                            const SourceManager &SM,
                                            const LocationOrAnalysisDeclContext LAC);

  /// Create the location for the operator of the binary expression.
  /// Assumes the statement has a valid location.
  static PathDiagnosticLocation createOperatorLoc(const BinaryOperator *BO,
                                                  const SourceManager &SM);

  /// For member expressions, return the location of the '.' or '->'.
  /// Assumes the statement has a valid location.
  static PathDiagnosticLocation createMemberLoc(const MemberExpr *ME,
                                                const SourceManager &SM);

  /// Create a location for the beginning of the compound statement.
  /// Assumes the statement has a valid location.
  static PathDiagnosticLocation createBeginBrace(const CompoundStmt *CS,
                                                 const SourceManager &SM);

  /// Create a location for the end of the compound statement.
  /// Assumes the statement has a valid location.
  static PathDiagnosticLocation createEndBrace(const CompoundStmt *CS,
                                               const SourceManager &SM);

  /// Create a location for the beginning of the enclosing declaration body.
  /// Defaults to the beginning of the first statement in the declaration body.
  static PathDiagnosticLocation createDeclBegin(const LocationContext *LC,
                                                const SourceManager &SM);

  /// Constructs a location for the end of the enclosing declaration body.
  /// Defaults to the end of brace.
  static PathDiagnosticLocation createDeclEnd(const LocationContext *LC,
                                                   const SourceManager &SM);

  /// Create a location corresponding to the given valid ExplodedNode.
  static PathDiagnosticLocation create(const ProgramPoint& P,
                                       const SourceManager &SMng);

  /// Create a location corresponding to the next valid ExplodedNode as end
  /// of path location.
  static PathDiagnosticLocation createEndOfPath(const ExplodedNode* N,
                                                const SourceManager &SM);

  /// Convert the given location into a single kind location.
  static PathDiagnosticLocation createSingleLocation(
                                             const PathDiagnosticLocation &PDL);

  bool operator==(const PathDiagnosticLocation &X) const {
    return K == X.K && Loc == X.Loc && Range == X.Range;
  }

  bool operator!=(const PathDiagnosticLocation &X) const {
    return !(*this == X);
  }

  bool isValid() const {
    return SM != 0;
  }

  FullSourceLoc asLocation() const {
    return Loc;
  }

  PathDiagnosticRange asRange() const {
    return Range;
  }

  const Stmt *asStmt() const { assert(isValid()); return S; }
  const Decl *asDecl() const { assert(isValid()); return D; }

  bool hasRange() const { return K == StmtK || K == RangeK || K == DeclK; }

  void invalidate() {
    *this = PathDiagnosticLocation();
  }

  void flatten();

  const SourceManager& getManager() const { assert(isValid()); return *SM; }
  
  void Profile(llvm::FoldingSetNodeID &ID) const;
};

class PathDiagnosticLocationPair {
private:
  PathDiagnosticLocation Start, End;
public:
  PathDiagnosticLocationPair(const PathDiagnosticLocation &start,
                             const PathDiagnosticLocation &end)
    : Start(start), End(end) {}

  const PathDiagnosticLocation &getStart() const { return Start; }
  const PathDiagnosticLocation &getEnd() const { return End; }

  void flatten() {
    Start.flatten();
    End.flatten();
  }
  
  void Profile(llvm::FoldingSetNodeID &ID) const {
    Start.Profile(ID);
    End.Profile(ID);
  }
};

//===----------------------------------------------------------------------===//
// Path "pieces" for path-sensitive diagnostics.
//===----------------------------------------------------------------------===//

class PathDiagnosticPiece : public RefCountedBaseVPTR {
public:
  enum Kind { ControlFlow, Event, Macro, Call };
  enum DisplayHint { Above, Below };

private:
  const std::string str;
  const Kind kind;
  const DisplayHint Hint;
  std::vector<SourceRange> ranges;

  // Do not implement:
  PathDiagnosticPiece();
  PathDiagnosticPiece(const PathDiagnosticPiece &P);
  PathDiagnosticPiece& operator=(const PathDiagnosticPiece &P);

protected:
  PathDiagnosticPiece(StringRef s, Kind k, DisplayHint hint = Below);

  PathDiagnosticPiece(Kind k, DisplayHint hint = Below);

public:
  virtual ~PathDiagnosticPiece();

  const std::string& getString() const { return str; }

  /// getDisplayHint - Return a hint indicating where the diagnostic should
  ///  be displayed by the PathDiagnosticConsumer.
  DisplayHint getDisplayHint() const { return Hint; }

  virtual PathDiagnosticLocation getLocation() const = 0;
  virtual void flattenLocations() = 0;

  Kind getKind() const { return kind; }

  void addRange(SourceRange R) {
    if (!R.isValid())
      return;
    ranges.push_back(R);
  }

  void addRange(SourceLocation B, SourceLocation E) {
    if (!B.isValid() || !E.isValid())
      return;
    ranges.push_back(SourceRange(B,E));
  }

  typedef const SourceRange* range_iterator;

  range_iterator ranges_begin() const {
    return ranges.empty() ? NULL : &ranges[0];
  }

  range_iterator ranges_end() const {
    return ranges_begin() + ranges.size();
  }

  static inline bool classof(const PathDiagnosticPiece *P) {
    return true;
  }
  
  virtual void Profile(llvm::FoldingSetNodeID &ID) const;
};
  
  
class PathPieces :
  public std::deque<IntrusiveRefCntPtr<PathDiagnosticPiece> > {
public:
  ~PathPieces();  
};

class PathDiagnosticSpotPiece : public PathDiagnosticPiece {
private:
  PathDiagnosticLocation Pos;
public:
  PathDiagnosticSpotPiece(const PathDiagnosticLocation &pos,
                          StringRef s,
                          PathDiagnosticPiece::Kind k,
                          bool addPosRange = true)
  : PathDiagnosticPiece(s, k), Pos(pos) {
    assert(Pos.isValid() && Pos.asLocation().isValid() &&
           "PathDiagnosticSpotPiece's must have a valid location.");
    if (addPosRange && Pos.hasRange()) addRange(Pos.asRange());
  }

  PathDiagnosticLocation getLocation() const { return Pos; }
  virtual void flattenLocations() { Pos.flatten(); }
  
  virtual void Profile(llvm::FoldingSetNodeID &ID) const;
};

class PathDiagnosticEventPiece : public PathDiagnosticSpotPiece {

public:
  PathDiagnosticEventPiece(const PathDiagnosticLocation &pos,
                           StringRef s, bool addPosRange = true)
    : PathDiagnosticSpotPiece(pos, s, Event, addPosRange) {}

  ~PathDiagnosticEventPiece();

  static inline bool classof(const PathDiagnosticPiece *P) {
    return P->getKind() == Event;
  }
};

class PathDiagnosticCallPiece : public PathDiagnosticPiece {
  PathDiagnosticCallPiece(const Decl *callerD,
                          const PathDiagnosticLocation &callReturnPos)
    : PathDiagnosticPiece(Call), Caller(callerD),
      Callee(0), callReturn(callReturnPos) {}

  PathDiagnosticCallPiece(PathPieces &oldPath)
    : PathDiagnosticPiece(Call), Caller(0), Callee(0), path(oldPath) {}
  
  const Decl *Caller;
  const Decl *Callee;
public:
  PathDiagnosticLocation callEnter;
  PathDiagnosticLocation callReturn;  
  PathPieces path;
  
  virtual ~PathDiagnosticCallPiece();
  
  const Decl *getCaller() const { return Caller; }
  
  const Decl *getCallee() const { return Callee; }
  void setCallee(const CallEnter &CE, const SourceManager &SM);
  
  virtual PathDiagnosticLocation getLocation() const {
    return callEnter;
  }
  
  IntrusiveRefCntPtr<PathDiagnosticEventPiece> getCallEnterEvent() const;
  IntrusiveRefCntPtr<PathDiagnosticEventPiece> getCallExitEvent() const;

  virtual void flattenLocations() {
    callEnter.flatten();
    callReturn.flatten();
    for (PathPieces::iterator I = path.begin(), 
         E = path.end(); I != E; ++I) (*I)->flattenLocations();
  }
  
  static PathDiagnosticCallPiece *construct(const ExplodedNode *N,
                                            const CallExit &CE,
                                            const SourceManager &SM);
  
  static PathDiagnosticCallPiece *construct(PathPieces &pieces);
  
  virtual void Profile(llvm::FoldingSetNodeID &ID) const;

  static inline bool classof(const PathDiagnosticPiece *P) {
    return P->getKind() == Call;
  }
};

class PathDiagnosticControlFlowPiece : public PathDiagnosticPiece {
  std::vector<PathDiagnosticLocationPair> LPairs;
public:
  PathDiagnosticControlFlowPiece(const PathDiagnosticLocation &startPos,
                                 const PathDiagnosticLocation &endPos,
                                 StringRef s)
    : PathDiagnosticPiece(s, ControlFlow) {
      LPairs.push_back(PathDiagnosticLocationPair(startPos, endPos));
    }

  PathDiagnosticControlFlowPiece(const PathDiagnosticLocation &startPos,
                                 const PathDiagnosticLocation &endPos)
    : PathDiagnosticPiece(ControlFlow) {
      LPairs.push_back(PathDiagnosticLocationPair(startPos, endPos));
    }

  ~PathDiagnosticControlFlowPiece();

  PathDiagnosticLocation getStartLocation() const {
    assert(!LPairs.empty() &&
           "PathDiagnosticControlFlowPiece needs at least one location.");
    return LPairs[0].getStart();
  }

  PathDiagnosticLocation getEndLocation() const {
    assert(!LPairs.empty() &&
           "PathDiagnosticControlFlowPiece needs at least one location.");
    return LPairs[0].getEnd();
  }

  void push_back(const PathDiagnosticLocationPair &X) { LPairs.push_back(X); }

  virtual PathDiagnosticLocation getLocation() const {
    return getStartLocation();
  }

  typedef std::vector<PathDiagnosticLocationPair>::iterator iterator;
  iterator begin() { return LPairs.begin(); }
  iterator end()   { return LPairs.end(); }

  virtual void flattenLocations() {
    for (iterator I=begin(), E=end(); I!=E; ++I) I->flatten();
  }

  typedef std::vector<PathDiagnosticLocationPair>::const_iterator
          const_iterator;
  const_iterator begin() const { return LPairs.begin(); }
  const_iterator end() const   { return LPairs.end(); }

  static inline bool classof(const PathDiagnosticPiece *P) {
    return P->getKind() == ControlFlow;
  }
  
  virtual void Profile(llvm::FoldingSetNodeID &ID) const;
};

class PathDiagnosticMacroPiece : public PathDiagnosticSpotPiece {
public:
  PathDiagnosticMacroPiece(const PathDiagnosticLocation &pos)
    : PathDiagnosticSpotPiece(pos, "", Macro) {}

  ~PathDiagnosticMacroPiece();

  PathPieces subPieces;
  
  bool containsEvent() const;

  virtual void flattenLocations() {
    PathDiagnosticSpotPiece::flattenLocations();
    for (PathPieces::iterator I = subPieces.begin(), 
         E = subPieces.end(); I != E; ++I) (*I)->flattenLocations();
  }

  static inline bool classof(const PathDiagnosticPiece *P) {
    return P->getKind() == Macro;
  }
  
  virtual void Profile(llvm::FoldingSetNodeID &ID) const;
};

/// PathDiagnostic - PathDiagnostic objects represent a single path-sensitive
///  diagnostic.  It represents an ordered-collection of PathDiagnosticPieces,
///  each which represent the pieces of the path.
class PathDiagnostic : public llvm::FoldingSetNode {
  std::string BugType;
  std::string Desc;
  std::string Category;
  std::deque<std::string> OtherDesc;
  PathPieces pathImpl;
  llvm::SmallVector<PathPieces *, 3> pathStack;
public:
  const PathPieces &path;

  /// Return the path currently used by builders for constructing the 
  /// PathDiagnostic.
  PathPieces &getActivePath() {
    if (pathStack.empty())
      return pathImpl;
    return *pathStack.back();
  }
  
  /// Return a mutable version of 'path'.
  PathPieces &getMutablePieces() {
    return pathImpl;
  }
    
  /// Return the unrolled size of the path.
  unsigned full_size();

  void pushActivePath(PathPieces *p) { pathStack.push_back(p); }
  void popActivePath() { if (!pathStack.empty()) pathStack.pop_back(); }

  PathDiagnostic();
  PathDiagnostic(StringRef bugtype, StringRef desc,
                 StringRef category);

  ~PathDiagnostic();

  StringRef getDescription() const { return Desc; }
  StringRef getBugType() const { return BugType; }
  StringRef getCategory() const { return Category; }
  

  typedef std::deque<std::string>::const_iterator meta_iterator;
  meta_iterator meta_begin() const { return OtherDesc.begin(); }
  meta_iterator meta_end() const { return OtherDesc.end(); }
  void addMeta(StringRef s) { OtherDesc.push_back(s); }

  PathDiagnosticLocation getLocation() const;

  void flattenLocations() {
    for (PathPieces::iterator I = pathImpl.begin(), E = pathImpl.end(); 
         I != E; ++I) (*I)->flattenLocations();
  }
  
  void Profile(llvm::FoldingSetNodeID &ID) const;
  
  void FullProfile(llvm::FoldingSetNodeID &ID) const;
};  

} // end GR namespace

} //end clang namespace

#endif
