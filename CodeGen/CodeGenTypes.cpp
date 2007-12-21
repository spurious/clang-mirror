//===--- CodeGenTypes.cpp - Type translation for LLVM CodeGen -------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file was developed by Chris Lattner and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
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
  /// FIXME : At the moment assume 
  ///    - one to one mapping between AST FieldDecls and 
  ///      llvm::StructType elements.
  ///    - Ignore bit fields
  ///    - Ignore field aligments
  ///    - Ignore packed structs
  class RecordOrganizer {
  public:
    explicit RecordOrganizer(CodeGenTypes &Types) : 
      CGT(Types), STy(NULL), FieldNo(0), Cursor(0), ExtraBits(0),
      CurrentFieldStart(0), llvmSize(0) {}
    
    /// addField - Add new field.
    void addField(const FieldDecl *FD);

    /// addLLVMField - Add llvm struct field that corresponds to llvm type Ty. 
    /// Update cursor and increment field count.
    void addLLVMField(const llvm::Type *Ty, uint64_t Size, 
                      const FieldDecl *FD = NULL, unsigned Begin = 0, 
                      unsigned End = 0);

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

    /// fixCursorPosition - When bit-field is followed by a normal field
    /// cursor position may require some adjustments. 
    ///
    /// For example,          struct { char a; short b:2;  char c; }; 
    ///
    /// At the beginning of field 'c' layout, cursor position is 10.
    /// However, only llvm struct field allocated so far is of type i8.
    /// This happens because 'b' shares llvm field with 'a'. Add padding
    /// field of i8 type and reposition cursor to point at 16. This 
    /// should be done only if next field (i.e. 'c' here) is not a bit-field
    /// or last record field is a bit-field.
    void fixCursorPosition(const ASTRecordLayout &RL);

    /// placeBitField - Find a place for FD, which is a bit-field. 
    void placeBitField(const FieldDecl *FD);

  private:
    CodeGenTypes &CGT;
    llvm::Type *STy;
    unsigned FieldNo;
    uint64_t Cursor;
    /* If last field is a bitfield then it may not have occupied all allocated 
       bits. Use remaining bits for next field if it also a bitfield. */
    uint64_t ExtraBits; 
    /* CurrentFieldStart - Indicates starting offset for current llvm field.
       When current llvm field is shared by multiple bitfields, this is
       used find starting bit offset for the bitfield from the beginning of
       llvm field. */
    uint64_t CurrentFieldStart;
    uint64_t llvmSize;
    llvm::SmallVector<const FieldDecl *, 8> FieldDecls;
    std::vector<const llvm::Type*> LLVMFields;
    llvm::SmallVector<uint64_t, 8> Offsets;
  };
}

CodeGenTypes::CodeGenTypes(ASTContext &Ctx, llvm::Module& M,
                           const llvm::TargetData &TD)
  : Context(Ctx), Target(Ctx.Target), TheModule(M), TheTargetData(TD) {
}

CodeGenTypes::~CodeGenTypes() {
  for(llvm::DenseMap<const llvm::Type *, CGRecordLayout *>::iterator
        I = CGRecordLayouts.begin(), E = CGRecordLayouts.end();
      I != E; ++I)
    delete I->second;
  CGRecordLayouts.clear();
}

/// isOpaqueTypeDefinition - Return true if LT is a llvm::OpaqueType
/// and T is tag definition. This helper routine does not check
/// relationship between T and LT.
static bool isOpaqueTypeDefinition(QualType T, llvm::Type *LT) {
  
  if (!isa<llvm::OpaqueType>(LT))
    return false;

  const clang::Type &Ty = *T.getCanonicalType();
  if (Ty.getTypeClass() == Type::Tagged) {
    const TagType &TT = cast<TagType>(Ty);
    const TagDecl *TD = TT.getDecl();
    if (TD->isDefinition())
      return true;
  }

  return false;
}

