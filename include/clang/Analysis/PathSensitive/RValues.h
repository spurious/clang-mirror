//== RValues.h - Abstract RValues for Path-Sens. Value Tracking -*- C++ -*--==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This files defines RVal, LVal, and NonLVal, classes that represent
//  abstract r-values for use with path-sensitive value tracking.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_ANALYSIS_RVALUE_H
#define LLVM_CLANG_ANALYSIS_RVALUE_H

#include "clang/Analysis/PathSensitive/ValueManager.h"
#include "llvm/Support/Casting.h"
  
//==------------------------------------------------------------------------==//
//  Base RVal types.
//==------------------------------------------------------------------------==// 

namespace clang {
  
class RVal {
public:
  enum BaseKind { UndefinedKind, UnknownKind, LValKind, NonLValKind };
  enum { BaseBits = 2, BaseMask = 0x3 };
  
protected:
  void* Data;
  unsigned Kind;
  
protected:
  RVal(const void* d, bool isLVal, unsigned ValKind)
  : Data(const_cast<void*>(d)),
    Kind((isLVal ? LValKind : NonLValKind) | (ValKind << BaseBits)) {}
  
  explicit RVal(BaseKind k, void* D = NULL)
    : Data(D), Kind(k) {}
  
public:
  ~RVal() {};
  
  /// BufferTy - A temporary buffer to hold a set of RVals.
  typedef llvm::SmallVector<RVal,5> BufferTy;
  
  inline unsigned getRawKind() const { return Kind; }
  inline BaseKind getBaseKind() const { return (BaseKind) (Kind & BaseMask); }
  inline unsigned getSubKind() const { return (Kind & ~BaseMask) >> BaseBits; }
  
  inline void Profile(llvm::FoldingSetNodeID& ID) const {
    ID.AddInteger((unsigned) getRawKind());
    ID.AddPointer(reinterpret_cast<void*>(Data));
  }
  
  inline bool operator==(const RVal& R) const {
    return getRawKind() == R.getRawKind() && Data == R.Data;
  }
  
  
  inline bool operator!=(const RVal& R) const {
    return !(*this == R);
  }
  
  static RVal GetSymbolValue(SymbolManager& SymMgr, VarDecl *D);
  
  inline bool isUnknown() const {
    return getRawKind() == UnknownKind;
  }

  inline bool isUndef() const {
    return getRawKind() == UndefinedKind;
  }

  inline bool isUnknownOrUndef() const {
    return getRawKind() <= UnknownKind;
  }
  
  inline bool isValid() const {
    return getRawKind() > UnknownKind;
  }
  
  void print(std::ostream& OS) const;
  void printStdErr() const;
  
  typedef const SymbolID* symbol_iterator;
  symbol_iterator symbol_begin() const;
  symbol_iterator symbol_end() const;  
  
  // Implement isa<T> support.
  static inline bool classof(const RVal*) { return true; }
};

class UnknownVal : public RVal {
public:
  UnknownVal() : RVal(UnknownKind) {}
  
  static inline bool classof(const RVal* V) {
    return V->getBaseKind() == UnknownKind;
  }  
};

class UndefinedVal : public RVal {
public:
  UndefinedVal() : RVal(UndefinedKind) {}
  UndefinedVal(void* D) : RVal(UndefinedKind, D) {}
  
  static inline bool classof(const RVal* V) {
    return V->getBaseKind() == UndefinedKind;
  }
  
  void* getData() const { return Data; }  
};

class NonLVal : public RVal {
protected:
  NonLVal(unsigned SubKind, const void* d) : RVal(d, false, SubKind) {}
  
public:
  void print(std::ostream& Out) const;
  
  // Utility methods to create NonLVals.
  static NonLVal MakeVal(ValueManager& ValMgr, uint64_t X, QualType T);
  
  static NonLVal MakeVal(ValueManager& ValMgr, IntegerLiteral* I);
  
  static NonLVal MakeIntTruthVal(ValueManager& ValMgr, bool b);
    
  // Implement isa<T> support.
  static inline bool classof(const RVal* V) {
    return V->getBaseKind() == NonLValKind;
  }
};

class LVal : public RVal {
protected:
  LVal(unsigned SubKind, const void* D)
    : RVal(const_cast<void*>(D), true, SubKind) {}
  
  // Equality operators.
  NonLVal EQ(ValueManager& ValMgr, const LVal& R) const;
  NonLVal NE(ValueManager& ValMgr, const LVal& R) const;
  
public:
  void print(std::ostream& Out) const;
    
  static LVal MakeVal(AddrLabelExpr* E);
  
  // Implement isa<T> support.
  static inline bool classof(const RVal* V) {
    return V->getBaseKind() == LValKind;
  }
};
  
//==------------------------------------------------------------------------==//
//  Subclasses of NonLVal.
//==------------------------------------------------------------------------==// 

namespace nonlval {
  
enum Kind { ConcreteIntKind, SymbolValKind, SymIntConstraintValKind };

class SymbolVal : public NonLVal {
public:
  SymbolVal(unsigned SymID)
    : NonLVal(SymbolValKind, reinterpret_cast<void*>((uintptr_t) SymID)) {}
  
  SymbolID getSymbol() const {
    return (SymbolID) reinterpret_cast<uintptr_t>(Data);
  }
  
  static inline bool classof(const RVal* V) {
    return V->getBaseKind() == NonLValKind && 
           V->getSubKind() == SymbolValKind;
  }
  
