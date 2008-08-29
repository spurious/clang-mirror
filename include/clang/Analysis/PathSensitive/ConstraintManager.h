#ifndef LLVM_CLANG_ANALYSIS_CONSTRAINT_MANAGER_H
#define LLVM_CLANG_ANALYSIS_CONSTRAINT_MANAGER_H

// FIXME: Typedef LiveSymbolsTy/DeadSymbolsTy at a more appropriate place.
#include "clang/Analysis/PathSensitive/Store.h"

namespace llvm {
class APSInt;
}

namespace clang {

class GRState;
class GRStateManager;
class RVal;
class SymbolID;

class ConstraintManager {
public:
  virtual ~ConstraintManager();
  virtual const GRState* Assume(const GRState* St, RVal Cond, bool Assumption,
                                bool& isFeasible) = 0;

  virtual const GRState* AddNE(const GRState* St, SymbolID sym, 
                               const llvm::APSInt& V) = 0;
  virtual const llvm::APSInt* getSymVal(const GRState* St, SymbolID sym) = 0;

  virtual bool isEqual(const GRState* St, SymbolID sym, 
                       const llvm::APSInt& V) const = 0;

  virtual const GRState* RemoveDeadBindings(const GRState* St,
                                    StoreManager::LiveSymbolsTy& LSymbols,
                                    StoreManager::DeadSymbolsTy& DSymbols) = 0;

  virtual void print(const GRState* St, std::ostream& Out, 
                     const char* nl, const char *sep) = 0;
};

ConstraintManager* CreateBasicConstraintManager(GRStateManager& statemgr);

} // end clang namespace

#endif
