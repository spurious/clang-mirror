//===--- UndefinedArraySubscriptChecker.h ----------------------*- C++ -*--===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This defines UndefinedArraySubscriptChecker, a builtin check in ExprEngine
// that performs checks for undefined array subscripts.
//
//===----------------------------------------------------------------------===//

#include "ExprEngineInternalChecks.h"
#include "clang/GR/BugReporter/BugType.h"
#include "clang/GR/PathSensitive/CheckerVisitor.h"

using namespace clang;
using namespace GR;

namespace {
class UndefinedArraySubscriptChecker
  : public CheckerVisitor<UndefinedArraySubscriptChecker> {
  BugType *BT;
public:
  UndefinedArraySubscriptChecker() : BT(0) {}
  static void *getTag() {
    static int x = 0;
    return &x;
  }
  void PreVisitArraySubscriptExpr(CheckerContext &C, 
                                  const ArraySubscriptExpr *A);
};
} // end anonymous namespace

void GR::RegisterUndefinedArraySubscriptChecker(ExprEngine &Eng) {
  Eng.registerCheck(new UndefinedArraySubscriptChecker());
}

void 
UndefinedArraySubscriptChecker::PreVisitArraySubscriptExpr(CheckerContext &C, 
                                                const ArraySubscriptExpr *A) {
  if (C.getState()->getSVal(A->getIdx()).isUndef()) {
    if (ExplodedNode *N = C.generateSink()) {
      if (!BT)
        BT = new BuiltinBug("Array subscript is undefined");

      // Generate a report for this bug.
      EnhancedBugReport *R = new EnhancedBugReport(*BT, BT->getName(), N);
      R->addRange(A->getIdx()->getSourceRange());
      R->addVisitorCreator(bugreporter::registerTrackNullOrUndefValue, 
                           A->getIdx());
      C.EmitReport(R);
    }
  }
}
