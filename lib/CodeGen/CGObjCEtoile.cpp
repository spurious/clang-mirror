//===------- CGObjCEtoile.cpp - Emit LLVM Code from ASTs for a Module --------===// 
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This provides Objective-C code generation targetting the Etoile runtime.
//
//===----------------------------------------------------------------------===//

#include <stdarg.h>
#include "CGObjCRuntime.h"
#include "llvm/Module.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/LLVMBuilder.h"
#include "llvm/ADT/SmallVector.h"


clang::CodeGen::CGObjCRuntime::~CGObjCRuntime() {}

namespace {
class CGObjCEtoile : public clang::CodeGen::CGObjCRuntime {
private:
  llvm::Module &TheModule;
  const llvm::Type *SelectorTy;
  const llvm::PointerType *PtrToInt8Ty;
  const llvm::Type *IMPTy;
  const llvm::Type *IntTy;
  const llvm::Type *PtrTy;
  const llvm::Type *LongTy;
  const llvm::Type *PtrToIntTy;
  const llvm::Type *IdTy;
  const llvm::Type *CallTy;
  const llvm::Type *SlotTy;
  const llvm::Type *LookupFunctionTy;
public:
  CGObjCEtoile(llvm::Module &Mp,
    const llvm::Type *LLVMIntType,
    const llvm::Type *LLVMLongType);
  virtual llvm::Value *generateMessageSend(llvm::LLVMFoldingBuilder &Builder,
                                           const llvm::Type *ReturnTy,
                                           llvm::Value *Sender,
                                           llvm::Value *Receiver,
                                           llvm::Value *Selector,
                                           llvm::Value** ArgV,
                                           unsigned ArgC);
  llvm::Value *getSelector(llvm::LLVMFoldingBuilder &Builder,
      llvm::Value *SelName,
      llvm::Value *SelTypes);
  virtual llvm::Function *MethodPreamble(const llvm::Type *ReturnTy,
                                         const llvm::Type *SelfTy,
                                         const llvm::Type **ArgTy,
                                         unsigned ArgC,
                                         bool isVarArg);
};
} // end anonymous namespace

CGObjCEtoile::CGObjCEtoile(llvm::Module &M,
    const llvm::Type *LLVMIntType,
    const llvm::Type *LLVMLongType) : 
  TheModule(M),
  IntTy(LLVMIntType),
  LongTy(LLVMLongType)
{
  // C string type.  Used in lots of places.
  PtrToInt8Ty = 
    llvm::PointerType::getUnqual(llvm::Type::Int8Ty);
  // Get the selector Type.
  SelectorTy = llvm::Type::Int32Ty;
  PtrToIntTy = llvm::PointerType::getUnqual(IntTy);
  PtrTy = llvm::PointerType::getUnqual(llvm::Type::Int8Ty);
 
  // Object type
  llvm::OpaqueType *OpaqueObjTy = llvm::OpaqueType::get();
  llvm::Type *OpaqueIdTy = llvm::PointerType::getUnqual(OpaqueObjTy);
  IdTy = llvm::PointerType::getUnqual(llvm::StructType::get(OpaqueIdTy, 0));
  OpaqueObjTy->refineAbstractTypeTo(IdTy);

  // Call structure type.
  llvm::OpaqueType *OpaqueSlotTy = llvm::OpaqueType::get();
  CallTy = llvm::StructType::get(llvm::PointerType::getUnqual(OpaqueSlotTy),
      SelectorTy,
      IdTy,
      0);
  //CallTy = llvm::PointerType::getUnqual(CallTy);

  // IMP type
  std::vector<const llvm::Type*> IMPArgs;
  IMPArgs.push_back(IdTy);
  IMPArgs.push_back(llvm::PointerType::getUnqual(CallTy));
  IMPTy = llvm::FunctionType::get(IdTy, IMPArgs, true);

  // Slot type
  SlotTy = llvm::StructType::get(IntTy,
      IMPTy,
      PtrToInt8Ty,
      PtrToInt8Ty,
      llvm::Type::Int32Ty,
      0);
  OpaqueSlotTy->refineAbstractTypeTo(SlotTy);
  SlotTy = llvm::PointerType::getUnqual(SlotTy);

  // Lookup function type
  std::vector<const llvm::Type*> LookupFunctionArgs;
  LookupFunctionArgs.push_back(llvm::PointerType::getUnqual(IdTy));
  LookupFunctionArgs.push_back(IdTy);
  LookupFunctionArgs.push_back(SelectorTy);
  LookupFunctionArgs.push_back(IdTy);
  LookupFunctionTy = llvm::FunctionType::get(SlotTy, LookupFunctionArgs, false);
  LookupFunctionTy = llvm::PointerType::getUnqual(LookupFunctionTy);

}

// Looks up the selector for the specified name / type pair.
llvm::Value *CGObjCEtoile::getSelector(llvm::LLVMFoldingBuilder &Builder,
    llvm::Value *SelName,
    llvm::Value *SelTypes)
{
  // Look up the selector.
  if(SelTypes == 0) {
    SelTypes = llvm::ConstantPointerNull::get(PtrToInt8Ty);
  }
  llvm::Constant *SelFunction = 
    TheModule.getOrInsertFunction("lookup_typed_selector",
        SelectorTy,
        PtrToInt8Ty,
        PtrToInt8Ty,
        0);
  llvm::SmallVector<llvm::Value*, 2> Args;
  Args.push_back(SelName);
  Args.push_back(SelTypes);
  return Builder.CreateCall(SelFunction, Args.begin(), Args.end());
}

