//== GenericTaintChecker.cpp ----------------------------------- -*- C++ -*--=//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This checker defines the attack surface for generic taint propagation.
//
// The taint information produced by it might be useful to other checkers. For
// example, checkers should report errors which involve tainted data more
// aggressively, even if the involved symbols are under constrained.
//
//===----------------------------------------------------------------------===//
#include "ClangSACheckers.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramStateTrait.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include <climits>

using namespace clang;
using namespace ento;

namespace {
class GenericTaintChecker : public Checker< check::PostStmt<CallExpr>,
                                            check::PreStmt<CallExpr> > {
public:
  static void *getTag() { static int Tag; return &Tag; }

  void checkPostStmt(const CallExpr *CE, CheckerContext &C) const;
  void checkPostStmt(const DeclRefExpr *DRE, CheckerContext &C) const;

  void checkPreStmt(const CallExpr *CE, CheckerContext &C) const;

private:
  static const unsigned ReturnValueIndex = UINT_MAX;
  static const unsigned InvalidArgIndex = UINT_MAX - 1;

  mutable llvm::OwningPtr<BugType> BT;
  void initBugType() const;

  /// \brief Catch taint related bugs. Check if tainted data is passed to a
  /// system call etc.
  bool checkPre(const CallExpr *CE, CheckerContext &C) const;

  /// \brief Add taint sources on a pre-visit.
  void addSourcesPre(const CallExpr *CE, CheckerContext &C) const;

  /// \brief Propagate taint generated at pre-visit.
  bool propagateFromPre(const CallExpr *CE, CheckerContext &C) const;

  /// \brief Add taint sources on a post visit.
  void addSourcesPost(const CallExpr *CE, CheckerContext &C) const;

  /// \brief Given a pointer argument, get the symbol of the value it contains
  /// (points to).
  SymbolRef getPointedToSymbol(CheckerContext &C,
                               const Expr *Arg,
                               bool IssueWarning = false) const;

  inline bool isTaintedOrPointsToTainted(const Expr *E,
                                         const ProgramState *State,
                                         CheckerContext &C) const {
    return (State->isTainted(E, C.getLocationContext()) ||
            (E->getType().getTypePtr()->isPointerType() &&
             State->isTainted(getPointedToSymbol(C, E))));
  }

  /// Functions defining the attack surface.
  typedef const ProgramState *(GenericTaintChecker::*FnCheck)(const CallExpr *,
                                                       CheckerContext &C) const;
  const ProgramState *postScanf(const CallExpr *CE, CheckerContext &C) const;
  const ProgramState *postRetTaint(const CallExpr *CE, CheckerContext &C) const;

  /// Taint the scanned input if the file is tainted.
  const ProgramState *preFscanf(const CallExpr *CE, CheckerContext &C) const;
  /// Taint if any of the arguments are tainted.
  const ProgramState *preAnyArgs(const CallExpr *CE, CheckerContext &C) const;
  const ProgramState *preStrcpy(const CallExpr *CE, CheckerContext &C) const;

  /// Check if the region the expression evaluates to is the standard input,
  /// and thus, is tainted.
  bool isStdin(const Expr *E, CheckerContext &C) const;

  /// Check for CWE-134: Uncontrolled Format String.
  static const char MsgUncontrolledFormatString[];
  bool checkUncontrolledFormatString(const CallExpr *CE,
                                     CheckerContext &C) const;

  /// Check for:
  /// CERT/STR02-C. "Sanitize data passed to complex subsystems"
  /// CWE-78, "Failure to Sanitize Data into an OS Command"
  static const char MsgSanitizeSystemArgs[];
  bool checkSystemCall(const CallExpr *CE, StringRef Name,
                       CheckerContext &C) const;

  /// Generate a report if the expression is tainted or points to tainted data.
  bool generateReportIfTainted(const Expr *E, const char Msg[],
                               CheckerContext &C) const;
                               
  
  typedef llvm::SmallVector<unsigned, 2> ArgVector;

  /// \brief A struct used to specify taint propagation rules for a function.
  ///
  /// If any of the possible taint source arguments is tainted, all of the
  /// destination arguments should also be tainted. Use InvalidArgIndex in the
  /// src list to specify that all of the arguments can introduce taint. Use
  /// InvalidArgIndex in the dst arguments to signify that all the non-const
  /// pointer and reference arguments might be tainted on return. If
  /// ReturnValueIndex is added to the dst list, the return value will be
  /// tainted.
  struct TaintPropagationRule {
    /// List of arguments which can be taint sources and should be checked.
    ArgVector SrcArgs;
    /// List of arguments which should be tainted on function return.
    ArgVector DstArgs;

    TaintPropagationRule() {}

    TaintPropagationRule(unsigned SArg, unsigned DArg) {
      SrcArgs.push_back(SArg);
      DstArgs.push_back(DArg);
    }

    inline void addSrcArg(unsigned A) { SrcArgs.push_back(A); }
    inline void addDstArg(unsigned A)  { DstArgs.push_back(A); }

    inline bool isNull() { return SrcArgs.empty(); }
  };   
  
  /// \brief Pre-process a function which propagates taint according to the
  /// given taint rule.
  const ProgramState *prePropagateTaint(const CallExpr *CE,
                                        CheckerContext &C,
                                        const TaintPropagationRule PR) const;

                              
};
// TODO: We probably could use TableGen here.
const char GenericTaintChecker::MsgUncontrolledFormatString[] =
  "Tainted format string (CWE-134: Uncontrolled Format String)";

const char GenericTaintChecker::MsgSanitizeSystemArgs[] =
  "Tainted data passed to a system call "
  "(CERT/STR02-C. Sanitize data passed to complex subsystems)";

}

