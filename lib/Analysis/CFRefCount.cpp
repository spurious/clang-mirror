// CFRefCount.cpp - Transfer functions for tracking simple values -*- C++ -*--//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the methods for CFRefCount, which implements
//  a reference count checker for Core Foundation (Mac OS X).
//
//===----------------------------------------------------------------------===//

#include "GRSimpleVals.h"
#include "clang/Analysis/PathSensitive/ValueState.h"
#include "clang/Analysis/PathDiagnostic.h"
#include "clang/Analysis/LocalCheckers.h"
#include "clang/Analysis/PathDiagnostic.h"
#include "clang/Analysis/PathSensitive/BugReporter.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/ImmutableMap.h"
#include "llvm/Support/Compiler.h"
#include <ostream>

using namespace clang;

//===----------------------------------------------------------------------===//
// Symbolic Evaluation of Reference Counting Logic
//===----------------------------------------------------------------------===//

namespace {  
  enum ArgEffect { IncRef, DecRef, DoNothing };
  typedef std::vector<ArgEffect> ArgEffects;
}

namespace llvm {
  template <> struct FoldingSetTrait<ArgEffects> {
    static void Profile(const ArgEffects& X, FoldingSetNodeID& ID) {
      for (ArgEffects::const_iterator I = X.begin(), E = X.end(); I!= E; ++I)
        ID.AddInteger((unsigned) *I);
    }    
  };
} // end llvm namespace

namespace {
  
class RetEffect {
public:
  enum Kind { NoRet = 0x0, Alias = 0x1, OwnedSymbol = 0x2,
              NotOwnedSymbol = 0x3 };

private:
  unsigned Data;
  RetEffect(Kind k, unsigned D) { Data = (D << 2) | (unsigned) k; }
  
public:

  Kind getKind() const { return (Kind) (Data & 0x3); }

  unsigned getValue() const { 
    assert(getKind() == Alias);
    return Data >> 2;
  }
    
  static RetEffect MakeAlias(unsigned Idx) { return RetEffect(Alias, Idx); }
  
  static RetEffect MakeOwned() { return RetEffect(OwnedSymbol, 0); }
  
  static RetEffect MakeNotOwned() { return RetEffect(NotOwnedSymbol, 0); }
  
  static RetEffect MakeNoRet() { return RetEffect(NoRet, 0); }
  
  operator Kind() const { return getKind(); }
  
  void Profile(llvm::FoldingSetNodeID& ID) const { ID.AddInteger(Data); }
};

  
class CFRefSummary : public llvm::FoldingSetNode {
  ArgEffects* Args;
  RetEffect   Ret;
public:
  
  CFRefSummary(ArgEffects* A, RetEffect R) : Args(A), Ret(R) {}
  
  unsigned getNumArgs() const { return Args->size(); }
  
  ArgEffect getArg(unsigned idx) const {
    assert (idx < getNumArgs());
    return (*Args)[idx];
  }
  
  RetEffect getRet() const {
    return Ret;
  }
  
  typedef ArgEffects::const_iterator arg_iterator;
  
  arg_iterator begin_args() const { return Args->begin(); }
  arg_iterator end_args()   const { return Args->end(); }
  
  static void Profile(llvm::FoldingSetNodeID& ID, ArgEffects* A, RetEffect R) {
    ID.AddPointer(A);
    ID.Add(R);
  }
      
  void Profile(llvm::FoldingSetNodeID& ID) const {
    Profile(ID, Args, Ret);
  }
};

  
class CFRefSummaryManager {
  typedef llvm::FoldingSet<llvm::FoldingSetNodeWrapper<ArgEffects> > AESetTy;
  typedef llvm::FoldingSet<CFRefSummary>                SummarySetTy;
  typedef llvm::DenseMap<FunctionDecl*, CFRefSummary*>  SummaryMapTy;
  
  ASTContext& Ctx;  
  SummarySetTy SummarySet;
  SummaryMapTy SummaryMap;  
  AESetTy AESet;  
  llvm::BumpPtrAllocator BPAlloc;  
  ArgEffects ScratchArgs;
  
  
  ArgEffects*   getArgEffects();

  CFRefSummary* getCannedCFSummary(FunctionTypeProto* FT, bool isRetain);

  CFRefSummary* getCFSummary(FunctionDecl* FD, const char* FName);
  
  CFRefSummary* getCFSummaryCreateRule(FunctionTypeProto* FT);
  CFRefSummary* getCFSummaryGetRule(FunctionTypeProto* FT);  
  
  CFRefSummary* getPersistentSummary(ArgEffects* AE, RetEffect RE);
  
  void FillDoNothing(unsigned Args);

  
public:
  CFRefSummaryManager(ASTContext& ctx) : Ctx(ctx) {}
  ~CFRefSummaryManager();
  
