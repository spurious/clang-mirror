//===--- CodeGenFunction.cpp - Emit LLVM Code from ASTs for a Function ----===//
//
//                     The LLVM Compiler Infrastructure
//
// This file was developed by Chris Lattner and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This coordinates the per-function state used while generating code.
//
//===----------------------------------------------------------------------===//

#include "CodeGenFunction.h"
#include "CodeGenModule.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/AST/AST.h"
#include "llvm/Constants.h"
#include "llvm/DerivedTypes.h"
#include "llvm/Function.h"
#include "llvm/Analysis/Verifier.h"
using namespace llvm;
using namespace clang;
using namespace CodeGen;

CodeGenFunction::CodeGenFunction(CodeGenModule &cgm) 
  : CGM(cgm), Target(CGM.getContext().Target) {}


llvm::BasicBlock *CodeGenFunction::getBasicBlockForLabel(const LabelStmt *S) {
  BasicBlock *&BB = LabelMap[S];
  if (BB) return BB;
  
  // Create, but don't insert, the new block.
  return BB = new BasicBlock(S->getName());
}


/// ConvertType - Convert the specified type to its LLVM form.
const llvm::Type *CodeGenFunction::ConvertType(QualType T, SourceLocation Loc) {
  // FIXME: Cache these, move the CodeGenModule, expand, etc.
  const clang::Type &Ty = *T.getCanonicalType();
  
  switch (Ty.getTypeClass()) {
  case Type::Builtin: {
    switch (cast<BuiltinType>(Ty).getKind()) {
    case BuiltinType::Void:
      // LLVM void type can only be used as the result of a function call.  Just
      // map to the same as char.
    case BuiltinType::Char:
    case BuiltinType::SChar:
    case BuiltinType::UChar:
      return IntegerType::get(Target.getCharWidth(Loc));

    case BuiltinType::Bool:
      return IntegerType::get(Target.getBoolWidth(Loc));
      
    case BuiltinType::Short:
    case BuiltinType::UShort:
      return IntegerType::get(Target.getShortWidth(Loc));
      
    case BuiltinType::Int:
    case BuiltinType::UInt:
      return IntegerType::get(Target.getIntWidth(Loc));

    case BuiltinType::Long:
    case BuiltinType::ULong:
      return IntegerType::get(Target.getLongWidth(Loc));

    case BuiltinType::LongLong:
    case BuiltinType::ULongLong:
      return IntegerType::get(Target.getLongLongWidth(Loc));
      
    case BuiltinType::Float:      return llvm::Type::FloatTy;
    case BuiltinType::Double:     return llvm::Type::DoubleTy;
    case BuiltinType::LongDouble:
    case BuiltinType::FloatComplex:
    case BuiltinType::DoubleComplex:
    case BuiltinType::LongDoubleComplex:
      ;
    }
    break;
  }
  case Type::Pointer:
  case Type::Reference:
  case Type::Array:
    break;
  case Type::FunctionNoProto:
  case Type::FunctionProto: {
    const FunctionType &FP = cast<FunctionType>(Ty);
    const llvm::Type *ResultType;
    
    if (FP.getResultType()->isVoidType())
      ResultType = llvm::Type::VoidTy;    // Result of function uses llvm void.
    else
      ResultType = ConvertType(FP.getResultType(), Loc);
    
    // FIXME: Convert argument types.
    
    return llvm::FunctionType::get(ResultType,
                                   std::vector<const llvm::Type*>(),
                                   false,
                                   0);
  }
  case Type::TypeName:
  case Type::Tagged:
    break;
  }
  
  // FIXME: implement.
  return OpaqueType::get();
}


void CodeGenFunction::GenerateCode(const FunctionDecl *FD) {
  const llvm::FunctionType *Ty = 
    cast<llvm::FunctionType>(ConvertType(FD->getType(), FD->getLocation()));
  
  CurFuncDecl = FD;
  CurFn = new Function(Ty, Function::ExternalLinkage,
                       FD->getName(), &CGM.getModule());
  
  BasicBlock *EntryBB = new BasicBlock("entry", CurFn);
  
  // TODO: Walk the decls, creating allocas etc.
  
  Builder.SetInsertPoint(EntryBB);

  // Create a marker to make it easy to insert allocas into the entryblock
  // later.
  AllocaInsertPt = Builder.CreateBitCast(UndefValue::get(llvm::Type::Int32Ty),
                                         llvm::Type::Int32Ty, "allocapt");
  
  // TODO: handle params. 
  
  // Emit the function body.
  EmitStmt(FD->getBody());
  
  // Emit a return for code that falls off the end.
  // FIXME: if this is C++ main, this should return 0.
  if (Ty->getReturnType() == llvm::Type::VoidTy)
    Builder.CreateRetVoid();
  else
    Builder.CreateRet(UndefValue::get(Ty->getReturnType()));
      
  
  
  // Verify that the function is well formed.
  assert(!verifyFunction(*CurFn));
}

