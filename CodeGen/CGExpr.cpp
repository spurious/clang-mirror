//===--- CGExpr.cpp - Emit LLVM Code from Expressions ---------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file was developed by Chris Lattner and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This contains code to emit Expr nodes as LLVM code.
//
//===----------------------------------------------------------------------===//

#include "CodeGenFunction.h"
#include "CodeGenModule.h"
#include "clang/AST/AST.h"
#include "clang/Lex/IdentifierTable.h"
#include "llvm/Constants.h"
#include "llvm/DerivedTypes.h"
#include "llvm/Function.h"
#include "llvm/GlobalVariable.h"
#include "llvm/Support/MathExtras.h"
using namespace clang;
using namespace CodeGen;

//===--------------------------------------------------------------------===//
//                        Miscellaneous Helper Methods
//===--------------------------------------------------------------------===//

/// CreateTempAlloca - This creates a alloca and inserts it into the entry
/// block.
llvm::AllocaInst *CodeGenFunction::CreateTempAlloca(const llvm::Type *Ty,
                                                    const char *Name) {
  return new llvm::AllocaInst(Ty, 0, Name, AllocaInsertPt);
}

/// EvaluateExprAsBool - Perform the usual unary conversions on the specified
/// expression and compare the result against zero, returning an Int1Ty value.
llvm::Value *CodeGenFunction::EvaluateExprAsBool(const Expr *E) {
  QualType BoolTy = getContext().BoolTy;
  if (!E->getType()->isComplexType())
    return EmitScalarConversion(EmitScalarExpr(E), E->getType(), BoolTy);

  return EmitComplexToScalarConversion(EmitComplexExpr(E), E->getType(),BoolTy);
}

//===----------------------------------------------------------------------===//
//                         LValue Expression Emission
//===----------------------------------------------------------------------===//

/// EmitLValue - Emit code to compute a designator that specifies the location
/// of the expression.
///
/// This can return one of two things: a simple address or a bitfield
/// reference.  In either case, the LLVM Value* in the LValue structure is
/// guaranteed to be an LLVM pointer type.
///
/// If this returns a bitfield reference, nothing about the pointee type of
/// the LLVM value is known: For example, it may not be a pointer to an
/// integer.
///
/// If this returns a normal address, and if the lvalue's C type is fixed
/// size, this method guarantees that the returned pointer type will point to
/// an LLVM type of the same size of the lvalue's type.  If the lvalue has a
/// variable length type, this is not possible.
///
LValue CodeGenFunction::EmitLValue(const Expr *E) {
  switch (E->getStmtClass()) {
  default: {
    fprintf(stderr, "Unimplemented lvalue expr!\n");
    E->dump();
    llvm::Type *Ty = llvm::PointerType::get(ConvertType(E->getType()));
    return LValue::MakeAddr(llvm::UndefValue::get(Ty));
  }

  case Expr::DeclRefExprClass: return EmitDeclRefLValue(cast<DeclRefExpr>(E));
  case Expr::ParenExprClass:return EmitLValue(cast<ParenExpr>(E)->getSubExpr());
  case Expr::PreDefinedExprClass:
    return EmitPreDefinedLValue(cast<PreDefinedExpr>(E));
  case Expr::StringLiteralClass:
    return EmitStringLiteralLValue(cast<StringLiteral>(E));
    
  case Expr::UnaryOperatorClass: 
    return EmitUnaryOpLValue(cast<UnaryOperator>(E));
  case Expr::ArraySubscriptExprClass:
    return EmitArraySubscriptExpr(cast<ArraySubscriptExpr>(E));
  case Expr::OCUVectorElementExprClass:
    return EmitOCUVectorElementExpr(cast<OCUVectorElementExpr>(E));
  }
}

