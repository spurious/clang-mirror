//===--- CGBlocks.cpp - Emit LLVM Code for declarations -------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This contains code to emit blocks.
//
//===----------------------------------------------------------------------===//

#include "CodeGenFunction.h"
#include "CodeGenModule.h"
#include "llvm/Module.h"
#include "llvm/Target/TargetData.h"

#include <algorithm>

using namespace clang;
using namespace CodeGen;

// Temporary code to enable testing of __block variables
// #include "clang/Frontend/CompileOptions.h"
#include "llvm/Support/CommandLine.h"
static llvm::cl::opt<bool>
Enable__block("f__block",
              // See all the FIXMEs for the various work that needs to be done
              llvm::cl::desc("temporary option to turn on __block precessing "
                             "even though the code isn't done yet"),
              llvm::cl::ValueDisallowed, llvm::cl::AllowInverse,
              llvm::cl::ZeroOrMore,
              llvm::cl::init(false));

llvm::Constant *CodeGenFunction::BuildDescriptorBlockDecl(uint64_t Size) {
  const llvm::Type *UnsignedLongTy
    = CGM.getTypes().ConvertType(getContext().UnsignedLongTy);
  llvm::Constant *C;
  std::vector<llvm::Constant*> Elts;

  // reserved
  C = llvm::ConstantInt::get(UnsignedLongTy, 0);
  Elts.push_back(C);

  // Size
  // FIXME: What is the right way to say this doesn't fit?  We should give
  // a user diagnostic in that case.  Better fix would be to change the
  // API to size_t.
  C = llvm::ConstantInt::get(UnsignedLongTy, Size);
  Elts.push_back(C);

  if (BlockHasCopyDispose) {
    // copy_func_helper_decl
    Elts.push_back(BuildCopyHelper());

    // destroy_func_decl
    Elts.push_back(BuildDestroyHelper());
  }

  C = llvm::ConstantStruct::get(Elts);

  C = new llvm::GlobalVariable(C->getType(), true,
                               llvm::GlobalValue::InternalLinkage,
                               C, "__block_descriptor_tmp", &CGM.getModule());
  return C;
}

llvm::Constant *BlockModule::getNSConcreteGlobalBlock() {
  if (NSConcreteGlobalBlock)
    return NSConcreteGlobalBlock;

  // FIXME: We should have a CodeGenModule::AddRuntimeVariable that does the
  // same thing as CreateRuntimeFunction if there's already a variable with the
  // same name.
  NSConcreteGlobalBlock
    = new llvm::GlobalVariable(PtrToInt8Ty, false,
                               llvm::GlobalValue::ExternalLinkage,
                               0, "_NSConcreteGlobalBlock",
                               &getModule());

  return NSConcreteGlobalBlock;
}

llvm::Constant *BlockModule::getNSConcreteStackBlock() {
  if (NSConcreteStackBlock)
    return NSConcreteStackBlock;

  // FIXME: We should have a CodeGenModule::AddRuntimeVariable that does the
  // same thing as CreateRuntimeFunction if there's already a variable with the
  // same name.
  NSConcreteStackBlock
    = new llvm::GlobalVariable(PtrToInt8Ty, false,
                               llvm::GlobalValue::ExternalLinkage,
                               0, "_NSConcreteStackBlock",
                               &getModule());

  return NSConcreteStackBlock;
}

static void CollectBlockDeclRefInfo(const Stmt *S,
                                    CodeGenFunction::BlockInfo &Info) {
  for (Stmt::const_child_iterator I = S->child_begin(), E = S->child_end();
       I != E; ++I)
    if (*I)
      CollectBlockDeclRefInfo(*I, Info);

  if (const BlockDeclRefExpr *DE = dyn_cast<BlockDeclRefExpr>(S)) {
    // FIXME: Handle enums.
    if (isa<FunctionDecl>(DE->getDecl()))
      return;

    if (DE->isByRef())
      Info.ByRefDeclRefs.push_back(DE);
    else
      Info.ByCopyDeclRefs.push_back(DE);
  }
}

/// CanBlockBeGlobal - Given a BlockInfo struct, determines if a block can be
/// declared as a global variable instead of on the stack.
static bool CanBlockBeGlobal(const CodeGenFunction::BlockInfo &Info)
{
  return Info.ByRefDeclRefs.empty() && Info.ByCopyDeclRefs.empty();
}

