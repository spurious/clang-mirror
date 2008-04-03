//===--- CodeGenTypes.cpp - Type translation for LLVM CodeGen -------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This is the code that handles AST -> LLVM type lowering. 
//
//===----------------------------------------------------------------------===//

#include "CodeGenTypes.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/AST/AST.h"
#include "llvm/DerivedTypes.h"
#include "llvm/Module.h"
#include "llvm/Target/TargetData.h"

using namespace clang;
using namespace CodeGen;

namespace {
  /// RecordOrganizer - This helper class, used by CGRecordLayout, layouts 
  /// structs and unions. It manages transient information used during layout.
  /// FIXME : Handle field aligments. Handle packed structs.
  class RecordOrganizer {
  public:
    explicit RecordOrganizer(CodeGenTypes &Types) : 
      CGT(Types), STy(NULL), llvmFieldNo(0), Cursor(0),
      llvmSize(0) {}
    
    /// addField - Add new field.
    void addField(const FieldDecl *FD);

    /// addLLVMField - Add llvm struct field that corresponds to llvm type Ty. 
    /// Increment field count.
    void addLLVMField(const llvm::Type *Ty, bool isPaddingField = false);

    /// addPaddingFields - Current cursor is not suitable place to add next 
    /// field. Add required padding fields.
    void addPaddingFields(unsigned WaterMark);

    /// layoutStructFields - Do the actual work and lay out all fields. Create
    /// corresponding llvm struct type.  This should be invoked only after
    /// all fields are added.
    void layoutStructFields(const ASTRecordLayout &RL);

    /// layoutUnionFields - Do the actual work and lay out all fields. Create
    /// corresponding llvm struct type.  This should be invoked only after
    /// all fields are added.
    void layoutUnionFields();

    /// getLLVMType - Return associated llvm struct type. This may be NULL
    /// if fields are not laid out.
    llvm::Type *getLLVMType() const {
      return STy;
    }

    /// placeBitField - Find a place for FD, which is a bit-field. 
    void placeBitField(const FieldDecl *FD);

    llvm::SmallSet<unsigned, 8> &getPaddingFields() {
      return PaddingFields;
    }

  private:
    CodeGenTypes &CGT;
    llvm::Type *STy;
    unsigned llvmFieldNo;
    uint64_t Cursor; 
    uint64_t llvmSize;
    llvm::SmallVector<const FieldDecl *, 8> FieldDecls;
    std::vector<const llvm::Type*> LLVMFields;
    llvm::SmallSet<unsigned, 8> PaddingFields;
  };
}

CodeGenTypes::CodeGenTypes(ASTContext &Ctx, llvm::Module& M,
                           const llvm::TargetData &TD)
  : Context(Ctx), Target(Ctx.Target), TheModule(M), TheTargetData(TD) {
}

CodeGenTypes::~CodeGenTypes() {
  for(llvm::DenseMap<const TagDecl *, CGRecordLayout *>::iterator
        I = CGRecordLayouts.begin(), E = CGRecordLayouts.end();
      I != E; ++I)
    delete I->second;
  CGRecordLayouts.clear();
}

/// ConvertType - Convert the specified type to its LLVM form.
const llvm::Type *CodeGenTypes::ConvertType(QualType T) {
  llvm::PATypeHolder Result = ConvertTypeRecursive(T);

  // Any pointers that were converted defered evaluation of their pointee type,
  // creating an opaque type instead.  This is in order to avoid problems with
  // circular types.  Loop through all these defered pointees, if any, and
  // resolve them now.
  while (!PointersToResolve.empty()) {
    std::pair<const PointerLikeType *, llvm::OpaqueType*> P =
      PointersToResolve.back();
    PointersToResolve.pop_back();
    // We can handle bare pointers here because we know that the only pointers
    // to the Opaque type are P.second and from other types.  Refining the
    // opqaue type away will invalidate P.second, but we don't mind :).
    const llvm::Type *NT = ConvertTypeRecursive(P.first->getPointeeType());
    P.second->refineAbstractTypeTo(NT);
  }

  return Result;
}

const llvm::Type *CodeGenTypes::ConvertTypeRecursive(QualType T) {
  // See if type is already cached.
  llvm::DenseMap<Type *, llvm::PATypeHolder>::iterator
    I = TypeCache.find(T.getCanonicalType().getTypePtr());
  // If type is found in map and this is not a definition for a opaque
  // place holder type then use it. Otherwise, convert type T.
  if (I != TypeCache.end())
    return I->second.get();

  const llvm::Type *ResultType = ConvertNewType(T);
  TypeCache.insert(std::make_pair(T.getCanonicalType().getTypePtr(), 
                                  llvm::PATypeHolder(ResultType)));
  return ResultType;
}

