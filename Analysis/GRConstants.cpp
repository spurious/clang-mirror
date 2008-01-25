//===-- GRConstants.cpp - Simple, Path-Sens. Constant Prop. ------*- C++ -*-==//
//   
//                     The LLValM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//               Constant Propagation via Graph Reachability
//
//  This files defines a simple analysis that performs path-sensitive
//  constant propagation within a function.  An example use of this analysis
//  is to perform simple checks for NULL dereferences.
//
//===----------------------------------------------------------------------===//

#include "clang/Analysis/PathSensitive/GREngine.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ASTContext.h"
#include "clang/Analysis/Analyses/LiveVariables.h"

#include "llvm/Support/Casting.h"
#include "llvm/Support/DataTypes.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/ImmutableMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Streams.h"

#include <functional>

#ifndef NDEBUG
#include "llvm/Support/GraphWriter.h"
#include <sstream>
#endif

using namespace clang;
using llvm::dyn_cast;
using llvm::cast;
using llvm::APSInt;

//===----------------------------------------------------------------------===//
/// ValueKey - A variant smart pointer that wraps either a ValueDecl* or a
///  Stmt*.  Use cast<> or dyn_cast<> to get actual pointer type
//===----------------------------------------------------------------------===//
namespace {
  
class VISIBILITY_HIDDEN ValueKey {
  uintptr_t Raw;
public:
  enum  Kind { IsSubExpr=0x0, IsBlkExpr=0x1, IsDecl=0x2, Flags=0x3 };
  inline void* getPtr() const { return reinterpret_cast<void*>(Raw & ~Flags); }
  inline Kind getKind() const { return (Kind) (Raw & Flags); }
  
  ValueKey(const ValueDecl* VD)
    : Raw(reinterpret_cast<uintptr_t>(VD) | IsDecl) {}

  ValueKey(Stmt* S, bool isBlkExpr = false) 
    : Raw(reinterpret_cast<uintptr_t>(S) | isBlkExpr ? IsBlkExpr : IsSubExpr) {}
  
  bool isSubExpr() const { return getKind() == IsSubExpr; }
  bool isDecl() const { return getKind() == IsDecl; }
  
  inline void Profile(llvm::FoldingSetNodeID& ID) const {
    ID.AddPointer(getPtr());
    ID.AddInteger((unsigned) getKind());
  }
  
  inline bool operator==(const ValueKey& X) const {
    return getPtr() == X.getPtr();
  }
  
  inline bool operator!=(const ValueKey& X) const {
    return !operator==(X);
  }
  
  inline bool operator<(const ValueKey& X) const { 
    Kind k = getKind(), Xk = X.getKind();
    
    if (k == IsDecl) {
      if (Xk != IsDecl)
        return false;
    }
    else {
      if (Xk == IsDecl)
        return true;
    }

    return getPtr() < X.getPtr();    
  }
};
} // end anonymous namespace

// Machinery to get cast<> and dyn_cast<> working with ValueKey.
namespace llvm {
  template<> inline bool isa<ValueDecl,ValueKey>(const ValueKey& V) {
    return V.getKind() == ValueKey::IsDecl;
  }
  template<> inline bool isa<Stmt,ValueKey>(const ValueKey& V) {
    return ((unsigned) V.getKind()) != ValueKey::IsDecl;
  }
  template<> struct VISIBILITY_HIDDEN cast_retty_impl<ValueDecl,ValueKey> {
    typedef const ValueDecl* ret_type;
  };
  template<> struct VISIBILITY_HIDDEN cast_retty_impl<Stmt,ValueKey> {
    typedef const Stmt* ret_type;
  };
  template<> struct VISIBILITY_HIDDEN simplify_type<ValueKey> {
    typedef void* SimpleType;
    static inline SimpleType getSimplifiedValue(const ValueKey &V) {
      return V.getPtr();
    }
  };
} // end llvm namespace

//===----------------------------------------------------------------------===//
// ValueManager.
//===----------------------------------------------------------------------===//

namespace {
  
typedef llvm::ImmutableSet<APSInt > APSIntSetTy;

  
class VISIBILITY_HIDDEN ValueManager {
  APSIntSetTy::Factory APSIntSetFactory;
  ASTContext* Ctx;
  
public:
  ValueManager() {}
  ~ValueManager() {}
  
  void setContext(ASTContext* ctx) { Ctx = ctx; }
  ASTContext* getContext() const { return Ctx; }
  
  APSIntSetTy GetEmptyAPSIntSet() {
    return APSIntSetFactory.GetEmptySet();
  }
  
  APSIntSetTy AddToSet(const APSIntSetTy& Set, const APSInt& Val) {
    return APSIntSetFactory.Add(Set, Val);
  }
};
} // end anonymous namespace

template <typename OpTy>
static inline APSIntSetTy APSIntSetOp(ValueManager& ValMgr,
                                      APSIntSetTy S1, APSIntSetTy S2,
                                      OpTy Op) {
  
  APSIntSetTy M = ValMgr.GetEmptyAPSIntSet();
  
  for (APSIntSetTy::iterator I1=S1.begin(), E1=S2.end(); I1!=E1; ++I1)
    for (APSIntSetTy::iterator I2=S2.begin(), E2=S2.end(); I2!=E2; ++I2)
      M = ValMgr.AddToSet(M, Op(*I1,*I2));
  
  return M;
}