// FIXME: Push most into CGM, passing down a few bits, like current function
// name.
llvm::Value *CodeGenFunction::BuildBlockLiteralTmp(const BlockExpr *BE) {

  std::string Name = CurFn->getName();
  CodeGenFunction::BlockInfo Info(0, Name.c_str());
  CollectBlockDeclRefInfo(BE->getBody(), Info);

  // Check if the block can be global.
  // FIXME: This test doesn't work for nested blocks yet.  Longer term, I'd like
  // to just have one code path.  We should move this function into CGM and pass
  // CGF, then we can just check to see if CGF is 0.
  if (0 && CanBlockBeGlobal(Info))
    return CGM.GetAddrOfGlobalBlock(BE, Name.c_str());

  std::vector<llvm::Constant*> Elts(5);
  llvm::Constant *C;
  llvm::Value *V;

  {
    // C = BuildBlockStructInitlist();
    unsigned int flags = BLOCK_HAS_DESCRIPTOR;

    // We run this first so that we set BlockHasCopyDispose from the entire
    // block literal.
    // __invoke
    uint64_t subBlockSize, subBlockAlign;
    llvm::SmallVector<const Expr *, 8> subBlockDeclRefDecls;
    llvm::Function *Fn
      = CodeGenFunction(CGM).GenerateBlockFunction(BE, Info, subBlockSize,
                                                   subBlockAlign,
                                                   subBlockDeclRefDecls,
                                                   BlockHasCopyDispose);
    Elts[3] = Fn;

    if (!Enable__block && BlockHasCopyDispose)
      ErrorUnsupported(BE, "block literal that requires copy/dispose");

    if (BlockHasCopyDispose)
      flags |= BLOCK_HAS_COPY_DISPOSE;

    // __isa
    C = CGM.getNSConcreteStackBlock();
    C = llvm::ConstantExpr::getBitCast(C, PtrToInt8Ty);
    Elts[0] = C;

    // __flags
    const llvm::IntegerType *IntTy = cast<llvm::IntegerType>(
      CGM.getTypes().ConvertType(CGM.getContext().IntTy));
    C = llvm::ConstantInt::get(IntTy, flags);
    Elts[1] = C;

    // __reserved
    C = llvm::ConstantInt::get(IntTy, 0);
    Elts[2] = C;

    // __descriptor
    Elts[4] = BuildDescriptorBlockDecl(subBlockSize);

    if (subBlockDeclRefDecls.size() == 0) {
      // Optimize to being a global block.
      Elts[0] = CGM.getNSConcreteGlobalBlock();
      Elts[1] = llvm::ConstantInt::get(IntTy, flags|BLOCK_IS_GLOBAL);

      C = llvm::ConstantStruct::get(Elts);

      char Name[32];
      sprintf(Name, "__block_holder_tmp_%d", CGM.getGlobalUniqueCount());
      C = new llvm::GlobalVariable(C->getType(), true,
                                   llvm::GlobalValue::InternalLinkage,
                                   C, Name, &CGM.getModule());
      QualType BPT = BE->getType();
      C = llvm::ConstantExpr::getBitCast(C, ConvertType(BPT));
      return C;
    }

    std::vector<const llvm::Type *> Types(5+subBlockDeclRefDecls.size());
    for (int i=0; i<5; ++i)
      Types[i] = Elts[i]->getType();

    for (unsigned i=0; i < subBlockDeclRefDecls.size(); ++i) {
      const Expr *E = subBlockDeclRefDecls[i];
      const BlockDeclRefExpr *BDRE = dyn_cast<BlockDeclRefExpr>(E);
      QualType Ty = E->getType();
      if (BDRE && BDRE->isByRef()) {
        uint64_t Align = getContext().getDeclAlignInBytes(BDRE->getDecl());
        Types[i+5] = llvm::PointerType::get(BuildByRefType(Ty, Align), 0);
      } else
        Types[i+5] = ConvertType(Ty);
    }

    llvm::Type *Ty = llvm::StructType::get(Types, true);

    llvm::AllocaInst *A = CreateTempAlloca(Ty);
    A->setAlignment(subBlockAlign);
    V = A;

    for (unsigned i=0; i<5; ++i)
      Builder.CreateStore(Elts[i], Builder.CreateStructGEP(V, i, "block.tmp"));

    for (unsigned i=0; i < subBlockDeclRefDecls.size(); ++i)
      {
        // FIXME: Push const down.
        Expr *E = const_cast<Expr*>(subBlockDeclRefDecls[i]);
        DeclRefExpr *DR;
        ValueDecl *VD;

        DR = dyn_cast<DeclRefExpr>(E);
        // Skip padding.
        if (DR) continue;

        BlockDeclRefExpr *BDRE = dyn_cast<BlockDeclRefExpr>(E);
        VD = BDRE->getDecl();

        llvm::Value* Addr = Builder.CreateStructGEP(V, i+5, "tmp");
        // FIXME: I want a better way to do this.
        if (LocalDeclMap[VD]) {
          if (BDRE->isByRef()) {
            const llvm::Type *Ty = Types[i+5];
            llvm::Value *Loc = LocalDeclMap[VD];
            Loc = Builder.CreateStructGEP(Loc, 1, "forwarding");
            Loc = Builder.CreateLoad(Loc, false);
            Loc = Builder.CreateBitCast(Loc, Ty);
            Builder.CreateStore(Loc, Addr);
            continue;
          } else
            E = new (getContext()) DeclRefExpr (cast<NamedDecl>(VD),
                                                VD->getType(), SourceLocation(),
                                                false, false);
        }
        if (BDRE->isByRef()) {
          E = new (getContext())
            UnaryOperator(E, UnaryOperator::AddrOf,
                          getContext().getPointerType(E->getType()),
                          SourceLocation());
        }

        RValue r = EmitAnyExpr(E, Addr, false);
        if (r.isScalar()) {
          llvm::Value *Loc = r.getScalarVal();
          const llvm::Type *Ty = Types[i+5];
          if  (BDRE->isByRef()) {
            // E is now the address of the value field, instead, we want the
            // address of the actual ByRef struct.  We optimize this slightly
            // compared to gcc by not grabbing the forwarding slot as this must
            // be done during Block_copy for us, and we can postpone the work
            // until then.
            uint64_t offset = BlockDecls[BDRE->getDecl()];

            llvm::Value *BlockLiteral = LoadBlockStruct();

            Loc = Builder.CreateGEP(BlockLiteral,
                                    llvm::ConstantInt::get(llvm::Type::Int64Ty,
                                                           offset),
                                    "block.literal");
            Ty = llvm::PointerType::get(Ty, 0);
            Loc = Builder.CreateBitCast(Loc, Ty);
            Loc = Builder.CreateLoad(Loc, false);
            // Loc = Builder.CreateBitCast(Loc, Ty);
          }
          Builder.CreateStore(Loc, Addr);
        } else if (r.isComplex())
          // FIXME: implement
          ErrorUnsupported(BE, "complex in block literal");
        else if (r.isAggregate())
          ; // Already created into the destination
        else
          assert (0 && "bad block variable");
        // FIXME: Ensure that the offset created by the backend for
        // the struct matches the previously computed offset in BlockDecls.
      }
  }

  QualType BPT = BE->getType();
  return Builder.CreateBitCast(V, ConvertType(BPT));
}


