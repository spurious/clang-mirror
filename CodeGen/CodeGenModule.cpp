//===--- CodeGenModule.cpp - Emit LLVM Code from ASTs for a Module --------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This coordinates the per-module state used while generating code.
//
//===----------------------------------------------------------------------===//

#include "CodeGenModule.h"
#include "CodeGenFunction.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/TargetInfo.h"
#include "llvm/Constants.h"
#include "llvm/DerivedTypes.h"
#include "llvm/Module.h"
#include "llvm/Intrinsics.h"
#include <algorithm>
using namespace clang;
using namespace CodeGen;


CodeGenModule::CodeGenModule(ASTContext &C, const LangOptions &LO,
                             llvm::Module &M, const llvm::TargetData &TD,
                             Diagnostic &diags)
  : Context(C), Features(LO), TheModule(M), TheTargetData(TD), Diags(diags),
    Types(C, M, TD), MemCpyFn(0), CFConstantStringClassRef(0) {}

/// WarnUnsupported - Print out a warning that codegen doesn't support the
/// specified stmt yet.
void CodeGenModule::WarnUnsupported(const Stmt *S, const char *Type) {
  unsigned DiagID = getDiags().getCustomDiagID(Diagnostic::Warning, 
                                               "cannot codegen this %0 yet");
  SourceRange Range = S->getSourceRange();
  std::string Msg = Type;
  getDiags().Report(Context.getFullLoc(S->getLocStart()), DiagID,
                    &Msg, 1, &Range, 1);
}

/// WarnUnsupported - Print out a warning that codegen doesn't support the
/// specified decl yet.
void CodeGenModule::WarnUnsupported(const Decl *D, const char *Type) {
  unsigned DiagID = getDiags().getCustomDiagID(Diagnostic::Warning, 
                                               "cannot codegen this %0 yet");
  std::string Msg = Type;
  getDiags().Report(Context.getFullLoc(D->getLocation()), DiagID,
                    &Msg, 1);
}

/// ReplaceMapValuesWith - This is a really slow and bad function that
/// searches for any entries in GlobalDeclMap that point to OldVal, changing
/// them to point to NewVal.  This is badbadbad, FIXME!
void CodeGenModule::ReplaceMapValuesWith(llvm::Constant *OldVal,
                                         llvm::Constant *NewVal) {
  for (llvm::DenseMap<const Decl*, llvm::Constant*>::iterator 
       I = GlobalDeclMap.begin(), E = GlobalDeclMap.end(); I != E; ++I)
    if (I->second == OldVal) I->second = NewVal;
}


llvm::Constant *CodeGenModule::GetAddrOfFunctionDecl(const FunctionDecl *D,
                                                     bool isDefinition) {
  // See if it is already in the map.  If so, just return it.
  llvm::Constant *&Entry = GlobalDeclMap[D];
  if (Entry) return Entry;
  
  const llvm::Type *Ty = getTypes().ConvertType(D->getType());
  
  // Check to see if the function already exists.
  llvm::Function *F = getModule().getFunction(D->getName());
  const llvm::FunctionType *FTy = cast<llvm::FunctionType>(Ty);

  // If it doesn't already exist, just create and return an entry.
  if (F == 0) {
    // FIXME: param attributes for sext/zext etc.
    return Entry = new llvm::Function(FTy, llvm::Function::ExternalLinkage,
                                      D->getName(), &getModule());
  }
  
  // If the pointer type matches, just return it.
  llvm::Type *PFTy = llvm::PointerType::getUnqual(Ty);
  if (PFTy == F->getType()) return Entry = F;
    
  // If this isn't a definition, just return it casted to the right type.
  if (!isDefinition)
    return Entry = llvm::ConstantExpr::getBitCast(F, PFTy);
  
  // Otherwise, we have a definition after a prototype with the wrong type.
  // F is the Function* for the one with the wrong type, we must make a new
  // Function* and update everything that used F (a declaration) with the new
  // Function* (which will be a definition).
  //
  // This happens if there is a prototype for a function (e.g. "int f()") and
  // then a definition of a different type (e.g. "int f(int x)").  Start by
  // making a new function of the correct type, RAUW, then steal the name.
  llvm::Function *NewFn = new llvm::Function(FTy, 
                                             llvm::Function::ExternalLinkage,
                                             "", &getModule());
  NewFn->takeName(F);
  
  // Replace uses of F with the Function we will endow with a body.
  llvm::Constant *NewPtrForOldDecl = 
    llvm::ConstantExpr::getBitCast(NewFn, F->getType());
  F->replaceAllUsesWith(NewPtrForOldDecl);
  
  // FIXME: Update the globaldeclmap for the previous decl of this name.  We
  // really want a way to walk all of these, but we don't have it yet.  This
  // is incredibly slow!
  ReplaceMapValuesWith(F, NewPtrForOldDecl);
  
  // Ok, delete the old function now, which is dead.
  assert(F->isDeclaration() && "Shouldn't replace non-declaration");
  F->eraseFromParent();

  // Return the new function which has the right type.
  return Entry = NewFn;
}

