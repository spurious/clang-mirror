//== BasicStore.cpp - Basic map from Locations to Values --------*- C++ -*--==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defined the BasicStore and BasicStoreManager classes.
//
//===----------------------------------------------------------------------===//

#include "clang/Analysis/Analyses/LiveVariables.h"
#include "clang/Analysis/PathSensitive/GRState.h"
#include "llvm/ADT/ImmutableMap.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Streams.h"

using namespace clang;
using store::Region;
using store::RegionExtent;

namespace {
  
class VISIBILITY_HIDDEN BasicStoreManager : public StoreManager {
  typedef llvm::ImmutableMap<VarDecl*,RVal> VarBindingsTy;  
  VarBindingsTy::Factory VBFactory;
  GRStateManager& StMgr;
  
public:
  BasicStoreManager(GRStateManager& mgr) : StMgr(mgr) {}
  
  virtual ~BasicStoreManager() {}

  virtual RVal GetRVal(Store St, LVal LV, QualType T);  
  virtual Store SetRVal(Store St, LVal LV, RVal V);  
  virtual Store Remove(Store St, LVal LV);

  virtual Store getInitialStore(GRStateManager& StateMgr);
  
  virtual Store RemoveDeadBindings(Store store, Stmt* Loc,
                                   const LiveVariables& Live,
                                   DeclRootsTy& DRoots, LiveSymbolsTy& LSymbols,
                                   DeadSymbolsTy& DSymbols);

  virtual Store AddDecl(Store store, GRStateManager& StateMgr,
                        const VarDecl* VD, Expr* Ex, 
                        RVal InitVal = UndefinedVal(), unsigned Count = 0);

  static inline VarBindingsTy GetVarBindings(Store store) {
    return VarBindingsTy(static_cast<const VarBindingsTy::TreeTy*>(store));
  }

  virtual void print(Store store, std::ostream& Out,
                     const char* nl, const char *sep);
  
  virtual RegionExtent getExtent(Region R);
  
  /// getBindings - Returns all bindings in the specified store that bind
  ///  to the specified symbolic value.
  virtual void getBindings(llvm::SmallVectorImpl<store::Binding>& bindings,
                           Store store, SymbolID Sym);
  
  /// BindingAsString - Returns a string representing the given binding.
  virtual std::string BindingAsString(store::Binding binding);
};  
  
} // end anonymous namespace


StoreManager* clang::CreateBasicStoreManager(GRStateManager& StMgr) {
  return new BasicStoreManager(StMgr);
}

RegionExtent BasicStoreManager::getExtent(Region R) {
  VarDecl* VD = (VarDecl*) R;
  QualType T = VD->getType();
  
  // FIXME: Add support for VLAs.  This may require passing in additional
  //  information, or tracking a different region type.
  if (!T.getTypePtr()->isConstantSizeType())
    return store::UnknownExtent();
  
  ASTContext& C = StMgr.getContext();
  assert (!T->isObjCInterfaceType()); // @interface not a possible VarDecl type.
  assert (T != C.VoidTy); // void not a possible VarDecl type.  
  return store::IntExtent(StMgr.getBasicVals().getValue(C.getTypeSize(T),
                                                        C.VoidPtrTy));
}


RVal BasicStoreManager::GetRVal(Store St, LVal LV, QualType T) {
  
  if (isa<UnknownVal>(LV))
    return UnknownVal();
  
  assert (!isa<UndefinedVal>(LV));
  
  switch (LV.getSubKind()) {

    case lval::DeclValKind: {      
      VarBindingsTy B(static_cast<const VarBindingsTy::TreeTy*>(St));      
      VarBindingsTy::data_type* T = B.lookup(cast<lval::DeclVal>(LV).getDecl());      
      return T ? *T : UnknownVal();
    }
      
    case lval::SymbolValKind:
      return UnknownVal();
      
    case lval::ConcreteIntKind:
      // Some clients may call GetRVal with such an option simply because
      // they are doing a quick scan through their LVals (potentially to
      // invalidate their bindings).  Just return Undefined.
      return UndefinedVal();
      
    case lval::ArrayOffsetKind:
    case lval::FieldOffsetKind:
      return UnknownVal();
      
    case lval::FuncValKind:
      return LV;
      
    case lval::StringLiteralValKind:
      // FIXME: Implement better support for fetching characters from strings.
      return UnknownVal();
      
    default:
      assert (false && "Invalid LVal.");
      break;
  }
  
  return UnknownVal();
}

