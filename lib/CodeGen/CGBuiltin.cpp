//===---- CGBuiltin.cpp - Emit LLVM Code for builtins ---------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This contains code to emit Builtin calls as LLVM code.
//
//===----------------------------------------------------------------------===//

#include "TargetInfo.h"
#include "CodeGenFunction.h"
#include "CodeGenModule.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/AST/APValue.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/Basic/TargetBuiltins.h"
#include "llvm/Intrinsics.h"
#include "llvm/Target/TargetData.h"
using namespace clang;
using namespace CodeGen;
using namespace llvm;

static void EmitMemoryBarrier(CodeGenFunction &CGF,
                              bool LoadLoad, bool LoadStore,
                              bool StoreLoad, bool StoreStore,
                              bool Device) {
  Value *True = llvm::ConstantInt::getTrue(CGF.getLLVMContext());
  Value *False = llvm::ConstantInt::getFalse(CGF.getLLVMContext());
  Value *C[5] = { LoadLoad ? True : False,
                  LoadStore ? True : False,
                  StoreLoad ? True : False,
                  StoreStore  ? True : False,
                  Device ? True : False };
  CGF.Builder.CreateCall(CGF.CGM.getIntrinsic(Intrinsic::memory_barrier),
                         C, C + 5);
}

// The atomic builtins are also full memory barriers. This is a utility for
// wrapping a call to the builtins with memory barriers.
static Value *EmitCallWithBarrier(CodeGenFunction &CGF, Value *Fn,
                                  Value **ArgBegin, Value **ArgEnd) {
  // FIXME: We need a target hook for whether this applies to device memory or
  // not.
  bool Device = true;

  // Create barriers both before and after the call.
  EmitMemoryBarrier(CGF, true, true, true, true, Device);
  Value *Result = CGF.Builder.CreateCall(Fn, ArgBegin, ArgEnd);
  EmitMemoryBarrier(CGF, true, true, true, true, Device);
  return Result;
}

/// Utility to insert an atomic instruction based on Instrinsic::ID
/// and the expression node.
static RValue EmitBinaryAtomic(CodeGenFunction &CGF,
                               Intrinsic::ID Id, const CallExpr *E) {
  Value *Args[2] = { CGF.EmitScalarExpr(E->getArg(0)),
                     CGF.EmitScalarExpr(E->getArg(1)) };
  const llvm::Type *ResType[2];
  ResType[0] = CGF.ConvertType(E->getType());
  ResType[1] = CGF.ConvertType(E->getArg(0)->getType());
  Value *AtomF = CGF.CGM.getIntrinsic(Id, ResType, 2);
  return RValue::get(EmitCallWithBarrier(CGF, AtomF, Args, Args + 2));
}

/// Utility to insert an atomic instruction based Instrinsic::ID and
// the expression node, where the return value is the result of the
// operation.
static RValue EmitBinaryAtomicPost(CodeGenFunction &CGF,
                                   Intrinsic::ID Id, const CallExpr *E,
                                   Instruction::BinaryOps Op) {
  const llvm::Type *ResType[2];
  ResType[0] = CGF.ConvertType(E->getType());
  ResType[1] = CGF.ConvertType(E->getArg(0)->getType());
  Value *AtomF = CGF.CGM.getIntrinsic(Id, ResType, 2);
  Value *Args[2] = { CGF.EmitScalarExpr(E->getArg(0)),
                     CGF.EmitScalarExpr(E->getArg(1)) };
  Value *Result = EmitCallWithBarrier(CGF, AtomF, Args, Args + 2);
  return RValue::get(CGF.Builder.CreateBinOp(Op, Result, Args[1]));
}

static llvm::ConstantInt *getInt32(llvm::LLVMContext &Context, int32_t Value) {
  return llvm::ConstantInt::get(llvm::Type::getInt32Ty(Context), Value);
}


/// EmitFAbs - Emit a call to fabs/fabsf/fabsl, depending on the type of ValTy,
/// which must be a scalar floating point type.
static Value *EmitFAbs(CodeGenFunction &CGF, Value *V, QualType ValTy) {
  const BuiltinType *ValTyP = ValTy->getAs<BuiltinType>();
  assert(ValTyP && "isn't scalar fp type!");
  
  StringRef FnName;
  switch (ValTyP->getKind()) {
  default: assert(0 && "Isn't a scalar fp type!");
  case BuiltinType::Float:      FnName = "fabsf"; break;
  case BuiltinType::Double:     FnName = "fabs"; break;
  case BuiltinType::LongDouble: FnName = "fabsl"; break;
  }
  
  // The prototype is something that takes and returns whatever V's type is.
  std::vector<const llvm::Type*> Args;
  Args.push_back(V->getType());
  llvm::FunctionType *FT = llvm::FunctionType::get(V->getType(), Args, false);
  llvm::Value *Fn = CGF.CGM.CreateRuntimeFunction(FT, FnName);

  return CGF.Builder.CreateCall(Fn, V, "abs");
}

