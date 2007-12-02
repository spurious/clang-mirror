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
#include "clang/Basic/Diagnostic.h"
#include "clang/AST/AST.h"
#include "llvm/Constants.h"
#include "llvm/DerivedTypes.h"
#include "llvm/Function.h"
#include "llvm/Analysis/Verifier.h"
#include "llvm/Support/CFG.h"
using namespace clang;
using namespace CodeGen;

CodeGenFunction::CodeGenFunction(CodeGenModule &cgm) 
  : CGM(cgm), Target(CGM.getContext().Target), SwitchInsn(NULL), 
    CaseRangeBlock(NULL) {}

ASTContext &CodeGenFunction::getContext() const {
  return CGM.getContext();
}


llvm::BasicBlock *CodeGenFunction::getBasicBlockForLabel(const LabelStmt *S) {
  llvm::BasicBlock *&BB = LabelMap[S];
  if (BB) return BB;
  
  // Create, but don't insert, the new block.
  return BB = new llvm::BasicBlock(S->getName());
}


const llvm::Type *CodeGenFunction::ConvertType(QualType T) {
  return CGM.getTypes().ConvertType(T);
}

bool CodeGenFunction::hasAggregateLLVMType(QualType T) {
  return !T->isRealType() && !T->isPointerType() && !T->isReferenceType() &&
         !T->isVoidType() && !T->isVectorType() && !T->isFunctionType();
}


void CodeGenFunction::GenerateCode(const FunctionDecl *FD) {
  LLVMIntTy = ConvertType(getContext().IntTy);
  LLVMPointerWidth = static_cast<unsigned>(
    getContext().getTypeSize(getContext().getPointerType(getContext().VoidTy),
                             SourceLocation()));
  
  CurFuncDecl = FD;
  llvm::Constant *CurFnC = CGM.GetAddrOfGlobalDecl(FD);
  if (!(CurFn = dyn_cast<llvm::Function>(CurFnC))) {
    // If CurFnC is not a constant, it must be a bitcast of another function.
    llvm::ConstantExpr *CurFnCE = cast<llvm::ConstantExpr>(CurFnC);
    assert(CurFnCE->getOpcode() == llvm::Instruction::BitCast &&
           "Unexpected name collision");
    llvm::Function *OtherFunc = cast<llvm::Function>(CurFnCE->getOperand(0));
    
    // This happens if there is a prototype for a function (e.g. "int f()") and
    // then a definition of a different type (e.g. "int f(int x)").  Start by
    // making a new function of the correct type, RAUW, then steal the name.
    const llvm::PointerType *PTy = cast<llvm::PointerType>(CurFnC->getType());
    const llvm::FunctionType *FTy =
      cast<llvm::FunctionType>(PTy->getElementType());
    CurFn = new llvm::Function(FTy, llvm::Function::ExternalLinkage, "",
                               &CGM.getModule());
    CurFn->takeName(OtherFunc);
    
    // Replace uses of OtherFunc with the Function we will endow with a body.
    llvm::Constant *NewPtrForOldDecl = 
      llvm::ConstantExpr::getBitCast(CurFn, OtherFunc->getType());
    OtherFunc->replaceAllUsesWith(NewPtrForOldDecl);
    
    // Make sure the GlobalDecl map for FD is up-to-date.
    CGM.ChangeGlobalDeclMap(FD, CurFn);
    
    // FIXME: Update the globaldeclmap for the previous decl of this name.  We
    // really want a way to walk all of these, but we don't have it yet.  This
    // is incredibly slow!
    CGM.ReplaceMapValuesWith(OtherFunc, NewPtrForOldDecl);
    
    // Ok, delete the old function now, which is dead.
    assert(OtherFunc->isDeclaration() && "Shouldn't replace non-declaration");
    OtherFunc->eraseFromParent();
  }
  
  assert(CurFn->isDeclaration() && "Function already has body?");
  
  // TODO: Set up linkage and many other things.  Note, this is a simple 
  // approximation of what we really want.
  if (FD->getStorageClass() == FunctionDecl::Static)
    CurFn->setLinkage(llvm::Function::InternalLinkage);
  else if (FD->isInline())
    CurFn->setLinkage(llvm::Function::WeakLinkage);
  
  llvm::BasicBlock *EntryBB = new llvm::BasicBlock("entry", CurFn);
  
  Builder.SetInsertPoint(EntryBB);

  // Create a marker to make it easy to insert allocas into the entryblock
  // later.
  llvm::Value *Undef = llvm::UndefValue::get(llvm::Type::Int32Ty);
  AllocaInsertPt = Builder.CreateBitCast(Undef,llvm::Type::Int32Ty, "allocapt");
  
  // Emit allocs for param decls.  Give the LLVM Argument nodes names.
  llvm::Function::arg_iterator AI = CurFn->arg_begin();
  
  // Name the struct return argument.
  if (hasAggregateLLVMType(FD->getResultType())) {
    AI->setName("agg.result");
    ++AI;
  }
  
  for (unsigned i = 0, e = FD->getNumParams(); i != e; ++i, ++AI) {
    assert(AI != CurFn->arg_end() && "Argument mismatch!");
    EmitParmDecl(*FD->getParamDecl(i), AI);
  }
  
  // Emit the function body.
  EmitStmt(FD->getBody());
  
  // Emit a return for code that falls off the end. If insert point
  // is a dummy block with no predecessors then remove the block itself.
  llvm::BasicBlock *BB = Builder.GetInsertBlock();
  if (isDummyBlock(BB))
    BB->eraseFromParent();
  else {
    // FIXME: if this is C++ main, this should return 0.
    if (CurFn->getReturnType() == llvm::Type::VoidTy)
      Builder.CreateRetVoid();
    else
      Builder.CreateRet(llvm::UndefValue::get(CurFn->getReturnType()));
  }
  assert(BreakContinueStack.empty() &&
         "mismatched push/pop in break/continue stack!");
  
  // Remove the AllocaInsertPt instruction, which is just a convenience for us.
  AllocaInsertPt->eraseFromParent();
  AllocaInsertPt = 0;
  
  // Verify that the function is well formed.
  assert(!verifyFunction(*CurFn));
}

