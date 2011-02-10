//=== PointerArithChecker.cpp - Pointer arithmetic checker -----*- C++ -*--===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This files defines PointerArithChecker, a builtin checker that checks for
// pointer arithmetic on locations other than array elements.
//
//===----------------------------------------------------------------------===//

#include "InternalChecks.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerVisitor.h"

using namespace clang;
using namespace ento;

namespace {
class PointerArithChecker 
  : public CheckerVisitor<PointerArithChecker> {
  BuiltinBug *BT;
public:
  PointerArithChecker() : BT(0) {}
  static void *getTag();
  void PreVisitBinaryOperator(CheckerContext &C, const BinaryOperator *B);
};
}

void *PointerArithChecker::getTag() {
  static int x;
  return &x;
}

void PointerArithChecker::PreVisitBinaryOperator(CheckerContext &C,
                                                 const BinaryOperator *B) {
  if (B->getOpcode() != BO_Sub && B->getOpcode() != BO_Add)
    return;

  const GRState *state = C.getState();
  SVal LV = state->getSVal(B->getLHS());
  SVal RV = state->getSVal(B->getRHS());

  const MemRegion *LR = LV.getAsRegion();

  if (!LR || !RV.isConstant())
    return;

  // If pointer arithmetic is done on variables of non-array type, this often
  // means behavior rely on memory organization, which is dangerous.
  if (isa<VarRegion>(LR) || isa<CodeTextRegion>(LR) || 
      isa<CompoundLiteralRegion>(LR)) {

    if (ExplodedNode *N = C.generateNode()) {
      if (!BT)
        BT = new BuiltinBug("Dangerous pointer arithmetic",
                            "Pointer arithmetic done on non-array variables "
                            "means reliance on memory layout, which is "
                            "dangerous.");
      RangedBugReport *R = new RangedBugReport(*BT, BT->getDescription(), N);
      R->addRange(B->getSourceRange());
      C.EmitReport(R);
    }
  }
}

void ento::RegisterPointerArithChecker(ExprEngine &Eng) {
  Eng.registerCheck(new PointerArithChecker());
}