/// EmitLoadOfLValue - Given an expression that represents a value lvalue,
/// this method emits the address of the lvalue, then loads the result as an
/// rvalue, returning the rvalue.
RValue CodeGenFunction::EmitLoadOfLValue(LValue LV, QualType ExprType) {
  if (LV.isSimple()) {
    llvm::Value *Ptr = LV.getAddress();
    const llvm::Type *EltTy =
      cast<llvm::PointerType>(Ptr->getType())->getElementType();
    
    // Simple scalar l-value.
    if (EltTy->isFirstClassType())
      return RValue::get(Builder.CreateLoad(Ptr, "tmp"));
    
    assert(ExprType->isFunctionType() && "Unknown scalar value");
    return RValue::get(Ptr);
  }
  
  if (LV.isVectorElt()) {
    llvm::Value *Vec = Builder.CreateLoad(LV.getVectorAddr(), "tmp");
    return RValue::get(Builder.CreateExtractElement(Vec, LV.getVectorIdx(),
                                                    "vecext"));
  }

  // If this is a reference to a subset of the elements of a vector, either
  // shuffle the input or extract/insert them as appropriate.
  if (LV.isOCUVectorElt())
    return EmitLoadOfOCUElementLValue(LV, ExprType);
  
  assert(0 && "Bitfield ref not impl!");
}

// If this is a reference to a subset of the elements of a vector, either
// shuffle the input or extract/insert them as appropriate.
RValue CodeGenFunction::EmitLoadOfOCUElementLValue(LValue LV,
                                                   QualType ExprType) {
  llvm::Value *Vec = Builder.CreateLoad(LV.getOCUVectorAddr(), "tmp");
  
  unsigned EncFields = LV.getOCUVectorElts();
  
  // If the result of the expression is a non-vector type, we must be
  // extracting a single element.  Just codegen as an extractelement.
  const VectorType *ExprVT = ExprType->getAsVectorType();
  if (!ExprVT) {
    unsigned InIdx = OCUVectorElementExpr::getAccessedFieldNo(0, EncFields);
    llvm::Value *Elt = llvm::ConstantInt::get(llvm::Type::Int32Ty, InIdx);
    return RValue::get(Builder.CreateExtractElement(Vec, Elt, "tmp"));
  }
  
  // If the source and destination have the same number of elements, use a
  // vector shuffle instead of insert/extracts.
  unsigned NumResultElts = ExprVT->getNumElements();
  unsigned NumSourceElts =
    cast<llvm::VectorType>(Vec->getType())->getNumElements();
  
  if (NumResultElts == NumSourceElts) {
    llvm::SmallVector<llvm::Constant*, 4> Mask;
    for (unsigned i = 0; i != NumResultElts; ++i) {
      unsigned InIdx = OCUVectorElementExpr::getAccessedFieldNo(i, EncFields);
      Mask.push_back(llvm::ConstantInt::get(llvm::Type::Int32Ty, InIdx));
    }
    
    llvm::Value *MaskV = llvm::ConstantVector::get(&Mask[0], Mask.size());
    Vec = Builder.CreateShuffleVector(Vec,
                                      llvm::UndefValue::get(Vec->getType()),
                                      MaskV, "tmp");
    return RValue::get(Vec);
  }
  
  // Start out with an undef of the result type.
  llvm::Value *Result = llvm::UndefValue::get(ConvertType(ExprType));
  
  // Extract/Insert each element of the result.
  for (unsigned i = 0; i != NumResultElts; ++i) {
    unsigned InIdx = OCUVectorElementExpr::getAccessedFieldNo(i, EncFields);
    llvm::Value *Elt = llvm::ConstantInt::get(llvm::Type::Int32Ty, InIdx);
    Elt = Builder.CreateExtractElement(Vec, Elt, "tmp");
    
    llvm::Value *OutIdx = llvm::ConstantInt::get(llvm::Type::Int32Ty, i);
    Result = Builder.CreateInsertElement(Result, Elt, OutIdx, "tmp");
  }
  
  return RValue::get(Result);
}



