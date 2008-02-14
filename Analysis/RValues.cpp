//= RValues.cpp - Abstract RValues for Path-Sens. Value Tracking -*- C++ -*-==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This files defines RValue, LValue, and NonLValue, classes that represent
//  abstract r-values for use with path-sensitive value tracking.
//
//===----------------------------------------------------------------------===//

#include "clang/Analysis/PathSensitive/RValues.h"

using namespace clang;
using llvm::dyn_cast;
using llvm::cast;
using llvm::APSInt;

//===----------------------------------------------------------------------===//
// SymbolManager.
//===----------------------------------------------------------------------===//

SymbolID SymbolManager::getSymbol(ParmVarDecl* D) {
  SymbolID& X = DataToSymbol[getKey(D)];
  
  if (!X.isInitialized()) {
    X = SymbolToData.size();
    SymbolToData.push_back(SymbolDataParmVar(D));
  }
  
  return X;
}

SymbolID SymbolManager::getContentsOfSymbol(SymbolID sym) {
  SymbolID& X = DataToSymbol[getKey(sym)];
  
  if (!X.isInitialized()) {
    X = SymbolToData.size();
    SymbolToData.push_back(SymbolDataContentsOf(sym));
  }
  
  return X;  
}

QualType SymbolData::getType() const {
  switch (getKind()) {
    default:
      assert (false && "getType() not implemented for this symbol.");
    
    case ParmKind:
      return cast<SymbolDataParmVar>(this)->getDecl()->getType();

  }
}

SymbolManager::SymbolManager() {}
SymbolManager::~SymbolManager() {}

//===----------------------------------------------------------------------===//
// Values and ValueManager.
//===----------------------------------------------------------------------===//

ValueManager::~ValueManager() {
  // Note that the dstor for the contents of APSIntSet will never be called,
  // so we iterate over the set and invoke the dstor for each APSInt.  This
  // frees an aux. memory allocated to represent very large constants.
  for (APSIntSetTy::iterator I=APSIntSet.begin(), E=APSIntSet.end(); I!=E; ++I)
    I->getValue().~APSInt();
}

const APSInt& ValueManager::getValue(const APSInt& X) {
  llvm::FoldingSetNodeID ID;
  void* InsertPos;
  typedef llvm::FoldingSetNodeWrapper<APSInt> FoldNodeTy;
  
  X.Profile(ID);
  FoldNodeTy* P = APSIntSet.FindNodeOrInsertPos(ID, InsertPos);
  
  if (!P) {  
    P = (FoldNodeTy*) BPAlloc.Allocate<FoldNodeTy>();
    new (P) FoldNodeTy(X);
    APSIntSet.InsertNode(P, InsertPos);
  }
  
  return *P;
}

const APSInt& ValueManager::getValue(uint64_t X, unsigned BitWidth,
                                     bool isUnsigned) {
  APSInt V(BitWidth, isUnsigned);
  V = X;  
  return getValue(V);
}

const APSInt& ValueManager::getValue(uint64_t X, QualType T,
                                     SourceLocation Loc) {
  
  unsigned bits = Ctx.getTypeSize(T, Loc);
  APSInt V(bits, T->isUnsignedIntegerType());
  V = X;
  return getValue(V);
}

const SymIntConstraint&
ValueManager::getConstraint(SymbolID sym, BinaryOperator::Opcode Op,
                            const llvm::APSInt& V) {
  
  llvm::FoldingSetNodeID ID;
  SymIntConstraint::Profile(ID, sym, Op, V);
  void* InsertPos;
  
  SymIntConstraint* C = SymIntCSet.FindNodeOrInsertPos(ID, InsertPos);
  
  if (!C) {
    C = (SymIntConstraint*) BPAlloc.Allocate<SymIntConstraint>();
    new (C) SymIntConstraint(sym, Op, V);
    SymIntCSet.InsertNode(C, InsertPos);
  }
  
  return *C;
}

//===----------------------------------------------------------------------===//
// Symbol Iteration.
//===----------------------------------------------------------------------===//