/// ConvertType - Convert the specified type to its LLVM form.
const llvm::Type *CodeGenTypes::ConvertType(QualType T) {
  // See if type is already cached.
  llvm::DenseMap<Type *, llvm::PATypeHolder>::iterator
    I = TypeHolderMap.find(T.getTypePtr());
  // If type is found in map and this is not a definition for a opaque
  // place holder type then use it. Otherwise convert type T.
  if (I != TypeHolderMap.end() && !isOpaqueTypeDefinition(T, I->second.get()))
    return I->second.get();

  const llvm::Type *ResultType = ConvertNewType(T);
  TypeHolderMap.insert(std::make_pair(T.getTypePtr(), 
                                      llvm::PATypeHolder(ResultType)));
  return ResultType;
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
      // FIXME: This is very strange.  We want scalars to be i1, but in memory
      // they can be i1 or i32.  Should the codegen handle this issue?
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
        static_cast<unsigned>(Context.getTypeSize(T, SourceLocation())));
      
    case BuiltinType::Float:      return llvm::Type::FloatTy;
    case BuiltinType::Double:     return llvm::Type::DoubleTy;
    case BuiltinType::LongDouble:
      // FIXME: mapping long double onto double.
      return llvm::Type::DoubleTy;
    }
    break;
  }
  case Type::Complex: {
    std::vector<const llvm::Type*> Elts;
    Elts.push_back(ConvertType(cast<ComplexType>(Ty).getElementType()));
    Elts.push_back(Elts[0]);
    return llvm::StructType::get(Elts);
  }
  case Type::Pointer: {
    const PointerType &P = cast<PointerType>(Ty);
    return llvm::PointerType::getUnqual(ConvertType(P.getPointeeType())); 
  }
  case Type::Reference: {
    const ReferenceType &R = cast<ReferenceType>(Ty);
    return llvm::PointerType::getUnqual(ConvertType(R.getReferenceeType()));
  }
    
  case Type::VariableArray: {
    const VariableArrayType &A = cast<VariableArrayType>(Ty);
    assert(A.getSizeModifier() == ArrayType::Normal &&
           A.getIndexTypeQualifier() == 0 &&
           "FIXME: We only handle trivial array types so far!");
    if (A.getSizeExpr() == 0) {
      // int X[] -> [0 x int]
      return llvm::ArrayType::get(ConvertType(A.getElementType()), 0);
    } else {
      assert(0 && "FIXME: VLAs not implemented yet!");
    }
  }
  case Type::ConstantArray: {
    const ConstantArrayType &A = cast<ConstantArrayType>(Ty);
    const llvm::Type *EltTy = ConvertType(A.getElementType());
    return llvm::ArrayType::get(EltTy, A.getSize().getZExtValue());
  }
  case Type::OCUVector:
  case Type::Vector: {
    const VectorType &VT = cast<VectorType>(Ty);
    return llvm::VectorType::get(ConvertType(VT.getElementType()),
                                 VT.getNumElements());
  }
  case Type::FunctionNoProto:
  case Type::FunctionProto: {
    const FunctionType &FP = cast<FunctionType>(Ty);
    const llvm::Type *ResultType;
    
    if (FP.getResultType()->isVoidType())
      ResultType = llvm::Type::VoidTy;    // Result of function uses llvm void.
    else
      ResultType = ConvertType(FP.getResultType());
    
    // FIXME: Convert argument types.
    bool isVarArg;
    std::vector<const llvm::Type*> ArgTys;
    
    // Struct return passes the struct byref.
    if (!ResultType->isFirstClassType() && ResultType != llvm::Type::VoidTy) {
      const llvm::Type *RType = llvm::PointerType::getUnqual(ResultType);
      QualType RTy = Context.getPointerType(FP.getResultType());
      TypeHolderMap.insert(std::make_pair(RTy.getTypePtr(), 
                                          llvm::PATypeHolder(RType)));
  
      ArgTys.push_back(RType);
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

  case Type::ObjcInterface:
    assert(0 && "FIXME: add missing functionality here");
    break;
      
  case Type::ObjcQualifiedInterface:
    assert(0 && "FIXME: add missing functionality here");
    break;

  case Type::ObjcQualifiedId:
    assert(0 && "FIXME: add missing functionality here");
    break;

  case Type::Tagged:
    const TagType &TT = cast<TagType>(Ty);
    const TagDecl *TD = TT.getDecl();
    llvm::Type *&ResultType = TagDeclTypes[TD];
      
    // If corresponding llvm type is not a opaque struct type
    // then use it.
    if (ResultType && !isOpaqueTypeDefinition(T, ResultType))
      return ResultType;
    
    if (!TD->isDefinition()) {
      ResultType = llvm::OpaqueType::get();  
    } else if (TD->getKind() == Decl::Enum) {
      return ConvertType(cast<EnumDecl>(TD)->getIntegerType());
    } else if (TD->getKind() == Decl::Struct) {
      const RecordDecl *RD = cast<const RecordDecl>(TD);
      
      // If this is nested record and this RecordDecl is already under
      // process then return associated OpaqueType for now.
      llvm::DenseMap<const RecordDecl *, llvm::Type *>::iterator 
        OpaqueI = RecordTypesToResolve.find(RD);
      if (OpaqueI != RecordTypesToResolve.end())
        return OpaqueI->second;

      llvm::OpaqueType *OpaqueTy = NULL;
      if (ResultType)
        OpaqueTy = dyn_cast<llvm::OpaqueType>(ResultType);
      if (!OpaqueTy) {
        // Create new OpaqueType now for later use.
        // FIXME: This creates a lot of opaque types, most of them are not 
        // needed. Reevaluate this when performance analyis finds tons of 
        // opaque types.
        OpaqueTy = llvm::OpaqueType::get();
        TypeHolderMap.insert(std::make_pair(T.getTypePtr(), 
                                            llvm::PATypeHolder(OpaqueTy)));
      }
      RecordTypesToResolve[RD] = OpaqueTy;

      // Layout fields.
      RecordOrganizer RO(*this);
      for (unsigned i = 0, e = RD->getNumMembers(); i != e; ++i)
        RO.addField(RD->getMember(i));
      const ASTRecordLayout &RL = Context.getASTRecordLayout(RD, 
                                                             SourceLocation());
      RO.layoutStructFields(RL);

      // Get llvm::StructType.
      CGRecordLayout *RLI = new CGRecordLayout(RO.getLLVMType());
      ResultType = RLI->getLLVMType();
      CGRecordLayouts[ResultType] = RLI;

      // Refine any OpaqueType associated with this RecordDecl.
      OpaqueTy->refineAbstractTypeTo(ResultType);
      OpaqueI = RecordTypesToResolve.find(RD);
      assert (OpaqueI != RecordTypesToResolve.end() 
              && "Expected RecordDecl in RecordTypesToResolve");
      RecordTypesToResolve.erase(OpaqueI);

    } else if (TD->getKind() == Decl::Union) {
      const RecordDecl *RD = cast<const RecordDecl>(TD);
      // Just use the largest element of the union, breaking ties with the
      // highest aligned member.

      if (RD->getNumMembers() != 0) {
        RecordOrganizer RO(*this);
        for (unsigned i = 0, e = RD->getNumMembers(); i != e; ++i)
          RO.addField(RD->getMember(i));
        RO.layoutUnionFields();

        // Get llvm::StructType.
        CGRecordLayout *RLI = new CGRecordLayout(RO.getLLVMType());
        ResultType = RLI->getLLVMType();
        CGRecordLayouts[ResultType] = RLI;
      } else {       
        std::vector<const llvm::Type*> Fields;
        ResultType = llvm::StructType::get(Fields);
      }
    } else {
      assert(0 && "FIXME: Implement tag decl kind!");
    }
          
    std::string TypeName(TD->getKindName());
    TypeName += '.';
    
    // Name the codegen type after the typedef name
    // if there is no tag type name available
    if (TD->getIdentifier() == 0) {
      if (T->getTypeClass() == Type::TypeName) {
        const TypedefType *TdT = cast<TypedefType>(T);
        TypeName += TdT->getDecl()->getName();
      } else
        TypeName += "anon";
    } else 
      TypeName += TD->getName();
          
    TheModule.addTypeName(TypeName, ResultType);  
    return ResultType;
  }
  
  // FIXME: implement.
  return llvm::OpaqueType::get();
}