llvm::Constant *CodeGenModule::GetAddrOfGlobalVar(const VarDecl *D,
                                                  bool isDefinition) {
  assert(D->hasGlobalStorage() && "Not a global variable");
  
  // See if it is already in the map.
  llvm::Constant *&Entry = GlobalDeclMap[D];
  if (Entry) return Entry;
  
  const llvm::Type *Ty = getTypes().ConvertTypeForMem(D->getType());

  // Check to see if the global already exists.
  llvm::GlobalVariable *GV = getModule().getGlobalVariable(D->getName());

  // If it doesn't already exist, just create and return an entry.
  if (GV == 0) {
    return Entry = new llvm::GlobalVariable(Ty, false, 
                                            llvm::GlobalValue::ExternalLinkage,
                                            0, D->getName(), &getModule());
  }
  
  // If the pointer type matches, just return it.
  llvm::Type *PTy = llvm::PointerType::getUnqual(Ty);
  if (PTy == GV->getType()) return Entry = GV;
  
  // If this isn't a definition, just return it casted to the right type.
  if (!isDefinition)
    return Entry = llvm::ConstantExpr::getBitCast(GV, PTy);
  
  
  // Otherwise, we have a definition after a prototype with the wrong type.
  // GV is the GlobalVariable* for the one with the wrong type, we must make a
  /// new GlobalVariable* and update everything that used GV (a declaration)
  // with the new GlobalVariable* (which will be a definition).
  //
  // This happens if there is a prototype for a global (e.g. "extern int x[];")
  // and then a definition of a different type (e.g. "int x[10];").  Start by
  // making a new global of the correct type, RAUW, then steal the name.
  llvm::GlobalVariable *NewGV = 
    new llvm::GlobalVariable(Ty, false, llvm::GlobalValue::ExternalLinkage,
                             0, D->getName(), &getModule());
  NewGV->takeName(GV);
  
  // Replace uses of GV with the globalvalue we will endow with a body.
  llvm::Constant *NewPtrForOldDecl = 
    llvm::ConstantExpr::getBitCast(NewGV, GV->getType());
  GV->replaceAllUsesWith(NewPtrForOldDecl);
  
  // FIXME: Update the globaldeclmap for the previous decl of this name.  We
  // really want a way to walk all of these, but we don't have it yet.  This
  // is incredibly slow!
  ReplaceMapValuesWith(GV, NewPtrForOldDecl);
  
  // Ok, delete the old global now, which is dead.
  assert(GV->isDeclaration() && "Shouldn't replace non-declaration");
  GV->eraseFromParent();
  
  // Return the new global which has the right type.
  return Entry = NewGV;
}


void CodeGenModule::EmitFunction(const FunctionDecl *FD) {
  // If this is not a prototype, emit the body.
  if (FD->getBody())
    CodeGenFunction(*this).GenerateCode(FD);
}