const llvm::Type *BlockModule::getBlockDescriptorType() {
  if (BlockDescriptorType)
    return BlockDescriptorType;

  const llvm::Type *UnsignedLongTy =
    getTypes().ConvertType(getContext().UnsignedLongTy);

  // struct __block_descriptor {
  //   unsigned long reserved;
  //   unsigned long block_size;
  // };
  BlockDescriptorType = llvm::StructType::get(UnsignedLongTy,
                                              UnsignedLongTy,
                                              NULL);

  getModule().addTypeName("struct.__block_descriptor",
                          BlockDescriptorType);

  return BlockDescriptorType;
}

const llvm::Type *BlockModule::getGenericBlockLiteralType() {
  if (GenericBlockLiteralType)
    return GenericBlockLiteralType;

  const llvm::Type *BlockDescPtrTy =
    llvm::PointerType::getUnqual(getBlockDescriptorType());

  const llvm::IntegerType *IntTy = cast<llvm::IntegerType>(
    getTypes().ConvertType(getContext().IntTy));

  // struct __block_literal_generic {
  //   void *__isa;
  //   int __flags;
  //   int __reserved;
  //   void (*__invoke)(void *);
  //   struct __block_descriptor *__descriptor;
  // };
  GenericBlockLiteralType = llvm::StructType::get(PtrToInt8Ty,
                                                  IntTy,
                                                  IntTy,
                                                  PtrToInt8Ty,
                                                  BlockDescPtrTy,
                                                  NULL);

  getModule().addTypeName("struct.__block_literal_generic",
                          GenericBlockLiteralType);

  return GenericBlockLiteralType;
}

const llvm::Type *BlockModule::getGenericExtendedBlockLiteralType() {
  if (GenericExtendedBlockLiteralType)
    return GenericExtendedBlockLiteralType;

  const llvm::Type *BlockDescPtrTy =
    llvm::PointerType::getUnqual(getBlockDescriptorType());

  const llvm::IntegerType *IntTy = cast<llvm::IntegerType>(
    getTypes().ConvertType(getContext().IntTy));

  // struct __block_literal_generic {
  //   void *__isa;
  //   int __flags;
  //   int __reserved;
  //   void (*__invoke)(void *);
  //   struct __block_descriptor *__descriptor;
  //   void *__copy_func_helper_decl;
  //   void *__destroy_func_decl;
  // };
  GenericExtendedBlockLiteralType = llvm::StructType::get(PtrToInt8Ty,
                                                          IntTy,
                                                          IntTy,
                                                          PtrToInt8Ty,
                                                          BlockDescPtrTy,
                                                          PtrToInt8Ty,
                                                          PtrToInt8Ty,
                                                          NULL);

  getModule().addTypeName("struct.__block_literal_extended_generic",
                          GenericExtendedBlockLiteralType);

  return GenericExtendedBlockLiteralType;
}