void CodeGenTypes::DecodeArgumentTypes(const FunctionTypeProto &FTP, 
                                       std::vector<const llvm::Type*> &ArgTys) {
  for (unsigned i = 0, e = FTP.getNumArgs(); i != e; ++i) {
    const llvm::Type *Ty = ConvertType(FTP.getArgType(i));
    if (Ty->isFirstClassType())
      ArgTys.push_back(Ty);
    else {
      QualType PTy = Context.getPointerType(FTP.getArgType(i));
      const llvm::Type *PtrTy = llvm::PointerType::getUnqual(Ty);
      TypeHolderMap.insert(std::make_pair(PTy.getTypePtr(), 
                                          llvm::PATypeHolder(PtrTy)));

      ArgTys.push_back(PtrTy);
    }
  }
}

/// getLLVMFieldNo - Return llvm::StructType element number
/// that corresponds to the field FD.
unsigned CodeGenTypes::getLLVMFieldNo(const FieldDecl *FD) {
  // FIXME : Check bit fields also
  llvm::DenseMap<const FieldDecl *, unsigned>::iterator
    I = FieldInfo.find(FD);
  assert (I != FieldInfo.end()  && "Unable to find field info");
  return I->second;
}

/// addFieldInfo - Assign field number to field FD.
void CodeGenTypes::addFieldInfo(const FieldDecl *FD, unsigned No,
                                unsigned Begin, unsigned End) {
  if (Begin == 0 && End == 0)
    FieldInfo[FD] = No;
  else
    // FD is a bit field
    BitFields.insert(std::make_pair(FD, BitFieldInfo(No, Begin, End)));
}

