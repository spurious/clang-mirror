//===----- CGCall.h - Encapsulate calling convention details ----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// These classes wrap the information about a call or function
// definition used to handle ABI compliancy.
//
//===----------------------------------------------------------------------===//

#include "CGCall.h"
#include "CodeGenFunction.h"
#include "CodeGenModule.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclObjC.h"
#include "llvm/ParameterAttributes.h"
using namespace clang;
using namespace CodeGen;

/***/

// FIXME: Use iterator and sidestep silly type array creation.

CGFunctionInfo::CGFunctionInfo(const FunctionDecl *FD)
  : TheDecl(FD) 
{
  const FunctionType *FTy = FD->getType()->getAsFunctionType();
  const FunctionTypeProto *FTP = dyn_cast<FunctionTypeProto>(FTy);
  
  ArgTypes.push_back(FTy->getResultType());
  if (FTP)
    for (unsigned i = 0, e = FTP->getNumArgs(); i != e; ++i)
      ArgTypes.push_back(FTP->getArgType(i));
}

CGFunctionInfo::CGFunctionInfo(const ObjCMethodDecl *MD,
                               const ASTContext &Context)
  : TheDecl(MD) 
{
  ArgTypes.push_back(MD->getResultType());
  ArgTypes.push_back(MD->getSelfDecl()->getType());
  ArgTypes.push_back(Context.getObjCSelType());
  for (ObjCMethodDecl::param_const_iterator i = MD->param_begin(),
         e = MD->param_end(); i != e; ++i)
    ArgTypes.push_back((*i)->getType());
}

ArgTypeIterator CGFunctionInfo::argtypes_begin() const {
  return ArgTypes.begin();
}

ArgTypeIterator CGFunctionInfo::argtypes_end() const {
  return ArgTypes.end();
}

/***/

CGCallInfo::CGCallInfo(QualType _ResultType, const CallArgList &_Args) {
  ArgTypes.push_back(_ResultType);
  for (CallArgList::const_iterator i = _Args.begin(), e = _Args.end(); i!=e; ++i)
    ArgTypes.push_back(i->second);
}

ArgTypeIterator CGCallInfo::argtypes_begin() const {
  return ArgTypes.begin();
}

ArgTypeIterator CGCallInfo::argtypes_end() const {
  return ArgTypes.end();
}

/***/

bool CodeGenModule::ReturnTypeUsesSret(QualType RetTy) {
  return CodeGenFunction::hasAggregateLLVMType(RetTy);
}

void CodeGenModule::ConstructParamAttrList(const Decl *TargetDecl,
                                             ArgTypeIterator begin,
                                             ArgTypeIterator end,
                                             ParamAttrListType &PAL) {
  unsigned FuncAttrs = 0;

  if (TargetDecl) {
    if (TargetDecl->getAttr<NoThrowAttr>())
      FuncAttrs |= llvm::ParamAttr::NoUnwind;
    if (TargetDecl->getAttr<NoReturnAttr>())
      FuncAttrs |= llvm::ParamAttr::NoReturn;
  }

  QualType ResTy = *begin;
  unsigned Index = 1;
  if (ReturnTypeUsesSret(ResTy)) {
    PAL.push_back(llvm::ParamAttrsWithIndex::get(Index, 
                                                 llvm::ParamAttr::StructRet));
    ++Index;
  } else if (ResTy->isPromotableIntegerType()) {
    if (ResTy->isSignedIntegerType()) {
      FuncAttrs |= llvm::ParamAttr::SExt;
    } else if (ResTy->isUnsignedIntegerType()) {
      FuncAttrs |= llvm::ParamAttr::ZExt;
    }
  }
  if (FuncAttrs)
    PAL.push_back(llvm::ParamAttrsWithIndex::get(0, FuncAttrs));
  for (++begin; begin != end; ++begin, ++Index) {
    QualType ParamType = *begin;
    unsigned ParamAttrs = 0;
    if (ParamType->isRecordType())
      ParamAttrs |= llvm::ParamAttr::ByVal;
    if (ParamType->isPromotableIntegerType()) {
      if (ParamType->isSignedIntegerType()) {
        ParamAttrs |= llvm::ParamAttr::SExt;
      } else if (ParamType->isUnsignedIntegerType()) {
        ParamAttrs |= llvm::ParamAttr::ZExt;
      }
    }
    if (ParamAttrs)
      PAL.push_back(llvm::ParamAttrsWithIndex::get(Index, ParamAttrs));
  }
}