  CFRefSummary* getSummary(FunctionDecl* FD, ASTContext& Ctx);
};
  
} // end anonymous namespace

//===----------------------------------------------------------------------===//
// Implementation of checker data structures.
//===----------------------------------------------------------------------===//

CFRefSummaryManager::~CFRefSummaryManager() {
  
  // FIXME: The ArgEffects could eventually be allocated from BPAlloc, 
  //   mitigating the need to do explicit cleanup of the
  //   Argument-Effect summaries.
  
  for (AESetTy::iterator I = AESet.begin(), E = AESet.end(); I!=E; ++I)
    I->getValue().~ArgEffects();
}

ArgEffects* CFRefSummaryManager::getArgEffects() {

  llvm::FoldingSetNodeID profile;
  profile.Add(ScratchArgs);
  void* InsertPos;
  
  llvm::FoldingSetNodeWrapper<ArgEffects>* E =
    AESet.FindNodeOrInsertPos(profile, InsertPos);
  
  if (E) {    
    ScratchArgs.clear();
    return &E->getValue();
  }
  
  E = (llvm::FoldingSetNodeWrapper<ArgEffects>*)
      BPAlloc.Allocate<llvm::FoldingSetNodeWrapper<ArgEffects> >();
                       
  new (E) llvm::FoldingSetNodeWrapper<ArgEffects>(ScratchArgs);
  AESet.InsertNode(E, InsertPos);

  ScratchArgs.clear();
  return &E->getValue();
}

CFRefSummary* CFRefSummaryManager::getPersistentSummary(ArgEffects* AE,
                                                        RetEffect RE) {
  
  llvm::FoldingSetNodeID profile;
  CFRefSummary::Profile(profile, AE, RE);
  void* InsertPos;
  
  CFRefSummary* Summ = SummarySet.FindNodeOrInsertPos(profile, InsertPos);
  
  if (Summ)
    return Summ;
  
  Summ = (CFRefSummary*) BPAlloc.Allocate<CFRefSummary>();
  new (Summ) CFRefSummary(AE, RE);
  SummarySet.InsertNode(Summ, InsertPos);
  
  return Summ;
}


CFRefSummary* CFRefSummaryManager::getSummary(FunctionDecl* FD,
                                              ASTContext& Ctx) {

  SourceLocation Loc = FD->getLocation();
  
  if (!Loc.isFileID())
    return NULL;
  
  { // Look into our cache of summaries to see if we have already computed
    // a summary for this FunctionDecl.
      
    SummaryMapTy::iterator I = SummaryMap.find(FD);
    
    if (I != SummaryMap.end())
      return I->second;
  }
  
#if 0
  SourceManager& SrcMgr = Ctx.getSourceManager();
  unsigned fid = Loc.getFileID();
  const FileEntry* FE = SrcMgr.getFileEntryForID(fid);
  
  if (!FE)
    return NULL;
  
  const char* DirName = FE->getDir()->getName();  
  assert (DirName);
  assert (strlen(DirName) > 0);
  
  if (!strstr(DirName, "CoreFoundation")) {
    SummaryMap[FD] = NULL;
    return NULL;
  }
#endif
  
  const char* FName = FD->getIdentifier()->getName();
    
  if (FName[0] == 'C' && FName[1] == 'F') {
    CFRefSummary* S = getCFSummary(FD, FName);
    SummaryMap[FD] = S;
    return S;
  }
  
  return NULL;  
}

CFRefSummary* CFRefSummaryManager::getCFSummary(FunctionDecl* FD,
                                                const char* FName) {
  
  // For now, only generate summaries for functions that have a prototype.
  
  FunctionTypeProto* FT =
    dyn_cast<FunctionTypeProto>(FD->getType().getTypePtr());
  
  if (!FT)
    return NULL;
  
  FName += 2;

  if (strcmp(FName, "Retain") == 0)
    return getCannedCFSummary(FT, true);
  
  if (strcmp(FName, "Release") == 0)
    return getCannedCFSummary(FT, false);
  
  assert (ScratchArgs.empty());
  bool usesCreateRule = false;
  
  if (strstr(FName, "Create"))
    usesCreateRule = true;
  
  if (!usesCreateRule && strstr(FName, "Copy"))
    usesCreateRule = true;
  
  if (usesCreateRule)
    return getCFSummaryCreateRule(FT);

  if (strstr(FName, "Get"))
    return getCFSummaryGetRule(FT);
  
  return NULL;
}