/// getBlockFunctionType - Given a BlockPointerType, will return the
/// function type for the block, including the first block literal argument.
static QualType getBlockFunctionType(ASTContext &Ctx,
                                     const BlockPointerType *BPT) {
  const FunctionProtoType *FTy = cast<FunctionProtoType>(BPT->getPointeeType());

  llvm::SmallVector<QualType, 8> Types;
  Types.push_back(Ctx.getPointerType(Ctx.VoidTy));

  for (FunctionProtoType::arg_type_iterator i = FTy->arg_type_begin(),
       e = FTy->arg_type_end(); i != e; ++i)
    Types.push_back(*i);

  return Ctx.getFunctionType(FTy->getResultType(),
                             &Types[0], Types.size(),
                             FTy->isVariadic(), 0);
}

RValue CodeGenFunction::EmitBlockCallExpr(const CallExpr* E) {
  const BlockPointerType *BPT =
    E->getCallee()->getType()->getAsBlockPointerType();

  llvm::Value *Callee = EmitScalarExpr(E->getCallee());

  // Get a pointer to the generic block literal.
  const llvm::Type *BlockLiteralTy =
    llvm::PointerType::getUnqual(CGM.getGenericBlockLiteralType());

  // Bitcast the callee to a block literal.
  llvm::Value *BlockLiteral =
    Builder.CreateBitCast(Callee, BlockLiteralTy, "block.literal");

  // Get the function pointer from the literal.
  llvm::Value *FuncPtr = Builder.CreateStructGEP(BlockLiteral, 3, "tmp");
  llvm::Value *Func = Builder.CreateLoad(FuncPtr, false, "tmp");

  // Cast the function pointer to the right type.
  const llvm::Type *BlockFTy =
    ConvertType(getBlockFunctionType(getContext(), BPT));
  const llvm::Type *BlockFTyPtr = llvm::PointerType::getUnqual(BlockFTy);
  Func = Builder.CreateBitCast(Func, BlockFTyPtr);

  BlockLiteral =
    Builder.CreateBitCast(BlockLiteral,
                          llvm::PointerType::getUnqual(llvm::Type::Int8Ty),
                          "tmp");

  // Add the block literal.
  QualType VoidPtrTy = getContext().getPointerType(getContext().VoidTy);
  CallArgList Args;
  Args.push_back(std::make_pair(RValue::get(BlockLiteral), VoidPtrTy));

  // And the rest of the arguments.
  for (CallExpr::const_arg_iterator i = E->arg_begin(), e = E->arg_end();
       i != e; ++i)
    Args.push_back(std::make_pair(EmitAnyExprToTemp(*i),
                                  i->getType()));

  // And call the block.
  return EmitCall(CGM.getTypes().getFunctionInfo(E->getType(), Args),
                  Func, Args);
}

llvm::Value *CodeGenFunction::GetAddrOfBlockDecl(const BlockDeclRefExpr *E) {
  uint64_t &offset = BlockDecls[E->getDecl()];

  const llvm::Type *Ty;
  Ty = CGM.getTypes().ConvertType(E->getDecl()->getType());

  // FIXME: add support for copy/dispose helpers.
  if (!Enable__block && E->isByRef())
    ErrorUnsupported(E, "__block variable in block literal");
  else if (!Enable__block && E->getType()->isBlockPointerType())
    ErrorUnsupported(E, "block pointer in block literal");
  else if (E->getDecl()->getAttr<ObjCNSObjectAttr>() ||
           getContext().isObjCNSObjectType(E->getType()))
    ErrorUnsupported(E, "__attribute__((NSObject)) variable in block "
                     "literal");
  else if (!Enable__block && getContext().isObjCObjectPointerType(E->getType()))
    ErrorUnsupported(E, "Objective-C variable in block literal");

  // See if we have already allocated an offset for this variable.
  if (offset == 0) {
    // Don't run the expensive check, unless we have to.
    if (!BlockHasCopyDispose && BlockRequiresCopying(E->getType()))
      BlockHasCopyDispose = true;
    // if not, allocate one now.
    offset = getBlockOffset(E);
  }

  llvm::Value *BlockLiteral = LoadBlockStruct();
  llvm::Value *V = Builder.CreateGEP(BlockLiteral,
                                     llvm::ConstantInt::get(llvm::Type::Int64Ty,
                                                            offset),
                                     "block.literal");
  if (E->isByRef()) {
    bool needsCopyDispose = BlockRequiresCopying(E->getType());
    uint64_t Align = getContext().getDeclAlignInBytes(E->getDecl());
    const llvm::Type *PtrStructTy
      = llvm::PointerType::get(BuildByRefType(E->getType(), Align), 0);
    Ty = PtrStructTy;
    Ty = llvm::PointerType::get(Ty, 0);
    V = Builder.CreateBitCast(V, Ty);
    V = Builder.CreateLoad(V, false);
    V = Builder.CreateStructGEP(V, 1, "forwarding");
    V = Builder.CreateLoad(V, false);
    V = Builder.CreateBitCast(V, PtrStructTy);
    V = Builder.CreateStructGEP(V, needsCopyDispose*2 + 4, "x");
  } else {
    Ty = llvm::PointerType::get(Ty, 0);
    V = Builder.CreateBitCast(V, Ty);
  }
  return V;
}

