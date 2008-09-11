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

CGFunctionInfo::CGFunctionInfo(const FunctionTypeNoProto *FTNP)
  : IsVariadic(true)
{
  ArgTypes.push_back(FTNP->getResultType());
}

CGFunctionInfo::CGFunctionInfo(const FunctionTypeProto *FTP)
  : IsVariadic(FTP->isVariadic())
{
  ArgTypes.push_back(FTP->getResultType());
  for (unsigned i = 0, e = FTP->getNumArgs(); i != e; ++i)
    ArgTypes.push_back(FTP->getArgType(i));
}

// FIXME: Is there really any reason to have this still?
CGFunctionInfo::CGFunctionInfo(const FunctionDecl *FD)
{
  const FunctionType *FTy = FD->getType()->getAsFunctionType();
  const FunctionTypeProto *FTP = dyn_cast<FunctionTypeProto>(FTy);

  ArgTypes.push_back(FTy->getResultType());
  if (FTP) {
    IsVariadic = FTP->isVariadic();
    for (unsigned i = 0, e = FTP->getNumArgs(); i != e; ++i)
      ArgTypes.push_back(FTP->getArgType(i));
  } else {
    IsVariadic = true;
  }
}

CGFunctionInfo::CGFunctionInfo(const ObjCMethodDecl *MD,
                               const ASTContext &Context)
  : IsVariadic(MD->isVariadic())
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

class ABIArgInfo {
public:
  enum Kind {
    Default,
    StructRet, // Only valid for struct return types
    Coerce     // Only valid for return types
  };

private:
  Kind TheKind;
  const llvm::Type *TypeData;

  ABIArgInfo(Kind K, const llvm::Type *TD) : TheKind(K),
                                             TypeData(TD) {}
public:
  static ABIArgInfo getDefault() { 
    return ABIArgInfo(Default, 0); 
  }
  static ABIArgInfo getStructRet() { 
    return ABIArgInfo(StructRet, 0); 
  }
  static ABIArgInfo getCoerce(const llvm::Type *T) { 
    assert(T->isSingleValueType() && "Can only coerce to simple types");
    return ABIArgInfo(Coerce, T);
  }

  Kind getKind() const { return TheKind; }
  bool isDefault() const { return TheKind == Default; }
  bool isStructRet() const { return TheKind == StructRet; }
  bool isCoerce() const { return TheKind == Coerce; }

  // Coerce accessors
  const llvm::Type *getCoerceToType() const {
    assert(TheKind == Coerce && "Invalid kind!");
    return TypeData;
  }
};

/***/

static ABIArgInfo classifyReturnType(QualType RetTy,
                                     ASTContext &Context) {
  assert(!RetTy->isArrayType() && 
         "Array types cannot be passed directly.");
  if (CodeGenFunction::hasAggregateLLVMType(RetTy)) {
    uint64_t Size = Context.getTypeSize(RetTy);
    if (Size == 8) {
      return ABIArgInfo::getCoerce(llvm::Type::Int8Ty);
    } else if (Size == 16) {
      return ABIArgInfo::getCoerce(llvm::Type::Int16Ty);
    } else if (Size == 32) {
      return ABIArgInfo::getCoerce(llvm::Type::Int32Ty);
    } else if (Size == 64) {
      return ABIArgInfo::getCoerce(llvm::Type::Int64Ty);
    } else {
      return ABIArgInfo::getStructRet();
    }
  } else {
    return ABIArgInfo::getDefault();
  }
}

/***/

const llvm::FunctionType *
CodeGenTypes::GetFunctionType(const CGCallInfo &CI, bool IsVariadic) {
  return GetFunctionType(CI.argtypes_begin(), CI.argtypes_end(), IsVariadic);
}

const llvm::FunctionType *
CodeGenTypes::GetFunctionType(const CGFunctionInfo &FI) {
  return GetFunctionType(FI.argtypes_begin(), FI.argtypes_end(), FI.isVariadic());
}

const llvm::FunctionType *
CodeGenTypes::GetFunctionType(ArgTypeIterator begin, ArgTypeIterator end,
                              bool IsVariadic) {
  std::vector<const llvm::Type*> ArgTys;

  const llvm::Type *ResultType = 0;

  QualType RetTy = *begin;
  ABIArgInfo RetAI = classifyReturnType(RetTy, getContext());
  switch (RetAI.getKind()) {    
  case ABIArgInfo::Default:
    if (RetTy->isVoidType()) {
      ResultType = llvm::Type::VoidTy;
    } else {
      ResultType = ConvertType(RetTy);
    }
    break;

  case ABIArgInfo::StructRet: {
    ResultType = llvm::Type::VoidTy;
    const llvm::Type *STy = ConvertType(RetTy);
    ArgTys.push_back(llvm::PointerType::get(STy, RetTy.getAddressSpace()));
    break;
  }

  case ABIArgInfo::Coerce:
    ResultType = RetAI.getCoerceToType();
    break;
  }
  
  for (++begin; begin != end; ++begin) {
    const llvm::Type *Ty = ConvertType(*begin);
    assert(!(*begin)->isArrayType() && 
           "Array types cannot be passed directly.");
    if (Ty->isSingleValueType())
      ArgTys.push_back(Ty);
    else
      // byval arguments are always on the stack, which is addr space #0.
      ArgTys.push_back(llvm::PointerType::getUnqual(Ty));
  }

  return llvm::FunctionType::get(ResultType, ArgTys, IsVariadic);
}