/// ConvertTypeForMem - Convert type T into a llvm::Type.  This differs from
/// ConvertType in that it is used to convert to the memory representation for
/// a type.  For example, the scalar representation for _Bool is i1, but the
/// memory representation is usually i8 or i32, depending on the target.
const llvm::Type *CodeGenTypes::ConvertTypeForMem(QualType T) {
  const llvm::Type *R = ConvertType(T);
  
  // If this is a non-bool type, don't map it.
  if (R != llvm::Type::Int1Ty)
    return R;
    
  // Otherwise, return an integer of the target-specified size.
  return llvm::IntegerType::get((unsigned)Context.getTypeSize(T));
  
}

/// UpdateCompletedType - When we find the full definition for a TagDecl,
/// replace the 'opaque' type we previously made for it if applicable.
void CodeGenTypes::UpdateCompletedType(const TagDecl *TD) {
  llvm::DenseMap<const TagDecl*, llvm::PATypeHolder>::iterator TDTI = 
    TagDeclTypes.find(TD);
  if (TDTI == TagDeclTypes.end()) return;
  
  // Remember the opaque LLVM type for this tagdecl.
  llvm::PATypeHolder OpaqueHolder = TDTI->second;
  assert(isa<llvm::OpaqueType>(OpaqueHolder.get()) &&
         "Updating compilation of an already non-opaque type?");
  
  // Remove it from TagDeclTypes so that it will be regenerated.
  TagDeclTypes.erase(TDTI);

  // Generate the new type.
  const llvm::Type *NT = ConvertTagDeclType(TD);

  // Refine the old opaque type to its new definition.
  cast<llvm::OpaqueType>(OpaqueHolder.get())->refineAbstractTypeTo(NT);
}

/// Produces a vector containing the all of the instance variables in an
/// Objective-C object, in the order that they appear.  Used to create LLVM
/// structures corresponding to Objective-C objects.
void CodeGenTypes::CollectObjCIvarTypes(ObjCInterfaceDecl *ObjCClass,
                                    std::vector<const llvm::Type*> &IvarTypes) {
  ObjCInterfaceDecl *SuperClass = ObjCClass->getSuperClass();
  if (SuperClass)
    CollectObjCIvarTypes(SuperClass, IvarTypes);
  for (ObjCInterfaceDecl::ivar_iterator I = ObjCClass->ivar_begin(),
       E = ObjCClass->ivar_end(); I != E; ++I) {
    IvarTypes.push_back(ConvertType((*I)->getType()));
    ObjCIvarInfo[*I] = IvarTypes.size() - 1;
  }
}