RValue CodeGenFunction::EmitBuiltinExpr(const FunctionDecl *FD,
                                        unsigned BuiltinID, const CallExpr *E) {
  // See if we can constant fold this builtin.  If so, don't emit it at all.
  Expr::EvalResult Result;
  if (E->Evaluate(Result, CGM.getContext())) {
    if (Result.Val.isInt())
      return RValue::get(llvm::ConstantInt::get(VMContext,
                                                Result.Val.getInt()));
    else if (Result.Val.isFloat())
      return RValue::get(ConstantFP::get(VMContext, Result.Val.getFloat()));
  }

  switch (BuiltinID) {
  default: break;  // Handle intrinsics and libm functions below.
  case Builtin::BI__builtin___CFStringMakeConstantString:
  case Builtin::BI__builtin___NSStringMakeConstantString:
    return RValue::get(CGM.EmitConstantExpr(E, E->getType(), 0));
  case Builtin::BI__builtin_stdarg_start:
  case Builtin::BI__builtin_va_start:
  case Builtin::BI__builtin_va_end: {
    Value *ArgValue = EmitVAListRef(E->getArg(0));
    const llvm::Type *DestType = llvm::Type::getInt8PtrTy(VMContext);
    if (ArgValue->getType() != DestType)
      ArgValue = Builder.CreateBitCast(ArgValue, DestType,
                                       ArgValue->getName().data());

    Intrinsic::ID inst = (BuiltinID == Builtin::BI__builtin_va_end) ?
      Intrinsic::vaend : Intrinsic::vastart;
    return RValue::get(Builder.CreateCall(CGM.getIntrinsic(inst), ArgValue));
  }
  case Builtin::BI__builtin_va_copy: {
    Value *DstPtr = EmitVAListRef(E->getArg(0));
    Value *SrcPtr = EmitVAListRef(E->getArg(1));

    const llvm::Type *Type = llvm::Type::getInt8PtrTy(VMContext);

    DstPtr = Builder.CreateBitCast(DstPtr, Type);
    SrcPtr = Builder.CreateBitCast(SrcPtr, Type);
    return RValue::get(Builder.CreateCall2(CGM.getIntrinsic(Intrinsic::vacopy),
                                           DstPtr, SrcPtr));
  }
  case Builtin::BI__builtin_abs: {
    Value *ArgValue = EmitScalarExpr(E->getArg(0));

    Value *NegOp = Builder.CreateNeg(ArgValue, "neg");
    Value *CmpResult =
    Builder.CreateICmpSGE(ArgValue,
                          llvm::Constant::getNullValue(ArgValue->getType()),
                                                            "abscond");
    Value *Result =
      Builder.CreateSelect(CmpResult, ArgValue, NegOp, "abs");

    return RValue::get(Result);
  }
  case Builtin::BI__builtin_ctz:
  case Builtin::BI__builtin_ctzl:
  case Builtin::BI__builtin_ctzll: {
    Value *ArgValue = EmitScalarExpr(E->getArg(0));

    const llvm::Type *ArgType = ArgValue->getType();
    Value *F = CGM.getIntrinsic(Intrinsic::cttz, &ArgType, 1);

    const llvm::Type *ResultType = ConvertType(E->getType());
    Value *Result = Builder.CreateCall(F, ArgValue, "tmp");
    if (Result->getType() != ResultType)
      Result = Builder.CreateIntCast(Result, ResultType, /*isSigned*/true,
                                     "cast");
    return RValue::get(Result);
  }
  case Builtin::BI__builtin_clz:
  case Builtin::BI__builtin_clzl:
  case Builtin::BI__builtin_clzll: {
    Value *ArgValue = EmitScalarExpr(E->getArg(0));

    const llvm::Type *ArgType = ArgValue->getType();
    Value *F = CGM.getIntrinsic(Intrinsic::ctlz, &ArgType, 1);

    const llvm::Type *ResultType = ConvertType(E->getType());
    Value *Result = Builder.CreateCall(F, ArgValue, "tmp");
    if (Result->getType() != ResultType)
      Result = Builder.CreateIntCast(Result, ResultType, /*isSigned*/true,
                                     "cast");
    return RValue::get(Result);
  }
  case Builtin::BI__builtin_ffs:
  case Builtin::BI__builtin_ffsl:
  case Builtin::BI__builtin_ffsll: {
    // ffs(x) -> x ? cttz(x) + 1 : 0
    Value *ArgValue = EmitScalarExpr(E->getArg(0));

    const llvm::Type *ArgType = ArgValue->getType();
    Value *F = CGM.getIntrinsic(Intrinsic::cttz, &ArgType, 1);

    const llvm::Type *ResultType = ConvertType(E->getType());
    Value *Tmp = Builder.CreateAdd(Builder.CreateCall(F, ArgValue, "tmp"),
                                   llvm::ConstantInt::get(ArgType, 1), "tmp");
    Value *Zero = llvm::Constant::getNullValue(ArgType);
    Value *IsZero = Builder.CreateICmpEQ(ArgValue, Zero, "iszero");
    Value *Result = Builder.CreateSelect(IsZero, Zero, Tmp, "ffs");
    if (Result->getType() != ResultType)
      Result = Builder.CreateIntCast(Result, ResultType, /*isSigned*/true,
                                     "cast");
    return RValue::get(Result);
  }
  case Builtin::BI__builtin_parity:
  case Builtin::BI__builtin_parityl:
  case Builtin::BI__builtin_parityll: {
    // parity(x) -> ctpop(x) & 1
    Value *ArgValue = EmitScalarExpr(E->getArg(0));

    const llvm::Type *ArgType = ArgValue->getType();
    Value *F = CGM.getIntrinsic(Intrinsic::ctpop, &ArgType, 1);

    const llvm::Type *ResultType = ConvertType(E->getType());
    Value *Tmp = Builder.CreateCall(F, ArgValue, "tmp");
    Value *Result = Builder.CreateAnd(Tmp, llvm::ConstantInt::get(ArgType, 1),
                                      "tmp");
    if (Result->getType() != ResultType)
      Result = Builder.CreateIntCast(Result, ResultType, /*isSigned*/true,
                                     "cast");
    return RValue::get(Result);
  }
  case Builtin::BI__builtin_popcount:
  case Builtin::BI__builtin_popcountl:
  case Builtin::BI__builtin_popcountll: {
    Value *ArgValue = EmitScalarExpr(E->getArg(0));

    const llvm::Type *ArgType = ArgValue->getType();
    Value *F = CGM.getIntrinsic(Intrinsic::ctpop, &ArgType, 1);

    const llvm::Type *ResultType = ConvertType(E->getType());
    Value *Result = Builder.CreateCall(F, ArgValue, "tmp");
    if (Result->getType() != ResultType)
      Result = Builder.CreateIntCast(Result, ResultType, /*isSigned*/true,
                                     "cast");
    return RValue::get(Result);
  }
  case Builtin::BI__builtin_expect:
    // FIXME: pass expect through to LLVM
    return RValue::get(EmitScalarExpr(E->getArg(0)));
  case Builtin::BI__builtin_bswap32:
  case Builtin::BI__builtin_bswap64: {
    Value *ArgValue = EmitScalarExpr(E->getArg(0));
    const llvm::Type *ArgType = ArgValue->getType();
    Value *F = CGM.getIntrinsic(Intrinsic::bswap, &ArgType, 1);
    return RValue::get(Builder.CreateCall(F, ArgValue, "tmp"));
  }
  case Builtin::BI__builtin_object_size: {
    // We pass this builtin onto the optimizer so that it can
    // figure out the object size in more complex cases.
    const llvm::Type *ResType[] = {
      ConvertType(E->getType())
    };
    
    // LLVM only supports 0 and 2, make sure that we pass along that
    // as a boolean.
    Value *Ty = EmitScalarExpr(E->getArg(1));
    ConstantInt *CI = dyn_cast<ConstantInt>(Ty);
    assert(CI);
    uint64_t val = CI->getZExtValue();
    CI = ConstantInt::get(llvm::Type::getInt1Ty(VMContext), (val & 0x2) >> 1);    
    
    Value *F = CGM.getIntrinsic(Intrinsic::objectsize, ResType, 1);
    return RValue::get(Builder.CreateCall2(F,
                                           EmitScalarExpr(E->getArg(0)),
                                           CI));
  }
  case Builtin::BI__builtin_prefetch: {
    Value *Locality, *RW, *Address = EmitScalarExpr(E->getArg(0));
    // FIXME: Technically these constants should of type 'int', yes?
    RW = (E->getNumArgs() > 1) ? EmitScalarExpr(E->getArg(1)) :
      llvm::ConstantInt::get(llvm::Type::getInt32Ty(VMContext), 0);
    Locality = (E->getNumArgs() > 2) ? EmitScalarExpr(E->getArg(2)) :
      llvm::ConstantInt::get(llvm::Type::getInt32Ty(VMContext), 3);
    Value *F = CGM.getIntrinsic(Intrinsic::prefetch, 0, 0);
    return RValue::get(Builder.CreateCall3(F, Address, RW, Locality));
  }
  case Builtin::BI__builtin_trap: {
    Value *F = CGM.getIntrinsic(Intrinsic::trap, 0, 0);
    return RValue::get(Builder.CreateCall(F));
  }
  case Builtin::BI__builtin_unreachable: {
    if (CatchUndefined && HaveInsertPoint())
      EmitBranch(getTrapBB());
    Value *V = Builder.CreateUnreachable();
    Builder.ClearInsertionPoint();
    return RValue::get(V);
  }
      
  case Builtin::BI__builtin_powi:
  case Builtin::BI__builtin_powif:
  case Builtin::BI__builtin_powil: {
    Value *Base = EmitScalarExpr(E->getArg(0));
    Value *Exponent = EmitScalarExpr(E->getArg(1));
    const llvm::Type *ArgType = Base->getType();
    Value *F = CGM.getIntrinsic(Intrinsic::powi, &ArgType, 1);
    return RValue::get(Builder.CreateCall2(F, Base, Exponent, "tmp"));
  }

  case Builtin::BI__builtin_isgreater:
  case Builtin::BI__builtin_isgreaterequal:
  case Builtin::BI__builtin_isless:
  case Builtin::BI__builtin_islessequal:
  case Builtin::BI__builtin_islessgreater:
  case Builtin::BI__builtin_isunordered: {
    // Ordered comparisons: we know the arguments to these are matching scalar
    // floating point values.
    Value *LHS = EmitScalarExpr(E->getArg(0));
    Value *RHS = EmitScalarExpr(E->getArg(1));

    switch (BuiltinID) {
    default: assert(0 && "Unknown ordered comparison");
    case Builtin::BI__builtin_isgreater:
      LHS = Builder.CreateFCmpOGT(LHS, RHS, "cmp");
      break;
    case Builtin::BI__builtin_isgreaterequal:
      LHS = Builder.CreateFCmpOGE(LHS, RHS, "cmp");
      break;
    case Builtin::BI__builtin_isless:
      LHS = Builder.CreateFCmpOLT(LHS, RHS, "cmp");
      break;
    case Builtin::BI__builtin_islessequal:
      LHS = Builder.CreateFCmpOLE(LHS, RHS, "cmp");
      break;
    case Builtin::BI__builtin_islessgreater:
      LHS = Builder.CreateFCmpONE(LHS, RHS, "cmp");
      break;
    case Builtin::BI__builtin_isunordered:
      LHS = Builder.CreateFCmpUNO(LHS, RHS, "cmp");
      break;
    }
    // ZExt bool to int type.
    return RValue::get(Builder.CreateZExt(LHS, ConvertType(E->getType()),
                                          "tmp"));
  }
  case Builtin::BI__builtin_isnan: {
    Value *V = EmitScalarExpr(E->getArg(0));
    V = Builder.CreateFCmpUNO(V, V, "cmp");
    return RValue::get(Builder.CreateZExt(V, ConvertType(E->getType()), "tmp"));
  }
  
  case Builtin::BI__builtin_isinf: {
    // isinf(x) --> fabs(x) == infinity
    Value *V = EmitScalarExpr(E->getArg(0));
    V = EmitFAbs(*this, V, E->getArg(0)->getType());
    
    V = Builder.CreateFCmpOEQ(V, ConstantFP::getInfinity(V->getType()),"isinf");
    return RValue::get(Builder.CreateZExt(V, ConvertType(E->getType()), "tmp"));
  }
      
  // TODO: BI__builtin_isinf_sign
  //   isinf_sign(x) -> isinf(x) ? (signbit(x) ? -1 : 1) : 0

  case Builtin::BI__builtin_isnormal: {
    // isnormal(x) --> x == x && fabsf(x) < infinity && fabsf(x) >= float_min
    Value *V = EmitScalarExpr(E->getArg(0));
    Value *Eq = Builder.CreateFCmpOEQ(V, V, "iseq");

    Value *Abs = EmitFAbs(*this, V, E->getArg(0)->getType());
    Value *IsLessThanInf =
      Builder.CreateFCmpULT(Abs, ConstantFP::getInfinity(V->getType()),"isinf");
    APFloat Smallest = APFloat::getSmallestNormalized(
                   getContext().getFloatTypeSemantics(E->getArg(0)->getType()));
    Value *IsNormal =
      Builder.CreateFCmpUGE(Abs, ConstantFP::get(V->getContext(), Smallest),
                            "isnormal");
    V = Builder.CreateAnd(Eq, IsLessThanInf, "and");
    V = Builder.CreateAnd(V, IsNormal, "and");
    return RValue::get(Builder.CreateZExt(V, ConvertType(E->getType())));
  }

  case Builtin::BI__builtin_isfinite: {
    // isfinite(x) --> x == x && fabs(x) != infinity; }
    Value *V = EmitScalarExpr(E->getArg(0));
    Value *Eq = Builder.CreateFCmpOEQ(V, V, "iseq");
    
    Value *Abs = EmitFAbs(*this, V, E->getArg(0)->getType());
    Value *IsNotInf =
      Builder.CreateFCmpUNE(Abs, ConstantFP::getInfinity(V->getType()),"isinf");
    
    V = Builder.CreateAnd(Eq, IsNotInf, "and");
    return RValue::get(Builder.CreateZExt(V, ConvertType(E->getType())));
  }
      
  case Builtin::BIalloca:
  case Builtin::BI__builtin_alloca: {
    Value *Size = EmitScalarExpr(E->getArg(0));
    return RValue::get(Builder.CreateAlloca(llvm::Type::getInt8Ty(VMContext), Size, "tmp"));
  }
  case Builtin::BIbzero:
  case Builtin::BI__builtin_bzero: {
    Value *Address = EmitScalarExpr(E->getArg(0));
    Value *SizeVal = EmitScalarExpr(E->getArg(1));
    Builder.CreateCall5(CGM.getMemSetFn(Address->getType(), SizeVal->getType()),
                   Address,
                   llvm::ConstantInt::get(llvm::Type::getInt8Ty(VMContext), 0),
                   SizeVal,
                   llvm::ConstantInt::get(llvm::Type::getInt32Ty(VMContext), 1),
                   llvm::ConstantInt::get(llvm::Type::getInt1Ty(VMContext), 0));
    return RValue::get(Address);
  }
  case Builtin::BImemcpy:
  case Builtin::BI__builtin_memcpy: {
    Value *Address = EmitScalarExpr(E->getArg(0));
    Value *SrcAddr = EmitScalarExpr(E->getArg(1));
    Value *SizeVal = EmitScalarExpr(E->getArg(2));
    Builder.CreateCall5(CGM.getMemCpyFn(Address->getType(), SrcAddr->getType(),
                                        SizeVal->getType()),
                  Address, SrcAddr, SizeVal, 
                  llvm::ConstantInt::get(llvm::Type::getInt32Ty(VMContext), 1),
                  llvm::ConstantInt::get(llvm::Type::getInt1Ty(VMContext), 0));
    return RValue::get(Address);
  }
  case Builtin::BImemmove:
  case Builtin::BI__builtin_memmove: {
    Value *Address = EmitScalarExpr(E->getArg(0));
    Value *SrcAddr = EmitScalarExpr(E->getArg(1));
    Value *SizeVal = EmitScalarExpr(E->getArg(2));
    Builder.CreateCall5(CGM.getMemMoveFn(Address->getType(), SrcAddr->getType(),
                                         SizeVal->getType()),
                  Address, SrcAddr, SizeVal, 
                  llvm::ConstantInt::get(llvm::Type::getInt32Ty(VMContext), 1),
                  llvm::ConstantInt::get(llvm::Type::getInt1Ty(VMContext), 0));
    return RValue::get(Address);
  }
  case Builtin::BImemset:
  case Builtin::BI__builtin_memset: {
    Value *Address = EmitScalarExpr(E->getArg(0));
    Value *SizeVal = EmitScalarExpr(E->getArg(2));
    Builder.CreateCall5(CGM.getMemSetFn(Address->getType(), SizeVal->getType()),
                  Address,
                  Builder.CreateTrunc(EmitScalarExpr(E->getArg(1)),
                                      llvm::Type::getInt8Ty(VMContext)),
                  SizeVal,
                  llvm::ConstantInt::get(llvm::Type::getInt32Ty(VMContext), 1),
                  llvm::ConstantInt::get(llvm::Type::getInt1Ty(VMContext), 0));
    return RValue::get(Address);
  }
  case Builtin::BI__builtin_dwarf_cfa: {
    // The offset in bytes from the first argument to the CFA.
    //
    // Why on earth is this in the frontend?  Is there any reason at
    // all that the backend can't reasonably determine this while
    // lowering llvm.eh.dwarf.cfa()?
    //
    // TODO: If there's a satisfactory reason, add a target hook for
    // this instead of hard-coding 0, which is correct for most targets.
    int32_t Offset = 0;

    Value *F = CGM.getIntrinsic(Intrinsic::eh_dwarf_cfa, 0, 0);
    return RValue::get(Builder.CreateCall(F, getInt32(VMContext, Offset)));
  }
  case Builtin::BI__builtin_return_address: {
    Value *Depth = EmitScalarExpr(E->getArg(0));
    Depth = Builder.CreateIntCast(Depth,
                                  llvm::Type::getInt32Ty(VMContext),
                                  false, "tmp");
    Value *F = CGM.getIntrinsic(Intrinsic::returnaddress, 0, 0);
    return RValue::get(Builder.CreateCall(F, Depth));
  }
  case Builtin::BI__builtin_frame_address: {
    Value *Depth = EmitScalarExpr(E->getArg(0));
    Depth = Builder.CreateIntCast(Depth,
                                  llvm::Type::getInt32Ty(VMContext),
                                  false, "tmp");
    Value *F = CGM.getIntrinsic(Intrinsic::frameaddress, 0, 0);
    return RValue::get(Builder.CreateCall(F, Depth));
  }
  case Builtin::BI__builtin_extract_return_addr: {
    Value *Address = EmitScalarExpr(E->getArg(0));
    Value *Result = getTargetHooks().decodeReturnAddress(*this, Address);
    return RValue::get(Result);
  }
  case Builtin::BI__builtin_frob_return_addr: {
    Value *Address = EmitScalarExpr(E->getArg(0));
    Value *Result = getTargetHooks().encodeReturnAddress(*this, Address);
    return RValue::get(Result);
  }
  case Builtin::BI__builtin_dwarf_sp_column: {
    const llvm::IntegerType *Ty
      = cast<llvm::IntegerType>(ConvertType(E->getType()));
    int Column = getTargetHooks().getDwarfEHStackPointer(CGM);
    if (Column == -1) {
      CGM.ErrorUnsupported(E, "__builtin_dwarf_sp_column");
      return RValue::get(llvm::UndefValue::get(Ty));
    }
    return RValue::get(llvm::ConstantInt::get(Ty, Column, true));
  }
  case Builtin::BI__builtin_init_dwarf_reg_size_table: {
    Value *Address = EmitScalarExpr(E->getArg(0));
    if (getTargetHooks().initDwarfEHRegSizeTable(*this, Address))
      CGM.ErrorUnsupported(E, "__builtin_init_dwarf_reg_size_table");
    return RValue::get(llvm::UndefValue::get(ConvertType(E->getType())));
  }
  case Builtin::BI__builtin_eh_return: {
    Value *Int = EmitScalarExpr(E->getArg(0));
    Value *Ptr = EmitScalarExpr(E->getArg(1));

    const llvm::IntegerType *IntTy = cast<llvm::IntegerType>(Int->getType());
    assert((IntTy->getBitWidth() == 32 || IntTy->getBitWidth() == 64) &&
           "LLVM's __builtin_eh_return only supports 32- and 64-bit variants");
    Value *F = CGM.getIntrinsic(IntTy->getBitWidth() == 32
                                  ? Intrinsic::eh_return_i32
                                  : Intrinsic::eh_return_i64,
                                0, 0);
    Builder.CreateCall2(F, Int, Ptr);
    Value *V = Builder.CreateUnreachable();
    Builder.ClearInsertionPoint();
    return RValue::get(V);
  }
  case Builtin::BI__builtin_unwind_init: {
    Value *F = CGM.getIntrinsic(Intrinsic::eh_unwind_init, 0, 0);
    return RValue::get(Builder.CreateCall(F));
  }
  case Builtin::BI__builtin_extend_pointer: {
    // Extends a pointer to the size of an _Unwind_Word, which is
    // uint64_t on all platforms.  Generally this gets poked into a
    // register and eventually used as an address, so if the
    // addressing registers are wider than pointers and the platform
    // doesn't implicitly ignore high-order bits when doing
    // addressing, we need to make sure we zext / sext based on
    // the platform's expectations.
    //
    // See: http://gcc.gnu.org/ml/gcc-bugs/2002-02/msg00237.html

    LLVMContext &C = CGM.getLLVMContext();

    // Cast the pointer to intptr_t.
    Value *Ptr = EmitScalarExpr(E->getArg(0));
    const llvm::IntegerType *IntPtrTy = CGM.getTargetData().getIntPtrType(C);
    Value *Result = Builder.CreatePtrToInt(Ptr, IntPtrTy, "extend.cast");

    // If that's 64 bits, we're done.
    if (IntPtrTy->getBitWidth() == 64)
      return RValue::get(Result);

    // Otherwise, ask the codegen data what to do.
    const llvm::IntegerType *Int64Ty = llvm::IntegerType::get(C, 64);
    if (getTargetHooks().extendPointerWithSExt())
      return RValue::get(Builder.CreateSExt(Result, Int64Ty, "extend.sext"));
    else
      return RValue::get(Builder.CreateZExt(Result, Int64Ty, "extend.zext"));
  }
  case Builtin::BI__builtin_setjmp: {
    // Buffer is a void**.
    Value *Buf = EmitScalarExpr(E->getArg(0));

    // Store the frame pointer to the setjmp buffer.
    Value *FrameAddr =
      Builder.CreateCall(CGM.getIntrinsic(Intrinsic::frameaddress),
                         ConstantInt::get(llvm::Type::getInt32Ty(VMContext), 0));
    Builder.CreateStore(FrameAddr, Buf);

    // Store the stack pointer to the setjmp buffer.
    Value *StackAddr =
      Builder.CreateCall(CGM.getIntrinsic(Intrinsic::stacksave));
    Value *StackSaveSlot =
      Builder.CreateGEP(Buf, ConstantInt::get(llvm::Type::getInt32Ty(VMContext),
                                              2));
    Builder.CreateStore(StackAddr, StackSaveSlot);

    // Call LLVM's EH setjmp, which is lightweight.
    Value *F = CGM.getIntrinsic(Intrinsic::eh_sjlj_setjmp);
    Buf = Builder.CreateBitCast(Buf, llvm::Type::getInt8PtrTy(VMContext));
    return RValue::get(Builder.CreateCall(F, Buf));
  }
  case Builtin::BI__builtin_longjmp: {
    Value *Buf = EmitScalarExpr(E->getArg(0));
    Buf = Builder.CreateBitCast(Buf, llvm::Type::getInt8PtrTy(VMContext));

    // Call LLVM's EH longjmp, which is lightweight.
    Builder.CreateCall(CGM.getIntrinsic(Intrinsic::eh_sjlj_longjmp), Buf);

    // longjmp doesn't return; mark this as unreachable
    Value *V = Builder.CreateUnreachable();
    Builder.ClearInsertionPoint();
    return RValue::get(V);
  }
  case Builtin::BI__sync_fetch_and_add:
  case Builtin::BI__sync_fetch_and_sub:
  case Builtin::BI__sync_fetch_and_or:
  case Builtin::BI__sync_fetch_and_and:
  case Builtin::BI__sync_fetch_and_xor:
  case Builtin::BI__sync_add_and_fetch:
  case Builtin::BI__sync_sub_and_fetch:
  case Builtin::BI__sync_and_and_fetch:
  case Builtin::BI__sync_or_and_fetch:
  case Builtin::BI__sync_xor_and_fetch:
  case Builtin::BI__sync_val_compare_and_swap:
  case Builtin::BI__sync_bool_compare_and_swap:
  case Builtin::BI__sync_lock_test_and_set:
  case Builtin::BI__sync_lock_release:
    assert(0 && "Shouldn't make it through sema");
  case Builtin::BI__sync_fetch_and_add_1:
  case Builtin::BI__sync_fetch_and_add_2:
  case Builtin::BI__sync_fetch_and_add_4:
  case Builtin::BI__sync_fetch_and_add_8:
  case Builtin::BI__sync_fetch_and_add_16:
    return EmitBinaryAtomic(*this, Intrinsic::atomic_load_add, E);
  case Builtin::BI__sync_fetch_and_sub_1:
  case Builtin::BI__sync_fetch_and_sub_2:
  case Builtin::BI__sync_fetch_and_sub_4:
  case Builtin::BI__sync_fetch_and_sub_8:
  case Builtin::BI__sync_fetch_and_sub_16:
    return EmitBinaryAtomic(*this, Intrinsic::atomic_load_sub, E);
  case Builtin::BI__sync_fetch_and_or_1:
  case Builtin::BI__sync_fetch_and_or_2:
  case Builtin::BI__sync_fetch_and_or_4:
  case Builtin::BI__sync_fetch_and_or_8:
  case Builtin::BI__sync_fetch_and_or_16:
    return EmitBinaryAtomic(*this, Intrinsic::atomic_load_or, E);
  case Builtin::BI__sync_fetch_and_and_1:
  case Builtin::BI__sync_fetch_and_and_2:
  case Builtin::BI__sync_fetch_and_and_4:
  case Builtin::BI__sync_fetch_and_and_8:
  case Builtin::BI__sync_fetch_and_and_16:
    return EmitBinaryAtomic(*this, Intrinsic::atomic_load_and, E);
  case Builtin::BI__sync_fetch_and_xor_1:
  case Builtin::BI__sync_fetch_and_xor_2:
  case Builtin::BI__sync_fetch_and_xor_4:
  case Builtin::BI__sync_fetch_and_xor_8:
  case Builtin::BI__sync_fetch_and_xor_16:
    return EmitBinaryAtomic(*this, Intrinsic::atomic_load_xor, E);

  // Clang extensions: not overloaded yet.
  case Builtin::BI__sync_fetch_and_min:
    return EmitBinaryAtomic(*this, Intrinsic::atomic_load_min, E);
  case Builtin::BI__sync_fetch_and_max:
    return EmitBinaryAtomic(*this, Intrinsic::atomic_load_max, E);
  case Builtin::BI__sync_fetch_and_umin:
    return EmitBinaryAtomic(*this, Intrinsic::atomic_load_umin, E);
  case Builtin::BI__sync_fetch_and_umax:
    return EmitBinaryAtomic(*this, Intrinsic::atomic_load_umax, E);

  case Builtin::BI__sync_add_and_fetch_1:
  case Builtin::BI__sync_add_and_fetch_2:
  case Builtin::BI__sync_add_and_fetch_4:
  case Builtin::BI__sync_add_and_fetch_8:
  case Builtin::BI__sync_add_and_fetch_16:
    return EmitBinaryAtomicPost(*this, Intrinsic::atomic_load_add, E,
                                llvm::Instruction::Add);
  case Builtin::BI__sync_sub_and_fetch_1:
  case Builtin::BI__sync_sub_and_fetch_2:
  case Builtin::BI__sync_sub_and_fetch_4:
  case Builtin::BI__sync_sub_and_fetch_8:
  case Builtin::BI__sync_sub_and_fetch_16:
    return EmitBinaryAtomicPost(*this, Intrinsic::atomic_load_sub, E,
                                llvm::Instruction::Sub);
  case Builtin::BI__sync_and_and_fetch_1:
  case Builtin::BI__sync_and_and_fetch_2:
  case Builtin::BI__sync_and_and_fetch_4:
  case Builtin::BI__sync_and_and_fetch_8:
  case Builtin::BI__sync_and_and_fetch_16:
    return EmitBinaryAtomicPost(*this, Intrinsic::atomic_load_and, E,
                                llvm::Instruction::And);
  case Builtin::BI__sync_or_and_fetch_1:
  case Builtin::BI__sync_or_and_fetch_2:
  case Builtin::BI__sync_or_and_fetch_4:
  case Builtin::BI__sync_or_and_fetch_8:
  case Builtin::BI__sync_or_and_fetch_16:
    return EmitBinaryAtomicPost(*this, Intrinsic::atomic_load_or, E,
                                llvm::Instruction::Or);
  case Builtin::BI__sync_xor_and_fetch_1:
  case Builtin::BI__sync_xor_and_fetch_2:
  case Builtin::BI__sync_xor_and_fetch_4:
  case Builtin::BI__sync_xor_and_fetch_8:
  case Builtin::BI__sync_xor_and_fetch_16:
    return EmitBinaryAtomicPost(*this, Intrinsic::atomic_load_xor, E,
                                llvm::Instruction::Xor);

  case Builtin::BI__sync_val_compare_and_swap_1:
  case Builtin::BI__sync_val_compare_and_swap_2:
  case Builtin::BI__sync_val_compare_and_swap_4:
  case Builtin::BI__sync_val_compare_and_swap_8:
  case Builtin::BI__sync_val_compare_and_swap_16: {
    const llvm::Type *ResType[2];
    ResType[0]= ConvertType(E->getType());
    ResType[1] = ConvertType(E->getArg(0)->getType());
    Value *AtomF = CGM.getIntrinsic(Intrinsic::atomic_cmp_swap, ResType, 2);
    Value *Args[3] = { EmitScalarExpr(E->getArg(0)),
                       EmitScalarExpr(E->getArg(1)),
                       EmitScalarExpr(E->getArg(2)) };
    return RValue::get(EmitCallWithBarrier(*this, AtomF, Args, Args + 3));
  }

  case Builtin::BI__sync_bool_compare_and_swap_1:
  case Builtin::BI__sync_bool_compare_and_swap_2:
  case Builtin::BI__sync_bool_compare_and_swap_4:
  case Builtin::BI__sync_bool_compare_and_swap_8:
  case Builtin::BI__sync_bool_compare_and_swap_16: {
    const llvm::Type *ResType[2];
    ResType[0]= ConvertType(E->getArg(1)->getType());
    ResType[1] = llvm::PointerType::getUnqual(ResType[0]);
    Value *AtomF = CGM.getIntrinsic(Intrinsic::atomic_cmp_swap, ResType, 2);
    Value *OldVal = EmitScalarExpr(E->getArg(1));
    Value *Args[3] = { EmitScalarExpr(E->getArg(0)),
                       OldVal,
                       EmitScalarExpr(E->getArg(2)) };
    Value *PrevVal = EmitCallWithBarrier(*this, AtomF, Args, Args + 3);
    Value *Result = Builder.CreateICmpEQ(PrevVal, OldVal);
    // zext bool to int.
    return RValue::get(Builder.CreateZExt(Result, ConvertType(E->getType())));
  }

  case Builtin::BI__sync_lock_test_and_set_1:
  case Builtin::BI__sync_lock_test_and_set_2:
  case Builtin::BI__sync_lock_test_and_set_4:
  case Builtin::BI__sync_lock_test_and_set_8:
  case Builtin::BI__sync_lock_test_and_set_16:
    return EmitBinaryAtomic(*this, Intrinsic::atomic_swap, E);

  case Builtin::BI__sync_lock_release_1:
  case Builtin::BI__sync_lock_release_2:
  case Builtin::BI__sync_lock_release_4:
  case Builtin::BI__sync_lock_release_8:
  case Builtin::BI__sync_lock_release_16: {
    Value *Ptr = EmitScalarExpr(E->getArg(0));
    const llvm::Type *ElTy =
      cast<llvm::PointerType>(Ptr->getType())->getElementType();
    llvm::StoreInst *Store = 
      Builder.CreateStore(llvm::Constant::getNullValue(ElTy), Ptr);
    Store->setVolatile(true);
    return RValue::get(0);
  }

  case Builtin::BI__sync_synchronize: {
    // We assume like gcc appears to, that this only applies to cached memory.
    EmitMemoryBarrier(*this, true, true, true, true, false);
    return RValue::get(0);
  }

  case Builtin::BI__builtin_llvm_memory_barrier: {
    Value *C[5] = {
      EmitScalarExpr(E->getArg(0)),
      EmitScalarExpr(E->getArg(1)),
      EmitScalarExpr(E->getArg(2)),
      EmitScalarExpr(E->getArg(3)),
      EmitScalarExpr(E->getArg(4))
    };
    Builder.CreateCall(CGM.getIntrinsic(Intrinsic::memory_barrier), C, C + 5);
    return RValue::get(0);
  }
      
    // Library functions with special handling.
  case Builtin::BIsqrt:
  case Builtin::BIsqrtf:
  case Builtin::BIsqrtl: {
    // TODO: there is currently no set of optimizer flags
    // sufficient for us to rewrite sqrt to @llvm.sqrt.
    // -fmath-errno=0 is not good enough; we need finiteness.
    // We could probably precondition the call with an ult
    // against 0, but is that worth the complexity?
    break;
  }

  case Builtin::BIpow:
  case Builtin::BIpowf:
  case Builtin::BIpowl: {
    // Rewrite sqrt to intrinsic if allowed.
    if (!FD->hasAttr<ConstAttr>())
      break;
    Value *Base = EmitScalarExpr(E->getArg(0));
    Value *Exponent = EmitScalarExpr(E->getArg(1));
    const llvm::Type *ArgType = Base->getType();
    Value *F = CGM.getIntrinsic(Intrinsic::pow, &ArgType, 1);
    return RValue::get(Builder.CreateCall2(F, Base, Exponent, "tmp"));
  }

  case Builtin::BI__builtin_signbit:
  case Builtin::BI__builtin_signbitf:
  case Builtin::BI__builtin_signbitl: {
    LLVMContext &C = CGM.getLLVMContext();

    Value *Arg = EmitScalarExpr(E->getArg(0));
    const llvm::Type *ArgTy = Arg->getType();
    if (ArgTy->isPPC_FP128Ty())
      break; // FIXME: I'm not sure what the right implementation is here.
    int ArgWidth = ArgTy->getPrimitiveSizeInBits();
    const llvm::Type *ArgIntTy = llvm::IntegerType::get(C, ArgWidth);
    Value *BCArg = Builder.CreateBitCast(Arg, ArgIntTy);
    Value *ZeroCmp = llvm::Constant::getNullValue(ArgIntTy);
    Value *Result = Builder.CreateICmpSLT(BCArg, ZeroCmp);
    return RValue::get(Builder.CreateZExt(Result, ConvertType(E->getType())));
  }
  }

  // If this is an alias for a libm function (e.g. __builtin_sin) turn it into
  // that function.
  if (getContext().BuiltinInfo.isLibFunction(BuiltinID) ||
      getContext().BuiltinInfo.isPredefinedLibFunction(BuiltinID))
    return EmitCall(E->getCallee()->getType(),
                    CGM.getBuiltinLibFunction(FD, BuiltinID),
                    ReturnValueSlot(),
                    E->arg_begin(), E->arg_end());

  // See if we have a target specific intrinsic.
  const char *Name = getContext().BuiltinInfo.GetName(BuiltinID);
  Intrinsic::ID IntrinsicID = Intrinsic::not_intrinsic;
  if (const char *Prefix =
      llvm::Triple::getArchTypePrefix(Target.getTriple().getArch()))
    IntrinsicID = Intrinsic::getIntrinsicForGCCBuiltin(Prefix, Name);

  if (IntrinsicID != Intrinsic::not_intrinsic) {
    SmallVector<Value*, 16> Args;

    Function *F = CGM.getIntrinsic(IntrinsicID);
    const llvm::FunctionType *FTy = F->getFunctionType();

    for (unsigned i = 0, e = E->getNumArgs(); i != e; ++i) {
      Value *ArgValue = EmitScalarExpr(E->getArg(i));

      // If the intrinsic arg type is different from the builtin arg type
      // we need to do a bit cast.
      const llvm::Type *PTy = FTy->getParamType(i);
      if (PTy != ArgValue->getType()) {
        assert(PTy->canLosslesslyBitCastTo(FTy->getParamType(i)) &&
               "Must be able to losslessly bit cast to param");
        ArgValue = Builder.CreateBitCast(ArgValue, PTy);
      }

      Args.push_back(ArgValue);
    }

    Value *V = Builder.CreateCall(F, Args.data(), Args.data() + Args.size());
    QualType BuiltinRetType = E->getType();

    const llvm::Type *RetTy = llvm::Type::getVoidTy(VMContext);
    if (!BuiltinRetType->isVoidType()) RetTy = ConvertType(BuiltinRetType);

    if (RetTy != V->getType()) {
      assert(V->getType()->canLosslesslyBitCastTo(RetTy) &&
             "Must be able to losslessly bit cast result type");
      V = Builder.CreateBitCast(V, RetTy);
    }

    return RValue::get(V);
  }

  // See if we have a target specific builtin that needs to be lowered.
  if (Value *V = EmitTargetBuiltinExpr(BuiltinID, E))
    return RValue::get(V);

  ErrorUnsupported(E, "builtin function");

  // Unknown builtin, for now just dump it out and return undef.
  if (hasAggregateLLVMType(E->getType()))
    return RValue::getAggregate(CreateMemTemp(E->getType()));
  return RValue::get(llvm::UndefValue::get(ConvertType(E->getType())));
}

