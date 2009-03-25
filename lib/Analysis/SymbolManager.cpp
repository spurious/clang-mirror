//== SymbolManager.h - Management of Symbolic Values ------------*- C++ -*--==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines SymbolManager, a class that manages symbolic values
//  created for use by GRExprEngine and related classes.
//
//===----------------------------------------------------------------------===//

#include "clang/Analysis/PathSensitive/SymbolManager.h"
#include "clang/Analysis/PathSensitive/MemRegion.h"
#include "llvm/Support/raw_ostream.h"

using namespace clang;

llvm::raw_ostream& llvm::operator<<(llvm::raw_ostream& os,
                                    clang::SymbolRef sym)  {
  if (sym.isValid())
    os << sym.getNumber();
  else
    os << "(Invalid)";
  
  return os;
}

std::ostream& std::operator<<(std::ostream& os, clang::SymbolRef sym) {
  if (sym.isValid())
    os << sym.getNumber();
  else
    os << "(Invalid)";
  
  return os;
}

SymbolRef SymbolManager::getRegionRValueSymbol(const MemRegion* R) {  
  llvm::FoldingSetNodeID profile;

  SymbolRegionRValue::Profile(profile, R);
  void* InsertPos;  
  SymbolData* SD = DataSet.FindNodeOrInsertPos(profile, InsertPos);    
  if (SD) return SD->getSymbol();
  
  SD = (SymbolData*) BPAlloc.Allocate<SymbolRegionRValue>();
  new (SD) SymbolRegionRValue(SymbolCounter, R);  
  DataSet.InsertNode(SD, InsertPos);
  DataMap[SymbolCounter] = SD;  
  return SymbolCounter++;
}

SymbolRef SymbolManager::getConjuredSymbol(const Stmt* E, QualType T,
                                           unsigned Count,
                                           const void* SymbolTag) {
  
  llvm::FoldingSetNodeID profile;
  SymbolConjured::Profile(profile, E, T, Count, SymbolTag);
  void* InsertPos;
  
  SymbolData* SD = DataSet.FindNodeOrInsertPos(profile, InsertPos);
  
  if (SD)
    return SD->getSymbol();
  
  SD = (SymbolData*) BPAlloc.Allocate<SymbolConjured>();
  new (SD) SymbolConjured(SymbolCounter, E, T, Count, SymbolTag);
  
  DataSet.InsertNode(SD, InsertPos);  
  DataMap[SymbolCounter] = SD;
  
  return SymbolCounter++;
}

SymbolRef SymbolManager::getSymIntExpr(SymbolRef lhs,BinaryOperator::Opcode op, 
                                       const llvm::APSInt& v, QualType t) {
  llvm::FoldingSetNodeID ID;
  SymIntExpr::Profile(ID, lhs, op, v, t);
  void* InsertPos;

  SymbolData* data = DataSet.FindNodeOrInsertPos(ID, InsertPos);

  if (data)
    return data->getSymbol();

  data = (SymIntExpr*) BPAlloc.Allocate<SymIntExpr>();
  new (data) SymIntExpr(SymbolCounter, lhs, op, v, t);

  DataSet.InsertNode(data, InsertPos);
  DataMap[SymbolCounter] = data;

  return SymbolCounter++;
}

SymbolRef SymbolManager::getSymSymExpr(SymbolRef lhs, BinaryOperator::Opcode op,
                                       SymbolRef rhs, QualType t) {
  llvm::FoldingSetNodeID ID;
  SymSymExpr::Profile(ID, lhs, op, rhs, t);
  void* InsertPos;

  SymbolData* data = DataSet.FindNodeOrInsertPos(ID, InsertPos);

  if (data)
    return data->getSymbol();

  data = (SymSymExpr*) BPAlloc.Allocate<SymSymExpr>();
  new (data) SymSymExpr(SymbolCounter, lhs, op, rhs, t);

  DataSet.InsertNode(data, InsertPos);
  DataMap[SymbolCounter] = data;

  return SymbolCounter++;
}


const SymbolData& SymbolManager::getSymbolData(SymbolRef Sym) const {  
  DataMapTy::const_iterator I = DataMap.find(Sym);
  assert (I != DataMap.end());  
  return *I->second;
}


QualType SymbolConjured::getType(ASTContext&) const {
  return T;
}

QualType SymbolRegionRValue::getType(ASTContext& C) const {
  if (const TypedRegion* TR = dyn_cast<TypedRegion>(R))
    return TR->getRValueType(C);
  
  return QualType();
}

SymbolManager::~SymbolManager() {}

bool SymbolManager::canSymbolicate(QualType T) {
  return Loc::IsLocType(T) || T->isIntegerType();  
}

void SymbolReaper::markLive(SymbolRef sym) {
  TheLiving = F.Add(TheLiving, sym);
  TheDead = F.Remove(TheDead, sym);
}

bool SymbolReaper::maybeDead(SymbolRef sym) {
  if (isLive(sym))
    return false;
  
  TheDead = F.Add(TheDead, sym);
  return true;
}

bool SymbolReaper::isLive(SymbolRef sym) {
  if (TheLiving.contains(sym))
    return true;
  
  // Interogate the symbol.  It may derive from an input value to
  // the analyzed function/method.
  return isa<SymbolRegionRValue>(SymMgr.getSymbolData(sym));
}

SymbolVisitor::~SymbolVisitor() {}