const llvm::Type *CodeGenTypes::ConvertNewType(QualType T) {
  const clang::Type &Ty = *T.getCanonicalType();
  
  switch (Ty.getTypeClass()) {
  case Type::TypeName:        // typedef isn't canonical.
  case Type::TypeOfExp:       // typeof isn't canonical.
  case Type::TypeOfTyp:       // typeof isn't canonical.
    assert(0 && "Non-canonical type, shouldn't happen");
  case Type::Builtin: {
    switch (cast<BuiltinType>(Ty).getKind()) {
    case BuiltinType::Void:
      // LLVM void type can only be used as the result of a function call.  Just
      // map to the same as char.
      return llvm::IntegerType::get(8);

    case BuiltinType::Bool:
      // Note that we always return bool as i1 for use as a scalar type.
      return llvm::Type::Int1Ty;
      
    case BuiltinType::Char_S:
    case BuiltinType::Char_U:
    case BuiltinType::SChar:
    case BuiltinType::UChar:
    case BuiltinType::Short:
    case BuiltinType::UShort:
    case BuiltinType::Int:
    case BuiltinType::UInt:
    case BuiltinType::Long:
    case BuiltinType::ULong:
    case BuiltinType::LongLong:
    case BuiltinType::ULongLong:
      return llvm::IntegerType::get(
        static_cast<unsigned>(Context.getTypeSize(T)));
      
    case BuiltinType::Float:      return llvm::Type::FloatTy;
    case BuiltinType::Double:     return llvm::Type::DoubleTy;
    case BuiltinType::LongDouble:
      // FIXME: mapping long double onto double.
      return llvm::Type::DoubleTy;
    }
    break;
  }
  case Type::Complex: {
    const llvm::Type *EltTy = 
      ConvertTypeRecursive(cast<ComplexType>(Ty).getElementType());
    return llvm::StructType::get(EltTy, EltTy, NULL);
  }
  case Type::Reference:
  case Type::Pointer: {
    const PointerLikeType &PTy = cast<PointerLikeType>(Ty);
    QualType ETy = PTy.getPointeeType();
    llvm::OpaqueType *PointeeType = llvm::OpaqueType::get();
    PointersToResolve.push_back(std::make_pair(&PTy, PointeeType));
    return llvm::PointerType::get(PointeeType, ETy.getAddressSpace());
  }
    
  case Type::VariableArray: {
    const VariableArrayType &A = cast<VariableArrayType>(Ty);
    assert(A.getIndexTypeQualifier() == 0 &&
           "FIXME: We only handle trivial array types so far!");
    // VLAs resolve to the innermost element type; this matches
    // the return of alloca, and there isn't any obviously better choice.
    return ConvertTypeRecursive(A.getElementType());
  }
  case Type::IncompleteArray: {
    const IncompleteArrayType &A = cast<IncompleteArrayType>(Ty);
    assert(A.getIndexTypeQualifier() == 0 &&
           "FIXME: We only handle trivial array types so far!");
    // int X[] -> [0 x int]
    return llvm::ArrayType::get(ConvertTypeRecursive(A.getElementType()), 0);
  }
  case Type::ConstantArray: {
    const ConstantArrayType &A = cast<ConstantArrayType>(Ty);
    const llvm::Type *EltTy = ConvertTypeRecursive(A.getElementType());
    return llvm::ArrayType::get(EltTy, A.getSize().getZExtValue());
  }
  case Type::OCUVector:
  case Type::Vector: {
    const VectorType &VT = cast<VectorType>(Ty);
    return llvm::VectorType::get(ConvertTypeRecursive(VT.getElementType()),
                                 VT.getNumElements());
  }
  case Type::FunctionNoProto:
  case Type::FunctionProto: {
    const FunctionType &FP = cast<FunctionType>(Ty);
    const llvm::Type *ResultType;
    
    if (FP.getResultType()->isVoidType())
      ResultType = llvm::Type::VoidTy;    // Result of function uses llvm void.
    else
      ResultType = ConvertTypeRecursive(FP.getResultType());
    
    // FIXME: Convert argument types.
    bool isVarArg;
    std::vector<const llvm::Type*> ArgTys;
    
    // Struct return passes the struct byref.
    if (!ResultType->isFirstClassType() && ResultType != llvm::Type::VoidTy) {
      ArgTys.push_back(llvm::PointerType::get(ResultType, 
                                        FP.getResultType().getAddressSpace()));
      ResultType = llvm::Type::VoidTy;
    }
    
    if (const FunctionTypeProto *FTP = dyn_cast<FunctionTypeProto>(&FP)) {
      DecodeArgumentTypes(*FTP, ArgTys);
      isVarArg = FTP->isVariadic();
    } else {
      isVarArg = true;
    }
    
    return llvm::FunctionType::get(ResultType, ArgTys, isVarArg);
  }
  
  case Type::ASQual:
    return
      ConvertTypeRecursive(QualType(cast<ASQualType>(Ty).getBaseType(), 0));

  case Type::ObjCInterface: {
    // Warning: Use of this is strongly discouraged.  Late binding of instance
    // variables is supported on some runtimes and so using static binding can
    // break code when libraries are updated.  Only use this if you have
    // previously checked that the ObjCRuntime subclass in use does not support
    // late-bound ivars.
    ObjCInterfaceType OIT = cast<ObjCInterfaceType>(Ty);
    std::vector<const llvm::Type*> IvarTypes;
    // Pointer to the class.  This is just a placeholder.  Operations that
    // actually use the isa pointer should cast it to the Class type provided
    // by the runtime.
    IvarTypes.push_back(llvm::PointerType::getUnqual(llvm::Type::Int8Ty));
    CollectObjCIvarTypes(OIT.getDecl(), IvarTypes);
    return llvm::StructType::get(IvarTypes);
  }
      
  case Type::ObjCQualifiedInterface:
    assert(0 && "FIXME: add missing functionality here");
    break;

  case Type::ObjCQualifiedId:
    assert(0 && "FIXME: add missing functionality here");
    break;

  case Type::Tagged: {
    const TagDecl *TD = cast<TagType>(Ty).getDecl();
    const llvm::Type *Res = ConvertTagDeclType(TD);
    
    std::string TypeName(TD->getKindName());
    TypeName += '.';
    
    // Name the codegen type after the typedef name
    // if there is no tag type name available
    if (TD->getIdentifier())
      TypeName += TD->getName();
    else if (const TypedefType *TdT = dyn_cast<TypedefType>(T))
      TypeName += TdT->getDecl()->getName();
    else
      TypeName += "anon";
    
    TheModule.addTypeName(TypeName, Res);  
    return Res;
  }
  }
  
  // FIXME: implement.
  return llvm::OpaqueType::get();
}