CFRefSummary* CFRefSummaryManager::getCannedCFSummary(FunctionTypeProto* FT,
                                                      bool isRetain) {
  
  if (FT->getNumArgs() != 1)
    return NULL;
  
  TypedefType* ArgT = dyn_cast<TypedefType>(FT->getArgType(0).getTypePtr());
  
  if (!ArgT)
    return NULL;
  
  // For CFRetain/CFRelease, the first (and only) argument is of type 
  // "CFTypeRef".
  
  const char* TDName = ArgT->getDecl()->getIdentifier()->getName();
  assert (TDName);
  
  if (strcmp("CFTypeRef", TDName) != 0)
    return NULL;
  
  if (!ArgT->isPointerType())
    return NULL;

  QualType RetTy = FT->getResultType();
  
  if (isRetain) {
    // CFRetain: the return type should also be "CFTypeRef".
    if (RetTy.getTypePtr() != ArgT)
      return NULL;
    
    // The function's interface checks out.  Generate a canned summary.    
    assert (ScratchArgs.empty());
    ScratchArgs.push_back(IncRef);
    return getPersistentSummary(getArgEffects(), RetEffect::MakeAlias(0));
  }
  else {
    // CFRelease: the return type should be void.
    
    if (RetTy != Ctx.VoidTy)
      return NULL;
    
    assert (ScratchArgs.empty());
    ScratchArgs.push_back(DecRef);
    return getPersistentSummary(getArgEffects(), RetEffect::MakeNoRet());
  }
}

static bool isCFRefType(QualType T) {
  
  if (!T->isPointerType())
    return false;
  
  // Check the typedef for the name "CF" and the substring "Ref".
  
  TypedefType* TD = dyn_cast<TypedefType>(T.getTypePtr());
  
  if (!TD)
    return false;
  
  const char* TDName = TD->getDecl()->getIdentifier()->getName();
  assert (TDName);
  
  if (TDName[0] != 'C' || TDName[1] != 'F')
    return false;
  
  if (strstr(TDName, "Ref") == 0)
    return false;
  
  return true;
}
  
void CFRefSummaryManager::FillDoNothing(unsigned Args) {
  for (unsigned i = 0; i != Args; ++i)
    ScratchArgs.push_back(DoNothing);
}


CFRefSummary*
CFRefSummaryManager::getCFSummaryCreateRule(FunctionTypeProto* FT) {
 
  if (!isCFRefType(FT->getResultType()))
    return NULL;

  assert (ScratchArgs.empty());
  
  // FIXME: Add special-cases for functions that retain/release.  For now
  //  just handle the default case.
  
  FillDoNothing(FT->getNumArgs());  
  return getPersistentSummary(getArgEffects(), RetEffect::MakeOwned());
}

CFRefSummary*
CFRefSummaryManager::getCFSummaryGetRule(FunctionTypeProto* FT) {
  
  QualType RetTy = FT->getResultType();
  
  // FIXME: For now we assume that all pointer types returned are referenced
  // counted.  Since this is the "Get" rule, we assume non-ownership, which
  // works fine for things that are not reference counted.  We do this because
  // some generic data structures return "void*".  We need something better
  // in the future.
  
  if (!isCFRefType(RetTy) && !RetTy->isPointerType())
    return NULL;
  
  assert (ScratchArgs.empty());
  
  // FIXME: Add special-cases for functions that retain/release.  For now
  //  just handle the default case.
  
  FillDoNothing(FT->getNumArgs());  
  return getPersistentSummary(getArgEffects(), RetEffect::MakeNotOwned());
}

//===----------------------------------------------------------------------===//
// Bug Descriptions.
//===----------------------------------------------------------------------===//

namespace {
  
  class CFRefCount;
  
  class VISIBILITY_HIDDEN CFRefBug : public BugType {
  protected:
    CFRefCount& TF;
    
  public:
    CFRefBug(CFRefCount& tf) : TF(tf) {}
  };
  
  class VISIBILITY_HIDDEN UseAfterRelease : public CFRefBug {
  public:
    UseAfterRelease(CFRefCount& tf) : CFRefBug(tf) {}
    
    virtual const char* getName() const {
      return "(CoreFoundation) use-after-release";
    }
    virtual const char* getDescription() const {
      return "(CoreFoundation) Reference-counted object is used"
      " after it is released.";
    }
    
    virtual void EmitWarnings(BugReporter& BR);
    
  };
  
  class VISIBILITY_HIDDEN BadRelease : public CFRefBug {
  public:
    BadRelease(CFRefCount& tf) : CFRefBug(tf) {}
    
    virtual const char* getName() const {
      return "(CoreFoundation) release of non-owned object";
    }
    virtual const char* getDescription() const {
      return "Incorrect decrement of the reference count of a "
      "CoreFoundation object:\n"
      "The object is not owned at this point by the caller.";
    }
    
    virtual void EmitWarnings(BugReporter& BR);
  };
  
} // end anonymous namespace

//===----------------------------------------------------------------------===//
// Reference-counting logic (typestate + counts).
//===----------------------------------------------------------------------===//