void CodeGenFunction::EmitFunctionProlog(llvm::Function *Fn,
                                         QualType RetTy, 
                                         const FunctionArgList &Args) {
  // Emit allocs for param decls.  Give the LLVM Argument nodes names.
  llvm::Function::arg_iterator AI = Fn->arg_begin();
  
  // Name the struct return argument.
  if (hasAggregateLLVMType(RetTy)) {
    AI->setName("agg.result");
    ++AI;
  }
     
  for (FunctionArgList::const_iterator i = Args.begin(), e = Args.end();
       i != e; ++i, ++AI) {
    const VarDecl *Arg = i->first;
    QualType T = i->second;
    assert(AI != Fn->arg_end() && "Argument mismatch!");
    llvm::Value* V = AI;
    if (!getContext().typesAreCompatible(T, Arg->getType())) {
      // This must be a promotion, for something like
      // "void a(x) short x; {..."
      V = EmitScalarConversion(V, T, Arg->getType());
      }
    EmitParmDecl(*Arg, V);
  }
  assert(AI == Fn->arg_end() && "Argument mismatch!");
}

void CodeGenFunction::EmitFunctionEpilog(QualType RetTy, 
                                         llvm::Value *ReturnValue) {
  if (!ReturnValue) {
    Builder.CreateRetVoid();
  } else { 
    if (!hasAggregateLLVMType(RetTy)) {
      Builder.CreateRet(Builder.CreateLoad(ReturnValue));
    } else if (RetTy->isAnyComplexType()) {
      EmitAggregateCopy(CurFn->arg_begin(), ReturnValue, RetTy);
      Builder.CreateRetVoid();
    } else {
      EmitAggregateCopy(CurFn->arg_begin(), ReturnValue, RetTy);
      Builder.CreateRetVoid();
    }
  }
}

RValue CodeGenFunction::EmitCall(llvm::Value *Callee, 
                                 QualType ResultType, 
                                 const CallArgList &CallArgs) {
  // FIXME: Factor out code to load from args into locals into target.
  llvm::SmallVector<llvm::Value*, 16> Args;
  llvm::Value *TempArg0 = 0;

  // Handle struct-return functions by passing a pointer to the
  // location that we would like to return into.
  if (hasAggregateLLVMType(ResultType)) {
    // Create a temporary alloca to hold the result of the call. :(
    TempArg0 = CreateTempAlloca(ConvertType(ResultType));
    Args.push_back(TempArg0);
  }
  
  for (CallArgList::const_iterator I = CallArgs.begin(), E = CallArgs.end(); 
       I != E; ++I) {
    RValue RV = I->first;
    if (RV.isScalar()) {
      Args.push_back(RV.getScalarVal());
    } else if (RV.isComplex()) {
      // Make a temporary alloca to pass the argument.
      Args.push_back(CreateTempAlloca(ConvertType(I->second)));
      StoreComplexToAddr(RV.getComplexVal(), Args.back(), false); 
    } else {
      Args.push_back(RV.getAggregateAddr());
    }
  }
  
  llvm::CallInst *CI = Builder.CreateCall(Callee,&Args[0],&Args[0]+Args.size());
  CGCallInfo CallInfo(ResultType, CallArgs);

  // FIXME: Provide TargetDecl so nounwind, noreturn, etc, etc get set.
  CodeGen::ParamAttrListType ParamAttrList;
  CGM.ConstructParamAttrList(0, 
                             CallInfo.argtypes_begin(), CallInfo.argtypes_end(),
                             ParamAttrList);
  CI->setParamAttrs(llvm::PAListPtr::get(ParamAttrList.begin(), 
                                         ParamAttrList.size()));  

  if (const llvm::Function *F = dyn_cast<llvm::Function>(Callee))
    CI->setCallingConv(F->getCallingConv());
  if (CI->getType() != llvm::Type::VoidTy)
    CI->setName("call");
  else if (ResultType->isAnyComplexType())
    return RValue::getComplex(LoadComplexFromAddr(TempArg0, false));
  else if (hasAggregateLLVMType(ResultType))
    // Struct return.
    return RValue::getAggregate(TempArg0);
  else {
    // void return.
    assert(ResultType->isVoidType() && "Should only have a void expr here");
    CI = 0;
  }
      
  return RValue::get(CI);  
}