static llvm::Constant *GenerateConstantExpr(const Expr *Expression, 
                                            CodeGenModule &CGM);

/// GenerateConversionToBool - Generate comparison to zero for conversion to 
/// bool
static llvm::Constant *GenerateConversionToBool(llvm::Constant *Expression, 
                                            QualType Source) {
  if (Source->isRealFloatingType()) {
    // Compare against 0.0 for fp scalars.
    llvm::Constant *Zero = llvm::Constant::getNullValue(Expression->getType());
    return llvm::ConstantExpr::getFCmp(llvm::FCmpInst::FCMP_UNE, Expression, 
                                       Zero);
  }

  assert((Source->isIntegerType() || Source->isPointerType()) &&
         "Unknown scalar type to convert");

  // Compare against an integer or pointer null.
  llvm::Constant *Zero = llvm::Constant::getNullValue(Expression->getType());
  return llvm::ConstantExpr::getICmp(llvm::ICmpInst::ICMP_NE, Expression, Zero);
}

/// GenerateConstantCast - Generates a constant cast to convert the Expression
/// into the Target type.
static llvm::Constant *GenerateConstantCast(const Expr *Expression, 
                                            QualType Target, 
                                            CodeGenModule &CGM) {
  CodeGenTypes& Types = CGM.getTypes(); 
  QualType Source = Expression->getType().getCanonicalType();
  Target = Target.getCanonicalType();

  assert (!Target->isVoidType());

  llvm::Constant *SubExpr = GenerateConstantExpr(Expression, CGM);

  if (Source == Target)
      return SubExpr;

  // Handle conversions to bool first, they are special: comparisons against 0.
  if (Target->isBooleanType())
    return GenerateConversionToBool(SubExpr, Source);
    
  const llvm::Type *SourceType = Types.ConvertType(Source);
  const llvm::Type *TargetType = Types.ConvertType(Target);

  // Ignore conversions like int -> uint.
  if (SubExpr->getType() == TargetType)
    return SubExpr;

  // Handle pointer conversions next: pointers can only be converted to/from
  // other pointers and integers.
  if (isa<llvm::PointerType>(TargetType)) {
    // The source value may be an integer, or a pointer.
    if (isa<llvm::PointerType>(SubExpr->getType()))
      return llvm::ConstantExpr::getBitCast(SubExpr, TargetType);
    assert(Source->isIntegerType() && "Not ptr->ptr or int->ptr conversion?");
    return llvm::ConstantExpr::getIntToPtr(SubExpr, TargetType);
  }

  if (isa<llvm::PointerType>(SourceType)) {
    // Must be an ptr to int cast.
    assert(isa<llvm::IntegerType>(TargetType) && "not ptr->int?");
    return llvm::ConstantExpr::getPtrToInt(SubExpr, TargetType);
  }

  if (Source->isRealFloatingType() && Target->isRealFloatingType()) {
    return llvm::ConstantExpr::getFPCast(SubExpr, TargetType);
  }

  // Finally, we have the arithmetic types: real int/float.
  if (isa<llvm::IntegerType>(SourceType)) {
    bool InputSigned = Source->isSignedIntegerType();
    if (isa<llvm::IntegerType>(TargetType))
      return llvm::ConstantExpr::getIntegerCast(SubExpr, TargetType, 
                                                InputSigned);
    else if (InputSigned)
      return llvm::ConstantExpr::getSIToFP(SubExpr, TargetType);
    else
      return llvm::ConstantExpr::getUIToFP(SubExpr, TargetType);
  }

  assert(SubExpr->getType()->isFloatingPoint() && "Unknown real conversion");
  if (isa<llvm::IntegerType>(TargetType)) {
    if (Target->isSignedIntegerType())
      return llvm::ConstantExpr::getFPToSI(SubExpr, TargetType);
    else
      return llvm::ConstantExpr::getFPToUI(SubExpr, TargetType);
  }

  assert(TargetType->isFloatingPoint() && "Unknown real conversion");
  if (TargetType->getTypeID() < SubExpr->getType()->getTypeID())
    return llvm::ConstantExpr::getFPTrunc(SubExpr, TargetType);
  else
    return llvm::ConstantExpr::getFPExtend(SubExpr, TargetType);

  assert (!"Unsupported cast type in global intialiser.");
  return 0;
}