llvm::Constant *
BlockModule::GetAddrOfGlobalBlock(const BlockExpr *BE, const char * n) {
  // Generate the block descriptor.
  const llvm::Type *UnsignedLongTy = Types.ConvertType(Context.UnsignedLongTy);
  const llvm::IntegerType *IntTy = cast<llvm::IntegerType>(
    getTypes().ConvertType(getContext().IntTy));

  llvm::Constant *DescriptorFields[2];

  // Reserved
  DescriptorFields[0] = llvm::Constant::getNullValue(UnsignedLongTy);

  // Block literal size. For global blocks we just use the size of the generic
  // block literal struct.
  uint64_t BlockLiteralSize =
    TheTargetData.getTypeStoreSizeInBits(getGenericBlockLiteralType()) / 8;
  DescriptorFields[1] = llvm::ConstantInt::get(UnsignedLongTy,BlockLiteralSize);

  llvm::Constant *DescriptorStruct =
    llvm::ConstantStruct::get(&DescriptorFields[0], 2);

  llvm::GlobalVariable *Descriptor =
    new llvm::GlobalVariable(DescriptorStruct->getType(), true,
                             llvm::GlobalVariable::InternalLinkage,
                             DescriptorStruct, "__block_descriptor_global",
                             &getModule());

  // Generate the constants for the block literal.
  llvm::Constant *LiteralFields[5];

  CodeGenFunction::BlockInfo Info(0, n);
  uint64_t subBlockSize, subBlockAlign;
  llvm::SmallVector<const Expr *, 8> subBlockDeclRefDecls;
  bool subBlockHasCopyDispose;
  llvm::Function *Fn
    = CodeGenFunction(CGM).GenerateBlockFunction(BE, Info, subBlockSize,
                                                 subBlockAlign,
                                                 subBlockDeclRefDecls,
                                                 subBlockHasCopyDispose);
  assert(subBlockSize == BlockLiteralSize
         && "no imports allowed for global block");
  assert(!subBlockHasCopyDispose && "no imports allowed for global block");

  // isa
  LiteralFields[0] = getNSConcreteGlobalBlock();

  // Flags
  LiteralFields[1] =
    llvm::ConstantInt::get(IntTy, BLOCK_IS_GLOBAL | BLOCK_HAS_DESCRIPTOR);

  // Reserved
  LiteralFields[2] = llvm::Constant::getNullValue(IntTy);

  // Function
  LiteralFields[3] = Fn;

  // Descriptor
  LiteralFields[4] = Descriptor;

  llvm::Constant *BlockLiteralStruct =
    llvm::ConstantStruct::get(&LiteralFields[0], 5);

  llvm::GlobalVariable *BlockLiteral =
    new llvm::GlobalVariable(BlockLiteralStruct->getType(), true,
                             llvm::GlobalVariable::InternalLinkage,
                             BlockLiteralStruct, "__block_literal_global",
                             &getModule());

  return BlockLiteral;
}

llvm::Value *CodeGenFunction::LoadBlockStruct() {
  return Builder.CreateLoad(LocalDeclMap[getBlockStructDecl()], "self");
}