/// getCGRecordLayout - Return record layout info for the given llvm::Type.
const CGRecordLayout *
CodeGenTypes::getCGRecordLayout(const llvm::Type* Ty) const {
  llvm::DenseMap<const llvm::Type*, CGRecordLayout *>::iterator I
    = CGRecordLayouts.find(Ty);
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
  Cursor = 0;
  FieldNo = 0;
  LLVMFields.clear();
  for (llvm::SmallVector<const FieldDecl *, 8>::iterator I = FieldDecls.begin(),
         E = FieldDecls.end(); I != E; ++I) {
    const FieldDecl *FD = *I;

    if (FD->isBitField()) 
      placeBitField(FD);
    else {
      ExtraBits = 0;
      // FD is not a bitfield. If prev field was a bit field then it may have
      // positioned cursor such that it needs adjustment now.
      if (Cursor % 8 != 0)
        fixCursorPosition(RL);

      const llvm::Type *Ty = CGT.ConvertType(FD->getType());
      addLLVMField(Ty, CGT.getTargetData().getABITypeSizeInBits(Ty), FD, 0, 0);
    }
  }

  // At the end of structure, cursor should point to end of the strucutre.
  // This may not happen automatically if last field is a bit-field.
  fixCursorPosition(RL);

  STy = llvm::StructType::get(LLVMFields);
}

/// addPaddingFields - Current cursor is not suitable place to add next field.
/// Add required padding fields.
void RecordOrganizer::addPaddingFields(unsigned WaterMark) {
  unsigned RequiredBits = WaterMark - Cursor;
  assert ((RequiredBits % 8) == 0 && "FIXME Invalid struct layout");
  unsigned RequiredBytes = RequiredBits / 8;
  for (unsigned i = 0; i != RequiredBytes; ++i)
    addLLVMField(llvm::Type::Int8Ty, 
                 CGT.getTargetData().getABITypeSizeInBits(llvm::Type::Int8Ty));
}