/// A set which is used to pass information from call pre-visit instruction
/// to the call post-visit. The values are unsigned integers, which are either
/// ReturnValueIndex, or indexes of the pointer/reference argument, which
/// points to data, which should be tainted on return.
namespace { struct TaintArgsOnPostVisit{}; }
namespace clang { namespace ento {
template<> struct ProgramStateTrait<TaintArgsOnPostVisit>
    :  public ProgramStatePartialTrait<llvm::ImmutableSet<unsigned> > {
  static void *GDMIndex() { return GenericTaintChecker::getTag(); }
};
}}

inline void GenericTaintChecker::initBugType() const {
  if (!BT)
    BT.reset(new BugType("Taint Analysis", "General"));
}

void GenericTaintChecker::checkPreStmt(const CallExpr *CE,
                                       CheckerContext &C) const {
  // Check for errors first.
  if (checkPre(CE, C))
    return;

  // Add taint second.
  addSourcesPre(CE, C);
}

void GenericTaintChecker::checkPostStmt(const CallExpr *CE,
                                        CheckerContext &C) const {
  if (propagateFromPre(CE, C))
    return;
  addSourcesPost(CE, C);
}

void GenericTaintChecker::addSourcesPre(const CallExpr *CE,
                                        CheckerContext &C) const {
  // Set the evaluation function by switching on the callee name.
  StringRef Name = C.getCalleeName(CE);
  if (Name.empty())
    return;

  const ProgramState *State = 0;

  TaintPropagationRule Rule = llvm::StringSwitch<TaintPropagationRule>(Name)
    .Case("atoi", TaintPropagationRule(0, ReturnValueIndex))
    .Case("atol", TaintPropagationRule(0, ReturnValueIndex))
    .Case("atoll", TaintPropagationRule(0, ReturnValueIndex))
    .Default(TaintPropagationRule());

  if (!Rule.isNull()) {
    State = prePropagateTaint(CE, C, Rule);
    if (!State)
      return;
    C.addTransition(State);
  }

  FnCheck evalFunction = llvm::StringSwitch<FnCheck>(Name)
    .Case("fscanf", &GenericTaintChecker::preFscanf)
    .Cases("strcpy", "__builtin___strcpy_chk",
           "__inline_strcpy_chk", &GenericTaintChecker::preStrcpy)
    .Cases("stpcpy", "__builtin___stpcpy_chk", &GenericTaintChecker::preStrcpy)
    .Cases("strncpy", "__builtin___strncpy_chk", &GenericTaintChecker::preStrcpy)
    .Default(0);

  // Check and evaluate the call.
  if (evalFunction)
    State = (this->*evalFunction)(CE, C);
  if (!State)
    return;

  C.addTransition(State);
}