llvm::Function *
CodeGenFunction::GenerateBlockFunction(const BlockExpr *BExpr,
                                       const BlockInfo& Info,
                                       uint64_t &Size,
                                       uint64_t &Align,
                       llvm::SmallVector<const Expr *, 8> &subBlockDeclRefDecls,
                                       bool &subBlockHasCopyDispose) {
  const FunctionProtoType *FTy =
    cast<FunctionProtoType>(BExpr->getFunctionType());

  FunctionArgList Args;

  const BlockDecl *BD = BExpr->getBlockDecl();

  // FIXME: This leaks
  ImplicitParamDecl *SelfDecl =
    ImplicitParamDecl::Create(getContext(), 0,
                              SourceLocation(), 0,
                              getContext().getPointerType(getContext().VoidTy));

  Args.push_back(std::make_pair(SelfDecl, SelfDecl->getType()));
  BlockStructDecl = SelfDecl;

  for (BlockDecl::param_iterator i = BD->param_begin(),
       e = BD->param_end(); i != e; ++i)
    Args.push_back(std::make_pair(*i, (*i)->getType()));

  const CGFunctionInfo &FI =
    CGM.getTypes().getFunctionInfo(FTy->getResultType(), Args);

  std::string Name = std::string("__") + Info.Name + "_block_invoke_";
  CodeGenTypes &Types = CGM.getTypes();
  const llvm::FunctionType *LTy = Types.GetFunctionType(FI, FTy->isVariadic());

  llvm::Function *Fn =
    llvm::Function::Create(LTy, llvm::GlobalValue::InternalLinkage,
                           Name,
                           &CGM.getModule());

  StartFunction(BD, FTy->getResultType(), Fn, Args,
                BExpr->getBody()->getLocEnd());
  EmitStmt(BExpr->getBody());
  FinishFunction(cast<CompoundStmt>(BExpr->getBody())->getRBracLoc());

  // The runtime needs a minimum alignment of a void *.
  uint64_t MinAlign = getContext().getTypeAlign(getContext().VoidPtrTy) / 8;
  BlockOffset = llvm::RoundUpToAlignment(BlockOffset, MinAlign);

  Size = BlockOffset;
  Align = BlockAlign;
  subBlockDeclRefDecls = BlockDeclRefDecls;
  subBlockHasCopyDispose |= BlockHasCopyDispose;
  return Fn;
}

uint64_t CodeGenFunction::getBlockOffset(const BlockDeclRefExpr *BDRE) {
  const ValueDecl *D = dyn_cast<ValueDecl>(BDRE->getDecl());

  uint64_t Size = getContext().getTypeSize(D->getType()) / 8;
  uint64_t Align = getContext().getDeclAlignInBytes(D);

  if (BDRE->isByRef()) {
    Size = getContext().getTypeSize(getContext().VoidPtrTy) / 8;
    Align = getContext().getTypeAlign(getContext().VoidPtrTy) / 8;
  }

  assert ((Align > 0) && "alignment must be 1 byte or more");

  uint64_t OldOffset = BlockOffset;

  // Ensure proper alignment, even if it means we have to have a gap
  BlockOffset = llvm::RoundUpToAlignment(BlockOffset, Align);
  BlockAlign = std::max(Align, BlockAlign);

  uint64_t Pad = BlockOffset - OldOffset;
  if (Pad) {
    llvm::ArrayType::get(llvm::Type::Int8Ty, Pad);
    QualType PadTy = getContext().getConstantArrayType(getContext().CharTy,
                                                       llvm::APInt(32, Pad),
                                                       ArrayType::Normal, 0);
    ValueDecl *PadDecl = VarDecl::Create(getContext(), 0, SourceLocation(),
                                         0, QualType(PadTy), VarDecl::None,
                                         SourceLocation());
    Expr *E;
    E = new (getContext()) DeclRefExpr(PadDecl, PadDecl->getType(),
                                       SourceLocation(), false, false);
    BlockDeclRefDecls.push_back(E);
  }
  BlockDeclRefDecls.push_back(BDRE);

  BlockOffset += Size;
  return BlockOffset-Size;
}

llvm::Constant *BlockFunction::GenerateCopyHelperFunction() {
  QualType R = getContext().VoidTy;

  FunctionArgList Args;
  // FIXME: This leaks
  ImplicitParamDecl *Src =
    ImplicitParamDecl::Create(getContext(), 0, SourceLocation(), 0,
                              getContext().getPointerType(getContext().VoidTy));

  Args.push_back(std::make_pair(Src, Src->getType()));
  
  const CGFunctionInfo &FI =
    CGM.getTypes().getFunctionInfo(R, Args);

  std::string Name = std::string("__copy_helper_block_");
  CodeGenTypes &Types = CGM.getTypes();
  const llvm::FunctionType *LTy = Types.GetFunctionType(FI, false);

  llvm::Function *Fn =
    llvm::Function::Create(LTy, llvm::GlobalValue::InternalLinkage,
                           Name,
                           &CGM.getModule());

  IdentifierInfo *II
    = &CGM.getContext().Idents.get("__copy_helper_block_");

  FunctionDecl *FD = FunctionDecl::Create(getContext(),
                                          getContext().getTranslationUnitDecl(),
                                          SourceLocation(), II, R,
                                          FunctionDecl::Static, false,
                                          true);
  CGF.StartFunction(FD, R, Fn, Args, SourceLocation());
  // EmitStmt(BExpr->getBody());
  CGF.FinishFunction();

  return llvm::ConstantExpr::getBitCast(Fn, PtrToInt8Ty);
}