//===----------------------------------------------------------------------===//
// Expression Values.
//===----------------------------------------------------------------------===//

namespace {
  
class VISIBILITY_HIDDEN ExprValue {
public:
  enum BaseKind { LValueKind=0x1, RValueKind=0x2, InvalidKind=0x0 };
    
private:
  void* Data;
  unsigned Kind;
    
protected:
  ExprValue(void* d, bool isRValue, unsigned ValKind)
    : Data(d),
      Kind((isRValue ? RValueKind : LValueKind) | (ValKind << 2)) {}
  
  ExprValue() : Data(NULL), Kind(0) {}    
  
  void* getRawPtr() const { return Data; }   
  
public:
  ~ExprValue() {};

  ExprValue EvalCast(ValueManager& ValMgr, Expr* CastExpr) const;
  
  unsigned getRawKind() const { return Kind; }
  BaseKind getBaseKind() const { return (BaseKind) (Kind & 0x3); }
  unsigned getSubKind() const { return (Kind & ~0x3) >> 2; }   
  
  void Profile(llvm::FoldingSetNodeID& ID) const {
    ID.AddInteger((unsigned) getRawKind());
    ID.AddPointer(Data);
  }
  
  bool operator==(const ExprValue& RHS) const {
    return getRawKind() == RHS.getRawKind() && Data == RHS.Data;
  }

  inline bool isValid() const { return getRawKind() != InvalidKind; }
  inline bool isInvalid() const { return getRawKind() == InvalidKind; }
  
  
  void print(std::ostream& OS) const;
  void print() const { print(*llvm::cerr.stream()); }

  // Implement isa<T> support.
  static inline bool classof(const ExprValue*) { return true; }
};

class VISIBILITY_HIDDEN InvalidValue : public ExprValue {
public:
  InvalidValue() {}
  
  static inline bool classof(const ExprValue* V) {
    return V->getBaseKind() == InvalidKind;
  }  
};

class VISIBILITY_HIDDEN LValue : public ExprValue {
protected:
  LValue(unsigned SubKind, void* D) : ExprValue(D, false, SubKind) {}
  
public:  
  // Implement isa<T> support.
  static inline bool classof(const ExprValue* V) {
    return V->getBaseKind() == LValueKind;
  }
};

class VISIBILITY_HIDDEN RValue : public ExprValue {
protected:
  RValue(unsigned SubKind, void* d) : ExprValue(d, true, SubKind) {}
  
public:
  void print(std::ostream& Out) const;
  
  RValue EvalAdd(ValueManager& ValMgr, const RValue& RHS) const;
  RValue EvalSub(ValueManager& ValMgr, const RValue& RHS) const;
  RValue EvalMul(ValueManager& ValMgr, const RValue& RHS) const;
  RValue EvalDiv(ValueManager& ValMgr, const RValue& RHS) const;
  RValue EvalMinus(ValueManager& ValMgr, UnaryOperator* U) const;
  
  static RValue GetRValue(ValueManager& ValMgr, const APSInt& V);
  static RValue GetRValue(ValueManager& ValMgr, IntegerLiteral* I);
  
  // Implement isa<T> support.
  static inline bool classof(const ExprValue* V) {
    return V->getBaseKind() == RValueKind;
  }
};
    
} // end anonymous namespace

//===----------------------------------------------------------------------===//
// "R-Values": Interface.
//===----------------------------------------------------------------------===//

namespace {
  
enum { RValEqualityORSetKind,
       RValInequalityANDSetKind,
       NumRValueKind };
  
class VISIBILITY_HIDDEN RValEqualityORSet : public RValue {
public:
  RValEqualityORSet(const APSIntSetTy& S)
    : RValue(RValEqualityORSetKind, S.getRoot()) {}
  
  APSIntSetTy GetValues() const {
    return APSIntSetTy(reinterpret_cast<APSIntSetTy::TreeTy*>(getRawPtr()));
  }
  
  RValEqualityORSet
  EvalAdd(ValueManager& ValMgr, const RValEqualityORSet& V) const {
    return APSIntSetOp(ValMgr, GetValues(), V.GetValues(),
                       std::plus<APSInt>());
  }
  
  RValEqualityORSet
  EvalSub(ValueManager& ValMgr, const RValEqualityORSet& V) const {
    return APSIntSetOp(ValMgr, GetValues(), V.GetValues(),
                       std::minus<APSInt>());
  }
  
  RValEqualityORSet
  EvalMul(ValueManager& ValMgr, const RValEqualityORSet& V) const {
    return APSIntSetOp(ValMgr, GetValues(), V.GetValues(),
                       std::multiplies<APSInt>());
  }
  
  RValEqualityORSet
  EvalDiv(ValueManager& ValMgr, const RValEqualityORSet& V) const {
    return APSIntSetOp(ValMgr, GetValues(), V.GetValues(),
                       std::divides<APSInt>());
  }

  RValEqualityORSet
  EvalCast(ValueManager& ValMgr, Expr* CastExpr) const;
  
  RValEqualityORSet
  EvalMinus(ValueManager& ValMgr, UnaryOperator* U) const;
  
  // Implement isa<T> support.
  static inline bool classof(const ExprValue* V) {
    return V->getSubKind() == RValEqualityORSetKind;
  }
};
  
class VISIBILITY_HIDDEN RValInequalityANDSet : public RValue {
public:
  RValInequalityANDSet(const APSIntSetTy& S)
  : RValue(RValInequalityANDSetKind, S.getRoot()) {}
  