RValue::symbol_iterator RValue::symbol_begin() const {
  if (isa<LValue>(this)) {
    if (isa<lval::SymbolVal>(this))
      return (symbol_iterator) (&Data);
  }
  else {
    if (isa<nonlval::SymbolVal>(this))
      return (symbol_iterator) (&Data);
    else if (isa<nonlval::SymIntConstraintVal>(this)) {
      const SymIntConstraint& C =
        cast<nonlval::SymIntConstraintVal>(this)->getConstraint();
      return (symbol_iterator) &C.getSymbol();
    }
  }
  
  return NULL;
}

RValue::symbol_iterator RValue::symbol_end() const {
  symbol_iterator X = symbol_begin();
  return X ? X+1 : NULL;
}

//===----------------------------------------------------------------------===//
// Transfer function dispatch for Non-LValues.
//===----------------------------------------------------------------------===//

static const
llvm::APSInt& EvaluateAPSInt(ValueManager& ValMgr, BinaryOperator::Opcode Op,
                             const llvm::APSInt& V1, const llvm::APSInt& V2) {
  
  switch (Op) {
    default:
      assert (false && "Invalid Opcode.");
      
    case BinaryOperator::Mul:
      return ValMgr.getValue( V1 * V2 );
      
    case BinaryOperator::Div:
      return ValMgr.getValue( V1 / V2 );
      
    case BinaryOperator::Rem:
      return ValMgr.getValue( V1 % V2 );
      
    case BinaryOperator::Add:
      return ValMgr.getValue( V1 + V2 );

    case BinaryOperator::Sub:
      return ValMgr.getValue( V1 - V2 );

    case BinaryOperator::Shl:
      return ValMgr.getValue( V1.operator<<( (unsigned) V2.getZExtValue() ));
      
    case BinaryOperator::Shr:
      return ValMgr.getValue( V1.operator>>( (unsigned) V2.getZExtValue() ));
    
    case BinaryOperator::LT:
      return ValMgr.getTruthValue( V1 < V2 );
      
    case BinaryOperator::GT:
      return ValMgr.getTruthValue( V1 > V2 );
      
    case BinaryOperator::LE:
      return ValMgr.getTruthValue( V1 <= V2 );
      
    case BinaryOperator::GE:
      return ValMgr.getTruthValue( V1 >= V2 );
      
    case BinaryOperator::EQ:
      return ValMgr.getTruthValue( V1 == V2 );
      
    case BinaryOperator::NE:
      return ValMgr.getTruthValue( V1 != V2 );
      
    // Note: LAnd, LOr, Comma are handled specially by higher-level logic.
      
    case BinaryOperator::And:
      return ValMgr.getValue( V1 & V2 );
      
    case BinaryOperator::Or:
      return ValMgr.getValue( V1 | V2 );
  }
}

nonlval::ConcreteInt
nonlval::ConcreteInt::EvalBinaryOp(ValueManager& ValMgr,
                                   BinaryOperator::Opcode Op,
                                   const nonlval::ConcreteInt& RHS) const {

  return EvaluateAPSInt(ValMgr, Op, getValue(), RHS.getValue());
}


  // Bitwise-Complement.


nonlval::ConcreteInt
nonlval::ConcreteInt::EvalComplement(ValueManager& ValMgr) const {
  return ValMgr.getValue(~getValue()); 
}

  // Unary Minus.

nonlval::ConcreteInt
nonlval::ConcreteInt::EvalMinus(ValueManager& ValMgr, UnaryOperator* U) const {
  assert (U->getType() == U->getSubExpr()->getType());  
  assert (U->getType()->isIntegerType());  
  return ValMgr.getValue(-getValue()); 
}

//===----------------------------------------------------------------------===//
// Transfer function dispatch for LValues.
//===----------------------------------------------------------------------===//

lval::ConcreteInt
lval::ConcreteInt::EvalBinaryOp(ValueManager& ValMgr,
                                BinaryOperator::Opcode Op,
                                const lval::ConcreteInt& RHS) const {
  
  assert (Op == BinaryOperator::Add || Op == BinaryOperator::Sub ||
          (Op >= BinaryOperator::LT && Op <= BinaryOperator::NE));
  
  return EvaluateAPSInt(ValMgr, Op, getValue(), RHS.getValue());
}