Store BasicStoreManager::SetRVal(Store store, LVal LV, RVal V) {    
  switch (LV.getSubKind()) {      
    case lval::DeclValKind: {
      VarBindingsTy B = GetVarBindings(store);
      return V.isUnknown()
        ? VBFactory.Remove(B,cast<lval::DeclVal>(LV).getDecl()).getRoot()
        : VBFactory.Add(B, cast<lval::DeclVal>(LV).getDecl(), V).getRoot();
    }
    default:
      assert ("SetRVal for given LVal type not yet implemented.");
      return store;
  }
}

Store BasicStoreManager::Remove(Store store, LVal LV) {
  switch (LV.getSubKind()) {
    case lval::DeclValKind: {
      VarBindingsTy B = GetVarBindings(store);
      return VBFactory.Remove(B,cast<lval::DeclVal>(LV).getDecl()).getRoot();
    }
    default:
      assert ("Remove for given LVal type not yet implemented.");
      return store;
  }
}

Store BasicStoreManager::RemoveDeadBindings(Store store,
                                            Stmt* Loc,
                                            const LiveVariables& Liveness,
                                            DeclRootsTy& DRoots,
                                            LiveSymbolsTy& LSymbols,
                                            DeadSymbolsTy& DSymbols) {
  
  VarBindingsTy B = GetVarBindings(store);
  typedef RVal::symbol_iterator symbol_iterator;
  
  // Iterate over the variable bindings.
  for (VarBindingsTy::iterator I=B.begin(), E=B.end(); I!=E ; ++I)
    if (Liveness.isLive(Loc, I.getKey())) {
      DRoots.push_back(I.getKey());      
      RVal X = I.getData();
      
      for (symbol_iterator SI=X.symbol_begin(), SE=X.symbol_end(); SI!=SE; ++SI)
        LSymbols.insert(*SI);
    }
  
  // Scan for live variables and live symbols.
  llvm::SmallPtrSet<ValueDecl*, 10> Marked;
  
  while (!DRoots.empty()) {
    ValueDecl* V = DRoots.back();
    DRoots.pop_back();
    
    if (Marked.count(V))
      continue;
    
    Marked.insert(V);
    
    RVal X = GetRVal(store, lval::DeclVal(cast<VarDecl>(V)), QualType());      
    
    for (symbol_iterator SI=X.symbol_begin(), SE=X.symbol_end(); SI!=SE; ++SI)
      LSymbols.insert(*SI);
    
    if (!isa<lval::DeclVal>(X))
      continue;
    
    const lval::DeclVal& LVD = cast<lval::DeclVal>(X);
    DRoots.push_back(LVD.getDecl());
  }
  
  // Remove dead variable bindings.  
  for (VarBindingsTy::iterator I=B.begin(), E=B.end(); I!=E ; ++I)
    if (!Marked.count(I.getKey())) {
      store = Remove(store, lval::DeclVal(I.getKey()));
      RVal X = I.getData();
      
      for (symbol_iterator SI=X.symbol_begin(), SE=X.symbol_end(); SI!=SE; ++SI)
        if (!LSymbols.count(*SI)) DSymbols.insert(*SI);
    }

  return store;
}

Store BasicStoreManager::getInitialStore(GRStateManager& StateMgr) {
  // The LiveVariables information already has a compilation of all VarDecls
  // used in the function.  Iterate through this set, and "symbolicate"
  // any VarDecl whose value originally comes from outside the function.

  typedef LiveVariables::AnalysisDataTy LVDataTy;
  LVDataTy& D = StateMgr.getLiveVariables().getAnalysisData();

  Store St = VBFactory.GetEmptyMap().getRoot();

  for (LVDataTy::decl_iterator I=D.begin_decl(), E=D.end_decl(); I != E; ++I) {
    ScopedDecl* SD = const_cast<ScopedDecl*>(I->first);

    if (VarDecl* VD = dyn_cast<VarDecl>(SD)) {
      // Punt on static variables for now.
      if (VD->getStorageClass() == VarDecl::Static)
        continue;

      // Only handle pointers and integers for now.
      QualType T = VD->getType();
      if (LVal::IsLValType(T) || T->isIntegerType()) {
        // Initialize globals and parameters to symbolic values.
        // Initialize local variables to undefined.
        RVal X = (VD->hasGlobalStorage() || isa<ParmVarDecl>(VD) ||
                  isa<ImplicitParamDecl>(VD))
                 ? RVal::GetSymbolValue(StateMgr.getSymbolManager(), VD)
                 : UndefinedVal();

        St = SetRVal(St, lval::DeclVal(VD), X);
      }
    }
  }
  return St;
}