/// addLLVMField - Add llvm struct field that corresponds to llvm type Ty.
/// Update cursor and increment field count. If field decl FD is available than 
/// update field info at CodeGenTypes level.
void RecordOrganizer::addLLVMField(const llvm::Type *Ty, uint64_t Size,
                                   const FieldDecl *FD, unsigned Begin,
                                   unsigned End) {

  unsigned AlignmentInBits = CGT.getTargetData().getABITypeAlignment(Ty) * 8;
  unsigned WaterMark = Cursor + (Cursor % AlignmentInBits);
  if (Cursor != WaterMark)
    // At the moment, insert padding fields even if target specific llvm 
    // type alignment enforces implict padding fields for FD. Later on, 
    // optimize llvm fields by removing implicit padding fields and 
    // combining consequetive padding fields.
    addPaddingFields(WaterMark);

  Offsets.push_back(Cursor);
  CurrentFieldStart = Cursor;
  Cursor += Size;
  llvmSize += Size;
  LLVMFields.push_back(Ty);
  if (FD)
    CGT.addFieldInfo(FD, FieldNo, Begin, End);
  ++FieldNo;
}

/// layoutUnionFields - Do the actual work and lay out all fields. Create
/// corresponding llvm struct type.  This should be invoked only after
/// all fields are added.
void RecordOrganizer::layoutUnionFields() {
 
  unsigned PrimaryEltNo = 0;
  std::pair<uint64_t, unsigned> PrimaryElt =
    CGT.getContext().getTypeInfo(FieldDecls[0]->getType(), SourceLocation());
  CGT.addFieldInfo(FieldDecls[0], 0, 0, 0);

  unsigned Size = FieldDecls.size();
  for(unsigned i = 1; i != Size; ++i) {
    const FieldDecl *FD = FieldDecls[i];
    assert (!FD->isBitField() && "Bit fields are not yet supported");
    std::pair<uint64_t, unsigned> EltInfo = 
      CGT.getContext().getTypeInfo(FD->getType(), SourceLocation());

    // Use largest element, breaking ties with the hightest aligned member.
    if (EltInfo.first > PrimaryElt.first ||
        (EltInfo.first == PrimaryElt.first &&
         EltInfo.second > PrimaryElt.second)) {
      PrimaryElt = EltInfo;
      PrimaryEltNo = i;
    }

    // In union, each field gets first slot.
    CGT.addFieldInfo(FD, 0, 0, 0);
  }

  std::vector<const llvm::Type*> Fields;
  const llvm::Type *Ty = CGT.ConvertType(FieldDecls[PrimaryEltNo]->getType());
  Fields.push_back(Ty);
  STy = llvm::StructType::get(Fields);
}

/// fixCursorPosition - When bit-field is followed by a normal field
/// cursor position may require some adjustments. 
///
/// For example,          struct { char a; short b:2;  char c; }; 
///
/// At the beginning of field 'c' layout, cursor position is 10.
/// However, only llvm struct field allocated so far is of type i8.
/// This happens because 'b' shares llvm field with 'a'. Add padding
/// field of i8 type and reposition cursor to point at 16. This 
/// should be done only if next field (i.e. 'c' here) is not a bit-field
/// or last record field is a bit-field.
void RecordOrganizer::fixCursorPosition(const ASTRecordLayout &RL) {
  Cursor = llvmSize;
  unsigned llvmSizeBytes = llvmSize/8;
  unsigned StructAlign = RL.getAlignment() / 8;
  if (llvmSizeBytes % StructAlign) {
    unsigned StructPadding = StructAlign - (llvmSizeBytes % StructAlign);
    addPaddingFields(Cursor + StructPadding*8);
  }
}