namespace {
  
class VISIBILITY_HIDDEN RefVal {
  unsigned Data;
  
  RefVal(unsigned K, unsigned D) : Data((D << 3) | K) {
    assert ((K & ~0x7) == 0x0);
  }
  
  RefVal(unsigned K) : Data(K) {
    assert ((K & ~0x7) == 0x0);
  }

public:  
  
  enum Kind { Owned = 0, NotOwned = 1, Released = 2,
              ErrorUseAfterRelease = 3, ErrorReleaseNotOwned = 4,
              ErrorLeak = 5 };    
  
  Kind getKind() const { return (Kind) (Data & 0x7); }

  unsigned getCount() const {
    assert (getKind() == Owned || getKind() == NotOwned);
    return Data >> 3;
  }
  
  static bool isError(Kind k) { return k >= ErrorUseAfterRelease; }
  
  static bool isLeak(Kind k) { return k == ErrorLeak; }
  
  bool isOwned() const {
    return getKind() == Owned;
  }
  
  bool isNotOwned() const {
    return getKind() == NotOwned;
  }
  
  static RefVal makeOwned(unsigned Count = 0) {
    return RefVal(Owned, Count);
  }
  
  static RefVal makeNotOwned(unsigned Count = 0) {
    return RefVal(NotOwned, Count);
  }
  
  static RefVal makeLeak() { return RefVal(ErrorLeak); }  
  static RefVal makeReleased() { return RefVal(Released); }
  static RefVal makeUseAfterRelease() { return RefVal(ErrorUseAfterRelease); }
  static RefVal makeReleaseNotOwned() { return RefVal(ErrorReleaseNotOwned); }
  
  bool operator==(const RefVal& X) const { return Data == X.Data; }
  void Profile(llvm::FoldingSetNodeID& ID) const { ID.AddInteger(Data); }
  
  void print(std::ostream& Out) const;
};
  
void RefVal::print(std::ostream& Out) const {
  switch (getKind()) {
    default: assert(false);
    case Owned: { 
      Out << "Owned";
      unsigned cnt = getCount();
      if (cnt) Out << " (+ " << cnt << ")";
      break;
    }
      
    case NotOwned: {
      Out << "Not-Owned";
      unsigned cnt = getCount();
      if (cnt) Out << " (+ " << cnt << ")";
      break;
    }
      
    case Released:
      Out << "Released";
      break;
      
    case ErrorLeak:
      Out << "Leaked";
      break;            
      
    case ErrorUseAfterRelease:
      Out << "Use-After-Release [ERROR]";
      break;
      
    case ErrorReleaseNotOwned:
      Out << "Release of Not-Owned [ERROR]";
      break;
  }
}
  
//===----------------------------------------------------------------------===//
// Transfer functions.
//===----------------------------------------------------------------------===//

static inline Selector GetUnarySelector(const char* name, ASTContext& Ctx) {
  IdentifierInfo* II = &Ctx.Idents.get(name);
  return Ctx.Selectors.getSelector(0, &II);
}
  
class VISIBILITY_HIDDEN CFRefCount : public GRSimpleVals {
  
  // Type definitions.
  
  typedef llvm::ImmutableMap<SymbolID, RefVal> RefBindings;
  typedef RefBindings::Factory RefBFactoryTy;
  
  typedef llvm::DenseMap<GRExprEngine::NodeTy*,Expr*> UseAfterReleasesTy;
  typedef llvm::DenseMap<GRExprEngine::NodeTy*,Expr*> ReleasesNotOwnedTy;

  typedef llvm::SmallVector<std::pair<SymbolID, ExplodedNode<ValueState>*>, 2>
          LeaksTy;
  
  
  class BindingsPrinter : public ValueState::CheckerStatePrinter {
  public:
    virtual void PrintCheckerState(std::ostream& Out, void* State,
                                   const char* nl, const char* sep);
  };
  
  // Instance variables.
  
  CFRefSummaryManager Summaries;
  RefBFactoryTy       RefBFactory;
     
  UseAfterReleasesTy UseAfterReleases;
  ReleasesNotOwnedTy ReleasesNotOwned;
  LeaksTy            Leaks;
  
  BindingsPrinter Printer;
  
  Selector RetainSelector;
  Selector ReleaseSelector;

  // Private methods.

  static RefBindings GetRefBindings(ValueState& StImpl) {
    return RefBindings((RefBindings::TreeTy*) StImpl.CheckerState);
  }

  static void SetRefBindings(ValueState& StImpl, RefBindings B) {
    StImpl.CheckerState = B.getRoot();
  }

  RefBindings Remove(RefBindings B, SymbolID sym) {
    return RefBFactory.Remove(B, sym);
  }
  