bool GenericTaintChecker::propagateFromPre(const CallExpr *CE,
                                           CheckerContext &C) const {
  const ProgramState *State = C.getState();

  // Depending on what was tainted at pre-visit, we determined a set of
  // arguments which should be tainted after the function returns. These are
  // stored in the state as TaintArgsOnPostVisit set.
  llvm::ImmutableSet<unsigned> TaintArgs = State->get<TaintArgsOnPostVisit>();
  for (llvm::ImmutableSet<unsigned>::iterator
         I = TaintArgs.begin(), E = TaintArgs.end(); I != E; ++I) {
    unsigned ArgNum  = *I;

    // Special handling for the tainted return value.
    if (ArgNum == ReturnValueIndex) {
      State = State->addTaint(CE, C.getLocationContext());
      continue;
    }

    // The arguments are pointer arguments. The data they are pointing at is
    // tainted after the call.
    const Expr* Arg = CE->getArg(ArgNum);
    SymbolRef Sym = getPointedToSymbol(C, Arg, true);
    if (Sym)
      State = State->addTaint(Sym);
  }

  // Clear up the taint info from the state.
  State = State->remove<TaintArgsOnPostVisit>();

  if (State != C.getState()) {
    C.addTransition(State);
    return true;
  }
  return false;
}

void GenericTaintChecker::addSourcesPost(const CallExpr *CE,
                                         CheckerContext &C) const {
  // Define the attack surface.
  // Set the evaluation function by switching on the callee name.
  StringRef Name = C.getCalleeName(CE);
  if (Name.empty())
    return;
  FnCheck evalFunction = llvm::StringSwitch<FnCheck>(Name)
    .Case("scanf", &GenericTaintChecker::postScanf)
    // TODO: Add support for vfscanf & family.
    .Case("getchar", &GenericTaintChecker::postRetTaint)
    .Case("getenv", &GenericTaintChecker::postRetTaint)
    .Case("fopen", &GenericTaintChecker::postRetTaint)
    .Case("fdopen", &GenericTaintChecker::postRetTaint)
    .Case("freopen", &GenericTaintChecker::postRetTaint)
    .Default(0);

  // If the callee isn't defined, it is not of security concern.
  // Check and evaluate the call.
  const ProgramState *State = 0;
  if (evalFunction)
    State = (this->*evalFunction)(CE, C);
  if (!State)
    return;

  C.addTransition(State);
}

bool GenericTaintChecker::checkPre(const CallExpr *CE, CheckerContext &C) const{

  if (checkUncontrolledFormatString(CE, C))
    return true;

  StringRef Name = C.getCalleeName(CE);
  if (Name.empty())
    return false;

  if (checkSystemCall(CE, Name, C))
    return true;

  return false;
}

SymbolRef GenericTaintChecker::getPointedToSymbol(CheckerContext &C,
                                                  const Expr* Arg,
                                                  bool IssueWarning) const {
  const ProgramState *State = C.getState();
  SVal AddrVal = State->getSVal(Arg->IgnoreParens(), C.getLocationContext());
  if (AddrVal.isUnknownOrUndef())
    return 0;

  Loc *AddrLoc = dyn_cast<Loc>(&AddrVal);

  if (!AddrLoc && !IssueWarning)
    return 0;

  // If the Expr is not a location, issue a warning.
  if (!AddrLoc) {
    assert(IssueWarning);
    if (ExplodedNode *N = C.generateSink(State)) {
      initBugType();
      BugReport *report = new BugReport(*BT, "Pointer argument is expected.",N);
      report->addRange(Arg->getSourceRange());
      C.EmitReport(report);
    }
    return 0;
  }

  const PointerType *ArgTy =
    dyn_cast<PointerType>(Arg->getType().getCanonicalType().getTypePtr());
  assert(ArgTy);
  SVal Val = State->getSVal(*AddrLoc, ArgTy->getPointeeType());
  return Val.getAsSymbol();
}