/// GenerateAggregateInit - Generate a Constant initaliser for global array or
/// struct typed variables.
static llvm::Constant *GenerateAggregateInit(const InitListExpr *ILE, 
                                             CodeGenModule &CGM) {
  if (ILE->getType()->isVoidType()) {
    // FIXME: Remove this when sema of initializers is finished (and the code
    // below).
    CGM.WarnUnsupported(ILE, "initializer");
    return 0;
  }
  
  assert((ILE->getType()->isArrayType() || ILE->getType()->isStructureType() ||
          ILE->getType()->isVectorType()) &&
         "Bad type for init list!");
  CodeGenTypes& Types = CGM.getTypes();

  unsigned NumInitElements = ILE->getNumInits();
  unsigned NumInitableElts = NumInitElements;

  const llvm::CompositeType *CType = 
    cast<llvm::CompositeType>(Types.ConvertType(ILE->getType()));
  assert(CType);
  std::vector<llvm::Constant*> Elts;    
    
  // Initialising an array requires us to automatically initialise any 
  // elements that have not been initialised explicitly
  const llvm::ArrayType *AType = 0; 
  const llvm::Type *AElemTy = 0;
  unsigned NumArrayElements = 0;
  
  // If this is an array, we may have to truncate the initializer
  if ((AType = dyn_cast<llvm::ArrayType>(CType))) {
    NumArrayElements = AType->getNumElements();
    AElemTy = AType->getElementType();
    NumInitableElts = std::min(NumInitableElts, NumArrayElements);
  }
    
  // Copy initializer elements.
  unsigned i = 0;
  for (i = 0; i < NumInitableElts; ++i) {
    llvm::Constant *C = GenerateConstantExpr(ILE->getInit(i), CGM);
    // FIXME: Remove this when sema of initializers is finished (and the code
    // above).
    if (C == 0 && ILE->getInit(i)->getType()->isVoidType()) {
      if (ILE->getType()->isVoidType()) return 0;
      return llvm::UndefValue::get(CType);
    }
    assert (C && "Failed to create initialiser expression");
    Elts.push_back(C);
  }

  if (ILE->getType()->isStructureType())
    return llvm::ConstantStruct::get(cast<llvm::StructType>(CType), Elts);

  if (ILE->getType()->isVectorType())
    return llvm::ConstantVector::get(cast<llvm::VectorType>(CType), Elts);

  // Make sure we have an array at this point
  assert(AType);

  // Initialize remaining array elements.
  for (; i < NumArrayElements; ++i)
    Elts.push_back(llvm::Constant::getNullValue(AElemTy));
    
  return llvm::ConstantArray::get(AType, Elts);    
}