bool CodeGenModule::ReturnTypeUsesSret(QualType RetTy) {
  return classifyReturnType(RetTy, getContext()).isStructRet();
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

  QualType RetTy = *begin;
  unsigned Index = 1;
  ABIArgInfo RetAI = classifyReturnType(RetTy, getContext());
  switch (RetAI.getKind()) {
  case ABIArgInfo::Default:
    if (RetTy->isPromotableIntegerType()) {
      if (RetTy->isSignedIntegerType()) {
        FuncAttrs |= llvm::ParamAttr::SExt;
      } else if (RetTy->isUnsignedIntegerType()) {
        FuncAttrs |= llvm::ParamAttr::ZExt;
      }
    }
    break;

  case ABIArgInfo::StructRet:
    PAL.push_back(llvm::ParamAttrsWithIndex::get(Index, 
                                                 llvm::ParamAttr::StructRet));
    ++Index;
    break;

  case ABIArgInfo::Coerce:
    break;
  }

  if (FuncAttrs)
    PAL.push_back(llvm::ParamAttrsWithIndex::get(0, FuncAttrs));
  for (++begin; begin != end; ++begin, ++Index) {
    QualType ParamType = *begin;
    unsigned ParamAttrs = 0;
    assert(!ParamType->isArrayType() && 
           "Array types cannot be passed directly.");
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
  if (CGM.ReturnTypeUsesSret(RetTy)) {
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
  llvm::Value *RV = 0;

  // Functions with no result always return void.
  if (ReturnValue) { 
    ABIArgInfo RetAI = classifyReturnType(RetTy, getContext());
    
    switch (RetAI.getKind()) {
    case ABIArgInfo::StructRet:
      EmitAggregateCopy(CurFn->arg_begin(), ReturnValue, RetTy);
      break;
     
    case ABIArgInfo::Default:
      RV = Builder.CreateLoad(ReturnValue);
      break;

    case ABIArgInfo::Coerce: {
      const llvm::Type *CoerceToPTy = 
        llvm::PointerType::getUnqual(RetAI.getCoerceToType());
      RV = Builder.CreateLoad(Builder.CreateBitCast(ReturnValue, CoerceToPTy));
    }
    }
  }
  
  if (RV) {
    Builder.CreateRet(RV);
  } else {
    Builder.CreateRetVoid();
  }
}

RValue CodeGenFunction::EmitCall(llvm::Value *Callee, 
                                 QualType RetTy, 
                                 const CallArgList &CallArgs) {
  // FIXME: Factor out code to load from args into locals into target.
  llvm::SmallVector<llvm::Value*, 16> Args;
  llvm::Value *TempArg0 = 0;

  // Handle struct-return functions by passing a pointer to the
  // location that we would like to return into.
  ABIArgInfo RetAI = classifyReturnType(RetTy, getContext());
  switch (RetAI.getKind()) {
  case ABIArgInfo::StructRet:
    // Create a temporary alloca to hold the result of the call. :(
    TempArg0 = CreateTempAlloca(ConvertType(RetTy));
    Args.push_back(TempArg0);
    break;
    
  case ABIArgInfo::Default:
  case ABIArgInfo::Coerce:
    break;
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
  CGCallInfo CallInfo(RetTy, CallArgs);

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

  switch (RetAI.getKind()) {
  case ABIArgInfo::StructRet:
    if (RetTy->isAnyComplexType())
      return RValue::getComplex(LoadComplexFromAddr(TempArg0, false));
    else 
      // Struct return.
      return RValue::getAggregate(TempArg0);
    
  case ABIArgInfo::Default:
    return RValue::get(RetTy->isVoidType() ? 0 : CI);

  case ABIArgInfo::Coerce: {
    const llvm::Type *CoerceToPTy = 
      llvm::PointerType::getUnqual(RetAI.getCoerceToType());
    llvm::Value *V = CreateTempAlloca(ConvertType(RetTy), "tmp");
    Builder.CreateStore(CI, Builder.CreateBitCast(V, CoerceToPTy));
    return RValue::getAggregate(V);
  }
  }

  assert(0 && "Unhandled ABIArgInfo::Kind");
  return RValue::get(0);
}