/// placeBitField - Find a place for FD, which is a bit-field. 
/// There are three separate cases to handle
/// 1) Cursor starts at byte boundry and there are no extra
///    bits are available in last llvm struct field. 
/// 2) Extra bits from previous last llvm struct field are
///    available and have enough space to hold entire FD.
/// 3) Extra bits from previous last llvm struct field are
///    available but they are not enough to hold FD entirly.
void RecordOrganizer::placeBitField(const FieldDecl *FD) {

  assert (FD->isBitField() && "FD is not a bit-field");
  Expr *BitWidth = FD->getBitWidth();
  llvm::APSInt FieldSize(32);
  bool isBitField = 
    BitWidth->isIntegerConstantExpr(FieldSize, CGT.getContext());
  assert (isBitField  && "Invalid BitField size expression");
  uint64_t BitFieldSize =  FieldSize.getZExtValue();
  if (ExtraBits == 0) {
    // CurrentField is a bit-field and structure is in one of the
    // following form.
    // struct { char CurrentField:2; char B:4; }
    // struct { char A; char CurrentField:2; };
    // struct { char A; short CurrentField:2; };
    const llvm::Type *Ty = CGT.ConvertType(FD->getType());
    // Calculate extra bits available in this bitfield.
    ExtraBits = CGT.getTargetData().getABITypeSizeInBits(Ty) - BitFieldSize;
    
    if (LLVMFields.empty()) 
      // Ths is - struct { char CurrentField:2; char B:4; }
      addLLVMField(Ty, BitFieldSize, FD, 0, ExtraBits);
    else {
      const llvm::Type *PrevTy = LLVMFields.back();
      if (CGT.getTargetData().getABITypeSizeInBits(PrevTy) >=
          CGT.getTargetData().getABITypeSizeInBits(Ty)) 
        // This is - struct { char A; char CurrentField:2; };
        addLLVMField(Ty, BitFieldSize, FD, 0, ExtraBits);
      else {
        // This is - struct { char A; short CurrentField:2; };
        // Use one of the previous filed to access current field.
        bool FoundPrevField = false;
        unsigned TotalOffsets = Offsets.size();
        uint64_t TySize = CGT.getTargetData().getABITypeSizeInBits(Ty);
        for (unsigned i = TotalOffsets; i != 0; --i) {
          uint64_t O = Offsets[i - 1];
          if (O % TySize == 0) {
            // This is appropriate llvm field to share access.
            FoundPrevField = true;
            CurrentFieldStart = O % TySize;
            unsigned FieldBegin = Cursor - (O % TySize);
            unsigned FieldEnd = TySize - (FieldBegin + BitFieldSize);
            Cursor += BitFieldSize;
            CGT.addFieldInfo(FD, i, FieldBegin, FieldEnd);
          }
        }
        assert(FoundPrevField && 
               "Unable to find a place for bitfield in struct layout");
      }
    }
  } else  if (ExtraBits >= BitFieldSize) {
    const llvm::Type *Ty = CGT.ConvertType(FD->getType());
    uint64_t TySize = CGT.getTargetData().getABITypeSizeInBits(Ty);
    if (Cursor - CurrentFieldStart + BitFieldSize > TySize) {
      // This is : struct { char a; int b:10; int c:18; };
      // where 'b' shares first field with 'a'. However 'c' needs
      // new llvm field.

      //unsigned ExtraBitsInCurrentByte = 8 - (Cursor % 8);
      //Cursor = Cursor + ExtraBitsInCurrentByte;
      //ExtraBits = 0;
      Cursor = llvmSize;
      unsigned EndOfCurrentType = CurrentFieldStart + TySize;
      addPaddingFields(EndOfCurrentType);
      addLLVMField(Ty, TySize, FD, 0, BitFieldSize);
    } else {
    // Reuse existing llvm field
    ExtraBits = ExtraBits  - BitFieldSize;
    CGT.addFieldInfo(FD, FieldNo, Cursor - CurrentFieldStart, ExtraBits);
    Cursor = Cursor + BitFieldSize;
    ++FieldNo;
    }
  } else {
    //ExtraBits are not enough to hold entire FD.
    const llvm::Type *Ty = CGT.ConvertType(FD->getType());
    const llvm::Type *PrevTy = LLVMFields.back();
    uint64_t TySize = CGT.getTargetData().getABITypeSizeInBits(Ty);
    assert (CGT.getTargetData().getABITypeSizeInBits(PrevTy) >= TySize
            && "Unable to handle bit field");

    // Previous field does not allow sharing of ExtraBits. Use new field.
    // struct { char a; char b:5; char c:4; } where c is current FD.
    Cursor += ExtraBits;
    ExtraBits = 0;
    addLLVMField(Ty, TySize, FD, 0, BitFieldSize);
  }
}