/// GenerateConstantExpr - Recursively builds a constant initialiser for the
/// given expression.
static llvm::Constant *GenerateConstantExpr(const Expr *Expression, 
                                            CodeGenModule &CGM) {
  CodeGenTypes& Types = CGM.getTypes(); 
  ASTContext& Context = CGM.getContext();
  assert ((Expression->isConstantExpr(Context, 0) ||
           Expression->getStmtClass() == Stmt::InitListExprClass) &&
          "Only constant global initialisers are supported.");

  QualType type = Expression->getType().getCanonicalType();

  if (type->isIntegerType()) {
    llvm::APSInt
      Value(static_cast<uint32_t>(Context.getTypeSize(type, SourceLocation())));
    if (Expression->isIntegerConstantExpr(Value, Context)) {
      return llvm::ConstantInt::get(Value);
    } 
  }

  switch (Expression->getStmtClass()) {
  default: break; // default emits a warning and returns bogus value.
  case Stmt::DeclRefExprClass:  {
    const ValueDecl *Decl = cast<DeclRefExpr>(Expression)->getDecl();
    if (const FunctionDecl *FD = dyn_cast<FunctionDecl>(Decl))
      return CGM.GetAddrOfFunctionDecl(FD, false);
    break;
  }
      
  // Generate constant for floating point literal values.
  case Stmt::FloatingLiteralClass: {
    const FloatingLiteral *FLiteral = cast<FloatingLiteral>(Expression);
    return llvm::ConstantFP::get(Types.ConvertType(type), FLiteral->getValue());
  }

  // Generate constant for string literal values.
  case Stmt::StringLiteralClass: {
    const StringLiteral *String = cast<StringLiteral>(Expression);
    const char *StrData = String->getStrData();
    unsigned Len = String->getByteLength();

    // If the string has a pointer type, emit it as a global and use the pointer
    // to the global as its value.
    if (String->getType()->isPointerType()) 
      return CGM.GetAddrOfConstantString(std::string(StrData, StrData + Len));

    // Otherwise this must be a string initializing an array in a static
    // initializer.  Don't emit it as the address of the string, emit the string
    // data itself as an inline array.
    const ConstantArrayType *CAT = String->getType()->getAsConstantArrayType();
    assert(CAT && "String isn't pointer or array!");
    
    std::string Str(StrData, StrData + Len);
    // Null terminate the string before potentially truncating it.
    // FIXME: What about wchar_t strings?
    Str.push_back(0);
          
    uint64_t RealLen = CAT->getSize().getZExtValue();
    // String or grow the initializer to the required size.
    if (RealLen != Str.size())
      Str.resize(RealLen);
    
    return llvm::ConstantArray::get(Str, false);
  }

  // Generate initializer for the CompoundLiteral
  case Stmt::CompoundLiteralExprClass: {
    const CompoundLiteralExpr *CLE = cast<CompoundLiteralExpr>(Expression);
    return GenerateConstantExpr(CLE->getInitializer(), CGM);
  }

  // Elide parenthesis.
  case Stmt::ParenExprClass:
    return GenerateConstantExpr(cast<ParenExpr>(Expression)->getSubExpr(), CGM);
        
  // Generate constant for sizeof operator.
  // FIXME: Need to support AlignOf
  case Stmt::SizeOfAlignOfTypeExprClass: {
    const SizeOfAlignOfTypeExpr *SOExpr = 
      cast<SizeOfAlignOfTypeExpr>(Expression);
    assert (SOExpr->isSizeOf());
    return llvm::ConstantExpr::getSizeOf(Types.ConvertType(type));
  }

  // Generate constant cast expressions.
  case Stmt::CastExprClass:
    return GenerateConstantCast(cast<CastExpr>(Expression)->getSubExpr(), type,
                                CGM);
  case Stmt::UnaryOperatorClass: {
    const UnaryOperator *Op = cast<UnaryOperator>(Expression);
    llvm::Constant *SubExpr = GenerateConstantExpr(Op->getSubExpr(), CGM);
    // FIXME: These aren't right for complex.
    switch (Op->getOpcode()) {
    default: break;
    case UnaryOperator::Plus:
    case UnaryOperator::Extension:
      return SubExpr;
    case UnaryOperator::Minus:
      return llvm::ConstantExpr::getNeg(SubExpr);
    case UnaryOperator::Not:
      return llvm::ConstantExpr::getNot(SubExpr);
    case UnaryOperator::LNot:
      if (Op->getSubExpr()->getType()->isRealFloatingType()) {
        // Compare against 0.0 for fp scalars.
        llvm::Constant *Zero = llvm::Constant::getNullValue(SubExpr->getType());
        SubExpr = llvm::ConstantExpr::getFCmp(llvm::FCmpInst::FCMP_UNE, SubExpr,
                                              Zero);
      } else {
        assert((Op->getSubExpr()->getType()->isIntegerType() ||
                Op->getSubExpr()->getType()->isPointerType()) &&
               "Unknown scalar type to convert");
        // Compare against an integer or pointer null.
        llvm::Constant *Zero = llvm::Constant::getNullValue(SubExpr->getType());
        SubExpr = llvm::ConstantExpr::getICmp(llvm::ICmpInst::ICMP_NE, SubExpr,
                                              Zero);
      }
        
      return llvm::ConstantExpr::getZExt(SubExpr, Types.ConvertType(type));
    //SizeOf, AlignOf,  // [C99 6.5.3.4] Sizeof (expr, not type) operator.
    //Real, Imag,       // "__real expr"/"__imag expr" Extension.
    //OffsetOf          // __builtin_offsetof
    }
    break;
  }
  case Stmt::ImplicitCastExprClass: {
    const ImplicitCastExpr *ICExpr = cast<ImplicitCastExpr>(Expression);
    
    // If this is due to array->pointer conversion, emit the array expression as
    // an l-value.
    if (ICExpr->getSubExpr()->getType()->isArrayType()) {
      // Note that VLAs can't exist for global variables.
      // The only thing that can have array type like this is a
      // DeclRefExpr(FileVarDecl)?
      const DeclRefExpr *DRE = cast<DeclRefExpr>(ICExpr->getSubExpr());
      const VarDecl *VD = cast<VarDecl>(DRE->getDecl());
      llvm::Constant *C = CGM.GetAddrOfGlobalVar(VD, false);
      assert(isa<llvm::PointerType>(C->getType()) &&
             isa<llvm::ArrayType>(cast<llvm::PointerType>(C->getType())
                                  ->getElementType()));
      llvm::Constant *Idx0 = llvm::ConstantInt::get(llvm::Type::Int32Ty, 0);
      
      llvm::Constant *Ops[] = {Idx0, Idx0};
      C = llvm::ConstantExpr::getGetElementPtr(C, Ops, 2);
      
      // The resultant pointer type can be implicitly casted to other pointer
      // types as well, for example void*.
      const llvm::Type *DestPTy = Types.ConvertType(type);
      assert(isa<llvm::PointerType>(DestPTy) &&
             "Only expect implicit cast to pointer");
      return llvm::ConstantExpr::getBitCast(C, DestPTy);
    }
    
    return GenerateConstantCast(ICExpr->getSubExpr(), type, CGM);
  }

  // Generate a constant array access expression
  // FIXME: Clang's semantic analysis incorrectly prevents array access in 
  // global initialisers, preventing us from testing this.
  case Stmt::ArraySubscriptExprClass: {
    const ArraySubscriptExpr* ASExpr = cast<ArraySubscriptExpr>(Expression);
    llvm::Constant *Base = GenerateConstantExpr(ASExpr->getBase(), CGM);
    llvm::Constant *Index = GenerateConstantExpr(ASExpr->getIdx(), CGM);
    return llvm::ConstantExpr::getExtractElement(Base, Index);
  }

  // Generate a constant expression to initialise an aggregate type, such as 
  // an array or struct.
  case Stmt::InitListExprClass: 
    return GenerateAggregateInit(cast<InitListExpr>(Expression), CGM);
  }
        
  CGM.WarnUnsupported(Expression, "initializer");
  return llvm::UndefValue::get(Types.ConvertType(type));
}