/// EmitStoreThroughLValue - Store the specified rvalue into the specified
/// lvalue, where both are guaranteed to the have the same type, and that type
/// is 'Ty'.
void CodeGenFunction::EmitStoreThroughLValue(RValue Src, LValue Dst, 
                                             QualType Ty) {
  if (!Dst.isSimple()) {
    if (Dst.isVectorElt()) {
      // Read/modify/write the vector, inserting the new element.
      // FIXME: Volatility.
      llvm::Value *Vec = Builder.CreateLoad(Dst.getVectorAddr(), "tmp");
      Vec = Builder.CreateInsertElement(Vec, Src.getVal(),
                                        Dst.getVectorIdx(), "vecins");
      Builder.CreateStore(Vec, Dst.getVectorAddr());
      return;
    }
  
    // If this is an update of elements of a vector, insert them as appropriate.
    if (Dst.isOCUVectorElt())
      return EmitStoreThroughOCUComponentLValue(Src, Dst, Ty);
  
    assert(0 && "FIXME: Don't support store to bitfield yet");
  }
  
  llvm::Value *DstAddr = Dst.getAddress();
  assert(Src.isScalar() && "Can't emit an agg store with this method");
  // FIXME: Handle volatility etc.
  const llvm::Type *SrcTy = Src.getVal()->getType();
  const llvm::Type *AddrTy = 
    cast<llvm::PointerType>(DstAddr->getType())->getElementType();
  
  if (AddrTy != SrcTy)
    DstAddr = Builder.CreateBitCast(DstAddr, llvm::PointerType::get(SrcTy),
                                    "storetmp");
  Builder.CreateStore(Src.getVal(), DstAddr);
}

void CodeGenFunction::EmitStoreThroughOCUComponentLValue(RValue Src, LValue Dst, 
                                                         QualType Ty) {
  // This access turns into a read/modify/write of the vector.  Load the input
  // value now.
  llvm::Value *Vec = Builder.CreateLoad(Dst.getOCUVectorAddr(), "tmp");
  // FIXME: Volatility.
  unsigned EncFields = Dst.getOCUVectorElts();
  
  llvm::Value *SrcVal = Src.getVal();
  
  if (const VectorType *VTy = Ty->getAsVectorType()) {
    unsigned NumSrcElts = VTy->getNumElements();

    // Extract/Insert each element.
    for (unsigned i = 0; i != NumSrcElts; ++i) {
      llvm::Value *Elt = llvm::ConstantInt::get(llvm::Type::Int32Ty, i);
      Elt = Builder.CreateExtractElement(SrcVal, Elt, "tmp");
      
      unsigned Idx = OCUVectorElementExpr::getAccessedFieldNo(i, EncFields);
      llvm::Value *OutIdx = llvm::ConstantInt::get(llvm::Type::Int32Ty, Idx);
      Vec = Builder.CreateInsertElement(Vec, Elt, OutIdx, "tmp");
    }
  } else {
    // If the Src is a scalar (not a vector) it must be updating one element.
    unsigned InIdx = OCUVectorElementExpr::getAccessedFieldNo(0, EncFields);
    llvm::Value *Elt = llvm::ConstantInt::get(llvm::Type::Int32Ty, InIdx);
    Vec = Builder.CreateInsertElement(Vec, SrcVal, Elt, "tmp");
  }
  
  Builder.CreateStore(Vec, Dst.getOCUVectorAddr());
}


LValue CodeGenFunction::EmitDeclRefLValue(const DeclRefExpr *E) {
  const Decl *D = E->getDecl();
  if (isa<BlockVarDecl>(D) || isa<ParmVarDecl>(D)) {
    llvm::Value *V = LocalDeclMap[D];
    assert(V && "BlockVarDecl not entered in LocalDeclMap?");
    return LValue::MakeAddr(V);
  } else if (isa<FunctionDecl>(D) || isa<FileVarDecl>(D)) {
    return LValue::MakeAddr(CGM.GetAddrOfGlobalDecl(D));
  }
  assert(0 && "Unimp declref");
}

LValue CodeGenFunction::EmitUnaryOpLValue(const UnaryOperator *E) {
  // __extension__ doesn't affect lvalue-ness.
  if (E->getOpcode() == UnaryOperator::Extension)
    return EmitLValue(E->getSubExpr());
  
  assert(E->getOpcode() == UnaryOperator::Deref &&
         "'*' is the only unary operator that produces an lvalue");
  return LValue::MakeAddr(EmitScalarExpr(E->getSubExpr()));
}

