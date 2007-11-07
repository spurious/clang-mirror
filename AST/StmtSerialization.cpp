//===--- StmtSerialization.cpp - Serialization of Statements --------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file was developed by Ted Kremenek and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the type-specific methods for serializing statements
// and expressions.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/Expr.h"
#include "llvm/Bitcode/Serialize.h"
#include "llvm/Bitcode/Deserialize.h"

using namespace clang;

void Stmt::Emit(llvm::Serializer& S) const {
  S.EmitInt(getStmtClass());
  directEmit(S);
}  

Stmt* Stmt::Materialize(llvm::Deserializer& D) {
  StmtClass SC = static_cast<StmtClass>(D.ReadInt());
  
  switch (SC) {
    default:  
      assert (false && "Not implemented.");
      return NULL;
    
    case BinaryOperatorClass:
      return BinaryOperator::directMaterialize(D);
      
    case CompoundStmtClass:
      return CompoundStmt::directMaterialize(D);
      
    case DeclRefExprClass:
      return DeclRefExpr::directMaterialize(D);
      
    case DeclStmtClass:
      return DeclStmt::directMaterialize(D);
            
    case IntegerLiteralClass:
      return IntegerLiteral::directMaterialize(D);      
      
    case ReturnStmtClass:
      return ReturnStmt::directMaterialize(D);        
  }
}

void BinaryOperator::directEmit(llvm::Serializer& S) const {
  S.EmitInt(Opc);
  S.Emit(OpLoc);;
  S.Emit(getType());
  S.EmitOwnedPtr(getLHS());
  S.EmitOwnedPtr(getRHS());
}

BinaryOperator* BinaryOperator::directMaterialize(llvm::Deserializer& D) {
  Opcode Opc = static_cast<Opcode>(D.ReadInt());
  SourceLocation OpLoc = SourceLocation::ReadVal(D);
  QualType Result = QualType::ReadVal(D);
  Expr* LHS = D.ReadOwnedPtr<Expr>();
  Expr* RHS = D.ReadOwnedPtr<Expr>();
  return new BinaryOperator(LHS,RHS,Opc,Result,OpLoc);
}


void CompoundStmt::directEmit(llvm::Serializer& S) const {
  S.Emit(LBracLoc);
  S.Emit(RBracLoc);
  S.Emit(Body.size());
  
  for (const_body_iterator I=body_begin(), E=body_end(); I!=E; ++I)
    S.EmitOwnedPtr(*I);
}

CompoundStmt* CompoundStmt::directMaterialize(llvm::Deserializer& D) {
  SourceLocation LB = SourceLocation::ReadVal(D);
  SourceLocation RB = SourceLocation::ReadVal(D);
  unsigned size = D.ReadInt();
  
  CompoundStmt* stmt = new CompoundStmt(NULL,0,LB,RB);
  
  stmt->Body.reserve(size);
  
  for (unsigned i = 0; i < size; ++i)
    stmt->Body.push_back(D.ReadOwnedPtr<Stmt>());
  
  return stmt;
}

void DeclStmt::directEmit(llvm::Serializer& S) const {
  // FIXME: special handling for struct decls.
  S.EmitOwnedPtr(getDecl());  
}

void DeclRefExpr::directEmit(llvm::Serializer& S) const {
  S.Emit(Loc);
  S.Emit(getType());
  S.EmitPtr(getDecl());
}

DeclRefExpr* DeclRefExpr::directMaterialize(llvm::Deserializer& D) {
  SourceLocation Loc = SourceLocation::ReadVal(D);
  QualType T = QualType::ReadVal(D);
  DeclRefExpr* dr = new DeclRefExpr(NULL,T,Loc);
  D.ReadPtr(dr->D,false);  
  return dr;
}

DeclStmt* DeclStmt::directMaterialize(llvm::Deserializer& D) {
  ScopedDecl* decl = cast<ScopedDecl>(D.ReadOwnedPtr<Decl>());
  return new DeclStmt(decl);
}


void IntegerLiteral::directEmit(llvm::Serializer& S) const {
  S.Emit(Loc);
  S.Emit(getType());
  S.Emit(getValue());
}

IntegerLiteral* IntegerLiteral::directMaterialize(llvm::Deserializer& D) {
  SourceLocation Loc = SourceLocation::ReadVal(D);
  QualType T = QualType::ReadVal(D);
  
  // Create a dummy APInt because it is more efficient to deserialize
  // it in place with the deserialized IntegerLiteral. (fewer copies)
  llvm::APInt temp;  
  IntegerLiteral* expr = new IntegerLiteral(temp,T,Loc);
  D.Read(expr->Value);
  
  return expr;
}


void ReturnStmt::directEmit(llvm::Serializer& S) const {
  S.Emit(RetLoc);
  S.EmitOwnedPtr(RetExpr);
}

ReturnStmt* ReturnStmt::directMaterialize(llvm::Deserializer& D) {
  SourceLocation RetLoc = SourceLocation::ReadVal(D);
  Expr* RetExpr = D.ReadOwnedPtr<Expr>();  
  return new ReturnStmt(RetLoc,RetExpr);
}

