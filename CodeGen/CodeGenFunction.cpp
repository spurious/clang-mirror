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
using namespace clang;
using namespace CodeGen;

CodeGenFunction::CodeGenFunction(CodeGenModule &cgm) 
  : CGM(cgm), Target(CGM.getContext().Target) {}

ASTContext &CodeGenFunction::getContext() const {
  return CGM.getContext();
}


llvm::BasicBlock *CodeGenFunction::getBasicBlockForLabel(const LabelStmt *S) {
  llvm::BasicBlock *&BB = LabelMap[S];
  if (BB) return BB;
  
  // Create, but don't insert, the new block.
  return BB = new llvm::BasicBlock(S->getName());
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
    case BuiltinType::Char_S:
    case BuiltinType::Char_U:
    case BuiltinType::SChar:
    case BuiltinType::UChar:
      return llvm::IntegerType::get(Target.getCharWidth(Loc));

    case BuiltinType::Bool:
      // FIXME: This is very strange.  We want scalars to be i1, but in memory
      // they can be i1 or i32.  Should the codegen handle this issue?
      return llvm::Type::Int1Ty;
      
    case BuiltinType::Short:
    case BuiltinType::UShort:
      return llvm::IntegerType::get(Target.getShortWidth(Loc));
      
    case BuiltinType::Int:
    case BuiltinType::UInt:
      return llvm::IntegerType::get(Target.getIntWidth(Loc));

    case BuiltinType::Long:
    case BuiltinType::ULong:
      return llvm::IntegerType::get(Target.getLongWidth(Loc));

    case BuiltinType::LongLong:
    case BuiltinType::ULongLong:
      return llvm::IntegerType::get(Target.getLongLongWidth(Loc));
      
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
  case Type::Pointer: {
    const PointerType &P = cast<PointerType>(Ty);
    return llvm::PointerType::get(ConvertType(P.getPointeeType(), Loc));
  }
  case Type::Reference: {
    const ReferenceType &R = cast<ReferenceType>(Ty);
    return llvm::PointerType::get(ConvertType(R.getReferenceeType(), Loc));
  }
    
  case Type::Array: {
    const ArrayType &A = cast<ArrayType>(Ty);
    assert(A.getSizeModifier() == ArrayType::Normal &&
           A.getIndexTypeQualifier() == 0 &&
           "FIXME: We only handle trivial array types so far!");
    // FIXME: are there any promotions etc here?
    RValue Size = EmitExpr(A.getSize());
    assert(Size.isScalar() && isa<llvm::ConstantInt>(Size.getVal()) &&
           "FIXME: Only handle fixed-size arrays so far");
    const llvm::Type *EltTy = ConvertType(A.getElementType(), Loc);
    return llvm::ArrayType::get(EltTy, 
                      cast<llvm::ConstantInt>(Size.getVal())->getZExtValue());
  }
  case Type::FunctionNoProto:
  case Type::FunctionProto: {
    const FunctionType &FP = cast<FunctionType>(Ty);
    const llvm::Type *ResultType;
    
    if (FP.getResultType()->isVoidType())
      ResultType = llvm::Type::VoidTy;    // Result of function uses llvm void.
    else
      ResultType = ConvertType(FP.getResultType(), Loc);
    
    // FIXME: Convert argument types.
    bool isVarArg;
    std::vector<const llvm::Type*> ArgTys;
    if (const FunctionTypeProto *FTP = dyn_cast<FunctionTypeProto>(&FP)) {
      DecodeArgumentTypes(*FTP, ArgTys, Loc);
      isVarArg = FTP->isVariadic();
    } else {
      isVarArg = true;
    }
    
    return llvm::FunctionType::get(ResultType, ArgTys, isVarArg, 0);
  }
  case Type::TypeName:
  case Type::Tagged:
    break;
  }
  
  // FIXME: implement.
  return llvm::OpaqueType::get();
}

void CodeGenFunction::DecodeArgumentTypes(const FunctionTypeProto &FTP, 
                                          std::vector<const llvm::Type*> &
                                          ArgTys, SourceLocation Loc) {
  for (unsigned i = 0, e = FTP.getNumArgs(); i != e; ++i) {
    const llvm::Type *Ty = ConvertType(FTP.getArgType(i), Loc);
    if (Ty->isFirstClassType())
      ArgTys.push_back(Ty);
    else
      ArgTys.push_back(llvm::PointerType::get(Ty));
  }
}

void CodeGenFunction::GenerateCode(const FunctionDecl *FD) {
  LLVMIntTy = ConvertType(getContext().IntTy, FD->getLocation());
  LLVMPointerWidth = Target.getPointerWidth(FD->getLocation());
  
  const llvm::FunctionType *Ty = 
    cast<llvm::FunctionType>(ConvertType(FD->getType(), FD->getLocation()));
  
  // FIXME: param attributes for sext/zext etc.
  
  CurFuncDecl = FD;
  CurFn = new llvm::Function(Ty, llvm::Function::ExternalLinkage,
                             FD->getName(), &CGM.getModule());
  
  llvm::BasicBlock *EntryBB = new llvm::BasicBlock("entry", CurFn);
  
  Builder.SetInsertPoint(EntryBB);

  // Create a marker to make it easy to insert allocas into the entryblock
  // later.
  llvm::Value *Undef = llvm::UndefValue::get(llvm::Type::Int32Ty);
  AllocaInsertPt = Builder.CreateBitCast(Undef,llvm::Type::Int32Ty, "allocapt");
  
  // Emit allocs for param decls. 
  llvm::Function::arg_iterator AI = CurFn->arg_begin();
  for (unsigned i = 0, e = FD->getNumParams(); i != e; ++i, ++AI) {
    assert(AI != CurFn->arg_end() && "Argument mismatch!");
    EmitParmDecl(*FD->getParamDecl(i), AI);
  }
  
  // Emit the function body.
  EmitStmt(FD->getBody());
  
  // Emit a return for code that falls off the end.
  // FIXME: if this is C++ main, this should return 0.
  if (Ty->getReturnType() == llvm::Type::VoidTy)
    Builder.CreateRetVoid();
  else
    Builder.CreateRet(llvm::UndefValue::get(Ty->getReturnType()));
  
  // Verify that the function is well formed.
  assert(!verifyFunction(*CurFn));
}