llvm::Constant *CodeGenModule::EmitGlobalInit(const Expr *Expression) {
  return GenerateConstantExpr(Expression, *this);
}

void CodeGenModule::EmitGlobalVar(const FileVarDecl *D) {
  // If this is just a forward declaration of the variable, don't emit it now,
  // allow it to be emitted lazily on its first use.
  if (D->getStorageClass() == VarDecl::Extern && D->getInit() == 0)
    return;
  
  // Get the global, forcing it to be a direct reference.
  llvm::GlobalVariable *GV = 
    cast<llvm::GlobalVariable>(GetAddrOfGlobalVar(D, true));
  
  // Convert the initializer, or use zero if appropriate.
  llvm::Constant *Init = 0;
  if (D->getInit() == 0) {
    Init = llvm::Constant::getNullValue(GV->getType()->getElementType());
  } else if (D->getType()->isIntegerType()) {
    llvm::APSInt Value(static_cast<uint32_t>(
      getContext().getTypeSize(D->getInit()->getType(), SourceLocation())));
    if (D->getInit()->isIntegerConstantExpr(Value, Context))
      Init = llvm::ConstantInt::get(Value);
  }

  if (!Init)
    Init = EmitGlobalInit(D->getInit());

  assert(GV->getType()->getElementType() == Init->getType() &&
         "Initializer codegen type mismatch!");
  GV->setInitializer(Init);
  
  // Set the llvm linkage type as appropriate.
  // FIXME: This isn't right.  This should handle common linkage and other
  // stuff.
  switch (D->getStorageClass()) {
  case VarDecl::Auto:
  case VarDecl::Register:
    assert(0 && "Can't have auto or register globals");
  case VarDecl::None:
  case VarDecl::Extern:
    // todo: common
    break;
  case VarDecl::Static:
    GV->setLinkage(llvm::GlobalVariable::InternalLinkage);
    break;
  }
}