#define SET(structure, index, value) do {\
    llvm::Value *element_ptr = Builder.CreateStructGEP(structure, index);\
    Builder.CreateStore(value, element_ptr);} while(0)
// Generate code for a message send expression on the Etoile runtime.
// BIG FAT WARNING: Much of this code will need factoring out later.
llvm::Value *CGObjCEtoile::generateMessageSend(llvm::LLVMFoldingBuilder &Builder,
                                            const llvm::Type *ReturnTy,
                                            llvm::Value *Sender,
                                            llvm::Value *Receiver,
                                            llvm::Value *Selector,
                                            llvm::Value** ArgV,
                                            unsigned ArgC) {
  // FIXME: Selectors should be statically cached, not looked up on every call.
  llvm::Value *cmd = getSelector(Builder, Selector, 0);
  // TODO: [Polymorphic] inline caching

  // Get the lookup function for this object:
  llvm::Value *ObjAddr = Builder.CreateBitCast(Receiver, PtrToInt8Ty);
  llvm::Value *FunctionOffset = new llvm::GlobalVariable(llvm::Type::Int32Ty,
      false,
      llvm::GlobalValue::ExternalLinkage,
      0,
      "lookup_offset",
      &TheModule);
  FunctionOffset = Builder.CreateLoad(FunctionOffset);
  llvm::Value *Tag = Builder.CreateGEP(ObjAddr, FunctionOffset);
  llvm::Value *Lookup = Builder.CreateBitCast(Tag, LookupFunctionTy);

  // TODO: Remove this when the caller is providing sensible sender info
  if(Sender == 0) {
    Sender = llvm::ConstantPointerNull::get((llvm::PointerType*)IdTy);
  }
  Receiver = Builder.CreateBitCast(Receiver, IdTy);
  llvm::Value *ReceiverAddr = Builder.CreateAlloca(IdTy);
  Builder.CreateStore(Receiver, ReceiverAddr);
  // Look up the method implementation.
  llvm::SmallVector<llvm::Value*, 4> LookupArgs;
  LookupArgs.push_back(ReceiverAddr);
  LookupArgs.push_back(Receiver);
  LookupArgs.push_back(cmd);
  LookupArgs.push_back(Sender);
  llvm::Value *Slot = Builder.CreateCall(Lookup,
      LookupArgs.begin(),
      LookupArgs.end());
  
  // Create the call structure
  llvm::Value *Call = Builder.CreateAlloca(CallTy);
  SET(Call, 0, Slot);
  SET(Call, 1, cmd);
  SET(Call, 2, Sender);

  // Get the IMP from the slot and call it
  // TODO: Property load / store optimisations
  llvm::Value *IMP = Builder.CreateStructGEP(Slot, 1);
  // If the return type of the IMP is wrong, cast it so it isn't.
  if(ReturnTy != IdTy) {
    std::vector<const llvm::Type*> IMPArgs;
    IMPArgs.push_back(IdTy);
    IMPArgs.push_back(llvm::PointerType::getUnqual(CallTy));
    llvm::Type *NewIMPTy = llvm::FunctionType::get(ReturnTy, IMPArgs, true);
    IMP = Builder.CreateBitCast(IMP, llvm::PointerType::getUnqual(NewIMPTy));
  }
  llvm::SmallVector<llvm::Value*, 16> Args;
  Args.push_back(Receiver);
  Args.push_back(Call);
  Args.insert(Args.end(), ArgV, ArgV+ArgC);
  return Builder.CreateCall(IMP, Args.begin(), Args.end());
}

llvm::Function *CGObjCEtoile::MethodPreamble(
                                         const llvm::Type *ReturnTy,
                                         const llvm::Type *SelfTy,
                                         const llvm::Type **ArgTy,
                                         unsigned ArgC,
                                         bool isVarArg) {
  std::vector<const llvm::Type*> Args;
  //Args.push_back(SelfTy);
  Args.push_back(IdTy);
  Args.push_back(llvm::PointerType::getUnqual(CallTy));
  for (unsigned i=0; i<ArgC ; i++) {
    Args.push_back(ArgTy[i]);
  }
  llvm::FunctionType *MethodTy = llvm::FunctionType::get(ReturnTy, Args, isVarArg);
  llvm::Function *Method = new llvm::Function(MethodTy,
      llvm::GlobalValue::InternalLinkage,
      ".objc.method",
      &TheModule);
  //llvm::BasicBlock *EntryBB = new llvm::BasicBlock("entry", Method);
  // Set the names of the hidden arguments
  llvm::Function::arg_iterator AI = Method->arg_begin();
  AI[0].setName("self");
  AI[1].setName("_call");
  // FIXME: Should create the _cmd variable as _call->selector
  return Method;
}

/*
clang::CodeGen::CGObjCRuntime *clang::CodeGen::CreateObjCRuntime(
    llvm::Module &M,
    const llvm::Type *LLVMIntType,
    const llvm::Type *LLVMLongType) {
  return new CGObjCEtoile(M, LLVMIntType, LLVMLongType);
}
*/