  RefBindings Update(RefBindings B, SymbolID sym, RefVal V, ArgEffect E,
                     RefVal::Kind& hasErr);
  
  void ProcessNonLeakError(ExplodedNodeSet<ValueState>& Dst,
                           GRStmtNodeBuilder<ValueState>& Builder,
                           Expr* NodeExpr, Expr* ErrorExpr,                        
                           ExplodedNode<ValueState>* Pred,
                           ValueState* St,
                           RefVal::Kind hasErr);
  
  ValueState* HandleSymbolDeath(ValueStateManager& VMgr, ValueState* St,
                                SymbolID sid, RefVal V, bool& hasLeak);
  
  ValueState* NukeBinding(ValueStateManager& VMgr, ValueState* St,
                          SymbolID sid);
  
public:
  
  CFRefCount(ASTContext& Ctx)
    : Summaries(Ctx),
      RetainSelector(GetUnarySelector("retain", Ctx)),
      ReleaseSelector(GetUnarySelector("release", Ctx)) {}
  
  virtual ~CFRefCount() {}
  
  virtual void RegisterChecks(GRExprEngine& Eng);
 
  virtual ValueState::CheckerStatePrinter* getCheckerStatePrinter() {
    return &Printer;
  }
  
  // Calls.
  
  virtual void EvalCall(ExplodedNodeSet<ValueState>& Dst,
                        GRExprEngine& Eng,
                        GRStmtNodeBuilder<ValueState>& Builder,
                        CallExpr* CE, LVal L,
                        ExplodedNode<ValueState>* Pred);  
  
  virtual void EvalObjCMessageExpr(ExplodedNodeSet<ValueState>& Dst,
                                   GRExprEngine& Engine,
                                   GRStmtNodeBuilder<ValueState>& Builder,
                                   ObjCMessageExpr* ME,
                                   ExplodedNode<ValueState>* Pred);
  
  bool EvalObjCMessageExprAux(ExplodedNodeSet<ValueState>& Dst,
                              GRExprEngine& Engine,
                              GRStmtNodeBuilder<ValueState>& Builder,
                              ObjCMessageExpr* ME,
                              ExplodedNode<ValueState>* Pred);

  // Stores.
  
  virtual void EvalStore(ExplodedNodeSet<ValueState>& Dst,
                         GRExprEngine& Engine,
                         GRStmtNodeBuilder<ValueState>& Builder,
                         Expr* E, ExplodedNode<ValueState>* Pred,
                         ValueState* St, RVal TargetLV, RVal Val);
  // End-of-path.
  
  virtual void EvalEndPath(GRExprEngine& Engine,
                           GREndPathNodeBuilder<ValueState>& Builder);
  
  // Error iterators.

  typedef UseAfterReleasesTy::iterator use_after_iterator;  
  typedef ReleasesNotOwnedTy::iterator bad_release_iterator;
  
  use_after_iterator use_after_begin() { return UseAfterReleases.begin(); }
  use_after_iterator use_after_end() { return UseAfterReleases.end(); }
  
  bad_release_iterator bad_release_begin() { return ReleasesNotOwned.begin(); }
  bad_release_iterator bad_release_end() { return ReleasesNotOwned.end(); }
};

} // end anonymous namespace

void CFRefCount::RegisterChecks(GRExprEngine& Eng) {
  GRSimpleVals::RegisterChecks(Eng);
  Eng.Register(new UseAfterRelease(*this));
  Eng.Register(new BadRelease(*this));
}


void CFRefCount::BindingsPrinter::PrintCheckerState(std::ostream& Out,
                                                    void* State, const char* nl,
                                                    const char* sep) {
  RefBindings B((RefBindings::TreeTy*) State);
  
  if (State)
    Out << sep << nl;
  
  for (RefBindings::iterator I=B.begin(), E=B.end(); I!=E; ++I) {
    Out << (*I).first << " : ";
    (*I).second.print(Out);
    Out << nl;
  }
}

static inline ArgEffect GetArgE(CFRefSummary* Summ, unsigned idx) {
  return Summ ? Summ->getArg(idx) : DoNothing;
}

static inline RetEffect GetRetE(CFRefSummary* Summ) {
  return Summ ? Summ->getRet() : RetEffect::MakeNoRet();
}

void CFRefCount::ProcessNonLeakError(ExplodedNodeSet<ValueState>& Dst,
                                     GRStmtNodeBuilder<ValueState>& Builder,
                                     Expr* NodeExpr, Expr* ErrorExpr,                        
                                     ExplodedNode<ValueState>* Pred,
                                     ValueState* St,
                                     RefVal::Kind hasErr) {
  Builder.BuildSinks = true;
  GRExprEngine::NodeTy* N  = Builder.MakeNode(Dst, NodeExpr, Pred, St);

  if (!N) return;
    
  switch (hasErr) {
    default: assert(false);
    case RefVal::ErrorUseAfterRelease:
      UseAfterReleases[N] = ErrorExpr;
      break;
      
    case RefVal::ErrorReleaseNotOwned:
      ReleasesNotOwned[N] = ErrorExpr;
      break;
  }
}