  APSIntSetTy GetValues() const {
    return APSIntSetTy(reinterpret_cast<APSIntSetTy::TreeTy*>(getRawPtr()));
  }
  
  RValInequalityANDSet
  EvalAdd(ValueManager& ValMgr, const RValInequalityANDSet& V) const;
  
  RValInequalityANDSet
  EvalSub(ValueManager& ValMgr, const RValInequalityANDSet& V) const;
  
  RValInequalityANDSet
  EvalMul(ValueManager& ValMgr, const RValInequalityANDSet& V) const;
  
  RValInequalityANDSet
  EvalDiv(ValueManager& ValMgr, const RValInequalityANDSet& V) const;
  
  RValInequalityANDSet
  EvalCast(ValueManager& ValMgr, Expr* CastExpr) const;
  
  RValInequalityANDSet
  EvalMinus(ValueManager& ValMgr, UnaryOperator* U) const;
  
  // Implement isa<T> support.
  static inline bool classof(const ExprValue* V) {
    return V->getSubKind() == RValInequalityANDSetKind;
  }
};
  
} // end anonymous namespace

//===----------------------------------------------------------------------===//
// Transfer functions: Casts.
//===----------------------------------------------------------------------===//

ExprValue ExprValue::EvalCast(ValueManager& ValMgr, Expr* CastExpr) const {
  switch (getSubKind()) {
    case RValEqualityORSetKind:
      return cast<RValEqualityORSet>(this)->EvalCast(ValMgr, CastExpr);
    default:
      return InvalidValue();
  }
}

RValEqualityORSet
RValEqualityORSet::EvalCast(ValueManager& ValMgr, Expr* CastExpr) const {
  QualType T = CastExpr->getType();
  assert (T->isIntegerType());
  
  APSIntSetTy S1 = GetValues();  
  APSIntSetTy S2 = ValMgr.GetEmptyAPSIntSet();
  
  for (APSIntSetTy::iterator I=S1.begin(), E=S1.end(); I!=E; ++I) {
    APSInt X = *I;
    X.setIsSigned(T->isSignedIntegerType());
    X.extOrTrunc(ValMgr.getContext()->getTypeSize(T,CastExpr->getLocStart()));    
    S2 = ValMgr.AddToSet(S2, X);
  }
  
  return S2;
}

//===----------------------------------------------------------------------===//
// Transfer functions: Unary Operations over R-Values.
//===----------------------------------------------------------------------===//

RValue RValue::EvalMinus(ValueManager& ValMgr, UnaryOperator* U) const {
  switch (getSubKind()) {
    case RValEqualityORSetKind:
      return cast<RValEqualityORSet>(this)->EvalMinus(ValMgr, U);
    default:
      return cast<RValue>(InvalidValue());
  }
}

RValEqualityORSet
RValEqualityORSet::EvalMinus(ValueManager& ValMgr, UnaryOperator* U) const{
  
  assert (U->getType() == U->getSubExpr()->getType());  
  assert (U->getType()->isIntegerType());
  
  APSIntSetTy S1 = GetValues();  
  APSIntSetTy S2 = ValMgr.GetEmptyAPSIntSet();
  
  for (APSIntSetTy::iterator I=S1.begin(), E=S1.end(); I!=E; ++I) {
    assert ((*I).isSigned());
    
    // FIXME: Shouldn't operator- on APSInt return an APSInt with the proper
    //  sign?
    APSInt X(-(*I));
    X.setIsSigned(true);
    
    S2 = ValMgr.AddToSet(S2, X);
  }
  
  return S2;
}

//===----------------------------------------------------------------------===//
// Transfer functions: Binary Operations over R-Values.
//===----------------------------------------------------------------------===//