Value *CodeGenFunction::EmitTargetBuiltinExpr(unsigned BuiltinID,
                                              const CallExpr *E) {
  switch (Target.getTriple().getArch()) {
  case llvm::Triple::arm:
  case llvm::Triple::thumb:
    return EmitARMBuiltinExpr(BuiltinID, E);
  case llvm::Triple::x86:
  case llvm::Triple::x86_64:
    return EmitX86BuiltinExpr(BuiltinID, E);
  case llvm::Triple::ppc:
  case llvm::Triple::ppc64:
    return EmitPPCBuiltinExpr(BuiltinID, E);
  default:
    return 0;
  }
}

const llvm::Type *GetNeonType(LLVMContext &Ctx, unsigned type, bool q) {
  switch (type) {
    default: break;
    case 0: 
    case 5: return llvm::VectorType::get(llvm::Type::getInt8Ty(Ctx), 8 << q);
    case 6:
    case 7:
    case 1: return llvm::VectorType::get(llvm::Type::getInt16Ty(Ctx), 4 << q);
    case 2: return llvm::VectorType::get(llvm::Type::getInt32Ty(Ctx), 2 << q);
    case 3: return llvm::VectorType::get(llvm::Type::getInt64Ty(Ctx), 1 << q);
    case 4: return llvm::VectorType::get(llvm::Type::getFloatTy(Ctx), 2 << q);
  };
  return 0;
}