void CFRefCount::EvalCall(ExplodedNodeSet<ValueState>& Dst,
                          GRExprEngine& Eng,
                          GRStmtNodeBuilder<ValueState>& Builder,
                          CallExpr* CE, LVal L,
                          ExplodedNode<ValueState>* Pred) {
  
  ValueStateManager& StateMgr = Eng.getStateManager();
  
  CFRefSummary* Summ = NULL;
  
  // Get the summary.

  if (isa<lval::FuncVal>(L)) {  
    lval::FuncVal FV = cast<lval::FuncVal>(L);
    FunctionDecl* FD = FV.getDecl();
    Summ = Summaries.getSummary(FD, Eng.getContext());
  }

  // Get the state.
  
  ValueState* St = Builder.GetState(Pred);
  
  // Evaluate the effects of the call.
  
  ValueState StVals = *St;
  RefVal::Kind hasErr = (RefVal::Kind) 0;
 
  // This function has a summary.  Evaluate the effect of the arguments.
  
  unsigned idx = 0;
  
  Expr* ErrorExpr = NULL;
  
  for (CallExpr::arg_iterator I = CE->arg_begin(), E = CE->arg_end();
        I != E; ++I, ++idx) {
    
    RVal V = StateMgr.GetRVal(St, *I);
    
    if (isa<lval::SymbolVal>(V)) {
      SymbolID Sym = cast<lval::SymbolVal>(V).getSymbol();
      RefBindings B = GetRefBindings(StVals);      
      
      if (RefBindings::TreeTy* T = B.SlimFind(Sym)) {
        B = Update(B, Sym, T->getValue().second, GetArgE(Summ, idx), hasErr);
        SetRefBindings(StVals, B);
        
        if (hasErr) {
          ErrorExpr = *I;
          break;
        }
      }
    }  
    else if (isa<LVal>(V)) { // Nuke all arguments passed by reference.
      
      // FIXME: This is basically copy-and-paste from GRSimpleVals.  We 
      //  should compose behavior, not copy it.
      StateMgr.Unbind(StVals, cast<LVal>(V));
    }
  }    
  
  St = StateMgr.getPersistentState(StVals);
    
  if (hasErr) {
    ProcessNonLeakError(Dst, Builder, CE, ErrorExpr, Pred, St, hasErr);
    return;
  }
    
  // Finally, consult the summary for the return value.
  
  RetEffect RE = GetRetE(Summ);
  
  switch (RE.getKind()) {
    default:
      assert (false && "Unhandled RetEffect."); break;
    
    case RetEffect::NoRet:
    
      // Make up a symbol for the return value (not reference counted).
      // FIXME: This is basically copy-and-paste from GRSimpleVals.  We 
      //  should compose behavior, not copy it.
      
      if (CE->getType() != Eng.getContext().VoidTy) {    
        unsigned Count = Builder.getCurrentBlockCount();
        SymbolID Sym = Eng.getSymbolManager().getConjuredSymbol(CE, Count);
        
        RVal X = CE->getType()->isPointerType() 
          ? cast<RVal>(lval::SymbolVal(Sym)) 
          : cast<RVal>(nonlval::SymbolVal(Sym));
        
        St = StateMgr.SetRVal(St, CE, X, Eng.getCFG().isBlkExpr(CE), false);
      }      
      
      break;
      
    case RetEffect::Alias: {
      unsigned idx = RE.getValue();
      assert (idx < CE->getNumArgs());
      RVal V = StateMgr.GetRVal(St, CE->getArg(idx));
      St = StateMgr.SetRVal(St, CE, V, Eng.getCFG().isBlkExpr(CE), false);
      break;
    }
      
    case RetEffect::OwnedSymbol: {
      unsigned Count = Builder.getCurrentBlockCount();
      SymbolID Sym = Eng.getSymbolManager().getConjuredSymbol(CE, Count);

      ValueState StImpl = *St;
      RefBindings B = GetRefBindings(StImpl);
      SetRefBindings(StImpl, RefBFactory.Add(B, Sym, RefVal::makeOwned()));
      
      St = StateMgr.SetRVal(StateMgr.getPersistentState(StImpl),
                            CE, lval::SymbolVal(Sym),
                            Eng.getCFG().isBlkExpr(CE), false);
      
      break;
    }
      
    case RetEffect::NotOwnedSymbol: {
      unsigned Count = Builder.getCurrentBlockCount();
      SymbolID Sym = Eng.getSymbolManager().getConjuredSymbol(CE, Count);
      
      ValueState StImpl = *St;
      RefBindings B = GetRefBindings(StImpl);
      SetRefBindings(StImpl, RefBFactory.Add(B, Sym, RefVal::makeNotOwned()));
      
      St = StateMgr.SetRVal(StateMgr.getPersistentState(StImpl),
                            CE, lval::SymbolVal(Sym),
                            Eng.getCFG().isBlkExpr(CE), false);
      
      break;
    }
  }
      
  Builder.MakeNode(Dst, CE, Pred, St);
}