#define RVALUE_DISPATCH_CASE(k1,k2,Op)\
case (k1##Kind*NumRValueKind+k2##Kind):\
  return cast<k1>(*this).Eval##Op(ValMgr,cast<k2>(RHS));

#define RVALUE_DISPATCH(Op)\
switch (getSubKind()*NumRValueKind+RHS.getSubKind()){\
  RVALUE_DISPATCH_CASE(RValEqualityORSet,RValEqualityORSet,Op)\
  default:\
    assert (!isValid() || !RHS.isValid() && "Missing case.");\
    break;\
}\
return cast<RValue>(InvalidValue());

RValue RValue::EvalAdd(ValueManager& ValMgr, const RValue& RHS) const {
  RVALUE_DISPATCH(Add)
}

RValue RValue::EvalSub(ValueManager& ValMgr, const RValue& RHS) const {
  RVALUE_DISPATCH(Sub)
}

RValue RValue::EvalMul(ValueManager& ValMgr, const RValue& RHS) const {
  RVALUE_DISPATCH(Mul)
}

RValue RValue::EvalDiv(ValueManager& ValMgr, const RValue& RHS) const {
  RVALUE_DISPATCH(Div)
}

#undef RVALUE_DISPATCH_CASE
#undef RVALUE_DISPATCH


RValue RValue::GetRValue(ValueManager& ValMgr, const APSInt& V) {
  return RValEqualityORSet(ValMgr.AddToSet(ValMgr.GetEmptyAPSIntSet(), V));
}

RValue RValue::GetRValue(ValueManager& ValMgr, IntegerLiteral* I) {
  return GetRValue(ValMgr,
                   APSInt(I->getValue(),I->getType()->isUnsignedIntegerType()));
}

//===----------------------------------------------------------------------===//
// "L-Values".
//===----------------------------------------------------------------------===//

namespace {

enum { LValueDeclKind, MaxLValueKind };

class VISIBILITY_HIDDEN LValueDecl : public LValue {
public:
  LValueDecl(const ValueDecl* vd) 
    : LValue(LValueDeclKind,const_cast<ValueDecl*>(vd)) {}
  
  ValueDecl* getDecl() const {
    return static_cast<ValueDecl*>(getRawPtr());
  }
  
  // Implement isa<T> support.
  static inline bool classof(const ExprValue* V) {
    return V->getSubKind() == LValueDeclKind;
  }
};  
} // end anonymous namespace

//===----------------------------------------------------------------------===//
// Pretty-Printing.
//===----------------------------------------------------------------------===//

void ExprValue::print(std::ostream& Out) const {
  switch (getBaseKind()) {
    case InvalidKind:
      Out << "Invalid";
      break;
      
    case RValueKind:
      cast<RValue>(this)->print(Out);
      break;
      
    case LValueKind:
      assert (false && "FIXME: LValue printing not implemented.");  
      break;
      
    default:
      assert (false && "Invalid ExprValue.");
  }
}

void RValue::print(std::ostream& Out) const {
  switch (getSubKind()) {  
    case RValEqualityORSetKind: {
      APSIntSetTy S = cast<RValEqualityORSet>(this)->GetValues();
      bool first = true;

      for (APSIntSetTy::iterator I=S.begin(), E=S.end(); I!=E; ++I) {
        if (first) first = false;
        else Out << " | ";
        
        Out << (*I).toString();
      }
      
      break;
    }
      
    default:
      assert (false && "Pretty-printed not implemented for this RValue.");
      break;
  }
}

//===----------------------------------------------------------------------===//
// ValueMapTy - A ImmutableMap type Stmt*/Decl* to ExprValues.
//===----------------------------------------------------------------------===//

typedef llvm::ImmutableMap<ValueKey,ExprValue> ValueMapTy;

namespace clang {
  template<>
  struct VISIBILITY_HIDDEN GRTrait<ValueMapTy> {
    static inline void* toPtr(ValueMapTy M) {
      return reinterpret_cast<void*>(M.getRoot());
    }  
    static inline ValueMapTy toState(void* P) {
      return ValueMapTy(static_cast<ValueMapTy::TreeTy*>(P));
    }
  };
}

//===----------------------------------------------------------------------===//
// The Checker!
//===----------------------------------------------------------------------===//

namespace {
  
class VISIBILITY_HIDDEN GRConstants {
    
public:
  typedef ValueMapTy StateTy;
  typedef GRNodeBuilder<GRConstants> NodeBuilder;
  typedef ExplodedNode<StateTy> NodeTy;
  
  class NodeSet {
    typedef llvm::SmallVector<NodeTy*,3> ImplTy;
    ImplTy Impl;
  public:
    
    NodeSet() {}
    NodeSet(NodeTy* N) { assert (N && !N->isInfeasible()); Impl.push_back(N); }
    
    void Add(NodeTy* N) { if (N && !N->isInfeasible()) Impl.push_back(N); }
    
    typedef ImplTy::iterator       iterator;
    typedef ImplTy::const_iterator const_iterator;
        
    unsigned size() const { return Impl.size(); }
    bool empty() const { return Impl.empty(); }
    
    iterator begin() { return Impl.begin(); }
    iterator end()   { return Impl.end(); }

    const_iterator begin() const { return Impl.begin(); }
    const_iterator end() const { return Impl.end(); }
  };
                                                              
protected:
  /// Liveness - live-variables information the ValueDecl* and block-level
  ///  Expr* in the CFG.  Used to prune out dead state.
  LiveVariables* Liveness;

  /// Builder - The current GRNodeBuilder which is used when building the nodes
  ///  for a given statement.
  NodeBuilder* Builder;

  /// StateMgr - Object that manages the data for all created states.
  ValueMapTy::Factory StateMgr;
  
  /// ValueMgr - Object that manages the data for all created ExprValues.
  ValueManager ValMgr;
  
  /// cfg - the current CFG.
  CFG* cfg;
  
  /// StmtEntryNode - The immediate predecessor node.
  NodeTy* StmtEntryNode;
  
  /// CurrentStmt - The current block-level statement.
  Stmt* CurrentStmt;
  
  bool StateCleaned;
  
  ASTContext* getContext() const { return ValMgr.getContext(); }
  
public:
  GRConstants() : Liveness(NULL), Builder(NULL), cfg(NULL),
    StmtEntryNode(NULL), CurrentStmt(NULL) {}
    
  ~GRConstants() { delete Liveness; }
  
  /// getCFG - Returns the CFG associated with this analysis.
  CFG& getCFG() { assert (cfg); return *cfg; }
      
  /// Initialize - Initialize the checker's state based on the specified
  ///  CFG.  This results in liveness information being computed for
  ///  each block-level statement in the CFG.
  void Initialize(CFG& c, ASTContext& ctx) {
    cfg = &c;
    ValMgr.setContext(&ctx);
    Liveness = new LiveVariables(c);
    Liveness->runOnCFG(c);
    Liveness->runOnAllBlocks(c, NULL, true);
  }
  
  /// getInitialState - Return the initial state used for the root vertex
  ///  in the ExplodedGraph.
  StateTy getInitialState() {
    return StateMgr.GetEmptyMap();
  }

  /// ProcessStmt - Called by GREngine. Used to generate new successor
  ///  nodes by processing the 'effects' of a block-level statement.
  void ProcessStmt(Stmt* S, NodeBuilder& builder);    

  /// RemoveDeadBindings - Return a new state that is the same as 'M' except
  ///  that all subexpression mappings are removed and that any
  ///  block-level expressions that are not live at 'S' also have their
  ///  mappings removed.
  StateTy RemoveDeadBindings(Stmt* S, StateTy M);

  StateTy SetValue(StateTy St, Stmt* S, const ExprValue& V);  

  StateTy SetValue(StateTy St, const Stmt* S, const ExprValue& V) {
    return SetValue(St, const_cast<Stmt*>(S), V);
  }
  
  StateTy SetValue(StateTy St, const LValue& LV, const ExprValue& V);
  
  ExprValue GetValue(const StateTy& St, Stmt* S);  
  inline ExprValue GetValue(const StateTy& St, const Stmt* S) {
    return GetValue(St, const_cast<Stmt*>(S));
  }
  
  ExprValue GetValue(const StateTy& St, const LValue& LV);
  LValue GetLValue(const StateTy& St, Stmt* S);
  
  void Nodify(NodeSet& Dst, Stmt* S, NodeTy* Pred, StateTy St);
  
  /// Visit - Transfer function logic for all statements.  Dispatches to
  ///  other functions that handle specific kinds of statements.
  void Visit(Stmt* S, NodeTy* Pred, NodeSet& Dst);

  /// VisitCast - Transfer function logic for all casts (implicit and explicit).
  void VisitCast(Expr* CastE, Expr* E, NodeTy* Pred, NodeSet& Dst);
  
  /// VisitUnaryOperator - Transfer function logic for unary operators.
  void VisitUnaryOperator(UnaryOperator* B, NodeTy* Pred, NodeSet& Dst);
  
  /// VisitBinaryOperator - Transfer function logic for binary operators.
  void VisitBinaryOperator(BinaryOperator* B, NodeTy* Pred, NodeSet& Dst);
  
  /// VisitDeclStmt - Transfer function logic for DeclStmts.
  void VisitDeclStmt(DeclStmt* DS, NodeTy* Pred, NodeSet& Dst);
};
} // end anonymous namespace


void GRConstants::ProcessStmt(Stmt* S, NodeBuilder& builder) {
  Builder = &builder;

  StmtEntryNode = builder.getLastNode();
  CurrentStmt = S;
  NodeSet Dst;
  StateCleaned = false;

  Visit(S, StmtEntryNode, Dst);

  // If no nodes were generated, generate a new node that has all the
  // dead mappings removed.
  if (Dst.size() == 1 && *Dst.begin() == StmtEntryNode) {
    StateTy St = RemoveDeadBindings(S, StmtEntryNode->getState());
    builder.generateNode(S, St, StmtEntryNode);
  }
  
  CurrentStmt = NULL;
  StmtEntryNode = NULL;
  Builder = NULL;
}


ExprValue GRConstants::GetValue(const StateTy& St, const LValue& LV) {
  switch (LV.getSubKind()) {
    case LValueDeclKind: {
      StateTy::TreeTy* T = St.SlimFind(cast<LValueDecl>(LV).getDecl()); 
      return T ? T->getValue().second : InvalidValue();
    }
    default:
      assert (false && "Invalid LValue.");
      break;
  }
  
  return InvalidValue();
}
  
ExprValue GRConstants::GetValue(const StateTy& St, Stmt* S) {
  for (;;) {
    switch (S->getStmtClass()) {
      case Stmt::ParenExprClass:
        S = cast<ParenExpr>(S)->getSubExpr();
        continue;
        
      case Stmt::DeclRefExprClass:
        return GetValue(St, LValueDecl(cast<DeclRefExpr>(S)->getDecl()));

      case Stmt::IntegerLiteralClass:
        return RValue::GetRValue(ValMgr, cast<IntegerLiteral>(S));
      
      case Stmt::ImplicitCastExprClass: {
        ImplicitCastExpr* C = cast<ImplicitCastExpr>(S);
        if (C->getType() == C->getSubExpr()->getType()) {
          S = C->getSubExpr();
          continue;
        }
        break;
      }
        
      case Stmt::CastExprClass: {
        CastExpr* C = cast<CastExpr>(S);
        if (C->getType() == C->getSubExpr()->getType()) {
          S = C->getSubExpr();
          continue;
        }
        break;
      }

      default:
        break;
    };
    
    break;
  }
  
  StateTy::TreeTy* T = St.SlimFind(S);
    
  return T ? T->getValue().second : InvalidValue();
}

LValue GRConstants::GetLValue(const StateTy& St, Stmt* S) {
  if (Expr* E = dyn_cast<Expr>(S))
    S = E->IgnoreParens();
  
  if (DeclRefExpr* DR = dyn_cast<DeclRefExpr>(S))
    return LValueDecl(DR->getDecl());
    
  return cast<LValue>(GetValue(St, S));
}

GRConstants::StateTy GRConstants::SetValue(StateTy St, Stmt* S,
                                           const ExprValue& V) {
  
  if (!StateCleaned) {
    St = RemoveDeadBindings(CurrentStmt, St);
    StateCleaned = true;
  }
  
  bool isBlkExpr = false;
  
  if (S == CurrentStmt) {
    isBlkExpr = getCFG().isBlkExpr(S);

    if (!isBlkExpr)
      return St;
  }
  
  return V.isValid() ? StateMgr.Add(St, ValueKey(S,isBlkExpr), V)
                     : St;
}

GRConstants::StateTy GRConstants::SetValue(StateTy St, const LValue& LV,
                                           const ExprValue& V) {
  if (!LV.isValid())
    return St;
  
  if (!StateCleaned) {
    St = RemoveDeadBindings(CurrentStmt, St);
    StateCleaned = true;
  }

  switch (LV.getSubKind()) {
    case LValueDeclKind:        
      return V.isValid() ? StateMgr.Add(St, cast<LValueDecl>(LV).getDecl(), V)
                         : StateMgr.Remove(St, cast<LValueDecl>(LV).getDecl());
      
    default:
      assert ("SetValue for given LValue type not yet implemented.");
      return St;
  }
}

GRConstants::StateTy GRConstants::RemoveDeadBindings(Stmt* Loc, StateTy M) {
  // Note: in the code below, we can assign a new map to M since the
  //  iterators are iterating over the tree of the *original* map.
  StateTy::iterator I = M.begin(), E = M.end();

  // Remove old bindings for subexpressions and "dead" block-level expressions.
  for (; I!=E && !I.getKey().isDecl(); ++I) {
    if (I.getKey().isSubExpr() || !Liveness->isLive(Loc,cast<Stmt>(I.getKey())))
      M = StateMgr.Remove(M, I.getKey());
  }

  // Remove bindings for "dead" decls.
  for (; I!=E ; ++I) {
    assert (I.getKey().isDecl());
    
    if (VarDecl* V = dyn_cast<VarDecl>(cast<ValueDecl>(I.getKey())))
      if (!Liveness->isLive(Loc, V))
        M = StateMgr.Remove(M, I.getKey());
  }

  return M;
}

void GRConstants::Nodify(NodeSet& Dst, Stmt* S, GRConstants::NodeTy* Pred, 
                         GRConstants::StateTy St) {
 
  // If the state hasn't changed, don't generate a new node.
  if (St == Pred->getState())
    return;
  
  Dst.Add(Builder->generateNode(S, St, Pred));
}

void GRConstants::VisitCast(Expr* CastE, Expr* E, GRConstants::NodeTy* Pred,
                            GRConstants::NodeSet& Dst) {
  
  QualType T = CastE->getType();

  // Check for redundant casts.
  if (E->getType() == T) {
    Dst.Add(Pred);
    return;
  }
  
  NodeSet S1;
  Visit(E, Pred, S1);
  
  for (NodeSet::iterator I1=S1.begin(), E1=S1.end(); I1 != E1; ++I1) {
    NodeTy* N = *I1;
    StateTy St = N->getState();
    const ExprValue& V = GetValue(St, E);
    Nodify(Dst, CastE, N, SetValue(St, CastE, V.EvalCast(ValMgr, CastE)));
  }
}

void GRConstants::VisitDeclStmt(DeclStmt* DS, GRConstants::NodeTy* Pred,
                                GRConstants::NodeSet& Dst) {
  
  StateTy St = Pred->getState();
  
  for (const ScopedDecl* D = DS->getDecl(); D; D = D->getNextDeclarator())
    if (const VarDecl* VD = dyn_cast<VarDecl>(D))
      St = SetValue(St, LValueDecl(VD), GetValue(St, VD->getInit()));

  Nodify(Dst, DS, Pred, St);
  
  if (Dst.empty())
    Dst.Add(Pred);  
}

void GRConstants::VisitUnaryOperator(UnaryOperator* U,
                                     GRConstants::NodeTy* Pred,
                                     GRConstants::NodeSet& Dst) {
  NodeSet S1;
  Visit(U->getSubExpr(), Pred, S1);
    
  for (NodeSet::iterator I1=S1.begin(), E1=S1.end(); I1 != E1; ++I1) {
    NodeTy* N1 = *I1;
    StateTy St = N1->getState();
    
    switch (U->getOpcode()) {
      case UnaryOperator::PostInc: {
        const LValue& L1 = GetLValue(St, U->getSubExpr());
        RValue R1 = cast<RValue>(GetValue(St, L1));

        QualType T = U->getType();
        unsigned bits = getContext()->getTypeSize(T, U->getLocStart());
        APSInt One(llvm::APInt(bits, 1), T->isUnsignedIntegerType());
        RValue R2 = RValue::GetRValue(ValMgr, One);
        
        RValue Result = R1.EvalAdd(ValMgr, R2);
        Nodify(Dst, U, N1, SetValue(SetValue(St, U, R1), L1, Result));
        break;
      }
        
      case UnaryOperator::PostDec: {
        const LValue& L1 = GetLValue(St, U->getSubExpr());
        RValue R1 = cast<RValue>(GetValue(St, L1));
        
        QualType T = U->getType();
        unsigned bits = getContext()->getTypeSize(T, U->getLocStart());
        APSInt One(llvm::APInt(bits, 1), T->isUnsignedIntegerType());
        RValue R2 = RValue::GetRValue(ValMgr, One);
        
        RValue Result = R1.EvalSub(ValMgr, R2);
        Nodify(Dst, U, N1, SetValue(SetValue(St, U, R1), L1, Result));
        break;
      }
        
      case UnaryOperator::PreInc: {
        const LValue& L1 = GetLValue(St, U->getSubExpr());
        RValue R1 = cast<RValue>(GetValue(St, L1));
        
        QualType T = U->getType();
        unsigned bits = getContext()->getTypeSize(T, U->getLocStart());
        APSInt One(llvm::APInt(bits, 1), T->isUnsignedIntegerType());
        RValue R2 = RValue::GetRValue(ValMgr, One);        
        
        RValue Result = R1.EvalAdd(ValMgr, R2);
        Nodify(Dst, U, N1, SetValue(SetValue(St, U, Result), L1, Result));
        break;
      }
        
      case UnaryOperator::PreDec: {
        const LValue& L1 = GetLValue(St, U->getSubExpr());
        RValue R1 = cast<RValue>(GetValue(St, L1));
        
        QualType T = U->getType();
        unsigned bits = getContext()->getTypeSize(T, U->getLocStart());
        APSInt One(llvm::APInt(bits, 1), T->isUnsignedIntegerType());
        RValue R2 = RValue::GetRValue(ValMgr, One);       
        
        RValue Result = R1.EvalSub(ValMgr, R2);
        Nodify(Dst, U, N1, SetValue(SetValue(St, U, Result), L1, Result));
        break;
      }
        
      case UnaryOperator::Minus: {
        const RValue& R1 = cast<RValue>(GetValue(St, U->getSubExpr()));
        Nodify(Dst, U, N1, SetValue(St, U, R1.EvalMinus(ValMgr, U)));
        break;
      }
        
      default: ;
        assert (false && "Not implemented.");
    }    
  }
}

void GRConstants::VisitBinaryOperator(BinaryOperator* B,
                                      GRConstants::NodeTy* Pred,
                                      GRConstants::NodeSet& Dst) {
  NodeSet S1;
  Visit(B->getLHS(), Pred, S1);

  for (NodeSet::iterator I1=S1.begin(), E1=S1.end(); I1 != E1; ++I1) {
    NodeTy* N1 = *I1;
    
    // When getting the value for the LHS, check if we are in an assignment.
    // In such cases, we want to (initially) treat the LHS as an LValue,
    // so we use GetLValue instead of GetValue so that DeclRefExpr's are
    // evaluated to LValueDecl's instead of to an RValue.
    const ExprValue& V1 = 
      B->isAssignmentOp() ? GetLValue(N1->getState(), B->getLHS())
                          : GetValue(N1->getState(), B->getLHS());
    
    NodeSet S2;
    Visit(B->getRHS(), N1, S2);
  
    for (NodeSet::iterator I2=S2.begin(), E2=S2.end(); I2 != E2; ++I2) {
      NodeTy* N2 = *I2;
      StateTy St = N2->getState();
      const ExprValue& V2 = GetValue(St, B->getRHS());

      switch (B->getOpcode()) {
        case BinaryOperator::Add: {
          const RValue& R1 = cast<RValue>(V1);
          const RValue& R2 = cast<RValue>(V2);
          
          Nodify(Dst, B, N2, SetValue(St, B, R1.EvalAdd(ValMgr, R2)));
          break;
        }

        case BinaryOperator::Sub: {
          const RValue& R1 = cast<RValue>(V1);
          const RValue& R2 = cast<RValue>(V2);
	        Nodify(Dst, B, N2, SetValue(St, B, R1.EvalSub(ValMgr, R2)));
          break;
        }
          
        case BinaryOperator::Mul: {
          const RValue& R1 = cast<RValue>(V1);
          const RValue& R2 = cast<RValue>(V2);
	        Nodify(Dst, B, N2, SetValue(St, B, R1.EvalMul(ValMgr, R2)));
          break;
        }
          
        case BinaryOperator::Div: {
          const RValue& R1 = cast<RValue>(V1);
          const RValue& R2 = cast<RValue>(V2);
	        Nodify(Dst, B, N2, SetValue(St, B, R1.EvalDiv(ValMgr, R2)));
          break;
        }
          
        case BinaryOperator::Assign: {
          const LValue& L1 = cast<LValue>(V1);
          const RValue& R2 = cast<RValue>(V2);
          Nodify(Dst, B, N2, SetValue(SetValue(St, B, R2), L1, R2));
          break;
        }
          
        case BinaryOperator::AddAssign: {
          const LValue& L1 = cast<LValue>(V1);
          RValue R1 = cast<RValue>(GetValue(N1->getState(), L1));
          RValue Result = R1.EvalAdd(ValMgr, cast<RValue>(V2));
          Nodify(Dst, B, N2, SetValue(SetValue(St, B, Result), L1, Result));
          break;
        }
          
        case BinaryOperator::SubAssign: {
          const LValue& L1 = cast<LValue>(V1);
          RValue R1 = cast<RValue>(GetValue(N1->getState(), L1));
          RValue Result = R1.EvalSub(ValMgr, cast<RValue>(V2));
          Nodify(Dst, B, N2, SetValue(SetValue(St, B, Result), L1, Result));
          break;
        }
          
        case BinaryOperator::MulAssign: {
          const LValue& L1 = cast<LValue>(V1);
          RValue R1 = cast<RValue>(GetValue(N1->getState(), L1));
          RValue Result = R1.EvalMul(ValMgr, cast<RValue>(V2));
          Nodify(Dst, B, N2, SetValue(SetValue(St, B, Result), L1, Result));
          break;
        }

        default: 
          Dst.Add(N2);
          break;
      }
    }
  }
}


void GRConstants::Visit(Stmt* S, GRConstants::NodeTy* Pred,
                        GRConstants::NodeSet& Dst) {

  // FIXME: add metadata to the CFG so that we can disable
  //  this check when we KNOW that there is no block-level subexpression.
  //  The motivation is that this check requires a hashtable lookup.

  if (S != CurrentStmt && getCFG().isBlkExpr(S)) {
    Dst.Add(Pred);
    return;
  }

  switch (S->getStmtClass()) {
    case Stmt::BinaryOperatorClass:
    case Stmt::CompoundAssignOperatorClass:
      VisitBinaryOperator(cast<BinaryOperator>(S), Pred, Dst);
      break;
      
    case Stmt::UnaryOperatorClass:
      VisitUnaryOperator(cast<UnaryOperator>(S), Pred, Dst);
      break;
      
    case Stmt::ParenExprClass:
      Visit(cast<ParenExpr>(S)->getSubExpr(), Pred, Dst);
      break;
      
    case Stmt::ImplicitCastExprClass: {
      ImplicitCastExpr* C = cast<ImplicitCastExpr>(S);
      VisitCast(C, C->getSubExpr(), Pred, Dst);
      break;
    }
      
    case Stmt::CastExprClass: {
      CastExpr* C = cast<CastExpr>(S);
      VisitCast(C, C->getSubExpr(), Pred, Dst);
      break;
    }
      
    case Stmt::DeclStmtClass:
      VisitDeclStmt(cast<DeclStmt>(S), Pred, Dst);
      break;
      
    default:
      Dst.Add(Pred); // No-op. Simply propagate the current state unchanged.
      break;
  }
}

//===----------------------------------------------------------------------===//
// Driver.
//===----------------------------------------------------------------------===//

#ifndef NDEBUG
namespace llvm {
template<>
struct VISIBILITY_HIDDEN DOTGraphTraits<GRConstants::NodeTy*> :
  public DefaultDOTGraphTraits {

  static void PrintKindLabel(std::ostream& Out, ValueKey::Kind kind) {
    switch (kind) {
      case ValueKey::IsSubExpr:  Out << "Sub-Expressions:\\l"; break;
      case ValueKey::IsDecl:    Out << "Variables:\\l"; break;
      case ValueKey::IsBlkExpr: Out << "Block-level Expressions:\\l"; break;
      default: assert (false && "Unknown ValueKey type.");
    }
  }
    
  static void PrintKind(std::ostream& Out, GRConstants::StateTy M,
                        ValueKey::Kind kind, bool isFirstGroup = false) {
    bool isFirst = true;
    
    for (GRConstants::StateTy::iterator I=M.begin(), E=M.end();I!=E;++I) {        
      if (I.getKey().getKind() != kind)
        continue;
    
      if (isFirst) {
        if (!isFirstGroup) Out << "\\l\\l";
        PrintKindLabel(Out, kind);
        isFirst = false;
      }
      else
        Out << "\\l";
      
      Out << ' ';
    
      if (ValueDecl* V = dyn_cast<ValueDecl>(I.getKey()))
        Out << V->getName();          
      else {
        Stmt* E = cast<Stmt>(I.getKey());
        Out << " (" << (void*) E << ") ";
        E->printPretty(Out);
      }
    
      Out << " : ";
      I.getData().print(Out);
    }
  }
    
  static std::string getNodeLabel(const GRConstants::NodeTy* N, void*) {
    std::ostringstream Out;

    // Program Location.
    ProgramPoint Loc = N->getLocation();
    
    switch (Loc.getKind()) {
      case ProgramPoint::BlockEntranceKind:
        Out << "Block Entrance: B" 
            << cast<BlockEntrance>(Loc).getBlock()->getBlockID();
        break;
      
      case ProgramPoint::BlockExitKind:
        assert (false);
        break;
        
      case ProgramPoint::PostStmtKind: {
        const PostStmt& L = cast<PostStmt>(Loc);
        Out << L.getStmt()->getStmtClassName() << ':' 
            << (void*) L.getStmt() << ' ';
        
        L.getStmt()->printPretty(Out);
        break;
      }
    
      default: {
        const BlockEdge& E = cast<BlockEdge>(Loc);
        Out << "Edge: (B" << E.getSrc()->getBlockID() << ", B"
            << E.getDst()->getBlockID()  << ')';
      }
    }
    
    Out << "\\|";
    
    PrintKind(Out, N->getState(), ValueKey::IsDecl, true);
    PrintKind(Out, N->getState(), ValueKey::IsBlkExpr);
    PrintKind(Out, N->getState(), ValueKey::IsSubExpr);
      
    Out << "\\l";
    return Out.str();
  }
};
} // end llvm namespace    
#endif

namespace clang {
void RunGRConstants(CFG& cfg, ASTContext& Ctx) {
  GREngine<GRConstants> Engine(cfg, Ctx);
  Engine.ExecuteWorkList();  
#ifndef NDEBUG
  llvm::ViewGraph(*Engine.getGraph().roots_begin(),"GRConstants");
#endif  
}
} // end clang namespace