/// EmitGlobalVarDeclarator - Emit all the global vars attached to the specified
/// declarator chain.
void CodeGenModule::EmitGlobalVarDeclarator(const FileVarDecl *D) {
  for (; D; D = cast_or_null<FileVarDecl>(D->getNextDeclarator()))
    EmitGlobalVar(D);
}

/// getBuiltinLibFunction
llvm::Function *CodeGenModule::getBuiltinLibFunction(unsigned BuiltinID) {
  if (BuiltinID > BuiltinFunctions.size())
    BuiltinFunctions.resize(BuiltinID);
  
  // Cache looked up functions.  Since builtin id #0 is invalid we don't reserve
  // a slot for it.
  assert(BuiltinID && "Invalid Builtin ID");
  llvm::Function *&FunctionSlot = BuiltinFunctions[BuiltinID-1];
  if (FunctionSlot)
    return FunctionSlot;
  
  assert(Context.BuiltinInfo.isLibFunction(BuiltinID) && "isn't a lib fn");
  
  // Get the name, skip over the __builtin_ prefix.
  const char *Name = Context.BuiltinInfo.GetName(BuiltinID)+10;
  
  // Get the type for the builtin.
  QualType Type = Context.BuiltinInfo.GetBuiltinType(BuiltinID, Context);
  const llvm::FunctionType *Ty = 
    cast<llvm::FunctionType>(getTypes().ConvertType(Type));

  // FIXME: This has a serious problem with code like this:
  //  void abs() {}
  //    ... __builtin_abs(x);
  // The two versions of abs will collide.  The fix is for the builtin to win,
  // and for the existing one to be turned into a constantexpr cast of the
  // builtin.  In the case where the existing one is a static function, it
  // should just be renamed.
  if (llvm::Function *Existing = getModule().getFunction(Name)) {
    if (Existing->getFunctionType() == Ty && Existing->hasExternalLinkage())
      return FunctionSlot = Existing;
    assert(Existing == 0 && "FIXME: Name collision");
  }

  // FIXME: param attributes for sext/zext etc.
  return FunctionSlot = new llvm::Function(Ty, llvm::Function::ExternalLinkage,
                                           Name, &getModule());
}

llvm::Function *CodeGenModule::getIntrinsic(unsigned IID,const llvm::Type **Tys,
                                            unsigned NumTys) {
  return llvm::Intrinsic::getDeclaration(&getModule(),
                                         (llvm::Intrinsic::ID)IID, Tys, NumTys);
}

llvm::Function *CodeGenModule::getMemCpyFn() {
  if (MemCpyFn) return MemCpyFn;
  llvm::Intrinsic::ID IID;
  uint64_t Size; unsigned Align;
  Context.Target.getPointerInfo(Size, Align, FullSourceLoc());
  switch (Size) {
  default: assert(0 && "Unknown ptr width");
  case 32: IID = llvm::Intrinsic::memcpy_i32; break;
  case 64: IID = llvm::Intrinsic::memcpy_i64; break;
  }
  return MemCpyFn = getIntrinsic(IID);
}