const ProgramState *
GenericTaintChecker::prePropagateTaint(const CallExpr *CE,
                                       CheckerContext &C,
                                       const TaintPropagationRule PR) const {
  const ProgramState *State = C.getState();

  // Check for taint in arguments.
  bool IsTainted = false;
  for (ArgVector::const_iterator I = PR.SrcArgs.begin(),
                                 E = PR.SrcArgs.end(); I != E; ++I) {
    unsigned ArgNum = *I;

    if (ArgNum == InvalidArgIndex) {
      // Check if any of the arguments is tainted.
      for (unsigned int i = 0; i < CE->getNumArgs(); ++i)
        if ((IsTainted = isTaintedOrPointsToTainted(CE->getArg(i), State, C)))
          break;
      break;
    }

    assert(ArgNum < CE->getNumArgs());
    if ((IsTainted = isTaintedOrPointsToTainted(CE->getArg(ArgNum), State, C)))
      break;
  }
  if (!IsTainted)
    return State;

  // Mark the arguments which should be tainted after the function returns.
  for (ArgVector::const_iterator I = PR.DstArgs.begin(),
                                 E = PR.DstArgs.end(); I != E; ++I) {
    unsigned ArgNum = *I;

    // Should we mark all arguments as tainted?
    if (ArgNum == InvalidArgIndex) {
      // For all pointer and references that were passed in:
      //   If they are not pointing to const data, mark data as tainted.
      //   TODO: So far we are just going one level down; ideally we'd need to
      //         recurse here.
      for (unsigned int i = 0; i < CE->getNumArgs(); ++i) {
        const Expr *Arg = CE->getArg(i);
        // Process pointer argument.
        const Type *ArgTy = Arg->getType().getTypePtr();
        QualType PType = ArgTy->getPointeeType();
        if ((!PType.isNull() && !PType.isConstQualified())
            || (ArgTy->isReferenceType() && !Arg->getType().isConstQualified()))
          State = State->add<TaintArgsOnPostVisit>(i);
      }
      continue;
    }

    // Should mark the return value?
    if (ArgNum == ReturnValueIndex) {
      State = State->add<TaintArgsOnPostVisit>(ReturnValueIndex);
      continue;
    }

    // Mark the given argument.
    assert(ArgNum < CE->getNumArgs());
    State = State->add<TaintArgsOnPostVisit>(ArgNum);
  }

  return State;
}


// If argument 0 (file descriptor) is tainted, all arguments except for arg 0
// and arg 1 should get taint.
const ProgramState *GenericTaintChecker::preFscanf(const CallExpr *CE,
                                                   CheckerContext &C) const {
  assert(CE->getNumArgs() >= 2);
  const ProgramState *State = C.getState();

  // Check is the file descriptor is tainted.
  if (State->isTainted(CE->getArg(0), C.getLocationContext()) ||
      isStdin(CE->getArg(0), C)) {
    // All arguments except for the first two should get taint.
    for (unsigned int i = 2; i < CE->getNumArgs(); ++i)
        State = State->add<TaintArgsOnPostVisit>(i);
    return State;
  }

  return 0;
}

// If any arguments are tainted, mark the return value as tainted on post-visit.
const ProgramState * GenericTaintChecker::preAnyArgs(const CallExpr *CE,
                                                     CheckerContext &C) const {
  for (unsigned int i = 0; i < CE->getNumArgs(); ++i) {
    const ProgramState *State = C.getState();
    const Expr *Arg = CE->getArg(i);
    if (State->isTainted(Arg, C.getLocationContext()) ||
        State->isTainted(getPointedToSymbol(C, Arg)))
      return State = State->add<TaintArgsOnPostVisit>(ReturnValueIndex);
  }
  return 0;
}

const ProgramState * GenericTaintChecker::preStrcpy(const CallExpr *CE,
                                                    CheckerContext &C) const {
  assert(CE->getNumArgs() >= 2);
  const Expr *FromArg = CE->getArg(1);
  const ProgramState *State = C.getState();
  if (State->isTainted(FromArg, C.getLocationContext()) ||
      State->isTainted(getPointedToSymbol(C, FromArg)))
    return State = State->add<TaintArgsOnPostVisit>(0);
  return 0;
}

const ProgramState *GenericTaintChecker::postScanf(const CallExpr *CE,
                                                   CheckerContext &C) const {
  const ProgramState *State = C.getState();
  assert(CE->getNumArgs() >= 2);
  SVal x = State->getSVal(CE->getArg(1), C.getLocationContext());
  // All arguments except for the very first one should get taint.
  for (unsigned int i = 1; i < CE->getNumArgs(); ++i) {
    // The arguments are pointer arguments. The data they are pointing at is
    // tainted after the call.
    const Expr* Arg = CE->getArg(i);
        SymbolRef Sym = getPointedToSymbol(C, Arg, true);
    if (Sym)
      State = State->addTaint(Sym);
  }
  return State;
}

const ProgramState *GenericTaintChecker::postRetTaint(const CallExpr *CE,
                                                      CheckerContext &C) const {
  return C.getState()->addTaint(CE, C.getLocationContext());
}