void CFRefCount::EvalObjCMessageExpr(ExplodedNodeSet<ValueState>& Dst,
                                     GRExprEngine& Eng,
                                     GRStmtNodeBuilder<ValueState>& Builder,
                                     ObjCMessageExpr* ME,
                                     ExplodedNode<ValueState>* Pred) {
  
  if (EvalObjCMessageExprAux(Dst, Eng, Builder, ME, Pred))
    GRSimpleVals::EvalObjCMessageExpr(Dst, Eng, Builder, ME, Pred);
}

bool CFRefCount::EvalObjCMessageExprAux(ExplodedNodeSet<ValueState>& Dst,
                                        GRExprEngine& Eng,
                                        GRStmtNodeBuilder<ValueState>& Builder,
                                        ObjCMessageExpr* ME,
                                        ExplodedNode<ValueState>* Pred) {
    
  // Handle "toll-free bridging" of calls to "Release" and "Retain".
  
  // FIXME: track the underlying object type associated so that we can
  //  flag illegal uses of toll-free bridging (or at least handle it
  //  at casts).
  
  Selector S = ME->getSelector();
  
  if (!S.isUnarySelector())
    return true;

  Expr* Receiver = ME->getReceiver();
  
  if (!Receiver)
    return true;

  // Check if we are calling "Retain" or "Release".
  
  bool isRetain = false;
  
  if (S == RetainSelector)
    isRetain = true;
  else if (S != ReleaseSelector)
    return true;
  
  // We have "Retain" or "Release".  Get the reference binding.
  
  ValueStateManager& StateMgr = Eng.getStateManager();
  ValueState* St = Builder.GetState(Pred);
  RVal V = StateMgr.GetRVal(St, Receiver);
  
  if (!isa<lval::SymbolVal>(V))
    return true;

  SymbolID Sym = cast<lval::SymbolVal>(V).getSymbol();
  RefBindings B = GetRefBindings(*St);
  
  RefBindings::TreeTy* T = B.SlimFind(Sym);
  
  if (!T)
    return true;
  
  RefVal::Kind hasErr = (RefVal::Kind) 0;
  B = Update(B, Sym, T->getValue().second, isRetain ? IncRef : DecRef, hasErr);

  // Create a new state with the updated bindings.
  
  ValueState StVals = *St;
  SetRefBindings(StVals, B);
  St = StateMgr.getPersistentState(StVals);
  
  // Create an error node if it exists.
  
  if (hasErr)
    ProcessNonLeakError(Dst, Builder, ME, Receiver, Pred, St, hasErr);
  else
    Builder.MakeNode(Dst, ME, Pred, St);

  return false;
}

// Stores.

void CFRefCount::EvalStore(ExplodedNodeSet<ValueState>& Dst,
                           GRExprEngine& Eng,
                           GRStmtNodeBuilder<ValueState>& Builder,
                           Expr* E, ExplodedNode<ValueState>* Pred,
                           ValueState* St, RVal TargetLV, RVal Val) {
  
  // Check if we have a binding for "Val" and if we are storing it to something
  // we don't understand or otherwise the value "escapes" the function.
  
  if (!isa<lval::SymbolVal>(Val))
    return;
  
  // Are we storing to something that causes the value to "escape"?
  
  bool escapes = false;
  
  if (!isa<lval::DeclVal>(TargetLV))
    escapes = true;
  else
    escapes = cast<lval::DeclVal>(TargetLV).getDecl()->hasGlobalStorage();
  
  if (!escapes)
    return;
  
  SymbolID Sym = cast<lval::SymbolVal>(Val).getSymbol();
  RefBindings B = GetRefBindings(*St);
  RefBindings::TreeTy* T = B.SlimFind(Sym);
  
  if (!T)
    return;
  
  // Nuke the binding.  
  St = NukeBinding(Eng.getStateManager(), St, Sym);
  
  // Hand of the remaining logic to the parent implementation.
  GRSimpleVals::EvalStore(Dst, Eng, Builder, E, Pred, St, TargetLV, Val);
}