Store BasicStoreManager::AddDecl(Store store, GRStateManager& StateMgr,
                                 const VarDecl* VD, Expr* Ex,
                                 RVal InitVal, unsigned Count) {
  
  BasicValueFactory& BasicVals = StateMgr.getBasicVals();
  SymbolManager& SymMgr = StateMgr.getSymbolManager();
  
  // BasicStore does not model arrays and structs.
  if (VD->getType()->isArrayType() || VD->getType()->isStructureType())
    return store;

  if (VD->hasGlobalStorage()) {
    // Handle variables with global storage: extern, static, PrivateExtern.

    // FIXME:: static variables may have an initializer, but the second time a
    // function is called those values may not be current. Currently, a function
    // will not be called more than once.

    // Static global variables should not be visited here.
    assert(!(VD->getStorageClass() == VarDecl::Static &&
             VD->isFileVarDecl()));
    
    // Process static variables.
    if (VD->getStorageClass() == VarDecl::Static) {
      // C99: 6.7.8 Initialization
      //  If an object that has static storage duration is not initialized
      //  explicitly, then: 
      //   —if it has pointer type, it is initialized to a null pointer; 
      //   —if it has arithmetic type, it is initialized to (positive or 
      //     unsigned) zero;
      if (!Ex) {
        QualType T = VD->getType();
        if (LVal::IsLValType(T))
          store = SetRVal(store, lval::DeclVal(VD),
                          lval::ConcreteInt(BasicVals.getValue(0, T)));
        else if (T->isIntegerType())
          store = SetRVal(store, lval::DeclVal(VD),
                          nonlval::ConcreteInt(BasicVals.getValue(0, T)));
        else {
          // assert(0 && "ignore other types of variables");
        }
      } else {
        store = SetRVal(store, lval::DeclVal(VD), InitVal);
      }
    }
  } else {
    // Process local scalar variables.
    QualType T = VD->getType();
    if (LVal::IsLValType(T) || T->isIntegerType()) {
      RVal V = Ex ? InitVal : UndefinedVal();

      if (Ex && InitVal.isUnknown()) {
        // EXPERIMENTAL: "Conjured" symbols.
        SymbolID Sym = SymMgr.getConjuredSymbol(Ex, Count);

        V = LVal::IsLValType(Ex->getType())
          ? cast<RVal>(lval::SymbolVal(Sym))
          : cast<RVal>(nonlval::SymbolVal(Sym));
      }

      store = SetRVal(store, lval::DeclVal(VD), V);
    }
  }

  return store;
}

void BasicStoreManager::print(Store store, std::ostream& Out,
                              const char* nl, const char *sep) {
      
  VarBindingsTy B = GetVarBindings(store);
  Out << "Variables:" << nl;
  
  bool isFirst = true;
  
  for (VarBindingsTy::iterator I=B.begin(), E=B.end(); I != E; ++I) {
    if (isFirst) isFirst = false;
    else Out << nl;
    
    Out << ' ' << I.getKey()->getName() << " : ";
    I.getData().print(Out);
  }
}

void
BasicStoreManager::getBindings(llvm::SmallVectorImpl<store::Binding>& bindings,
                               Store store, SymbolID Sym) {
  
  VarBindingsTy VB((VarBindingsTy::TreeTy*) store);
  
  for (VarBindingsTy::iterator I=VB.begin(), E=VB.end(); I!=E; ++I) {
    if (const lval::SymbolVal* SV=dyn_cast<lval::SymbolVal>(&I->second)) {
      if (SV->getSymbol() == Sym) 
        bindings.push_back(I->first);
      
      continue;
    }
    
    if (const nonlval::SymbolVal* SV=dyn_cast<nonlval::SymbolVal>(&I->second)){
      if (SV->getSymbol() == Sym) 
        bindings.push_back(I->first);
    }
  }
}

std::string BasicStoreManager::BindingAsString(store::Binding binding) {
  // A binding is just an VarDecl*.
  return ((VarDecl*) binding)->getName();
}