NonLValue LValue::EQ(ValueManager& ValMgr, const LValue& RHS) const {
  switch (getSubKind()) {
    default:
      assert(false && "EQ not implemented for this LValue.");
      return cast<NonLValue>(UnknownVal());
      
    case lval::ConcreteIntKind:
      if (isa<lval::ConcreteInt>(RHS)) {
        bool b = cast<lval::ConcreteInt>(this)->getValue() ==
        cast<lval::ConcreteInt>(RHS).getValue();
        
        return NonLValue::GetIntTruthValue(ValMgr, b);
      }
      else if (isa<lval::SymbolVal>(RHS)) {
        
        const SymIntConstraint& C =
        ValMgr.getConstraint(cast<lval::SymbolVal>(RHS).getSymbol(),
                             BinaryOperator::EQ,
                             cast<lval::ConcreteInt>(this)->getValue());
        
        return nonlval::SymIntConstraintVal(C);        
      }
      
      break;
      
      case lval::SymbolValKind: {
        if (isa<lval::ConcreteInt>(RHS)) {
          
          const SymIntConstraint& C =
          ValMgr.getConstraint(cast<lval::SymbolVal>(this)->getSymbol(),
                               BinaryOperator::EQ,
                               cast<lval::ConcreteInt>(RHS).getValue());
          
          return nonlval::SymIntConstraintVal(C);
        }
        
        assert (!isa<lval::SymbolVal>(RHS) && "FIXME: Implement unification.");
        
        break;
      }
      
      case lval::DeclValKind:
      if (isa<lval::DeclVal>(RHS)) {        
        bool b = cast<lval::DeclVal>(*this) == cast<lval::DeclVal>(RHS);
        return NonLValue::GetIntTruthValue(ValMgr, b);
      }
      
      break;
  }
  
  return NonLValue::GetIntTruthValue(ValMgr, false);
}

NonLValue LValue::NE(ValueManager& ValMgr, const LValue& RHS) const {
  switch (getSubKind()) {
    default:
      assert(false && "NE not implemented for this LValue.");
      return cast<NonLValue>(UnknownVal());
      
    case lval::ConcreteIntKind:
      if (isa<lval::ConcreteInt>(RHS)) {
        bool b = cast<lval::ConcreteInt>(this)->getValue() !=
        cast<lval::ConcreteInt>(RHS).getValue();
        
        return NonLValue::GetIntTruthValue(ValMgr, b);
      }
      else if (isa<lval::SymbolVal>(RHS)) {
        
        const SymIntConstraint& C =
        ValMgr.getConstraint(cast<lval::SymbolVal>(RHS).getSymbol(),
                             BinaryOperator::NE,
                             cast<lval::ConcreteInt>(this)->getValue());
        
        return nonlval::SymIntConstraintVal(C);        
      }
      
      break;
      
      case lval::SymbolValKind: {
        if (isa<lval::ConcreteInt>(RHS)) {
          
          const SymIntConstraint& C =
          ValMgr.getConstraint(cast<lval::SymbolVal>(this)->getSymbol(),
                               BinaryOperator::NE,
                               cast<lval::ConcreteInt>(RHS).getValue());
          
          return nonlval::SymIntConstraintVal(C);
        }
        
        assert (!isa<lval::SymbolVal>(RHS) && "FIXME: Implement sym !=.");
        
        break;
      }
      
      case lval::DeclValKind:
      if (isa<lval::DeclVal>(RHS)) {        
        bool b = cast<lval::DeclVal>(*this) == cast<lval::DeclVal>(RHS);
        return NonLValue::GetIntTruthValue(ValMgr, b);
      }
      
      break;
  }
  
  return NonLValue::GetIntTruthValue(ValMgr, true);
}



//===----------------------------------------------------------------------===//
// Utility methods for constructing Non-LValues.
//===----------------------------------------------------------------------===//