LValue CodeGenFunction::EmitStringLiteralLValue(const StringLiteral *E) {
  assert(!E->isWide() && "FIXME: Wide strings not supported yet!");
  const char *StrData = E->getStrData();
  unsigned Len = E->getByteLength();
  
  // FIXME: Can cache/reuse these within the module.
  llvm::Constant *C=llvm::ConstantArray::get(std::string(StrData, StrData+Len));
  
  // Create a global variable for this.
  C = new llvm::GlobalVariable(C->getType(), true, 
                               llvm::GlobalValue::InternalLinkage,
                               C, ".str", CurFn->getParent());
  llvm::Constant *Zero = llvm::Constant::getNullValue(llvm::Type::Int32Ty);
  llvm::Constant *Zeros[] = { Zero, Zero };
  C = llvm::ConstantExpr::getGetElementPtr(C, Zeros, 2);
  return LValue::MakeAddr(C);
}

LValue CodeGenFunction::EmitPreDefinedLValue(const PreDefinedExpr *E) {
  std::string FunctionName(CurFuncDecl->getName());
  std::string GlobalVarName;
  
  switch (E->getIdentType()) {
    default:
      assert(0 && "unknown pre-defined ident type");
    case PreDefinedExpr::Func:
      GlobalVarName = "__func__.";
      break;
    case PreDefinedExpr::Function:
      GlobalVarName = "__FUNCTION__.";
      break;
    case PreDefinedExpr::PrettyFunction:
      // FIXME:: Demangle C++ method names
      GlobalVarName = "__PRETTY_FUNCTION__.";
      break;
  }
  
  GlobalVarName += CurFuncDecl->getName();
  
  // FIXME: Can cache/reuse these within the module.
  llvm::Constant *C=llvm::ConstantArray::get(FunctionName);
  
  // Create a global variable for this.
  C = new llvm::GlobalVariable(C->getType(), true, 
                               llvm::GlobalValue::InternalLinkage,
                               C, GlobalVarName, CurFn->getParent());
  llvm::Constant *Zero = llvm::Constant::getNullValue(llvm::Type::Int32Ty);
  llvm::Constant *Zeros[] = { Zero, Zero };
  C = llvm::ConstantExpr::getGetElementPtr(C, Zeros, 2);
  return LValue::MakeAddr(C);
}

LValue CodeGenFunction::EmitArraySubscriptExpr(const ArraySubscriptExpr *E) {
  // The index must always be an integer, which is not an aggregate.  Emit it.
  llvm::Value *Idx = EmitScalarExpr(E->getIdx());
  
  // If the base is a vector type, then we are forming a vector element lvalue
  // with this subscript.
  if (E->getLHS()->getType()->isVectorType()) {
    // Emit the vector as an lvalue to get its address.
    LValue LHS = EmitLValue(E->getLHS());
    assert(LHS.isSimple() && "Can only subscript lvalue vectors here!");
    // FIXME: This should properly sign/zero/extend or truncate Idx to i32.
    return LValue::MakeVectorElt(LHS.getAddress(), Idx);
  }
  
  // The base must be a pointer, which is not an aggregate.  Emit it.
  llvm::Value *Base = EmitScalarExpr(E->getBase());
  
  // Extend or truncate the index type to 32 or 64-bits.
  QualType IdxTy  = E->getIdx()->getType();
  bool IdxSigned = IdxTy->isSignedIntegerType();
  unsigned IdxBitwidth = cast<llvm::IntegerType>(Idx->getType())->getBitWidth();
  if (IdxBitwidth != LLVMPointerWidth)
    Idx = Builder.CreateIntCast(Idx, llvm::IntegerType::get(LLVMPointerWidth),
                                IdxSigned, "idxprom");

  // We know that the pointer points to a type of the correct size, unless the
  // size is a VLA.
  if (!E->getType()->isConstantSizeType(getContext()))
    assert(0 && "VLA idx not implemented");
  return LValue::MakeAddr(Builder.CreateGEP(Base, Idx, "arrayidx"));
}