llvm::Constant *BlockFunction::GenerateDestroyHelperFunction() {
  QualType R = getContext().VoidTy;

  FunctionArgList Args;
  // FIXME: This leaks
  ImplicitParamDecl *Src =
    ImplicitParamDecl::Create(getContext(), 0, SourceLocation(), 0,
                              getContext().getPointerType(getContext().VoidTy));

  Args.push_back(std::make_pair(Src, Src->getType()));
  
  const CGFunctionInfo &FI =
    CGM.getTypes().getFunctionInfo(R, Args);

  std::string Name = std::string("__destroy_helper_block_");
  CodeGenTypes &Types = CGM.getTypes();
  const llvm::FunctionType *LTy = Types.GetFunctionType(FI, false);

  llvm::Function *Fn =
    llvm::Function::Create(LTy, llvm::GlobalValue::InternalLinkage,
                           Name,
                           &CGM.getModule());

  IdentifierInfo *II
    = &CGM.getContext().Idents.get("__destroy_helper_block_");

  FunctionDecl *FD = FunctionDecl::Create(getContext(),
                                          getContext().getTranslationUnitDecl(),
                                          SourceLocation(), II, R,
                                          FunctionDecl::Static, false,
                                          true);
  CGF.StartFunction(FD, R, Fn, Args, SourceLocation());
  // EmitStmt(BExpr->getBody());
  CGF.FinishFunction();

  return llvm::ConstantExpr::getBitCast(Fn, PtrToInt8Ty);
}

llvm::Constant *BlockFunction::BuildCopyHelper() {
  return CodeGenFunction(CGM).GenerateCopyHelperFunction();
}

llvm::Constant *BlockFunction::BuildDestroyHelper() {
  return CodeGenFunction(CGM).GenerateDestroyHelperFunction();
}

llvm::Constant *BlockFunction::
GeneratebyrefCopyHelperFunction(const llvm::Type *T, int flag) {
  QualType R = getContext().VoidTy;

  FunctionArgList Args;
  // FIXME: This leaks
  ImplicitParamDecl *Dst =
    ImplicitParamDecl::Create(getContext(), 0, SourceLocation(), 0,
                              getContext().getPointerType(getContext().VoidTy));
  Args.push_back(std::make_pair(Dst, Dst->getType()));

  // FIXME: This leaks
  ImplicitParamDecl *Src =
    ImplicitParamDecl::Create(getContext(), 0, SourceLocation(), 0,
                              getContext().getPointerType(getContext().VoidTy));
  Args.push_back(std::make_pair(Src, Src->getType()));
  
  const CGFunctionInfo &FI =
    CGM.getTypes().getFunctionInfo(R, Args);

  std::string Name = std::string("__Block_byref_id_object_copy_");
  CodeGenTypes &Types = CGM.getTypes();
  const llvm::FunctionType *LTy = Types.GetFunctionType(FI, false);

  llvm::Function *Fn =
    llvm::Function::Create(LTy, llvm::GlobalValue::InternalLinkage,
                           Name,
                           &CGM.getModule());

  IdentifierInfo *II
    = &CGM.getContext().Idents.get("__Block_byref_id_object_copy_");

  FunctionDecl *FD = FunctionDecl::Create(getContext(),
                                          getContext().getTranslationUnitDecl(),
                                          SourceLocation(), II, R,
                                          FunctionDecl::Static, false,
                                          true);
  CGF.StartFunction(FD, R, Fn, Args, SourceLocation());

  // dst->x
  llvm::Value *V = CGF.GetAddrOfLocalVar(Dst);
  V = Builder.CreateBitCast(V, T);
  V = Builder.CreateStructGEP(V, 6, "x");
  llvm::Value *DstObj = Builder.CreateBitCast(V, PtrToInt8Ty);

  // src->x
  V = CGF.GetAddrOfLocalVar(Src);
  V = Builder.CreateLoad(V);
  V = Builder.CreateBitCast(V, T);
  V = Builder.CreateStructGEP(V, 6, "x");
  V = Builder.CreateBitCast(V, llvm::PointerType::get(PtrToInt8Ty, 0));
  llvm::Value *SrcObj = Builder.CreateLoad(V);
  
  flag |= BLOCK_BYREF_CALLER;

  llvm::Value *N = llvm::ConstantInt::get(llvm::Type::Int32Ty, flag);
  llvm::Value *F = getBlockObjectAssign();
  Builder.CreateCall3(F, DstObj, SrcObj, N);

  CGF.FinishFunction();

  return llvm::ConstantExpr::getBitCast(Fn, PtrToInt8Ty);
}