/// isDummyBlock - Return true if BB is an empty basic block
/// with no predecessors.
bool CodeGenFunction::isDummyBlock(const llvm::BasicBlock *BB) {
  if (BB->empty() && pred_begin(BB) == pred_end(BB)) 
    return true;
  return false;
}

/// StartBlock - Start new block named N. If insert block is a dummy block
/// then reuse it.
void CodeGenFunction::StartBlock(const char *N) {
  llvm::BasicBlock *BB = Builder.GetInsertBlock();
  if (!isDummyBlock(BB))
    EmitBlock(new llvm::BasicBlock(N));
  else
    BB->setName(N);
}

/// getCGRecordLayout - Return record layout info.
const CGRecordLayout *CodeGenFunction::getCGRecordLayout(CodeGenTypes &CGT,
                                                         QualType RTy) {
  assert (isa<RecordType>(RTy) 
          && "Unexpected type. RecordType expected here.");

  const llvm::Type *Ty = ConvertType(RTy);
  assert (Ty && "Unable to find llvm::Type");
  
  return CGT.getCGRecordLayout(Ty);
}

/// WarnUnsupported - Print out a warning that codegen doesn't support the
/// specified stmt yet.
void CodeGenFunction::WarnUnsupported(const Stmt *S, const char *Type) {
  unsigned DiagID = CGM.getDiags().getCustomDiagID(Diagnostic::Warning, 
                                                  "cannot codegen this %0 yet");
  SourceRange Range = S->getSourceRange();
  std::string Msg = Type;
  CGM.getDiags().Report(S->getLocStart(), DiagID, &Msg, 1, &Range, 1);
}