void CodeGenTypes::DecodeArgumentTypes(const FunctionTypeProto &FTP, 
                                       std::vector<const llvm::Type*> &ArgTys) {
  for (unsigned i = 0, e = FTP.getNumArgs(); i != e; ++i) {
    const llvm::Type *Ty = ConvertTypeRecursive(FTP.getArgType(i));
    if (Ty->isFirstClassType())
      ArgTys.push_back(Ty);
    else
      // byval arguments are always on the stack, which is addr space #0.
      ArgTys.push_back(llvm::PointerType::getUnqual(Ty));
  }
}

/// ConvertTagDeclType - Lay out a tagged decl type like struct or union or
/// enum.
const llvm::Type *CodeGenTypes::ConvertTagDeclType(const TagDecl *TD) {
  llvm::DenseMap<const TagDecl*, llvm::PATypeHolder>::iterator TDTI = 
    TagDeclTypes.find(TD);
  
  // If we've already compiled this tag type, use the previous definition.
  if (TDTI != TagDeclTypes.end())
    return TDTI->second;
  
  // If this is still a forward definition, just define an opaque type to use
  // for this tagged decl.
  if (!TD->isDefinition()) {
    llvm::Type *ResultType = llvm::OpaqueType::get();  
    TagDeclTypes.insert(std::make_pair(TD, ResultType));
    return ResultType;
  }
  
  // Okay, this is a definition of a type.  Compile the implementation now.
  
  if (TD->getKind() == Decl::Enum) {
    // Don't bother storing enums in TagDeclTypes.
    return ConvertTypeRecursive(cast<EnumDecl>(TD)->getIntegerType());
  }
  
  // This decl could well be recursive.  In this case, insert an opaque
  // definition of this type, which the recursive uses will get.  We will then
  // refine this opaque version later.

  // Create new OpaqueType now for later use in case this is a recursive
  // type.  This will later be refined to the actual type.
  llvm::PATypeHolder ResultHolder = llvm::OpaqueType::get();
  TagDeclTypes.insert(std::make_pair(TD, ResultHolder));
  
  const llvm::Type *ResultType;
  const RecordDecl *RD = cast<const RecordDecl>(TD);
  if (TD->getKind() == Decl::Struct || TD->getKind() == Decl::Class) {
    // Layout fields.
    RecordOrganizer RO(*this);
    for (unsigned i = 0, e = RD->getNumMembers(); i != e; ++i)
      RO.addField(RD->getMember(i));
    
    RO.layoutStructFields(Context.getASTRecordLayout(RD));
    
    // Get llvm::StructType.
    CGRecordLayouts[TD] = new CGRecordLayout(RO.getLLVMType(), 
                                             RO.getPaddingFields());
    ResultType = RO.getLLVMType();
    
  } else if (TD->getKind() == Decl::Union) {
    // Just use the largest element of the union, breaking ties with the
    // highest aligned member.
    if (RD->getNumMembers() != 0) {
      RecordOrganizer RO(*this);
      for (unsigned i = 0, e = RD->getNumMembers(); i != e; ++i)
        RO.addField(RD->getMember(i));
      
      RO.layoutUnionFields();
      
      // Get llvm::StructType.
      CGRecordLayouts[TD] = new CGRecordLayout(RO.getLLVMType(),
                                               RO.getPaddingFields());
      ResultType = RO.getLLVMType();
    } else {       
      ResultType = llvm::StructType::get(std::vector<const llvm::Type*>());
    }
  } else {
    assert(0 && "FIXME: Unknown tag decl kind!");
  }
  
  // Refine our Opaque type to ResultType.  This can invalidate ResultType, so
  // make sure to read the result out of the holder.
  cast<llvm::OpaqueType>(ResultHolder.get())
    ->refineAbstractTypeTo(ResultType);
  
  return ResultHolder.get();
}  