Value *CodeGenFunction::EmitARMBuiltinExpr(unsigned BuiltinID,
                                           const CallExpr *E) {
  llvm::SmallVector<Value*, 4> Ops;
  bool usgn, poly, half;
  const llvm::Type *Ty;
  unsigned Int;
  
  // Determine the type of this overloaded NEON intrinsic.
  if (BuiltinID != ARM::BI__builtin_thread_pointer) {
    for (unsigned i = 0, e = E->getNumArgs() - 1; i != e; i++)
      Ops.push_back(EmitScalarExpr(E->getArg(i)));
    
    llvm::APSInt Result;
    const Expr *Arg = E->getArg(E->getNumArgs()-1);
    if (!Arg->isIntegerConstantExpr(Result, getContext()))
      return 0;
    
    unsigned type = Result.getZExtValue();
    Ty = GetNeonType(VMContext, type & 0x7, type & 0x10);
    if (!Ty)
      return 0;
    
    usgn = type & 0x08;
    poly = type == 5 || type == 6;
    half = type == 7;
  }
  
  switch (BuiltinID) {
  default: return 0;
  case ARM::BI__clear_cache: {
    const FunctionDecl *FD = E->getDirectCallee();
    Value *a = EmitScalarExpr(E->getArg(0));
    Value *b = EmitScalarExpr(E->getArg(1));
    const llvm::Type *Ty = CGM.getTypes().ConvertType(FD->getType());
    const llvm::FunctionType *FTy = cast<llvm::FunctionType>(Ty);
    llvm::StringRef Name = FD->getName();
    return Builder.CreateCall2(CGM.CreateRuntimeFunction(FTy, Name),
                               a, b);
  }
  // FIXME: bitcast args, return.
  case ARM::BI__builtin_neon_vaba_v:
  case ARM::BI__builtin_neon_vabaq_v: {
    Int = usgn ? Intrinsic::arm_neon_vabau : Intrinsic::arm_neon_vabas;
    Value *F = CGM.getIntrinsic(Int, &Ty, 1);
    return Builder.CreateCall(F, &Ops[0], &Ops[0] + 3, "vaba");
  }
  case ARM::BI__builtin_neon_vabal_v: {
    Int = usgn ? Intrinsic::arm_neon_vabalu : Intrinsic::arm_neon_vabals;
    Value *F = CGM.getIntrinsic(Int, &Ty, 1);
    return Builder.CreateCall(F, &Ops[0], &Ops[0] + 3, "vabal");
  }
  case ARM::BI__builtin_neon_vabd_v:
  case ARM::BI__builtin_neon_vabdq_v: {
    Int = usgn ? Intrinsic::arm_neon_vabdu : Intrinsic::arm_neon_vabds;
    Value *F = CGM.getIntrinsic(Int, &Ty, 1);
    return Builder.CreateCall(F, &Ops[0], &Ops[0] + 2, "vabd");
  }
  case ARM::BI__builtin_neon_vabdl_v: {
    Int = usgn ? Intrinsic::arm_neon_vabdlu : Intrinsic::arm_neon_vabdls;
    Value *F = CGM.getIntrinsic(Int, &Ty, 1);
    return Builder.CreateCall(F, &Ops[0], &Ops[0] + 2, "vabdl");
  }
  case ARM::BI__builtin_neon_vabs_v:
  case ARM::BI__builtin_neon_vabsq_v: {
    Value *F = CGM.getIntrinsic(Intrinsic::arm_neon_vabs, &Ty, 1);
    return Builder.CreateCall(F, &Ops[0], &Ops[0] + 1, "vabs");
  }
  case ARM::BI__builtin_neon_vaddhn_v: {
    Value *F = CGM.getIntrinsic(Intrinsic::arm_neon_vaddhn, &Ty, 1);
    return Builder.CreateCall(F, &Ops[0], &Ops[0] + 2, "vaddhn");
  }
  case ARM::BI__builtin_neon_vaddl_v: {
    Int = usgn ? Intrinsic::arm_neon_vaddlu : Intrinsic::arm_neon_vaddls;
    Value *F = CGM.getIntrinsic(Int, &Ty, 1);
    return Builder.CreateCall(F, &Ops[0], &Ops[0] + 2, "vaddl");
  }
  case ARM::BI__builtin_neon_vaddw_v: {
    Int = usgn ? Intrinsic::arm_neon_vaddws : Intrinsic::arm_neon_vaddwu;
    Value *F = CGM.getIntrinsic(Int, &Ty, 1);
    return Builder.CreateCall(F, &Ops[0], &Ops[0] + 2, "vaddw");
  }
  // FIXME: vbsl -> or ((0 & 1), (0 & 2)), impl. with generic ops?
  case ARM::BI__builtin_neon_vcage_v:
    return Builder.CreateCall(CGM.getIntrinsic(Intrinsic::arm_neon_vacged),
                              &Ops[0], &Ops[0] + 2, "vcage");
  case ARM::BI__builtin_neon_vcageq_v:
    return Builder.CreateCall(CGM.getIntrinsic(Intrinsic::arm_neon_vacgeq),
                              &Ops[0], &Ops[0] + 2, "vcage");
  }
}