LValue CodeGenFunction::
EmitOCUVectorElementExpr(const OCUVectorElementExpr *E) {
  // Emit the base vector as an l-value.
  LValue Base = EmitLValue(E->getBase());
  assert(Base.isSimple() && "Can only subscript lvalue vectors here!");

  return LValue::MakeOCUVectorElt(Base.getAddress(), 
                                  E->getEncodedElementAccess());
}

//===--------------------------------------------------------------------===//
//                             Expression Emission
//===--------------------------------------------------------------------===//


RValue CodeGenFunction::EmitCallExpr(const CallExpr *E) {
  if (const ImplicitCastExpr *IcExpr = 
      dyn_cast<const ImplicitCastExpr>(E->getCallee()))
    if (const DeclRefExpr *DRExpr = 
        dyn_cast<const DeclRefExpr>(IcExpr->getSubExpr()))
      if (const FunctionDecl *FDecl = 
          dyn_cast<const FunctionDecl>(DRExpr->getDecl()))
        if (unsigned builtinID = FDecl->getIdentifier()->getBuiltinID())
          return EmitBuiltinExpr(builtinID, E);
        
  llvm::Value *Callee = EmitScalarExpr(E->getCallee());
  
  // The callee type will always be a pointer to function type, get the function
  // type.
  QualType CalleeTy = E->getCallee()->getType();
  CalleeTy = cast<PointerType>(CalleeTy.getCanonicalType())->getPointeeType();
  
  // Get information about the argument types.
  FunctionTypeProto::arg_type_iterator ArgTyIt = 0, ArgTyEnd = 0;
  
  // Calling unprototyped functions provides no argument info.
  if (const FunctionTypeProto *FTP = dyn_cast<FunctionTypeProto>(CalleeTy)) {
    ArgTyIt  = FTP->arg_type_begin();
    ArgTyEnd = FTP->arg_type_end();
  }
  
  llvm::SmallVector<llvm::Value*, 16> Args;
  
  // Handle struct-return functions by passing a pointer to the location that
  // we would like to return into.
  if (hasAggregateLLVMType(E->getType())) {
    // Create a temporary alloca to hold the result of the call. :(
    Args.push_back(CreateTempAlloca(ConvertType(E->getType())));
    // FIXME: set the stret attribute on the argument.
  }
  
  for (unsigned i = 0, e = E->getNumArgs(); i != e; ++i) {
    QualType ArgTy = E->getArg(i)->getType();
    
    if (!hasAggregateLLVMType(ArgTy)) {
      // Scalar argument is passed by-value.
      Args.push_back(EmitScalarExpr(E->getArg(i)));
      
      if (ArgTyIt == ArgTyEnd) {
        // Otherwise, if passing through "..." or to a function with no prototype,
        // perform the "default argument promotions" (C99 6.5.2.2p6), which
        // includes the usual unary conversions, but also promotes float to
        // double.
        // FIXME: remove this when the impcast is in place.
        if (Args.back()->getType() == llvm::Type::FloatTy)
          Args.back() = Builder.CreateFPExt(Args.back(), llvm::Type::DoubleTy,
                                            "tmp");
        // FIXME: Remove ArgIt when this is gone.
      }
    } else if (ArgTy->isComplexType()) {
      // Make a temporary alloca to pass the argument.
      llvm::Value *DestMem = CreateTempAlloca(ConvertType(ArgTy));
      EmitComplexExprIntoAddr(E->getArg(i), DestMem, false);
      Args.push_back(DestMem);
    } else {
      llvm::Value *DestMem = CreateTempAlloca(ConvertType(ArgTy));
      EmitAggExpr(E->getArg(i), DestMem, false);
      Args.push_back(DestMem);
    }
    
    if (ArgTyIt != ArgTyEnd)
      ++ArgTyIt;
  }
  
  llvm::Value *V = Builder.CreateCall(Callee, &Args[0], &Args[0]+Args.size());
  if (V->getType() != llvm::Type::VoidTy)
    V->setName("call");
  else if (hasAggregateLLVMType(E->getType()))
    // Struct return.
    return RValue::getAggregate(Args[0]);
      
  return RValue::get(V);
}