bool GenericTaintChecker::isStdin(const Expr *E,
                                  CheckerContext &C) const {
  const ProgramState *State = C.getState();
  SVal Val = State->getSVal(E, C.getLocationContext());

  // stdin is a pointer, so it would be a region.
  const MemRegion *MemReg = Val.getAsRegion();

  // The region should be symbolic, we do not know it's value.
  const SymbolicRegion *SymReg = dyn_cast_or_null<SymbolicRegion>(MemReg);
  if (!SymReg)
    return false;

  // Get it's symbol and find the declaration region it's pointing to.
  const SymbolRegionValue *Sm =dyn_cast<SymbolRegionValue>(SymReg->getSymbol());
  if (!Sm)
    return false;
  const DeclRegion *DeclReg = dyn_cast_or_null<DeclRegion>(Sm->getRegion());
  if (!DeclReg)
    return false;

  // This region corresponds to a declaration, find out if it's a global/extern
  // variable named stdin with the proper type.
  if (const VarDecl *D = dyn_cast_or_null<VarDecl>(DeclReg->getDecl())) {
    D = D->getCanonicalDecl();
    if ((D->getName().find("stdin") != StringRef::npos) && D->isExternC())
        if (const PointerType * PtrTy =
              dyn_cast<PointerType>(D->getType().getTypePtr()))
          if (PtrTy->getPointeeType() == C.getASTContext().getFILEType())
            return true;
  }
  return false;
}

static bool getPrintfFormatArgumentNum(const CallExpr *CE,
                                       const CheckerContext &C,
                                       unsigned int &ArgNum) {
  // Find if the function contains a format string argument.
  // Handles: fprintf, printf, sprintf, snprintf, vfprintf, vprintf, vsprintf,
  // vsnprintf, syslog, custom annotated functions.
  const FunctionDecl *FDecl = C.getCalleeDecl(CE);
  if (!FDecl)
    return false;
  for (specific_attr_iterator<FormatAttr>
         i = FDecl->specific_attr_begin<FormatAttr>(),
         e = FDecl->specific_attr_end<FormatAttr>(); i != e ; ++i) {

    const FormatAttr *Format = *i;
    ArgNum = Format->getFormatIdx() - 1;
    if ((Format->getType() == "printf") && CE->getNumArgs() > ArgNum)
      return true;
  }

  // Or if a function is named setproctitle (this is a heuristic).
  if (C.getCalleeName(CE).find("setproctitle") != StringRef::npos) {
    ArgNum = 0;
    return true;
  }

  return false;
}

bool GenericTaintChecker::generateReportIfTainted(const Expr *E,
                                                  const char Msg[],
                                                  CheckerContext &C) const {
  assert(E);

  // Check for taint.
  const ProgramState *State = C.getState();
  if (!State->isTainted(getPointedToSymbol(C, E)) &&
      !State->isTainted(E, C.getLocationContext()))
    return false;

  // Generate diagnostic.
  if (ExplodedNode *N = C.addTransition()) {
    initBugType();
    BugReport *report = new BugReport(*BT, Msg, N);
    report->addRange(E->getSourceRange());
    C.EmitReport(report);
    return true;
  }
  return false;
}

bool GenericTaintChecker::checkUncontrolledFormatString(const CallExpr *CE,
                                                        CheckerContext &C) const{
  // Check if the function contains a format string argument.
  unsigned int ArgNum = 0;
  if (!getPrintfFormatArgumentNum(CE, C, ArgNum))
    return false;

  // If either the format string content or the pointer itself are tainted, warn.
  if (generateReportIfTainted(CE->getArg(ArgNum),
                              MsgUncontrolledFormatString, C))
    return true;
  return false;
}

bool GenericTaintChecker::checkSystemCall(const CallExpr *CE,
                                          StringRef Name,
                                          CheckerContext &C) const {
  unsigned ArgNum = llvm::StringSwitch<unsigned>(Name)
    .Case("system", 0)
    .Case("popen", 0)
    .Default(UINT_MAX);

  if (ArgNum == UINT_MAX)
    return false;

  if (generateReportIfTainted(CE->getArg(ArgNum),
                              MsgSanitizeSystemArgs, C))
    return true;

  return false;
}

void ento::registerGenericTaintChecker(CheckerManager &mgr) {
  mgr.registerChecker<GenericTaintChecker>();
}