llvm::Constant *
BlockFunction::GeneratebyrefDestroyHelperFunction(const llvm::Type *T,
                                                  int flag) {
  QualType R = getContext().VoidTy;

  FunctionArgList Args;
  // FIXME: This leaks
  ImplicitParamDecl *Src =
    ImplicitParamDecl::Create(getContext(), 0, SourceLocation(), 0,
                              getContext().getPointerType(getContext().VoidTy));

  Args.push_back(std::make_pair(Src, Src->getType()));
  
  const CGFunctionInfo &FI =
    CGM.getTypes().getFunctionInfo(R, Args);

  std::string Name = std::string("__Block_byref_id_object_dispose_");
  CodeGenTypes &Types = CGM.getTypes();
  const llvm::FunctionType *LTy = Types.GetFunctionType(FI, false);

  llvm::Function *Fn =
    llvm::Function::Create(LTy, llvm::GlobalValue::InternalLinkage,
                           Name,
                           &CGM.getModule());

  IdentifierInfo *II
    = &CGM.getContext().Idents.get("__Block_byref_id_object_dispose_");

  FunctionDecl *FD = FunctionDecl::Create(getContext(),
                                          getContext().getTranslationUnitDecl(),
                                          SourceLocation(), II, R,
                                          FunctionDecl::Static, false,
                                          true);
  CGF.StartFunction(FD, R, Fn, Args, SourceLocation());

  llvm::Value *V = CGF.GetAddrOfLocalVar(Src);
  V = Builder.CreateBitCast(V, T);
  V = Builder.CreateStructGEP(V, 6, "x");
  V = Builder.CreateBitCast(V, PtrToInt8Ty);

  // FIXME: Move to other one.
  // int flag = BLOCK_FIELD_IS_BYREF;
  // FIXME: Add weak support
  if (0)
    flag |= BLOCK_FIELD_IS_WEAK;
  flag |= BLOCK_BYREF_CALLER;
  BuildBlockRelease(V, flag);
  CGF.FinishFunction();

  return llvm::ConstantExpr::getBitCast(Fn, PtrToInt8Ty);
}

llvm::Constant *BlockFunction::BuildbyrefCopyHelper(const llvm::Type *T,
                                                    int flag) {
  return CodeGenFunction(CGM).GeneratebyrefCopyHelperFunction(T, flag);
}

llvm::Constant *BlockFunction::BuildbyrefDestroyHelper(const llvm::Type *T,
                                                       int flag) {
  return CodeGenFunction(CGM).GeneratebyrefDestroyHelperFunction(T, flag);
}

llvm::Value *BlockFunction::getBlockObjectDispose() {
  if (CGM.BlockObjectDispose == 0) {
    const llvm::FunctionType *FTy;
    std::vector<const llvm::Type*> ArgTys;
    const llvm::Type *ResultType = llvm::Type::VoidTy;
    ArgTys.push_back(PtrToInt8Ty);
    ArgTys.push_back(llvm::Type::Int32Ty);
    FTy = llvm::FunctionType::get(ResultType, ArgTys, false);
    CGM.BlockObjectDispose
      = CGM.CreateRuntimeFunction(FTy, "_Block_object_dispose");
  }
  return CGM.BlockObjectDispose;
}

llvm::Value *BlockFunction::getBlockObjectAssign() {
  if (CGM.BlockObjectAssign == 0) {
    const llvm::FunctionType *FTy;
    std::vector<const llvm::Type*> ArgTys;
    const llvm::Type *ResultType = llvm::Type::VoidTy;
    ArgTys.push_back(PtrToInt8Ty);
    ArgTys.push_back(PtrToInt8Ty);
    ArgTys.push_back(llvm::Type::Int32Ty);
    FTy = llvm::FunctionType::get(ResultType, ArgTys, false);
    CGM.BlockObjectAssign
      = CGM.CreateRuntimeFunction(FTy, "_Block_object_assign");
  }
  return CGM.BlockObjectAssign;
}

void BlockFunction::BuildBlockRelease(llvm::Value *V, int flag) {
  llvm::Value *F = getBlockObjectDispose();
  llvm::Value *N;
  V = Builder.CreateBitCast(V, PtrToInt8Ty);
  N = llvm::ConstantInt::get(llvm::Type::Int32Ty, flag);
  Builder.CreateCall2(F, V, N);
}

ASTContext &BlockFunction::getContext() const { return CGM.getContext(); }