  static inline bool classof(const NonLVal* V) {
    return V->getSubKind() == SymbolValKind;
  }
};

class SymIntConstraintVal : public NonLVal {    
public:
  SymIntConstraintVal(const SymIntConstraint& C)
    : NonLVal(SymIntConstraintValKind, reinterpret_cast<const void*>(&C)) {}

  const SymIntConstraint& getConstraint() const {
    return *reinterpret_cast<SymIntConstraint*>(Data);
  }
  
  static inline bool classof(const RVal* V) {
    return V->getBaseKind() == NonLValKind &&
           V->getSubKind() == SymIntConstraintValKind;
  }
  
  static inline bool classof(const NonLVal* V) {
    return V->getSubKind() == SymIntConstraintValKind;
  }
};

class ConcreteInt : public NonLVal {
public:
  ConcreteInt(const llvm::APSInt& V) : NonLVal(ConcreteIntKind, &V) {}
  
  const llvm::APSInt& getValue() const {
    return *static_cast<llvm::APSInt*>(Data);
  }
  
  // Transfer functions for binary/unary operations on ConcreteInts.
  RVal EvalBinOp(ValueManager& ValMgr, BinaryOperator::Opcode Op,
                 const ConcreteInt& R) const;
  
  ConcreteInt EvalComplement(ValueManager& ValMgr) const;
  
  ConcreteInt EvalMinus(ValueManager& ValMgr, UnaryOperator* U) const;
  
  // Implement isa<T> support.
  static inline bool classof(const RVal* V) {
    return V->getBaseKind() == NonLValKind &&
           V->getSubKind() == ConcreteIntKind;
  }
  
  static inline bool classof(const NonLVal* V) {
    return V->getSubKind() == ConcreteIntKind;
  }
};
  
} // end namespace clang::nonlval

//==------------------------------------------------------------------------==//
//  Subclasses of LVal.
//==------------------------------------------------------------------------==// 

namespace lval {
  
enum Kind { SymbolValKind, GotoLabelKind, DeclValKind, FuncValKind,
            ConcreteIntKind };

class SymbolVal : public LVal {
public:
  SymbolVal(unsigned SymID)
  : LVal(SymbolValKind, reinterpret_cast<void*>((uintptr_t) SymID)) {}
  
  SymbolID getSymbol() const {
    return (SymbolID) reinterpret_cast<uintptr_t>(Data);
  }
  
  static inline bool classof(const RVal* V) {
    return V->getBaseKind() == LValKind &&
           V->getSubKind() == SymbolValKind;
  }
  
  static inline bool classof(const LVal* V) {
    return V->getSubKind() == SymbolValKind;
  }
};

class GotoLabel : public LVal {
public:
  GotoLabel(LabelStmt* Label) : LVal(GotoLabelKind, Label) {}
  
  LabelStmt* getLabel() const {
    return static_cast<LabelStmt*>(Data);
  }
  
  static inline bool classof(const RVal* V) {
    return V->getBaseKind() == LValKind &&
           V->getSubKind() == GotoLabelKind;
  }
  
  static inline bool classof(const LVal* V) {
    return V->getSubKind() == GotoLabelKind;
  } 
};
  

class DeclVal : public LVal {
public:
  DeclVal(const VarDecl* vd) : LVal(DeclValKind, vd) {}
  
  VarDecl* getDecl() const {
    return static_cast<VarDecl*>(Data);
  }
  
  inline bool operator==(const DeclVal& R) const {
    return getDecl() == R.getDecl();
  }
  
  inline bool operator!=(const DeclVal& R) const {
    return getDecl() != R.getDecl();
  }
  
  // Implement isa<T> support.
  static inline bool classof(const RVal* V) {
    return V->getBaseKind() == LValKind &&
           V->getSubKind() == DeclValKind;
  }
  
  static inline bool classof(const LVal* V) {
    return V->getSubKind() == DeclValKind;
  }    
};

class FuncVal : public LVal {
public:
  FuncVal(const FunctionDecl* fd) : LVal(FuncValKind, fd) {}
  
  FunctionDecl* getDecl() const {
    return static_cast<FunctionDecl*>(Data);
  }
  
  inline bool operator==(const FuncVal& R) const {
    return getDecl() == R.getDecl();
  }
  
  inline bool operator!=(const FuncVal& R) const {
    return getDecl() != R.getDecl();
  }
  
  // Implement isa<T> support.
  static inline bool classof(const RVal* V) {
    return V->getBaseKind() == LValKind &&
           V->getSubKind() == FuncValKind;
  }
  
  static inline bool classof(const LVal* V) {
    return V->getSubKind() == FuncValKind;
  }
};

class ConcreteInt : public LVal {
public:
  ConcreteInt(const llvm::APSInt& V) : LVal(ConcreteIntKind, &V) {}
  
  const llvm::APSInt& getValue() const {
    return *static_cast<llvm::APSInt*>(Data);
  }

  // Transfer functions for binary/unary operations on ConcreteInts.
  RVal EvalBinOp(ValueManager& ValMgr, BinaryOperator::Opcode Op,
                 const ConcreteInt& R) const;
      
  // Implement isa<T> support.
  static inline bool classof(const RVal* V) {
    return V->getBaseKind() == LValKind &&
           V->getSubKind() == ConcreteIntKind;
  }
  
  static inline bool classof(const LVal* V) {
    return V->getSubKind() == ConcreteIntKind;
  }
};
  
} // end clang::lval namespace
} // end clang namespace  

#endif
