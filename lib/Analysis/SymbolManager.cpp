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

using namespace clang;

SymbolRef SymbolManager::getSymbol(VarDecl* D) {

  assert (isa<ParmVarDecl>(D) || isa<ImplicitParamDecl>(D) || 
          D->hasGlobalStorage());
  
  llvm::FoldingSetNodeID profile;
  
  ParmVarDecl* PD = dyn_cast<ParmVarDecl>(D);
  
  if (PD)
    SymbolDataParmVar::Profile(profile, PD);
  else
    SymbolDataGlobalVar::Profile(profile, D);
  
  void* InsertPos;
  
  SymbolData* SD = DataSet.FindNodeOrInsertPos(profile, InsertPos);

  if (SD)
    return SD->getSymbol();
  
  if (PD) {
    SD = (SymbolData*) BPAlloc.Allocate<SymbolDataParmVar>();
    new (SD) SymbolDataParmVar(SymbolCounter, PD);
  }
  else {
    SD = (SymbolData*) BPAlloc.Allocate<SymbolDataGlobalVar>();
    new (SD) SymbolDataGlobalVar(SymbolCounter, D);
  }
  
  DataSet.InsertNode(SD, InsertPos);
  
  DataMap[SymbolCounter] = SD;
  return SymbolCounter++;
}  

SymbolRef SymbolManager::getElementSymbol(const MemRegion* R, 
                                         const llvm::APSInt* Idx){
  llvm::FoldingSetNodeID ID;
  SymbolDataElement::Profile(ID, R, Idx);
  void* InsertPos;
  SymbolData* SD = DataSet.FindNodeOrInsertPos(ID, InsertPos);

  if (SD)
    return SD->getSymbol();

  SD = (SymbolData*) BPAlloc.Allocate<SymbolDataElement>();
  new (SD) SymbolDataElement(SymbolCounter, R, Idx);

  DataSet.InsertNode(SD, InsertPos);
  DataMap[SymbolCounter] = SD;
  return SymbolCounter++;
}

SymbolRef SymbolManager::getFieldSymbol(const MemRegion* R, const FieldDecl* D) {
  llvm::FoldingSetNodeID ID;
  SymbolDataField::Profile(ID, R, D);
  void* InsertPos;
  SymbolData* SD = DataSet.FindNodeOrInsertPos(ID, InsertPos);

  if (SD)
    return SD->getSymbol();

  SD = (SymbolData*) BPAlloc.Allocate<SymbolDataField>();
  new (SD) SymbolDataField(SymbolCounter, R, D);

  DataSet.InsertNode(SD, InsertPos);
  DataMap[SymbolCounter] = SD;
  return SymbolCounter++;
}

SymbolRef SymbolManager::getConjuredSymbol(Stmt* E, QualType T, unsigned Count) {
  
  llvm::FoldingSetNodeID profile;
  SymbolConjured::Profile(profile, E, T, Count);
  void* InsertPos;
  
  SymbolData* SD = DataSet.FindNodeOrInsertPos(profile, InsertPos);
  
  if (SD)
    return SD->getSymbol();
  
  SD = (SymbolData*) BPAlloc.Allocate<SymbolConjured>();
  new (SD) SymbolConjured(SymbolCounter, E, T, Count);
  
  DataSet.InsertNode(SD, InsertPos);  
  DataMap[SymbolCounter] = SD;
  
  return SymbolCounter++;
}

const SymbolData& SymbolManager::getSymbolData(SymbolRef Sym) const {  
  DataMapTy::const_iterator I = DataMap.find(Sym);
  assert (I != DataMap.end());  
  return *I->second;
}


QualType SymbolData::getType(const SymbolManager& SymMgr) const {
  switch (getKind()) {
    default:
      assert (false && "getType() not implemented for this symbol.");
      
    case ParmKind:
      return cast<SymbolDataParmVar>(this)->getDecl()->getType();

    case GlobalKind:
      return cast<SymbolDataGlobalVar>(this)->getDecl()->getType();

    case ConjuredKind:
      return cast<SymbolConjured>(this)->getType();
  }
}

SymbolManager::~SymbolManager() {}