Value *CodeGenFunction::EmitX86BuiltinExpr(unsigned BuiltinID,
                                           const CallExpr *E) {

  llvm::SmallVector<Value*, 4> Ops;

  for (unsigned i = 0, e = E->getNumArgs(); i != e; i++)
    Ops.push_back(EmitScalarExpr(E->getArg(i)));

  switch (BuiltinID) {
  default: return 0;
  case X86::BI__builtin_ia32_pslldi128:
  case X86::BI__builtin_ia32_psllqi128:
  case X86::BI__builtin_ia32_psllwi128:
  case X86::BI__builtin_ia32_psradi128:
  case X86::BI__builtin_ia32_psrawi128:
  case X86::BI__builtin_ia32_psrldi128:
  case X86::BI__builtin_ia32_psrlqi128:
  case X86::BI__builtin_ia32_psrlwi128: {
    Ops[1] = Builder.CreateZExt(Ops[1], llvm::Type::getInt64Ty(VMContext), "zext");
    const llvm::Type *Ty = llvm::VectorType::get(llvm::Type::getInt64Ty(VMContext), 2);
    llvm::Value *Zero = llvm::ConstantInt::get(llvm::Type::getInt32Ty(VMContext), 0);
    Ops[1] = Builder.CreateInsertElement(llvm::UndefValue::get(Ty),
                                         Ops[1], Zero, "insert");
    Ops[1] = Builder.CreateBitCast(Ops[1], Ops[0]->getType(), "bitcast");
    const char *name = 0;
    Intrinsic::ID ID = Intrinsic::not_intrinsic;

    switch (BuiltinID) {
    default: assert(0 && "Unsupported shift intrinsic!");
    case X86::BI__builtin_ia32_pslldi128:
      name = "pslldi";
      ID = Intrinsic::x86_sse2_psll_d;
      break;
    case X86::BI__builtin_ia32_psllqi128:
      name = "psllqi";
      ID = Intrinsic::x86_sse2_psll_q;
      break;
    case X86::BI__builtin_ia32_psllwi128:
      name = "psllwi";
      ID = Intrinsic::x86_sse2_psll_w;
      break;
    case X86::BI__builtin_ia32_psradi128:
      name = "psradi";
      ID = Intrinsic::x86_sse2_psra_d;
      break;
    case X86::BI__builtin_ia32_psrawi128:
      name = "psrawi";
      ID = Intrinsic::x86_sse2_psra_w;
      break;
    case X86::BI__builtin_ia32_psrldi128:
      name = "psrldi";
      ID = Intrinsic::x86_sse2_psrl_d;
      break;
    case X86::BI__builtin_ia32_psrlqi128:
      name = "psrlqi";
      ID = Intrinsic::x86_sse2_psrl_q;
      break;
    case X86::BI__builtin_ia32_psrlwi128:
      name = "psrlwi";
      ID = Intrinsic::x86_sse2_psrl_w;
      break;
    }
    llvm::Function *F = CGM.getIntrinsic(ID);
    return Builder.CreateCall(F, &Ops[0], &Ops[0] + Ops.size(), name);
  }
  case X86::BI__builtin_ia32_pslldi:
  case X86::BI__builtin_ia32_psllqi:
  case X86::BI__builtin_ia32_psllwi:
  case X86::BI__builtin_ia32_psradi:
  case X86::BI__builtin_ia32_psrawi:
  case X86::BI__builtin_ia32_psrldi:
  case X86::BI__builtin_ia32_psrlqi:
  case X86::BI__builtin_ia32_psrlwi: {
    Ops[1] = Builder.CreateZExt(Ops[1], llvm::Type::getInt64Ty(VMContext), "zext");
    const llvm::Type *Ty = llvm::VectorType::get(llvm::Type::getInt64Ty(VMContext), 1);
    Ops[1] = Builder.CreateBitCast(Ops[1], Ty, "bitcast");
    const char *name = 0;
    Intrinsic::ID ID = Intrinsic::not_intrinsic;

    switch (BuiltinID) {
    default: assert(0 && "Unsupported shift intrinsic!");
    case X86::BI__builtin_ia32_pslldi:
      name = "pslldi";
      ID = Intrinsic::x86_mmx_psll_d;
      break;
    case X86::BI__builtin_ia32_psllqi:
      name = "psllqi";
      ID = Intrinsic::x86_mmx_psll_q;
      break;
    case X86::BI__builtin_ia32_psllwi:
      name = "psllwi";
      ID = Intrinsic::x86_mmx_psll_w;
      break;
    case X86::BI__builtin_ia32_psradi:
      name = "psradi";
      ID = Intrinsic::x86_mmx_psra_d;
      break;
    case X86::BI__builtin_ia32_psrawi:
      name = "psrawi";
      ID = Intrinsic::x86_mmx_psra_w;
      break;
    case X86::BI__builtin_ia32_psrldi:
      name = "psrldi";
      ID = Intrinsic::x86_mmx_psrl_d;
      break;
    case X86::BI__builtin_ia32_psrlqi:
      name = "psrlqi";
      ID = Intrinsic::x86_mmx_psrl_q;
      break;
    case X86::BI__builtin_ia32_psrlwi:
      name = "psrlwi";
      ID = Intrinsic::x86_mmx_psrl_w;
      break;
    }
    llvm::Function *F = CGM.getIntrinsic(ID);
    return Builder.CreateCall(F, &Ops[0], &Ops[0] + Ops.size(), name);
  }
  case X86::BI__builtin_ia32_cmpps: {
    llvm::Function *F = CGM.getIntrinsic(Intrinsic::x86_sse_cmp_ps);
    return Builder.CreateCall(F, &Ops[0], &Ops[0] + Ops.size(), "cmpps");
  }
  case X86::BI__builtin_ia32_cmpss: {
    llvm::Function *F = CGM.getIntrinsic(Intrinsic::x86_sse_cmp_ss);
    return Builder.CreateCall(F, &Ops[0], &Ops[0] + Ops.size(), "cmpss");
  }
  case X86::BI__builtin_ia32_ldmxcsr: {
    const llvm::Type *PtrTy = llvm::Type::getInt8PtrTy(VMContext);
    Value *One = llvm::ConstantInt::get(llvm::Type::getInt32Ty(VMContext), 1);
    Value *Tmp = Builder.CreateAlloca(llvm::Type::getInt32Ty(VMContext), One, "tmp");
    Builder.CreateStore(Ops[0], Tmp);
    return Builder.CreateCall(CGM.getIntrinsic(Intrinsic::x86_sse_ldmxcsr),
                              Builder.CreateBitCast(Tmp, PtrTy));
  }
  case X86::BI__builtin_ia32_stmxcsr: {
    const llvm::Type *PtrTy = llvm::Type::getInt8PtrTy(VMContext);
    Value *One = llvm::ConstantInt::get(llvm::Type::getInt32Ty(VMContext), 1);
    Value *Tmp = Builder.CreateAlloca(llvm::Type::getInt32Ty(VMContext), One, "tmp");
    One = Builder.CreateCall(CGM.getIntrinsic(Intrinsic::x86_sse_stmxcsr),
                             Builder.CreateBitCast(Tmp, PtrTy));
    return Builder.CreateLoad(Tmp, "stmxcsr");
  }
  case X86::BI__builtin_ia32_cmppd: {
    llvm::Function *F = CGM.getIntrinsic(Intrinsic::x86_sse2_cmp_pd);
    return Builder.CreateCall(F, &Ops[0], &Ops[0] + Ops.size(), "cmppd");
  }
  case X86::BI__builtin_ia32_cmpsd: {
    llvm::Function *F = CGM.getIntrinsic(Intrinsic::x86_sse2_cmp_sd);
    return Builder.CreateCall(F, &Ops[0], &Ops[0] + Ops.size(), "cmpsd");
  }
  case X86::BI__builtin_ia32_storehps:
  case X86::BI__builtin_ia32_storelps: {
    const llvm::Type *EltTy = llvm::Type::getInt64Ty(VMContext);
    llvm::Type *PtrTy = llvm::PointerType::getUnqual(EltTy);
    llvm::Type *VecTy = llvm::VectorType::get(EltTy, 2);

    // cast val v2i64
    Ops[1] = Builder.CreateBitCast(Ops[1], VecTy, "cast");

    // extract (0, 1)
    unsigned Index = BuiltinID == X86::BI__builtin_ia32_storelps ? 0 : 1;
    llvm::Value *Idx = llvm::ConstantInt::get(llvm::Type::getInt32Ty(VMContext), Index);
    Ops[1] = Builder.CreateExtractElement(Ops[1], Idx, "extract");

    // cast pointer to i64 & store
    Ops[0] = Builder.CreateBitCast(Ops[0], PtrTy);
    return Builder.CreateStore(Ops[1], Ops[0]);
  }
  case X86::BI__builtin_ia32_palignr: {
    unsigned shiftVal = cast<llvm::ConstantInt>(Ops[2])->getZExtValue();
    
    // If palignr is shifting the pair of input vectors less than 9 bytes,
    // emit a shuffle instruction.
    if (shiftVal <= 8) {
      const llvm::Type *IntTy = llvm::Type::getInt32Ty(VMContext);

      llvm::SmallVector<llvm::Constant*, 8> Indices;
      for (unsigned i = 0; i != 8; ++i)
        Indices.push_back(llvm::ConstantInt::get(IntTy, shiftVal + i));
      
      Value* SV = llvm::ConstantVector::get(Indices.begin(), Indices.size());
      return Builder.CreateShuffleVector(Ops[1], Ops[0], SV, "palignr");
    }
    
    // If palignr is shifting the pair of input vectors more than 8 but less
    // than 16 bytes, emit a logical right shift of the destination.
    if (shiftVal < 16) {
      // MMX has these as 1 x i64 vectors for some odd optimization reasons.
      const llvm::Type *EltTy = llvm::Type::getInt64Ty(VMContext);
      const llvm::Type *VecTy = llvm::VectorType::get(EltTy, 1);
      
      Ops[0] = Builder.CreateBitCast(Ops[0], VecTy, "cast");
      Ops[1] = llvm::ConstantInt::get(VecTy, (shiftVal-8) * 8);
      
      // create i32 constant
      llvm::Function *F = CGM.getIntrinsic(Intrinsic::x86_mmx_psrl_q);
      return Builder.CreateCall(F, &Ops[0], &Ops[0] + 2, "palignr");
    }
    
    // If palignr is shifting the pair of vectors more than 32 bytes, emit zero.
    return llvm::Constant::getNullValue(ConvertType(E->getType()));
  }
  case X86::BI__builtin_ia32_palignr128: {
    unsigned shiftVal = cast<llvm::ConstantInt>(Ops[2])->getZExtValue();
    
    // If palignr is shifting the pair of input vectors less than 17 bytes,
    // emit a shuffle instruction.
    if (shiftVal <= 16) {
      const llvm::Type *IntTy = llvm::Type::getInt32Ty(VMContext);

      llvm::SmallVector<llvm::Constant*, 16> Indices;
      for (unsigned i = 0; i != 16; ++i)
        Indices.push_back(llvm::ConstantInt::get(IntTy, shiftVal + i));
      
      Value* SV = llvm::ConstantVector::get(Indices.begin(), Indices.size());
      return Builder.CreateShuffleVector(Ops[1], Ops[0], SV, "palignr");
    }
    
    // If palignr is shifting the pair of input vectors more than 16 but less
    // than 32 bytes, emit a logical right shift of the destination.
    if (shiftVal < 32) {
      const llvm::Type *EltTy = llvm::Type::getInt64Ty(VMContext);
      const llvm::Type *VecTy = llvm::VectorType::get(EltTy, 2);
      const llvm::Type *IntTy = llvm::Type::getInt32Ty(VMContext);
      
      Ops[0] = Builder.CreateBitCast(Ops[0], VecTy, "cast");
      Ops[1] = llvm::ConstantInt::get(IntTy, (shiftVal-16) * 8);
      
      // create i32 constant
      llvm::Function *F = CGM.getIntrinsic(Intrinsic::x86_sse2_psrl_dq);
      return Builder.CreateCall(F, &Ops[0], &Ops[0] + 2, "palignr");
    }
    
    // If palignr is shifting the pair of vectors more than 32 bytes, emit zero.
    return llvm::Constant::getNullValue(ConvertType(E->getType()));
  }
  }
}