NonLValue NonLValue::GetValue(ValueManager& ValMgr, uint64_t X, QualType T,
                              SourceLocation Loc) {
  
  return nonlval::ConcreteInt(ValMgr.getValue(X, T, Loc));
}

NonLValue NonLValue::GetValue(ValueManager& ValMgr, IntegerLiteral* I) {
  return nonlval::ConcreteInt(ValMgr.getValue(APSInt(I->getValue(),
                                   I->getType()->isUnsignedIntegerType())));
}

NonLValue NonLValue::GetIntTruthValue(ValueManager& ValMgr, bool b) {
  return nonlval::ConcreteInt(ValMgr.getTruthValue(b));
}

RValue RValue::GetSymbolValue(SymbolManager& SymMgr, ParmVarDecl* D) {
  QualType T = D->getType();
  
  if (T->isPointerType() || T->isReferenceType())
    return lval::SymbolVal(SymMgr.getSymbol(D));
  else
    return nonlval::SymbolVal(SymMgr.getSymbol(D));
}

//===----------------------------------------------------------------------===//
// Utility methods for constructing LValues.
//===----------------------------------------------------------------------===//

LValue LValue::GetValue(AddrLabelExpr* E) {
  return lval::GotoLabel(E->getLabel());
}

//===----------------------------------------------------------------------===//
// Pretty-Printing.
//===----------------------------------------------------------------------===//

void RValue::print() const {
  print(*llvm::cerr.stream());
}

void RValue::print(std::ostream& Out) const {
  switch (getBaseKind()) {
    case UnknownKind:
      Out << "Invalid";
      break;
      
    case NonLValueKind:
      cast<NonLValue>(this)->print(Out);
      break;
      
    case LValueKind:
      cast<LValue>(this)->print(Out);
      break;
      
    case UninitializedKind:
      Out << "Uninitialized";
      break;
      
    default:
      assert (false && "Invalid RValue.");
  }
}

static void printOpcode(std::ostream& Out, BinaryOperator::Opcode Op) {
  switch (Op) {      
    case BinaryOperator::Add: Out << "+" ; break;
    case BinaryOperator::Sub: Out << "-" ; break;
    case BinaryOperator::EQ: Out << "=="; break;
    case BinaryOperator::NE: Out << "!="; break;
    default: assert(false && "Not yet implemented.");
  }        
}

void NonLValue::print(std::ostream& Out) const {
  switch (getSubKind()) {  
    case nonlval::ConcreteIntKind:
      Out << cast<nonlval::ConcreteInt>(this)->getValue().toString();

      if (cast<nonlval::ConcreteInt>(this)->getValue().isUnsigned())
        Out << 'U';
      
      break;
      
    case nonlval::SymbolValKind:
      Out << '$' << cast<nonlval::SymbolVal>(this)->getSymbol();
      break;
     
    case nonlval::SymIntConstraintValKind: {
      const nonlval::SymIntConstraintVal& C = 
        *cast<nonlval::SymIntConstraintVal>(this);
      
      Out << '$' << C.getConstraint().getSymbol() << ' ';
      printOpcode(Out, C.getConstraint().getOpcode());
      Out << ' ' << C.getConstraint().getInt().toString();
      
      if (C.getConstraint().getInt().isUnsigned())
        Out << 'U';
      
      break;
    }  
      
    default:
      assert (false && "Pretty-printed not implemented for this NonLValue.");
      break;
  }
}


void LValue::print(std::ostream& Out) const {
  switch (getSubKind()) {        
    case lval::ConcreteIntKind:
      Out << cast<lval::ConcreteInt>(this)->getValue().toString() 
          << " (LValue)";
      break;
      
    case lval::SymbolValKind:
      Out << '$' << cast<lval::SymbolVal>(this)->getSymbol();
      break;
      
    case lval::GotoLabelKind:
      Out << "&&"
          << cast<lval::GotoLabel>(this)->getLabel()->getID()->getName();
      break;

    case lval::DeclValKind:
      Out << '&' 
      << cast<lval::DeclVal>(this)->getDecl()->getIdentifier()->getName();
      break;
      
    default:
      assert (false && "Pretty-printed not implemented for this LValue.");
      break;
  }
}