/// getLLVMFieldNo - Return llvm::StructType element number
/// that corresponds to the field FD.
unsigned CodeGenTypes::getLLVMFieldNo(const FieldDecl *FD) {
  llvm::DenseMap<const FieldDecl*, unsigned>::iterator I = FieldInfo.find(FD);
  assert (I != FieldInfo.end()  && "Unable to find field info");
  return I->second;
}

unsigned CodeGenTypes::getLLVMFieldNo(const ObjCIvarDecl *OID) {
  llvm::DenseMap<const ObjCIvarDecl*, unsigned>::iterator
    I = ObjCIvarInfo.find(OID);
  assert(I != ObjCIvarInfo.end() && "Unable to find field info");
  return I->second;
}

/// addFieldInfo - Assign field number to field FD.
void CodeGenTypes::addFieldInfo(const FieldDecl *FD, unsigned No) {
  FieldInfo[FD] = No;
}

/// getBitFieldInfo - Return the BitFieldInfo  that corresponds to the field FD.
CodeGenTypes::BitFieldInfo CodeGenTypes::getBitFieldInfo(const FieldDecl *FD) {
  llvm::DenseMap<const FieldDecl *, BitFieldInfo>::iterator
    I = BitFields.find(FD);
  assert (I != BitFields.end()  && "Unable to find bitfield info");
  return I->second;
}

/// addBitFieldInfo - Assign a start bit and a size to field FD.
void CodeGenTypes::addBitFieldInfo(const FieldDecl *FD, unsigned Begin,
                                   unsigned Size) {
  BitFields.insert(std::make_pair(FD, BitFieldInfo(Begin, Size)));
}

/// getCGRecordLayout - Return record layout info for the given llvm::Type.
const CGRecordLayout *
CodeGenTypes::getCGRecordLayout(const TagDecl *TD) const {
  llvm::DenseMap<const TagDecl*, CGRecordLayout *>::iterator I
    = CGRecordLayouts.find(TD);
  assert (I != CGRecordLayouts.end() 
          && "Unable to find record layout information for type");
  return I->second;
}

/// addField - Add new field.
void RecordOrganizer::addField(const FieldDecl *FD) {
  assert (!STy && "Record fields are already laid out");
  FieldDecls.push_back(FD);
}

/// layoutStructFields - Do the actual work and lay out all fields. Create
/// corresponding llvm struct type.  This should be invoked only after
/// all fields are added.
/// FIXME : At the moment assume 
///    - one to one mapping between AST FieldDecls and 
///      llvm::StructType elements.
///    - Ignore bit fields
///    - Ignore field aligments
///    - Ignore packed structs
void RecordOrganizer::layoutStructFields(const ASTRecordLayout &RL) {
  // FIXME : Use SmallVector
  llvmSize = 0;
  llvmFieldNo = 0;
  Cursor = 0;
  LLVMFields.clear();

  for (llvm::SmallVector<const FieldDecl *, 8>::iterator I = FieldDecls.begin(),
         E = FieldDecls.end(); I != E; ++I) {
    const FieldDecl *FD = *I;

    if (FD->isBitField()) 
      placeBitField(FD);
    else {
      const llvm::Type *Ty = CGT.ConvertTypeRecursive(FD->getType());
      addLLVMField(Ty);
      CGT.addFieldInfo(FD, llvmFieldNo - 1);
      Cursor = llvmSize;
    }
  }

  unsigned StructAlign = RL.getAlignment();
  if (llvmSize % StructAlign) {
    unsigned StructPadding = StructAlign - (llvmSize % StructAlign);
    addPaddingFields(llvmSize + StructPadding);
  }

  STy = llvm::StructType::get(LLVMFields);
}

/// addPaddingFields - Current cursor is not suitable place to add next field.
/// Add required padding fields.
void RecordOrganizer::addPaddingFields(unsigned WaterMark) {
  assert(WaterMark >= llvmSize && "Invalid padding Field");
  unsigned RequiredBits = WaterMark - llvmSize;
  unsigned RequiredBytes = (RequiredBits + 7) / 8;
  for (unsigned i = 0; i != RequiredBytes; ++i)
    addLLVMField(llvm::Type::Int8Ty, true);
}