Value *CodeGenFunction::EmitPPCBuiltinExpr(unsigned BuiltinID,
                                           const CallExpr *E) {
  llvm::SmallVector<Value*, 4> Ops;

  for (unsigned i = 0, e = E->getNumArgs(); i != e; i++)
    Ops.push_back(EmitScalarExpr(E->getArg(i)));

  Intrinsic::ID ID = Intrinsic::not_intrinsic;

  switch (BuiltinID) {
  default: return 0;

  // vec_st
  case PPC::BI__builtin_altivec_stvx:
  case PPC::BI__builtin_altivec_stvxl:
  case PPC::BI__builtin_altivec_stvebx:
  case PPC::BI__builtin_altivec_stvehx:
  case PPC::BI__builtin_altivec_stvewx:
  {
    Ops[2] = Builder.CreateBitCast(Ops[2], llvm::Type::getInt8PtrTy(VMContext));
    Ops[1] = !isa<Constant>(Ops[1]) || !cast<Constant>(Ops[1])->isNullValue()
           ? Builder.CreateGEP(Ops[2], Ops[1], "tmp") : Ops[2];
    Ops.pop_back();

    switch (BuiltinID) {
    default: assert(0 && "Unsupported vavg intrinsic!");
    case PPC::BI__builtin_altivec_stvx:
      ID = Intrinsic::ppc_altivec_stvx;
      break;
    case PPC::BI__builtin_altivec_stvxl:
      ID = Intrinsic::ppc_altivec_stvxl;
      break;
    case PPC::BI__builtin_altivec_stvebx:
      ID = Intrinsic::ppc_altivec_stvebx;
      break;
    case PPC::BI__builtin_altivec_stvehx:
      ID = Intrinsic::ppc_altivec_stvehx;
      break;
    case PPC::BI__builtin_altivec_stvewx:
      ID = Intrinsic::ppc_altivec_stvewx;
      break;
    }
    llvm::Function *F = CGM.getIntrinsic(ID);
    return Builder.CreateCall(F, &Ops[0], &Ops[0] + Ops.size(), "");
  }
  }
  return 0;
}