ValueState* CFRefCount::NukeBinding(ValueStateManager& VMgr, ValueState* St,
                                    SymbolID sid) {
  ValueState StImpl = *St;
  RefBindings B = GetRefBindings(StImpl);
  StImpl.CheckerState = RefBFactory.Remove(B, sid).getRoot();
  return VMgr.getPersistentState(StImpl);
}

// End-of-path.



ValueState* CFRefCount::HandleSymbolDeath(ValueStateManager& VMgr,
                                          ValueState* St, SymbolID sid,
                                          RefVal V, bool& hasLeak) {
    
  hasLeak = V.isOwned() || V.isNotOwned() && V.getCount() > 0;

  if (!hasLeak)
    return NukeBinding(VMgr, St, sid);
  
  RefBindings B = GetRefBindings(*St);
  ValueState StImpl = *St;  
  StImpl.CheckerState = RefBFactory.Add(B, sid, RefVal::makeLeak()).getRoot();
  return VMgr.getPersistentState(StImpl);
}

void CFRefCount::EvalEndPath(GRExprEngine& Eng,
                             GREndPathNodeBuilder<ValueState>& Builder) {
  
  ValueState* St = Builder.getState();
  RefBindings B = GetRefBindings(*St);
  
  llvm::SmallVector<SymbolID, 10> Leaked;
  
  for (RefBindings::iterator I = B.begin(), E = B.end(); I != E; ++I) {
    bool hasLeak = false;
    
    St = HandleSymbolDeath(Eng.getStateManager(), St,
                           (*I).first, (*I).second, hasLeak);
    
    if (hasLeak) Leaked.push_back((*I).first);
  }
      
  ExplodedNode<ValueState>* N = Builder.MakeNode(St);
  
  for (llvm::SmallVector<SymbolID, 10>::iterator I=Leaked.begin(),
       E = Leaked.end(); I != E; ++I)
    Leaks.push_back(std::make_pair(*I, N));
}


CFRefCount::RefBindings CFRefCount::Update(RefBindings B, SymbolID sym,
                                           RefVal V, ArgEffect E,
                                           RefVal::Kind& hasErr) {
  
  // FIXME: This dispatch can potentially be sped up by unifiying it into
  //  a single switch statement.  Opt for simplicity for now.
  
  switch (E) {
    default:
      assert (false && "Unhandled CFRef transition.");
      
    case DoNothing:
      if (V.getKind() == RefVal::Released) {
        V = RefVal::makeUseAfterRelease();        
        hasErr = V.getKind();
        break;
      }
      
      return B;
      
    case IncRef:      
      switch (V.getKind()) {
        default:
          assert(false);

        case RefVal::Owned:
          V = RefVal::makeOwned(V.getCount()+1);
          break;
                    
        case RefVal::NotOwned:
          V = RefVal::makeNotOwned(V.getCount()+1);
          break;
          
        case RefVal::Released:
          V = RefVal::makeUseAfterRelease();
          hasErr = V.getKind();
          break;
      }
      
      break;
      
    case DecRef:
      switch (V.getKind()) {
        default:
          assert (false);
          
        case RefVal::Owned: {
          signed Count = ((signed) V.getCount()) - 1;
          V = Count >= 0 ? RefVal::makeOwned(Count) : RefVal::makeReleased();
          break;
        }
          
        case RefVal::NotOwned: {
          signed Count = ((signed) V.getCount()) - 1;
          
          if (Count >= 0)
            V = RefVal::makeNotOwned(Count);
          else {
            V = RefVal::makeReleaseNotOwned();
            hasErr = V.getKind();
          }
          
          break;
        }

        case RefVal::Released:
          V = RefVal::makeUseAfterRelease();
          hasErr = V.getKind();
          break;          
      }
      
      break;
  }

  return RefBFactory.Add(B, sym, V);
}


//===----------------------------------------------------------------------===//
// Error reporting.
//===----------------------------------------------------------------------===//

void UseAfterRelease::EmitWarnings(BugReporter& BR) {

  for (CFRefCount::use_after_iterator I = TF.use_after_begin(),
        E = TF.use_after_end(); I != E; ++I) {
    
    RangedBugReport report(*this, I->first);
    report.addRange(I->second->getSourceRange());    
    BR.EmitPathWarning(report);    
  }
}

void BadRelease::EmitWarnings(BugReporter& BR) {
  
  for (CFRefCount::bad_release_iterator I = TF.bad_release_begin(),
       E = TF.bad_release_end(); I != E; ++I) {
    
    RangedBugReport report(*this, I->first);
    report.addRange(I->second->getSourceRange());    
    BR.EmitPathWarning(report); 

  }  
}

//===----------------------------------------------------------------------===//
// Transfer function creation for external clients.
//===----------------------------------------------------------------------===//

GRTransferFuncs* clang::MakeCFRefCountTF(ASTContext& Ctx) {
  return new CFRefCount(Ctx);
}  