llvm::Constant *CodeGenModule::
GetAddrOfConstantCFString(const std::string &str) {
  llvm::StringMapEntry<llvm::Constant *> &Entry = 
    CFConstantStringMap.GetOrCreateValue(&str[0], &str[str.length()]);
  
  if (Entry.getValue())
    return Entry.getValue();
  
  std::vector<llvm::Constant*> Fields;
  
  if (!CFConstantStringClassRef) {
    const llvm::Type *Ty = getTypes().ConvertType(getContext().IntTy);
    Ty = llvm::ArrayType::get(Ty, 0);
  
    CFConstantStringClassRef = 
      new llvm::GlobalVariable(Ty, false,
                               llvm::GlobalVariable::ExternalLinkage, 0, 
                               "__CFConstantStringClassReference", 
                               &getModule());
  }
  
  // Class pointer.
  llvm::Constant *Zero = llvm::Constant::getNullValue(llvm::Type::Int32Ty);
  llvm::Constant *Zeros[] = { Zero, Zero };
  llvm::Constant *C = 
    llvm::ConstantExpr::getGetElementPtr(CFConstantStringClassRef, Zeros, 2);
  Fields.push_back(C);
  
  // Flags.
  const llvm::Type *Ty = getTypes().ConvertType(getContext().IntTy);
  Fields.push_back(llvm::ConstantInt::get(Ty, 1992));
    
  // String pointer.
  C = llvm::ConstantArray::get(str);
  C = new llvm::GlobalVariable(C->getType(), true, 
                               llvm::GlobalValue::InternalLinkage,
                               C, ".str", &getModule());
  
  C = llvm::ConstantExpr::getGetElementPtr(C, Zeros, 2);
  Fields.push_back(C);
  
  // String length.
  Ty = getTypes().ConvertType(getContext().LongTy);
  Fields.push_back(llvm::ConstantInt::get(Ty, str.length()));
  
  // The struct.
  Ty = getTypes().ConvertType(getContext().getCFConstantStringType());
  C = llvm::ConstantStruct::get(cast<llvm::StructType>(Ty), Fields);
  llvm::GlobalVariable *GV = 
    new llvm::GlobalVariable(C->getType(), true, 
                             llvm::GlobalVariable::InternalLinkage, 
                             C, "", &getModule());
  GV->setSection("__DATA,__cfstring");
  Entry.setValue(GV);
  return GV;
}

/// GenerateWritableString -- Creates storage for a string literal
static llvm::Constant *GenerateStringLiteral(const std::string &str, 
                                             bool constant,
                                             CodeGenModule &CGM) {
  // Create Constant for this string literal
  llvm::Constant *C=llvm::ConstantArray::get(str);
  
  // Create a global variable for this string
  C = new llvm::GlobalVariable(C->getType(), constant, 
                               llvm::GlobalValue::InternalLinkage,
                               C, ".str", &CGM.getModule());
  llvm::Constant *Zero = llvm::Constant::getNullValue(llvm::Type::Int32Ty);
  llvm::Constant *Zeros[] = { Zero, Zero };
  C = llvm::ConstantExpr::getGetElementPtr(C, Zeros, 2);
  return C;
}

/// CodeGenModule::GetAddrOfConstantString -- returns a pointer to the first 
/// element of a character array containing the literal.
llvm::Constant *CodeGenModule::GetAddrOfConstantString(const std::string &str) {
  // Don't share any string literals if writable-strings is turned on.
  if (Features.WritableStrings)
    return GenerateStringLiteral(str, false, *this);
  
  llvm::StringMapEntry<llvm::Constant *> &Entry = 
  ConstantStringMap.GetOrCreateValue(&str[0], &str[str.length()]);

  if (Entry.getValue())
      return Entry.getValue();

  // Create a global variable for this.
  llvm::Constant *C = GenerateStringLiteral(str, true, *this);
  Entry.setValue(C);
  return C;
}
