//===---- CGBuiltin.cpp - Emit LLVM Code for builtins ---------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file was developed by Anders Carlsson and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This contains code to emit Builtin calls as LLVM code.
//
//===----------------------------------------------------------------------===//

#include "CodeGenFunction.h"
#include "CodeGenModule.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Builtins.h"
#include "clang/AST/Expr.h"
#include "clang/AST/TargetBuiltins.h"
#include "llvm/Constants.h"
#include "llvm/Function.h"
#include "llvm/Intrinsics.h"

using namespace clang;
using namespace CodeGen;

using namespace llvm;

RValue CodeGenFunction::EmitBuiltinExpr(unsigned BuiltinID, const CallExpr *E) {
  switch (BuiltinID) {
  default: {
    if (getContext().BuiltinInfo.isLibFunction(BuiltinID))
      return EmitCallExpr(CGM.getBuiltinLibFunction(BuiltinID), E);
  
    // See if we have a target specific intrinsic.
    llvm::Intrinsic::ID IntrinsicID;
    const char *TargetPrefix = Target.getTargetPrefix();
    const char *BuiltinName = getContext().BuiltinInfo.GetName(BuiltinID);
#define GET_LLVM_INTRINSIC_FOR_GCC_BUILTIN
#include "llvm/Intrinsics.gen"
#undef GET_LLVM_INTRINSIC_FOR_GCC_BUILTIN
    
    if (IntrinsicID != Intrinsic::not_intrinsic) {
      llvm::SmallVector<llvm::Value*, 16> Args;
      
      llvm::Function *F = llvm::Intrinsic::getDeclaration(&CGM.getModule(), 
                                                          IntrinsicID);
      
      const llvm::FunctionType *FTy = F->getFunctionType();
      
      for (unsigned i = 0, e = E->getNumArgs(); i != e; ++i) {
        llvm::Value *ArgValue = EmitScalarExpr(E->getArg(i));
  
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
            
      llvm::Value *V = Builder.CreateCall(F, &Args[0], &Args[0] + Args.size());

      QualType BuiltinRetType = E->getType();
      
      const llvm::Type *RetTy = BuiltinRetType->isVoidType() ? 
        llvm::Type::VoidTy : ConvertType(BuiltinRetType);

      if (RetTy != V->getType()) {
        assert(V->getType()->canLosslesslyBitCastTo(RetTy) &&
               "Must be able to losslessly bit cast result type");
        
        V = Builder.CreateBitCast(V, RetTy);
      }
      
      return RValue::get(V);
    }

    // See if we have a target specific builtin that needs to be lowered.
    llvm::Value *V = 0;
    
    if (strcmp(TargetPrefix, "x86") == 0)
      V = EmitX86BuiltinExpr(BuiltinID, E);
    else if (strcmp(TargetPrefix, "ppc") == 0)
      V = EmitPPCBuiltinExpr(BuiltinID, E);

    if (V)
      return RValue::get(V);
    
    WarnUnsupported(E, "builtin function");

    // Unknown builtin, for now just dump it out and return undef.
    if (hasAggregateLLVMType(E->getType()))
      return RValue::getAggregate(CreateTempAlloca(ConvertType(E->getType())));
    return RValue::get(llvm::UndefValue::get(ConvertType(E->getType())));
  }    
  case Builtin::BI__builtin___CFStringMakeConstantString: {
    const Expr *Arg = E->getArg(0);
    
    while (1) {
      if (const ParenExpr *PE = dyn_cast<ParenExpr>(Arg))
        Arg = PE->getSubExpr();
      else if (const ImplicitCastExpr *CE = dyn_cast<ImplicitCastExpr>(Arg))
        Arg = CE->getSubExpr();
      else
        break;
    }
    
    const StringLiteral *Literal = cast<StringLiteral>(Arg);
    std::string S(Literal->getStrData(), Literal->getByteLength());
    
    return RValue::get(CGM.GetAddrOfConstantCFString(S));
  }
  case Builtin::BI__builtin_va_start:
  case Builtin::BI__builtin_va_end: {
    llvm::Value *ArgValue = EmitScalarExpr(E->getArg(0));
    const llvm::Type *DestType = llvm::PointerType::get(llvm::Type::Int8Ty);
    if (ArgValue->getType() != DestType)
      ArgValue = Builder.CreateBitCast(ArgValue, DestType, 
                                       ArgValue->getNameStart());

    llvm::Intrinsic::ID inst = (BuiltinID == Builtin::BI__builtin_va_start) ? 
      llvm::Intrinsic::vastart : llvm::Intrinsic::vaend;
    llvm::Value *F = llvm::Intrinsic::getDeclaration(&CGM.getModule(), inst);
    llvm::Value *V = Builder.CreateCall(F, ArgValue);

    return RValue::get(V);
  }
  case Builtin::BI__builtin_classify_type: {
    llvm::APSInt Result(32);
    
    if (!E->isBuiltinClassifyType(Result))
      assert(0 && "Expr not __builtin_classify_type!");
    
    return RValue::get(llvm::ConstantInt::get(Result));
  }
  case Builtin::BI__builtin_constant_p: {
    llvm::APSInt Result(32);

    // FIXME: Analyze the parameter and check if it is a constant.
    Result = 0;
    
    return RValue::get(llvm::ConstantInt::get(Result));
  }
  case Builtin::BI__builtin_abs: {
    llvm::Value *ArgValue = EmitScalarExpr(E->getArg(0));   
    
    llvm::BinaryOperator *NegOp = 
      Builder.CreateNeg(ArgValue, (ArgValue->getName() + "neg").c_str());
    llvm::Value *CmpResult = 
      Builder.CreateICmpSGE(ArgValue, NegOp->getOperand(0), "abscond");
    llvm::Value *Result = 
      Builder.CreateSelect(CmpResult, ArgValue, NegOp, "abs");
    
    return RValue::get(Result);
  }
  case Builtin::BI__builtin_expect: {
    llvm::Value *Condition = EmitScalarExpr(E->getArg(0));   
    return RValue::get(Condition);
  }
  case Builtin::BI__builtin_bswap32:
  case Builtin::BI__builtin_bswap64: {
    llvm::Value *ArgValue = EmitScalarExpr(E->getArg(0));
    const llvm::Type *ArgType = ArgValue->getType();
    llvm::Value *F = 
      llvm::Intrinsic::getDeclaration(&CGM.getModule(), 
                                      llvm::Intrinsic::bswap,
                                      &ArgType, 1);
    llvm::Value *V = Builder.CreateCall(F, ArgValue, "tmp");
      
    return RValue::get(V);      
  }
  case Builtin::BI__builtin_inff: {
    llvm::APFloat f(llvm::APFloat::IEEEsingle,
                    llvm::APFloat::fcInfinity, false);
    
    llvm::Value *V = llvm::ConstantFP::get(llvm::Type::FloatTy, f);
    return RValue::get(V);
  }
  case Builtin::BI__builtin_inf:
  // FIXME: mapping long double onto double.      
  case Builtin::BI__builtin_infl: {
    llvm::APFloat f(llvm::APFloat::IEEEdouble,
                    llvm::APFloat::fcInfinity, false);
    
    llvm::Value *V = llvm::ConstantFP::get(llvm::Type::DoubleTy, f);
    return RValue::get(V);
  }
  }
  return RValue::get(0);
}

llvm::Value *CodeGenFunction::EmitX86BuiltinExpr(unsigned BuiltinID, 
                                                 const CallExpr *E)
{
  switch (BuiltinID) {
    default: return 0;
    case X86::BI__builtin_ia32_mulps:
      return Builder.CreateMul(EmitScalarExpr(E->getArg(0)),
                               EmitScalarExpr(E->getArg(1)),
                               "result");
  }
}

llvm::Value *CodeGenFunction::EmitPPCBuiltinExpr(unsigned BuiltinID, 
                                                 const CallExpr *E)
{
  switch (BuiltinID) {
    default: return 0;
  }
}  