/// addLLVMField - Add llvm struct field that corresponds to llvm type Ty.
/// Increment field count.
void RecordOrganizer::addLLVMField(const llvm::Type *Ty, bool isPaddingField) {

  unsigned AlignmentInBits = CGT.getTargetData().getABITypeAlignment(Ty) * 8;
  if (llvmSize % AlignmentInBits) {
    // At the moment, insert padding fields even if target specific llvm 
    // type alignment enforces implict padding fields for FD. Later on, 
    // optimize llvm fields by removing implicit padding fields and 
    // combining consequetive padding fields.
    unsigned Padding = AlignmentInBits - (llvmSize % AlignmentInBits);
    addPaddingFields(llvmSize + Padding);
  }

  unsigned TySize = CGT.getTargetData().getABITypeSizeInBits(Ty);
  llvmSize += TySize;
  if (isPaddingField)
    PaddingFields.insert(llvmFieldNo);
  LLVMFields.push_back(Ty);
  ++llvmFieldNo;
}

/// layoutUnionFields - Do the actual work and lay out all fields. Create
/// corresponding llvm struct type.  This should be invoked only after
/// all fields are added.
void RecordOrganizer::layoutUnionFields() {
 
  unsigned PrimaryEltNo = 0;
  std::pair<uint64_t, unsigned> PrimaryElt =
    CGT.getContext().getTypeInfo(FieldDecls[0]->getType());
  CGT.addFieldInfo(FieldDecls[0], 0);

  unsigned Size = FieldDecls.size();
  for(unsigned i = 1; i != Size; ++i) {
    const FieldDecl *FD = FieldDecls[i];
    assert (!FD->isBitField() && "Bit fields are not yet supported");
    std::pair<uint64_t, unsigned> EltInfo = 
      CGT.getContext().getTypeInfo(FD->getType());

    // Use largest element, breaking ties with the hightest aligned member.
    if (EltInfo.first > PrimaryElt.first ||
        (EltInfo.first == PrimaryElt.first &&
         EltInfo.second > PrimaryElt.second)) {
      PrimaryElt = EltInfo;
      PrimaryEltNo = i;
    }

    // In union, each field gets first slot.
    CGT.addFieldInfo(FD, 0);
  }

  std::vector<const llvm::Type*> Fields;
  const llvm::Type *Ty =
    CGT.ConvertTypeRecursive(FieldDecls[PrimaryEltNo]->getType());
  Fields.push_back(Ty);
  STy = llvm::StructType::get(Fields);
}

/// placeBitField - Find a place for FD, which is a bit-field.
/// This function searches for the last aligned field. If the  bit-field fits in
/// it, it is reused. Otherwise, the bit-field is placed in a new field.
void RecordOrganizer::placeBitField(const FieldDecl *FD) {

  assert (FD->isBitField() && "FD is not a bit-field");
  Expr *BitWidth = FD->getBitWidth();
  llvm::APSInt FieldSize(32);
  bool isBitField = 
    BitWidth->isIntegerConstantExpr(FieldSize, CGT.getContext());
  assert (isBitField  && "Invalid BitField size expression");
  uint64_t BitFieldSize =  FieldSize.getZExtValue();

  const llvm::Type *Ty = CGT.ConvertTypeRecursive(FD->getType());
  uint64_t TySize = CGT.getTargetData().getABITypeSizeInBits(Ty);

  unsigned Idx = Cursor / TySize;
  unsigned BitsLeft = TySize - (Cursor % TySize);

  if (BitsLeft >= BitFieldSize) {
    // The bitfield fits in the last aligned field.
    // This is : struct { char a; int CurrentField:10;};
    // where 'CurrentField' shares first field with 'a'.
    CGT.addFieldInfo(FD, Idx);
    CGT.addBitFieldInfo(FD, TySize - BitsLeft, BitFieldSize);
    Cursor += BitFieldSize;
  } else {
    // Place the bitfield in a new LLVM field.
    // This is : struct { char a; short CurrentField:10;};
    // where 'CurrentField' needs a new llvm field.
    CGT.addFieldInfo(FD, Idx + 1);
    CGT.addBitFieldInfo(FD, 0, BitFieldSize);
    Cursor = (Idx + 1) * TySize + BitFieldSize;
  }
  if (Cursor > llvmSize)
    addPaddingFields(Cursor);
}
