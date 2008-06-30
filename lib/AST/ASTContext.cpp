//===--- ASTContext.cpp - Context to hold long-lived AST nodes ------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file implements the ASTContext interface.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclObjC.h"
#include "clang/Basic/TargetInfo.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Bitcode/Serialize.h"
#include "llvm/Bitcode/Deserialize.h"

using namespace clang;

enum FloatingRank {
  FloatRank, DoubleRank, LongDoubleRank
};

ASTContext::~ASTContext() {
  // Deallocate all the types.
  while (!Types.empty()) {
    Types.back()->Destroy(*this);
    Types.pop_back();
  }

  TUDecl->Destroy(*this);
}

void ASTContext::PrintStats() const {
  fprintf(stderr, "*** AST Context Stats:\n");
  fprintf(stderr, "  %d types total.\n", (int)Types.size());
  unsigned NumBuiltin = 0, NumPointer = 0, NumArray = 0, NumFunctionP = 0;
  unsigned NumVector = 0, NumComplex = 0;
  unsigned NumFunctionNP = 0, NumTypeName = 0, NumTagged = 0, NumReference = 0;
  
  unsigned NumTagStruct = 0, NumTagUnion = 0, NumTagEnum = 0, NumTagClass = 0;
  unsigned NumObjCInterfaces = 0, NumObjCQualifiedInterfaces = 0;
  unsigned NumObjCQualifiedIds = 0;
  unsigned NumTypeOfTypes = 0, NumTypeOfExprs = 0;
  
  for (unsigned i = 0, e = Types.size(); i != e; ++i) {
    Type *T = Types[i];
    if (isa<BuiltinType>(T))
      ++NumBuiltin;
    else if (isa<PointerType>(T))
      ++NumPointer;
    else if (isa<ReferenceType>(T))
      ++NumReference;
    else if (isa<ComplexType>(T))
      ++NumComplex;
    else if (isa<ArrayType>(T))
      ++NumArray;
    else if (isa<VectorType>(T))
      ++NumVector;
    else if (isa<FunctionTypeNoProto>(T))
      ++NumFunctionNP;
    else if (isa<FunctionTypeProto>(T))
      ++NumFunctionP;
    else if (isa<TypedefType>(T))
      ++NumTypeName;
    else if (TagType *TT = dyn_cast<TagType>(T)) {
      ++NumTagged;
      switch (TT->getDecl()->getTagKind()) {
      default: assert(0 && "Unknown tagged type!");
      case TagDecl::TK_struct: ++NumTagStruct; break;
      case TagDecl::TK_union:  ++NumTagUnion; break;
      case TagDecl::TK_class:  ++NumTagClass; break; 
      case TagDecl::TK_enum:   ++NumTagEnum; break;
      }
    } else if (isa<ObjCInterfaceType>(T))
      ++NumObjCInterfaces;
    else if (isa<ObjCQualifiedInterfaceType>(T))
      ++NumObjCQualifiedInterfaces;
    else if (isa<ObjCQualifiedIdType>(T))
      ++NumObjCQualifiedIds;
    else if (isa<TypeOfType>(T))
      ++NumTypeOfTypes;
    else if (isa<TypeOfExpr>(T))
      ++NumTypeOfExprs;
    else {
      QualType(T, 0).dump();
      assert(0 && "Unknown type!");
    }
  }

  fprintf(stderr, "    %d builtin types\n", NumBuiltin);
  fprintf(stderr, "    %d pointer types\n", NumPointer);
  fprintf(stderr, "    %d reference types\n", NumReference);
  fprintf(stderr, "    %d complex types\n", NumComplex);
  fprintf(stderr, "    %d array types\n", NumArray);
  fprintf(stderr, "    %d vector types\n", NumVector);
  fprintf(stderr, "    %d function types with proto\n", NumFunctionP);
  fprintf(stderr, "    %d function types with no proto\n", NumFunctionNP);
  fprintf(stderr, "    %d typename (typedef) types\n", NumTypeName);
  fprintf(stderr, "    %d tagged types\n", NumTagged);
  fprintf(stderr, "      %d struct types\n", NumTagStruct);
  fprintf(stderr, "      %d union types\n", NumTagUnion);
  fprintf(stderr, "      %d class types\n", NumTagClass);
  fprintf(stderr, "      %d enum types\n", NumTagEnum);
  fprintf(stderr, "    %d interface types\n", NumObjCInterfaces);
  fprintf(stderr, "    %d protocol qualified interface types\n",
          NumObjCQualifiedInterfaces);
  fprintf(stderr, "    %d protocol qualified id types\n",
          NumObjCQualifiedIds);
  fprintf(stderr, "    %d typeof types\n", NumTypeOfTypes);
  fprintf(stderr, "    %d typeof exprs\n", NumTypeOfExprs);
  
  fprintf(stderr, "Total bytes = %d\n", int(NumBuiltin*sizeof(BuiltinType)+
    NumPointer*sizeof(PointerType)+NumArray*sizeof(ArrayType)+
    NumComplex*sizeof(ComplexType)+NumVector*sizeof(VectorType)+
    NumFunctionP*sizeof(FunctionTypeProto)+
    NumFunctionNP*sizeof(FunctionTypeNoProto)+
    NumTypeName*sizeof(TypedefType)+NumTagged*sizeof(TagType)+
    NumTypeOfTypes*sizeof(TypeOfType)+NumTypeOfExprs*sizeof(TypeOfExpr)));
}


void ASTContext::InitBuiltinType(QualType &R, BuiltinType::Kind K) {
  Types.push_back((R = QualType(new BuiltinType(K),0)).getTypePtr());
}

void ASTContext::InitBuiltinTypes() {
  assert(VoidTy.isNull() && "Context reinitialized?");
  
  // C99 6.2.5p19.
  InitBuiltinType(VoidTy,              BuiltinType::Void);
  
  // C99 6.2.5p2.
  InitBuiltinType(BoolTy,              BuiltinType::Bool);
  // C99 6.2.5p3.
  if (Target.isCharSigned())
    InitBuiltinType(CharTy,            BuiltinType::Char_S);
  else
    InitBuiltinType(CharTy,            BuiltinType::Char_U);
  // C99 6.2.5p4.
  InitBuiltinType(SignedCharTy,        BuiltinType::SChar);
  InitBuiltinType(ShortTy,             BuiltinType::Short);
  InitBuiltinType(IntTy,               BuiltinType::Int);
  InitBuiltinType(LongTy,              BuiltinType::Long);
  InitBuiltinType(LongLongTy,          BuiltinType::LongLong);
  
  // C99 6.2.5p6.
  InitBuiltinType(UnsignedCharTy,      BuiltinType::UChar);
  InitBuiltinType(UnsignedShortTy,     BuiltinType::UShort);
  InitBuiltinType(UnsignedIntTy,       BuiltinType::UInt);
  InitBuiltinType(UnsignedLongTy,      BuiltinType::ULong);
  InitBuiltinType(UnsignedLongLongTy,  BuiltinType::ULongLong);
  
  // C99 6.2.5p10.
  InitBuiltinType(FloatTy,             BuiltinType::Float);
  InitBuiltinType(DoubleTy,            BuiltinType::Double);
  InitBuiltinType(LongDoubleTy,        BuiltinType::LongDouble);
  
  // C99 6.2.5p11.
  FloatComplexTy      = getComplexType(FloatTy);
  DoubleComplexTy     = getComplexType(DoubleTy);
  LongDoubleComplexTy = getComplexType(LongDoubleTy);
  
  BuiltinVaListType = QualType();
  ObjCIdType = QualType();
  IdStructType = 0;
  ObjCClassType = QualType();
  ClassStructType = 0;
  
  ObjCConstantStringType = QualType();
  
  // void * type
  VoidPtrTy = getPointerType(VoidTy);
}

//===----------------------------------------------------------------------===//
//                         Type Sizing and Analysis
//===----------------------------------------------------------------------===//

/// getFloatTypeSemantics - Return the APFloat 'semantics' for the specified
/// scalar floating point type.
const llvm::fltSemantics &ASTContext::getFloatTypeSemantics(QualType T) const {
  const BuiltinType *BT = T->getAsBuiltinType();
  assert(BT && "Not a floating point type!");
  switch (BT->getKind()) {
  default: assert(0 && "Not a floating point type!");
  case BuiltinType::Float:      return Target.getFloatFormat();
  case BuiltinType::Double:     return Target.getDoubleFormat();
  case BuiltinType::LongDouble: return Target.getLongDoubleFormat();
  }
}


/// getTypeSize - Return the size of the specified type, in bits.  This method
/// does not work on incomplete types.
std::pair<uint64_t, unsigned>
ASTContext::getTypeInfo(QualType T) {
  T = getCanonicalType(T);
  uint64_t Width;
  unsigned Align;
  switch (T->getTypeClass()) {
  case Type::TypeName: assert(0 && "Not a canonical type!");
  case Type::FunctionNoProto:
  case Type::FunctionProto:
  default:
    assert(0 && "Incomplete types have no size!");
  case Type::VariableArray:
    assert(0 && "VLAs not implemented yet!");
  case Type::ConstantArray: {
    ConstantArrayType *CAT = cast<ConstantArrayType>(T);
    
    std::pair<uint64_t, unsigned> EltInfo = getTypeInfo(CAT->getElementType());
    Width = EltInfo.first*CAT->getSize().getZExtValue();
    Align = EltInfo.second;
    break;
  }
  case Type::ExtVector:
  case Type::Vector: {
    std::pair<uint64_t, unsigned> EltInfo = 
      getTypeInfo(cast<VectorType>(T)->getElementType());
    Width = EltInfo.first*cast<VectorType>(T)->getNumElements();
    // FIXME: This isn't right for unusual vectors
    Align = Width;
    break;
  }

  case Type::Builtin:
    switch (cast<BuiltinType>(T)->getKind()) {
    default: assert(0 && "Unknown builtin type!");
    case BuiltinType::Void:
      assert(0 && "Incomplete types have no size!");
    case BuiltinType::Bool:
      Width = Target.getBoolWidth();
      Align = Target.getBoolAlign();
      break;
    case BuiltinType::Char_S:
    case BuiltinType::Char_U:
    case BuiltinType::UChar:
    case BuiltinType::SChar:
      Width = Target.getCharWidth();
      Align = Target.getCharAlign();
      break;
    case BuiltinType::UShort:
    case BuiltinType::Short:
      Width = Target.getShortWidth();
      Align = Target.getShortAlign();
      break;
    case BuiltinType::UInt:
    case BuiltinType::Int:
      Width = Target.getIntWidth();
      Align = Target.getIntAlign();
      break;
    case BuiltinType::ULong:
    case BuiltinType::Long:
      Width = Target.getLongWidth();
      Align = Target.getLongAlign();
      break;
    case BuiltinType::ULongLong:
    case BuiltinType::LongLong:
      Width = Target.getLongLongWidth();
      Align = Target.getLongLongAlign();
      break;
    case BuiltinType::Float:
      Width = Target.getFloatWidth();
      Align = Target.getFloatAlign();
      break;
    case BuiltinType::Double:
      Width = Target.getDoubleWidth();
      Align = Target.getDoubleAlign();
      break;
    case BuiltinType::LongDouble:
      Width = Target.getLongDoubleWidth();
      Align = Target.getLongDoubleAlign();
      break;
    }
    break;
  case Type::ASQual:
    // FIXME: Pointers into different addr spaces could have different sizes and
    // alignment requirements: getPointerInfo should take an AddrSpace.
    return getTypeInfo(QualType(cast<ASQualType>(T)->getBaseType(), 0));
  case Type::ObjCQualifiedId:
    Width = Target.getPointerWidth(0);
    Align = Target.getPointerAlign(0);
    break;
  case Type::Pointer: {
    unsigned AS = cast<PointerType>(T)->getPointeeType().getAddressSpace();
    Width = Target.getPointerWidth(AS);
    Align = Target.getPointerAlign(AS);
    break;
  }
  case Type::Reference:
    // "When applied to a reference or a reference type, the result is the size
    // of the referenced type." C++98 5.3.3p2: expr.sizeof.
    // FIXME: This is wrong for struct layout: a reference in a struct has
    // pointer size.
    return getTypeInfo(cast<ReferenceType>(T)->getPointeeType());
    
  case Type::Complex: {
    // Complex types have the same alignment as their elements, but twice the
    // size.
    std::pair<uint64_t, unsigned> EltInfo = 
      getTypeInfo(cast<ComplexType>(T)->getElementType());
    Width = EltInfo.first*2;
    Align = EltInfo.second;
    break;
  }
  case Type::ObjCInterface: {
    ObjCInterfaceType *ObjCI = cast<ObjCInterfaceType>(T);
    const ASTRecordLayout &Layout = getASTObjCInterfaceLayout(ObjCI->getDecl());
    Width = Layout.getSize();
    Align = Layout.getAlignment();
    break;
  }
  case Type::Tagged: {
    if (EnumType *ET = dyn_cast<EnumType>(cast<TagType>(T)))
      return getTypeInfo(ET->getDecl()->getIntegerType());

    RecordType *RT = cast<RecordType>(T);
    const ASTRecordLayout &Layout = getASTRecordLayout(RT->getDecl());
    Width = Layout.getSize();
    Align = Layout.getAlignment();
    break;
  }
  }
  
  assert(Align && (Align & (Align-1)) == 0 && "Alignment must be power of 2");
  return std::make_pair(Width, Align);
}

/// LayoutField - Field layout.
void ASTRecordLayout::LayoutField(const FieldDecl *FD, unsigned FieldNo,
                                  bool IsUnion, bool StructIsPacked,
                                  ASTContext &Context) {
  bool FieldIsPacked = StructIsPacked || FD->getAttr<PackedAttr>();
  uint64_t FieldOffset = IsUnion ? 0 : Size;
  uint64_t FieldSize;
  unsigned FieldAlign;
  
  if (const Expr *BitWidthExpr = FD->getBitWidth()) {
    // TODO: Need to check this algorithm on other targets!
    //       (tested on Linux-X86)
    llvm::APSInt I(32);
    bool BitWidthIsICE = 
      BitWidthExpr->isIntegerConstantExpr(I, Context);
    assert (BitWidthIsICE  && "Invalid BitField size expression");
    FieldSize = I.getZExtValue();
    
    std::pair<uint64_t, unsigned> FieldInfo = 
      Context.getTypeInfo(FD->getType());
    uint64_t TypeSize = FieldInfo.first;
    
    FieldAlign = FieldInfo.second;
    if (FieldIsPacked)
      FieldAlign = 1;
    if (const AlignedAttr *AA = FD->getAttr<AlignedAttr>())
      FieldAlign = std::max(FieldAlign, AA->getAlignment());
    
    // Check if we need to add padding to give the field the correct
    // alignment.
    if (FieldSize == 0 || (FieldOffset & (FieldAlign-1)) + FieldSize > TypeSize)
      FieldOffset = (FieldOffset + (FieldAlign-1)) & ~(FieldAlign-1);
    
    // Padding members don't affect overall alignment
    if (!FD->getIdentifier())
      FieldAlign = 1;
  } else {
    if (FD->getType()->isIncompleteType()) {
      // This must be a flexible array member; we can't directly
      // query getTypeInfo about these, so we figure it out here.
      // Flexible array members don't have any size, but they
      // have to be aligned appropriately for their element type.
      FieldSize = 0;
      const ArrayType* ATy = FD->getType()->getAsArrayType();
      FieldAlign = Context.getTypeAlign(ATy->getElementType());
    } else {
      std::pair<uint64_t, unsigned> FieldInfo = 
        Context.getTypeInfo(FD->getType());
      FieldSize = FieldInfo.first;
      FieldAlign = FieldInfo.second;
    }
    
    if (FieldIsPacked)
      FieldAlign = 8;
    if (const AlignedAttr *AA = FD->getAttr<AlignedAttr>())
      FieldAlign = std::max(FieldAlign, AA->getAlignment());
    
    // Round up the current record size to the field's alignment boundary.
    FieldOffset = (FieldOffset + (FieldAlign-1)) & ~(FieldAlign-1);
  }
  
  // Place this field at the current location.
  FieldOffsets[FieldNo] = FieldOffset;
  
  // Reserve space for this field.
  if (IsUnion) {
    Size = std::max(Size, FieldSize);
  } else {
    Size = FieldOffset + FieldSize;
  }
  
  // Remember max struct/class alignment.
  Alignment = std::max(Alignment, FieldAlign);
}


/// getASTObjcInterfaceLayout - Get or compute information about the layout of the
/// specified Objective C, which indicates its size and ivar
/// position information.
const ASTRecordLayout &
ASTContext::getASTObjCInterfaceLayout(const ObjCInterfaceDecl *D) {
  // Look up this layout, if already laid out, return what we have.
  const ASTRecordLayout *&Entry = ASTObjCInterfaces[D];
  if (Entry) return *Entry;

  // Allocate and assign into ASTRecordLayouts here.  The "Entry" reference can
  // be invalidated (dangle) if the ASTRecordLayouts hashtable is inserted into.
  ASTRecordLayout *NewEntry = NULL;
  unsigned FieldCount = D->ivar_size();
  if (ObjCInterfaceDecl *SD = D->getSuperClass()) {
    FieldCount++;
    const ASTRecordLayout &SL = getASTObjCInterfaceLayout(SD);
    unsigned Alignment = SL.getAlignment();
    uint64_t Size = SL.getSize();
    NewEntry = new ASTRecordLayout(Size, Alignment);
    NewEntry->InitializeLayout(FieldCount);
    NewEntry->SetFieldOffset(0, 0); // Super class is at the beginning of the layout.
  } else {
    NewEntry = new ASTRecordLayout();
    NewEntry->InitializeLayout(FieldCount);
  }
  Entry = NewEntry;

  bool IsPacked = D->getAttr<PackedAttr>();

  if (const AlignedAttr *AA = D->getAttr<AlignedAttr>())
    NewEntry->SetAlignment(std::max(NewEntry->getAlignment(), 
                                    AA->getAlignment()));

  // Layout each ivar sequentially.
  unsigned i = 0;
  for (ObjCInterfaceDecl::ivar_iterator IVI = D->ivar_begin(), 
       IVE = D->ivar_end(); IVI != IVE; ++IVI) {
    const ObjCIvarDecl* Ivar = (*IVI);
    NewEntry->LayoutField(Ivar, i++, false, IsPacked, *this);
  }

  // Finally, round the size of the total struct up to the alignment of the
  // struct itself.
  NewEntry->FinalizeLayout();
  return *NewEntry;
}

/// getASTRecordLayout - Get or compute information about the layout of the
/// specified record (struct/union/class), which indicates its size and field
/// position information.
const ASTRecordLayout &ASTContext::getASTRecordLayout(const RecordDecl *D) {
  assert(D->isDefinition() && "Cannot get layout of forward declarations!");

  // Look up this layout, if already laid out, return what we have.
  const ASTRecordLayout *&Entry = ASTRecordLayouts[D];
  if (Entry) return *Entry;

  // Allocate and assign into ASTRecordLayouts here.  The "Entry" reference can
  // be invalidated (dangle) if the ASTRecordLayouts hashtable is inserted into.
  ASTRecordLayout *NewEntry = new ASTRecordLayout();
  Entry = NewEntry;

  NewEntry->InitializeLayout(D->getNumMembers());
  bool StructIsPacked = D->getAttr<PackedAttr>();
  bool IsUnion = D->isUnion();

  if (const AlignedAttr *AA = D->getAttr<AlignedAttr>())
    NewEntry->SetAlignment(std::max(NewEntry->getAlignment(), 
                                    AA->getAlignment()));

  // Layout each field, for now, just sequentially, respecting alignment.  In
  // the future, this will need to be tweakable by targets.
  for (unsigned i = 0, e = D->getNumMembers(); i != e; ++i) {
    const FieldDecl *FD = D->getMember(i);
    NewEntry->LayoutField(FD, i, IsUnion, StructIsPacked, *this);
  }

  // Finally, round the size of the total struct up to the alignment of the
  // struct itself.
  NewEntry->FinalizeLayout();
  return *NewEntry;
}

//===----------------------------------------------------------------------===//
//                   Type creation/memoization methods
//===----------------------------------------------------------------------===//

QualType ASTContext::getASQualType(QualType T, unsigned AddressSpace) {
  QualType CanT = getCanonicalType(T);
  if (CanT.getAddressSpace() == AddressSpace)
    return T;
  
  // Type's cannot have multiple ASQuals, therefore we know we only have to deal
  // with CVR qualifiers from here on out.
  assert(CanT.getAddressSpace() == 0 &&
         "Type is already address space qualified");
  
  // Check if we've already instantiated an address space qual'd type of this
  // type.
  llvm::FoldingSetNodeID ID;
  ASQualType::Profile(ID, T.getTypePtr(), AddressSpace);      
  void *InsertPos = 0;
  if (ASQualType *ASQy = ASQualTypes.FindNodeOrInsertPos(ID, InsertPos))
    return QualType(ASQy, 0);
    
  // If the base type isn't canonical, this won't be a canonical type either,
  // so fill in the canonical type field.
  QualType Canonical;
  if (!T->isCanonical()) {
    Canonical = getASQualType(CanT, AddressSpace);
    
    // Get the new insert position for the node we care about.
    ASQualType *NewIP = ASQualTypes.FindNodeOrInsertPos(ID, InsertPos);
    assert(NewIP == 0 && "Shouldn't be in the map!");
  }
  ASQualType *New = new ASQualType(T.getTypePtr(), Canonical, AddressSpace);
  ASQualTypes.InsertNode(New, InsertPos);
  Types.push_back(New);
  return QualType(New, T.getCVRQualifiers());
}


/// getComplexType - Return the uniqued reference to the type for a complex
/// number with the specified element type.
QualType ASTContext::getComplexType(QualType T) {
  // Unique pointers, to guarantee there is only one pointer of a particular
  // structure.
  llvm::FoldingSetNodeID ID;
  ComplexType::Profile(ID, T);
  
  void *InsertPos = 0;
  if (ComplexType *CT = ComplexTypes.FindNodeOrInsertPos(ID, InsertPos))
    return QualType(CT, 0);
  
  // If the pointee type isn't canonical, this won't be a canonical type either,
  // so fill in the canonical type field.
  QualType Canonical;
  if (!T->isCanonical()) {
    Canonical = getComplexType(getCanonicalType(T));
    
    // Get the new insert position for the node we care about.
    ComplexType *NewIP = ComplexTypes.FindNodeOrInsertPos(ID, InsertPos);
    assert(NewIP == 0 && "Shouldn't be in the map!");
  }
  ComplexType *New = new ComplexType(T, Canonical);
  Types.push_back(New);
  ComplexTypes.InsertNode(New, InsertPos);
  return QualType(New, 0);
}


/// getPointerType - Return the uniqued reference to the type for a pointer to
/// the specified type.
QualType ASTContext::getPointerType(QualType T) {
  // Unique pointers, to guarantee there is only one pointer of a particular
  // structure.
  llvm::FoldingSetNodeID ID;
  PointerType::Profile(ID, T);
  
  void *InsertPos = 0;
  if (PointerType *PT = PointerTypes.FindNodeOrInsertPos(ID, InsertPos))
    return QualType(PT, 0);
  
  // If the pointee type isn't canonical, this won't be a canonical type either,
  // so fill in the canonical type field.
  QualType Canonical;
  if (!T->isCanonical()) {
    Canonical = getPointerType(getCanonicalType(T));
   
    // Get the new insert position for the node we care about.
    PointerType *NewIP = PointerTypes.FindNodeOrInsertPos(ID, InsertPos);
    assert(NewIP == 0 && "Shouldn't be in the map!");
  }
  PointerType *New = new PointerType(T, Canonical);
  Types.push_back(New);
  PointerTypes.InsertNode(New, InsertPos);
  return QualType(New, 0);
}

/// getReferenceType - Return the uniqued reference to the type for a reference
/// to the specified type.
QualType ASTContext::getReferenceType(QualType T) {
  // Unique pointers, to guarantee there is only one pointer of a particular
  // structure.
  llvm::FoldingSetNodeID ID;
  ReferenceType::Profile(ID, T);

  void *InsertPos = 0;
  if (ReferenceType *RT = ReferenceTypes.FindNodeOrInsertPos(ID, InsertPos))
    return QualType(RT, 0);
  
  // If the referencee type isn't canonical, this won't be a canonical type
  // either, so fill in the canonical type field.
  QualType Canonical;
  if (!T->isCanonical()) {
    Canonical = getReferenceType(getCanonicalType(T));
   
    // Get the new insert position for the node we care about.
    ReferenceType *NewIP = ReferenceTypes.FindNodeOrInsertPos(ID, InsertPos);
    assert(NewIP == 0 && "Shouldn't be in the map!");
  }

  ReferenceType *New = new ReferenceType(T, Canonical);
  Types.push_back(New);
  ReferenceTypes.InsertNode(New, InsertPos);
  return QualType(New, 0);
}

/// getConstantArrayType - Return the unique reference to the type for an 
/// array of the specified element type.
QualType ASTContext::getConstantArrayType(QualType EltTy, 
                                          const llvm::APInt &ArySize,
                                          ArrayType::ArraySizeModifier ASM,
                                          unsigned EltTypeQuals) {
  llvm::FoldingSetNodeID ID;
  ConstantArrayType::Profile(ID, EltTy, ArySize);
      
  void *InsertPos = 0;
  if (ConstantArrayType *ATP = 
      ConstantArrayTypes.FindNodeOrInsertPos(ID, InsertPos))
    return QualType(ATP, 0);
  
  // If the element type isn't canonical, this won't be a canonical type either,
  // so fill in the canonical type field.
  QualType Canonical;
  if (!EltTy->isCanonical()) {
    Canonical = getConstantArrayType(getCanonicalType(EltTy), ArySize, 
                                     ASM, EltTypeQuals);
    // Get the new insert position for the node we care about.
    ConstantArrayType *NewIP = 
      ConstantArrayTypes.FindNodeOrInsertPos(ID, InsertPos);

    assert(NewIP == 0 && "Shouldn't be in the map!");
  }
  
  ConstantArrayType *New = new ConstantArrayType(EltTy, Canonical, ArySize,
                                                 ASM, EltTypeQuals);
  ConstantArrayTypes.InsertNode(New, InsertPos);
  Types.push_back(New);
  return QualType(New, 0);
}

/// getVariableArrayType - Returns a non-unique reference to the type for a
/// variable array of the specified element type.
QualType ASTContext::getVariableArrayType(QualType EltTy, Expr *NumElts,
                                          ArrayType::ArraySizeModifier ASM,
                                          unsigned EltTypeQuals) {
  // Since we don't unique expressions, it isn't possible to unique VLA's
  // that have an expression provided for their size.

  VariableArrayType *New = new VariableArrayType(EltTy, QualType(), NumElts, 
                                                 ASM, EltTypeQuals);

  VariableArrayTypes.push_back(New);
  Types.push_back(New);
  return QualType(New, 0);
}

QualType ASTContext::getIncompleteArrayType(QualType EltTy,
                                            ArrayType::ArraySizeModifier ASM,
                                            unsigned EltTypeQuals) {
  llvm::FoldingSetNodeID ID;
  IncompleteArrayType::Profile(ID, EltTy);

  void *InsertPos = 0;
  if (IncompleteArrayType *ATP = 
       IncompleteArrayTypes.FindNodeOrInsertPos(ID, InsertPos))
    return QualType(ATP, 0);

  // If the element type isn't canonical, this won't be a canonical type
  // either, so fill in the canonical type field.
  QualType Canonical;

  if (!EltTy->isCanonical()) {
    Canonical = getIncompleteArrayType(getCanonicalType(EltTy),
                                       ASM, EltTypeQuals);

    // Get the new insert position for the node we care about.
    IncompleteArrayType *NewIP =
      IncompleteArrayTypes.FindNodeOrInsertPos(ID, InsertPos);

    assert(NewIP == 0 && "Shouldn't be in the map!");
  }

  IncompleteArrayType *New = new IncompleteArrayType(EltTy, Canonical,
                                                     ASM, EltTypeQuals);

  IncompleteArrayTypes.InsertNode(New, InsertPos);
  Types.push_back(New);
  return QualType(New, 0);
}

/// getVectorType - Return the unique reference to a vector type of
/// the specified element type and size. VectorType must be a built-in type.
QualType ASTContext::getVectorType(QualType vecType, unsigned NumElts) {
  BuiltinType *baseType;
  
  baseType = dyn_cast<BuiltinType>(getCanonicalType(vecType).getTypePtr());
  assert(baseType != 0 && "getVectorType(): Expecting a built-in type");
         
  // Check if we've already instantiated a vector of this type.
  llvm::FoldingSetNodeID ID;
  VectorType::Profile(ID, vecType, NumElts, Type::Vector);      
  void *InsertPos = 0;
  if (VectorType *VTP = VectorTypes.FindNodeOrInsertPos(ID, InsertPos))
    return QualType(VTP, 0);

  // If the element type isn't canonical, this won't be a canonical type either,
  // so fill in the canonical type field.
  QualType Canonical;
  if (!vecType->isCanonical()) {
    Canonical = getVectorType(getCanonicalType(vecType), NumElts);
    
    // Get the new insert position for the node we care about.
    VectorType *NewIP = VectorTypes.FindNodeOrInsertPos(ID, InsertPos);
    assert(NewIP == 0 && "Shouldn't be in the map!");
  }
  VectorType *New = new VectorType(vecType, NumElts, Canonical);
  VectorTypes.InsertNode(New, InsertPos);
  Types.push_back(New);
  return QualType(New, 0);
}

/// getExtVectorType - Return the unique reference to an extended vector type of
/// the specified element type and size. VectorType must be a built-in type.
QualType ASTContext::getExtVectorType(QualType vecType, unsigned NumElts) {
  BuiltinType *baseType;
  
  baseType = dyn_cast<BuiltinType>(getCanonicalType(vecType).getTypePtr());
  assert(baseType != 0 && "getExtVectorType(): Expecting a built-in type");
         
  // Check if we've already instantiated a vector of this type.
  llvm::FoldingSetNodeID ID;
  VectorType::Profile(ID, vecType, NumElts, Type::ExtVector);      
  void *InsertPos = 0;
  if (VectorType *VTP = VectorTypes.FindNodeOrInsertPos(ID, InsertPos))
    return QualType(VTP, 0);

  // If the element type isn't canonical, this won't be a canonical type either,
  // so fill in the canonical type field.
  QualType Canonical;
  if (!vecType->isCanonical()) {
    Canonical = getExtVectorType(getCanonicalType(vecType), NumElts);
    
    // Get the new insert position for the node we care about.
    VectorType *NewIP = VectorTypes.FindNodeOrInsertPos(ID, InsertPos);
    assert(NewIP == 0 && "Shouldn't be in the map!");
  }
  ExtVectorType *New = new ExtVectorType(vecType, NumElts, Canonical);
  VectorTypes.InsertNode(New, InsertPos);
  Types.push_back(New);
  return QualType(New, 0);
}

/// getFunctionTypeNoProto - Return a K&R style C function type like 'int()'.
///
QualType ASTContext::getFunctionTypeNoProto(QualType ResultTy) {
  // Unique functions, to guarantee there is only one function of a particular
  // structure.
  llvm::FoldingSetNodeID ID;
  FunctionTypeNoProto::Profile(ID, ResultTy);
  
  void *InsertPos = 0;
  if (FunctionTypeNoProto *FT = 
        FunctionTypeNoProtos.FindNodeOrInsertPos(ID, InsertPos))
    return QualType(FT, 0);
  
  QualType Canonical;
  if (!ResultTy->isCanonical()) {
    Canonical = getFunctionTypeNoProto(getCanonicalType(ResultTy));
    
    // Get the new insert position for the node we care about.
    FunctionTypeNoProto *NewIP =
      FunctionTypeNoProtos.FindNodeOrInsertPos(ID, InsertPos);
    assert(NewIP == 0 && "Shouldn't be in the map!");
  }
  
  FunctionTypeNoProto *New = new FunctionTypeNoProto(ResultTy, Canonical);
  Types.push_back(New);
  FunctionTypeNoProtos.InsertNode(New, InsertPos);
  return QualType(New, 0);
}

/// getFunctionType - Return a normal function type with a typed argument
/// list.  isVariadic indicates whether the argument list includes '...'.
QualType ASTContext::getFunctionType(QualType ResultTy, QualType *ArgArray,
                                     unsigned NumArgs, bool isVariadic) {
  // Unique functions, to guarantee there is only one function of a particular
  // structure.
  llvm::FoldingSetNodeID ID;
  FunctionTypeProto::Profile(ID, ResultTy, ArgArray, NumArgs, isVariadic);

  void *InsertPos = 0;
  if (FunctionTypeProto *FTP = 
        FunctionTypeProtos.FindNodeOrInsertPos(ID, InsertPos))
    return QualType(FTP, 0);
    
  // Determine whether the type being created is already canonical or not.  
  bool isCanonical = ResultTy->isCanonical();
  for (unsigned i = 0; i != NumArgs && isCanonical; ++i)
    if (!ArgArray[i]->isCanonical())
      isCanonical = false;

  // If this type isn't canonical, get the canonical version of it.
  QualType Canonical;
  if (!isCanonical) {
    llvm::SmallVector<QualType, 16> CanonicalArgs;
    CanonicalArgs.reserve(NumArgs);
    for (unsigned i = 0; i != NumArgs; ++i)
      CanonicalArgs.push_back(getCanonicalType(ArgArray[i]));
    
    Canonical = getFunctionType(getCanonicalType(ResultTy),
                                &CanonicalArgs[0], NumArgs,
                                isVariadic);
    
    // Get the new insert position for the node we care about.
    FunctionTypeProto *NewIP =
      FunctionTypeProtos.FindNodeOrInsertPos(ID, InsertPos);
    assert(NewIP == 0 && "Shouldn't be in the map!");
  }
  
  // FunctionTypeProto objects are not allocated with new because they have a
  // variable size array (for parameter types) at the end of them.
  FunctionTypeProto *FTP = 
    (FunctionTypeProto*)malloc(sizeof(FunctionTypeProto) + 
                               NumArgs*sizeof(QualType));
  new (FTP) FunctionTypeProto(ResultTy, ArgArray, NumArgs, isVariadic,
                              Canonical);
  Types.push_back(FTP);
  FunctionTypeProtos.InsertNode(FTP, InsertPos);
  return QualType(FTP, 0);
}

/// getTypeDeclType - Return the unique reference to the type for the
/// specified type declaration.
QualType ASTContext::getTypeDeclType(TypeDecl *Decl) {
  if (Decl->TypeForDecl) return QualType(Decl->TypeForDecl, 0);
  
  if (TypedefDecl *Typedef = dyn_cast_or_null<TypedefDecl>(Decl))
    return getTypedefType(Typedef);
  else if (ObjCInterfaceDecl *ObjCInterface 
             = dyn_cast_or_null<ObjCInterfaceDecl>(Decl))
    return getObjCInterfaceType(ObjCInterface);
  else if (RecordDecl *Record = dyn_cast_or_null<RecordDecl>(Decl)) {
    Decl->TypeForDecl = new RecordType(Record);
    Types.push_back(Decl->TypeForDecl);
    return QualType(Decl->TypeForDecl, 0);
  } else if (EnumDecl *Enum = dyn_cast_or_null<EnumDecl>(Decl)) {
    Decl->TypeForDecl = new EnumType(Enum);
    Types.push_back(Decl->TypeForDecl);
    return QualType(Decl->TypeForDecl, 0);    
  } else
    assert(false && "TypeDecl without a type?");
}

/// getTypedefType - Return the unique reference to the type for the
/// specified typename decl.
QualType ASTContext::getTypedefType(TypedefDecl *Decl) {
  if (Decl->TypeForDecl) return QualType(Decl->TypeForDecl, 0);
  
  QualType Canonical = getCanonicalType(Decl->getUnderlyingType());
  Decl->TypeForDecl = new TypedefType(Type::TypeName, Decl, Canonical);
  Types.push_back(Decl->TypeForDecl);
  return QualType(Decl->TypeForDecl, 0);
}

/// getObjCInterfaceType - Return the unique reference to the type for the
/// specified ObjC interface decl.
QualType ASTContext::getObjCInterfaceType(ObjCInterfaceDecl *Decl) {
  if (Decl->TypeForDecl) return QualType(Decl->TypeForDecl, 0);
  
  Decl->TypeForDecl = new ObjCInterfaceType(Type::ObjCInterface, Decl);
  Types.push_back(Decl->TypeForDecl);
  return QualType(Decl->TypeForDecl, 0);
}

/// CmpProtocolNames - Comparison predicate for sorting protocols
/// alphabetically.
static bool CmpProtocolNames(const ObjCProtocolDecl *LHS,
                            const ObjCProtocolDecl *RHS) {
  return strcmp(LHS->getName(), RHS->getName()) < 0;
}

static void SortAndUniqueProtocols(ObjCProtocolDecl **&Protocols,
                                   unsigned &NumProtocols) {
  ObjCProtocolDecl **ProtocolsEnd = Protocols+NumProtocols;
  
  // Sort protocols, keyed by name.
  std::sort(Protocols, Protocols+NumProtocols, CmpProtocolNames);

  // Remove duplicates.
  ProtocolsEnd = std::unique(Protocols, ProtocolsEnd);
  NumProtocols = ProtocolsEnd-Protocols;
}


/// getObjCQualifiedInterfaceType - Return a ObjCQualifiedInterfaceType type for
/// the given interface decl and the conforming protocol list.
QualType ASTContext::getObjCQualifiedInterfaceType(ObjCInterfaceDecl *Decl,
                       ObjCProtocolDecl **Protocols, unsigned NumProtocols) {
  // Sort the protocol list alphabetically to canonicalize it.
  SortAndUniqueProtocols(Protocols, NumProtocols);
  
  llvm::FoldingSetNodeID ID;
  ObjCQualifiedInterfaceType::Profile(ID, Decl, Protocols, NumProtocols);
  
  void *InsertPos = 0;
  if (ObjCQualifiedInterfaceType *QT =
      ObjCQualifiedInterfaceTypes.FindNodeOrInsertPos(ID, InsertPos))
    return QualType(QT, 0);
  
  // No Match;
  ObjCQualifiedInterfaceType *QType =
    new ObjCQualifiedInterfaceType(Decl, Protocols, NumProtocols);
  Types.push_back(QType);
  ObjCQualifiedInterfaceTypes.InsertNode(QType, InsertPos);
  return QualType(QType, 0);
}

/// getObjCQualifiedIdType - Return an ObjCQualifiedIdType for the 'id' decl
/// and the conforming protocol list.
QualType ASTContext::getObjCQualifiedIdType(QualType idType,
                                            ObjCProtocolDecl **Protocols, 
                                            unsigned NumProtocols) {
  // Sort the protocol list alphabetically to canonicalize it.
  SortAndUniqueProtocols(Protocols, NumProtocols);

  llvm::FoldingSetNodeID ID;
  ObjCQualifiedIdType::Profile(ID, Protocols, NumProtocols);
  
  void *InsertPos = 0;
  if (ObjCQualifiedIdType *QT =
      ObjCQualifiedIdTypes.FindNodeOrInsertPos(ID, InsertPos))
    return QualType(QT, 0);
  
  // No Match;
  QualType Canonical;
  if (!idType->isCanonical()) {
    Canonical = getObjCQualifiedIdType(getCanonicalType(idType), 
                                       Protocols, NumProtocols);
    ObjCQualifiedIdType *NewQT = 
      ObjCQualifiedIdTypes.FindNodeOrInsertPos(ID, InsertPos);
    assert(NewQT == 0 && "Shouldn't be in the map!");
  }
  
  ObjCQualifiedIdType *QType = 
    new ObjCQualifiedIdType(Canonical, Protocols, NumProtocols);
  Types.push_back(QType);
  ObjCQualifiedIdTypes.InsertNode(QType, InsertPos);
  return QualType(QType, 0);
}

/// getTypeOfExpr - Unlike many "get<Type>" functions, we can't unique
/// TypeOfExpr AST's (since expression's are never shared). For example,
/// multiple declarations that refer to "typeof(x)" all contain different
/// DeclRefExpr's. This doesn't effect the type checker, since it operates 
/// on canonical type's (which are always unique).
QualType ASTContext::getTypeOfExpr(Expr *tofExpr) {
  QualType Canonical = getCanonicalType(tofExpr->getType());
  TypeOfExpr *toe = new TypeOfExpr(tofExpr, Canonical);
  Types.push_back(toe);
  return QualType(toe, 0);
}

/// getTypeOfType -  Unlike many "get<Type>" functions, we don't unique
/// TypeOfType AST's. The only motivation to unique these nodes would be
/// memory savings. Since typeof(t) is fairly uncommon, space shouldn't be
/// an issue. This doesn't effect the type checker, since it operates 
/// on canonical type's (which are always unique).
QualType ASTContext::getTypeOfType(QualType tofType) {
  QualType Canonical = getCanonicalType(tofType);
  TypeOfType *tot = new TypeOfType(tofType, Canonical);
  Types.push_back(tot);
  return QualType(tot, 0);
}

/// getTagDeclType - Return the unique reference to the type for the
/// specified TagDecl (struct/union/class/enum) decl.
QualType ASTContext::getTagDeclType(TagDecl *Decl) {
  assert (Decl);
  return getTypeDeclType(Decl);
}

/// getSizeType - Return the unique type for "size_t" (C99 7.17), the result 
/// of the sizeof operator (C99 6.5.3.4p4). The value is target dependent and 
/// needs to agree with the definition in <stddef.h>. 
QualType ASTContext::getSizeType() const {
  // On Darwin, size_t is defined as a "long unsigned int". 
  // FIXME: should derive from "Target".
  return UnsignedLongTy; 
}

/// getWcharType - Return the unique type for "wchar_t" (C99 7.17), the
/// width of characters in wide strings, The value is target dependent and 
/// needs to agree with the definition in <stddef.h>.
QualType ASTContext::getWcharType() const {
  // On Darwin, wchar_t is defined as a "int". 
  // FIXME: should derive from "Target".
  return IntTy; 
}

/// getPointerDiffType - Return the unique type for "ptrdiff_t" (ref?)
/// defined in <stddef.h>. Pointer - pointer requires this (C99 6.5.6p9).
QualType ASTContext::getPointerDiffType() const {
  // On Darwin, ptrdiff_t is defined as a "int". This seems like a bug...
  // FIXME: should derive from "Target".
  return IntTy; 
}

//===----------------------------------------------------------------------===//
//                              Type Operators
//===----------------------------------------------------------------------===//

/// getCanonicalType - Return the canonical (structural) type corresponding to
/// the specified potentially non-canonical type.  The non-canonical version
/// of a type may have many "decorated" versions of types.  Decorators can
/// include typedefs, 'typeof' operators, etc. The returned type is guaranteed
/// to be free of any of these, allowing two canonical types to be compared
/// for exact equality with a simple pointer comparison.
QualType ASTContext::getCanonicalType(QualType T) {
  QualType CanType = T.getTypePtr()->getCanonicalTypeInternal();
  return QualType(CanType.getTypePtr(),
                  T.getCVRQualifiers() | CanType.getCVRQualifiers());
}


/// getArrayDecayedType - Return the properly qualified result of decaying the
/// specified array type to a pointer.  This operation is non-trivial when
/// handling typedefs etc.  The canonical type of "T" must be an array type,
/// this returns a pointer to a properly qualified element of the array.
///
/// See C99 6.7.5.3p7 and C99 6.3.2.1p3.
QualType ASTContext::getArrayDecayedType(QualType Ty) {
  // Handle the common case where typedefs are not involved directly.
  QualType EltTy;
  unsigned ArrayQuals = 0;
  unsigned PointerQuals = 0;
  if (ArrayType *AT = dyn_cast<ArrayType>(Ty)) {
    // Since T "isa" an array type, it could not have had an address space
    // qualifier, just CVR qualifiers.  The properly qualified element pointer
    // gets the union of the CVR qualifiers from the element and the array, and
    // keeps any address space qualifier on the element type if present.
    EltTy = AT->getElementType();
    ArrayQuals = Ty.getCVRQualifiers();
    PointerQuals = AT->getIndexTypeQualifier();
  } else {
    // Otherwise, we have an ASQualType or a typedef, etc.  Make sure we don't
    // lose qualifiers when dealing with typedefs. Example:
    //   typedef int arr[10];
    //   void test2() {
    //     const arr b;
    //     b[4] = 1;
    //   }
    //
    // The decayed type of b is "const int*" even though the element type of the
    // array is "int".
    QualType CanTy = getCanonicalType(Ty);
    const ArrayType *PrettyArrayType = Ty->getAsArrayType();
    assert(PrettyArrayType && "Not an array type!");
    
    // Get the element type with 'getAsArrayType' so that we don't lose any
    // typedefs in the element type of the array.
    EltTy = PrettyArrayType->getElementType();

    // If the array was address-space qualifier, make sure to ASQual the element
    // type.  We can just grab the address space from the canonical type.
    if (unsigned AS = CanTy.getAddressSpace())
      EltTy = getASQualType(EltTy, AS);
    
    // To properly handle [multiple levels of] typedefs, typeof's etc, we take
    // the CVR qualifiers directly from the canonical type, which is guaranteed
    // to have the full set unioned together.
    ArrayQuals = CanTy.getCVRQualifiers();
    PointerQuals = PrettyArrayType->getIndexTypeQualifier();
  }
  
  // Apply any CVR qualifiers from the array type to the element type.  This
  // implements C99 6.7.3p8: "If the specification of an array type includes
  // any type qualifiers, the element type is so qualified, not the array type."
  EltTy = EltTy.getQualifiedType(ArrayQuals | EltTy.getCVRQualifiers());

  QualType PtrTy = getPointerType(EltTy);

  // int x[restrict 4] ->  int *restrict
  PtrTy = PtrTy.getQualifiedType(PointerQuals);

  return PtrTy;
}

/// getFloatingRank - Return a relative rank for floating point types.
/// This routine will assert if passed a built-in type that isn't a float.
static FloatingRank getFloatingRank(QualType T) {
  if (const ComplexType *CT = T->getAsComplexType())
    return getFloatingRank(CT->getElementType());

  switch (T->getAsBuiltinType()->getKind()) {
  default: assert(0 && "getFloatingRank(): not a floating type");
  case BuiltinType::Float:      return FloatRank;
  case BuiltinType::Double:     return DoubleRank;
  case BuiltinType::LongDouble: return LongDoubleRank;
  }
}

/// getFloatingTypeOfSizeWithinDomain - Returns a real floating 
/// point or a complex type (based on typeDomain/typeSize). 
/// 'typeDomain' is a real floating point or complex type.
/// 'typeSize' is a real floating point or complex type.
QualType ASTContext::getFloatingTypeOfSizeWithinDomain(QualType Size,
                                                       QualType Domain) const {
  FloatingRank EltRank = getFloatingRank(Size);
  if (Domain->isComplexType()) {
    switch (EltRank) {
    default: assert(0 && "getFloatingRank(): illegal value for rank");
    case FloatRank:      return FloatComplexTy;
    case DoubleRank:     return DoubleComplexTy;
    case LongDoubleRank: return LongDoubleComplexTy;
    }
  }

  assert(Domain->isRealFloatingType() && "Unknown domain!");
  switch (EltRank) {
  default: assert(0 && "getFloatingRank(): illegal value for rank");
  case FloatRank:      return FloatTy;
  case DoubleRank:     return DoubleTy;
  case LongDoubleRank: return LongDoubleTy;
  }
}

/// getFloatingTypeOrder - Compare the rank of the two specified floating
/// point types, ignoring the domain of the type (i.e. 'double' ==
/// '_Complex double').  If LHS > RHS, return 1.  If LHS == RHS, return 0. If
/// LHS < RHS, return -1. 
int ASTContext::getFloatingTypeOrder(QualType LHS, QualType RHS) {
  FloatingRank LHSR = getFloatingRank(LHS);
  FloatingRank RHSR = getFloatingRank(RHS);
  
  if (LHSR == RHSR)
    return 0;
  if (LHSR > RHSR)
    return 1;
  return -1;
}

/// getIntegerRank - Return an integer conversion rank (C99 6.3.1.1p1). This
/// routine will assert if passed a built-in type that isn't an integer or enum,
/// or if it is not canonicalized.
static unsigned getIntegerRank(Type *T) {
  assert(T->isCanonical() && "T should be canonicalized");
  if (isa<EnumType>(T))
    return 4;
  
  switch (cast<BuiltinType>(T)->getKind()) {
  default: assert(0 && "getIntegerRank(): not a built-in integer");
  case BuiltinType::Bool:
    return 1;
  case BuiltinType::Char_S:
  case BuiltinType::Char_U:
  case BuiltinType::SChar:
  case BuiltinType::UChar:
    return 2;
  case BuiltinType::Short:
  case BuiltinType::UShort:
    return 3;
  case BuiltinType::Int:
  case BuiltinType::UInt:
    return 4;
  case BuiltinType::Long:
  case BuiltinType::ULong:
    return 5;
  case BuiltinType::LongLong:
  case BuiltinType::ULongLong:
    return 6;
  }
}

/// getIntegerTypeOrder - Returns the highest ranked integer type: 
/// C99 6.3.1.8p1.  If LHS > RHS, return 1.  If LHS == RHS, return 0. If
/// LHS < RHS, return -1. 
int ASTContext::getIntegerTypeOrder(QualType LHS, QualType RHS) {
  Type *LHSC = getCanonicalType(LHS).getTypePtr();
  Type *RHSC = getCanonicalType(RHS).getTypePtr();
  if (LHSC == RHSC) return 0;
  
  bool LHSUnsigned = LHSC->isUnsignedIntegerType();
  bool RHSUnsigned = RHSC->isUnsignedIntegerType();
  
  unsigned LHSRank = getIntegerRank(LHSC);
  unsigned RHSRank = getIntegerRank(RHSC);
  
  if (LHSUnsigned == RHSUnsigned) {  // Both signed or both unsigned.
    if (LHSRank == RHSRank) return 0;
    return LHSRank > RHSRank ? 1 : -1;
  }
  
  // Otherwise, the LHS is signed and the RHS is unsigned or visa versa.
  if (LHSUnsigned) {
    // If the unsigned [LHS] type is larger, return it.
    if (LHSRank >= RHSRank)
      return 1;
    
    // If the signed type can represent all values of the unsigned type, it
    // wins.  Because we are dealing with 2's complement and types that are
    // powers of two larger than each other, this is always safe. 
    return -1;
  }

  // If the unsigned [RHS] type is larger, return it.
  if (RHSRank >= LHSRank)
    return -1;
  
  // If the signed type can represent all values of the unsigned type, it
  // wins.  Because we are dealing with 2's complement and types that are
  // powers of two larger than each other, this is always safe. 
  return 1;
}

// getCFConstantStringType - Return the type used for constant CFStrings. 
QualType ASTContext::getCFConstantStringType() {
  if (!CFConstantStringTypeDecl) {
    CFConstantStringTypeDecl = 
      RecordDecl::Create(*this, TagDecl::TK_struct, TUDecl, SourceLocation(), 
                         &Idents.get("NSConstantString"), 0);
    QualType FieldTypes[4];
  
    // const int *isa;
    FieldTypes[0] = getPointerType(IntTy.getQualifiedType(QualType::Const));  
    // int flags;
    FieldTypes[1] = IntTy;
    // const char *str;
    FieldTypes[2] = getPointerType(CharTy.getQualifiedType(QualType::Const));  
    // long length;
    FieldTypes[3] = LongTy;  
    // Create fields
    FieldDecl *FieldDecls[4];
  
    for (unsigned i = 0; i < 4; ++i)
      FieldDecls[i] = FieldDecl::Create(*this, SourceLocation(), 0,
                                        FieldTypes[i]);
  
    CFConstantStringTypeDecl->defineBody(FieldDecls, 4);
  }
  
  return getTagDeclType(CFConstantStringTypeDecl);
}

// This returns true if a type has been typedefed to BOOL:
// typedef <type> BOOL;
static bool isTypeTypedefedAsBOOL(QualType T) {
  if (const TypedefType *TT = dyn_cast<TypedefType>(T))
    return !strcmp(TT->getDecl()->getName(), "BOOL");
        
  return false;
}

/// getObjCEncodingTypeSize returns size of type for objective-c encoding
/// purpose.
int ASTContext::getObjCEncodingTypeSize(QualType type) {
  uint64_t sz = getTypeSize(type);
  
  // Make all integer and enum types at least as large as an int
  if (sz > 0 && type->isIntegralType())
    sz = std::max(sz, getTypeSize(IntTy));
  // Treat arrays as pointers, since that's how they're passed in.
  else if (type->isArrayType())
    sz = getTypeSize(VoidPtrTy);
  return sz / getTypeSize(CharTy);
}

/// getObjCEncodingForMethodDecl - Return the encoded type for this method
/// declaration.
void ASTContext::getObjCEncodingForMethodDecl(ObjCMethodDecl *Decl, 
                                              std::string& S)
{
  // Encode type qualifer, 'in', 'inout', etc. for the return type.
  getObjCEncodingForTypeQualifier(Decl->getObjCDeclQualifier(), S);
  // Encode result type.
  getObjCEncodingForType(Decl->getResultType(), S, EncodingRecordTypes);
  // Compute size of all parameters.
  // Start with computing size of a pointer in number of bytes.
  // FIXME: There might(should) be a better way of doing this computation!
  SourceLocation Loc;
  int PtrSize = getTypeSize(VoidPtrTy) / getTypeSize(CharTy);
  // The first two arguments (self and _cmd) are pointers; account for
  // their size.
  int ParmOffset = 2 * PtrSize;
  int NumOfParams = Decl->getNumParams();
  for (int i = 0; i < NumOfParams; i++) {
    QualType PType = Decl->getParamDecl(i)->getType();
    int sz = getObjCEncodingTypeSize (PType);
    assert (sz > 0 && "getObjCEncodingForMethodDecl - Incomplete param type");
    ParmOffset += sz;
  }
  S += llvm::utostr(ParmOffset);
  S += "@0:";
  S += llvm::utostr(PtrSize);
  
  // Argument types.
  ParmOffset = 2 * PtrSize;
  for (int i = 0; i < NumOfParams; i++) {
    QualType PType = Decl->getParamDecl(i)->getType();
    // Process argument qualifiers for user supplied arguments; such as,
    // 'in', 'inout', etc.
    getObjCEncodingForTypeQualifier(
      Decl->getParamDecl(i)->getObjCDeclQualifier(), S);
    getObjCEncodingForType(PType, S, EncodingRecordTypes);
    S += llvm::utostr(ParmOffset);
    ParmOffset += getObjCEncodingTypeSize(PType);
  }
}

void ASTContext::getObjCEncodingForType(QualType T, std::string& S,
       llvm::SmallVector<const RecordType *, 8> &ERType) const
{
  // FIXME: This currently doesn't encode:
  // @ An object (whether statically typed or typed id)
  // # A class object (Class)
  // : A method selector (SEL)
  // {name=type...} A structure
  // (name=type...) A union
  // bnum A bit field of num bits
  
  if (const BuiltinType *BT = T->getAsBuiltinType()) {
    char encoding;
    switch (BT->getKind()) {
    default: assert(0 && "Unhandled builtin type kind");          
    case BuiltinType::Void:       encoding = 'v'; break;
    case BuiltinType::Bool:       encoding = 'B'; break;
    case BuiltinType::Char_U:
    case BuiltinType::UChar:      encoding = 'C'; break;
    case BuiltinType::UShort:     encoding = 'S'; break;
    case BuiltinType::UInt:       encoding = 'I'; break;
    case BuiltinType::ULong:      encoding = 'L'; break;
    case BuiltinType::ULongLong:  encoding = 'Q'; break;
    case BuiltinType::Char_S:
    case BuiltinType::SChar:      encoding = 'c'; break;
    case BuiltinType::Short:      encoding = 's'; break;
    case BuiltinType::Int:        encoding = 'i'; break;
    case BuiltinType::Long:       encoding = 'l'; break;
    case BuiltinType::LongLong:   encoding = 'q'; break;
    case BuiltinType::Float:      encoding = 'f'; break;
    case BuiltinType::Double:     encoding = 'd'; break;
    case BuiltinType::LongDouble: encoding = 'd'; break;
    }
    
    S += encoding;
  }
  else if (T->isObjCQualifiedIdType()) {
    // Treat id<P...> same as 'id' for encoding purposes.
    return getObjCEncodingForType(getObjCIdType(), S, ERType);
    
  }
  else if (const PointerType *PT = T->getAsPointerType()) {
    QualType PointeeTy = PT->getPointeeType();
    if (isObjCIdType(PointeeTy) || PointeeTy->isObjCInterfaceType()) {
      S += '@';
      return;
    } else if (isObjCClassType(PointeeTy)) {
      S += '#';
      return;
    } else if (isObjCSelType(PointeeTy)) {
      S += ':';
      return;
    }
    
    if (PointeeTy->isCharType()) {
      // char pointer types should be encoded as '*' unless it is a
      // type that has been typedef'd to 'BOOL'.
      if (!isTypeTypedefedAsBOOL(PointeeTy)) {
        S += '*';
        return;
      }
    }
    
    S += '^';
    getObjCEncodingForType(PT->getPointeeType(), S, ERType);
  } else if (const ArrayType *AT = T->getAsArrayType()) {
    S += '[';
    
    if (const ConstantArrayType *CAT = dyn_cast<ConstantArrayType>(AT))
      S += llvm::utostr(CAT->getSize().getZExtValue());
    else
      assert(0 && "Unhandled array type!");
    
    getObjCEncodingForType(AT->getElementType(), S, ERType);
    S += ']';
  } else if (T->getAsFunctionType()) {
    S += '?';
  } else if (const RecordType *RTy = T->getAsRecordType()) {
    RecordDecl *RDecl= RTy->getDecl();
    S += '{';
    S += RDecl->getName();
    bool found = false;
    for (unsigned i = 0, e = ERType.size(); i != e; ++i)
      if (ERType[i] == RTy) {
        found = true;
        break;
      }
    if (!found) {
      ERType.push_back(RTy);
      S += '=';
      for (int i = 0; i < RDecl->getNumMembers(); i++) {
        FieldDecl *field = RDecl->getMember(i);
        getObjCEncodingForType(field->getType(), S, ERType);
      }
      assert(ERType.back() == RTy && "Record Type stack mismatch.");
      ERType.pop_back();
    }
    S += '}';
  } else if (T->isEnumeralType()) {
    S += 'i';
  } else
    assert(0 && "@encode for type not implemented!");
}

void ASTContext::getObjCEncodingForTypeQualifier(Decl::ObjCDeclQualifier QT, 
                                                 std::string& S) const {
  if (QT & Decl::OBJC_TQ_In)
    S += 'n';
  if (QT & Decl::OBJC_TQ_Inout)
    S += 'N';
  if (QT & Decl::OBJC_TQ_Out)
    S += 'o';
  if (QT & Decl::OBJC_TQ_Bycopy)
    S += 'O';
  if (QT & Decl::OBJC_TQ_Byref)
    S += 'R';
  if (QT & Decl::OBJC_TQ_Oneway)
    S += 'V';
}

void ASTContext::setBuiltinVaListType(QualType T)
{
  assert(BuiltinVaListType.isNull() && "__builtin_va_list type already set!");
    
  BuiltinVaListType = T;
}

void ASTContext::setObjCIdType(TypedefDecl *TD)
{
  assert(ObjCIdType.isNull() && "'id' type already set!");
    
  ObjCIdType = getTypedefType(TD);

  // typedef struct objc_object *id;
  const PointerType *ptr = TD->getUnderlyingType()->getAsPointerType();
  assert(ptr && "'id' incorrectly typed");
  const RecordType *rec = ptr->getPointeeType()->getAsStructureType();
  assert(rec && "'id' incorrectly typed");
  IdStructType = rec;
}

void ASTContext::setObjCSelType(TypedefDecl *TD)
{
  assert(ObjCSelType.isNull() && "'SEL' type already set!");
    
  ObjCSelType = getTypedefType(TD);

  // typedef struct objc_selector *SEL;
  const PointerType *ptr = TD->getUnderlyingType()->getAsPointerType();
  assert(ptr && "'SEL' incorrectly typed");
  const RecordType *rec = ptr->getPointeeType()->getAsStructureType();
  assert(rec && "'SEL' incorrectly typed");
  SelStructType = rec;
}

void ASTContext::setObjCProtoType(QualType QT)
{
  assert(ObjCProtoType.isNull() && "'Protocol' type already set!");
  ObjCProtoType = QT;
}

void ASTContext::setObjCClassType(TypedefDecl *TD)
{
  assert(ObjCClassType.isNull() && "'Class' type already set!");
    
  ObjCClassType = getTypedefType(TD);

  // typedef struct objc_class *Class;
  const PointerType *ptr = TD->getUnderlyingType()->getAsPointerType();
  assert(ptr && "'Class' incorrectly typed");
  const RecordType *rec = ptr->getPointeeType()->getAsStructureType();
  assert(rec && "'Class' incorrectly typed");
  ClassStructType = rec;
}

void ASTContext::setObjCConstantStringInterface(ObjCInterfaceDecl *Decl) {
  assert(ObjCConstantStringType.isNull() && 
         "'NSConstantString' type already set!");
  
  ObjCConstantStringType = getObjCInterfaceType(Decl);
}

//===----------------------------------------------------------------------===//
//                        Type Compatibility Testing
//===----------------------------------------------------------------------===//

/// C99 6.2.7p1: If both are complete types, then the following additional
/// requirements apply.
/// FIXME (handle compatibility across source files).
static bool areCompatTagTypes(TagType *LHS, TagType *RHS,
                              const ASTContext &C) {
  // "Class" and "id" are compatible built-in structure types.
  if (C.isObjCIdType(QualType(LHS, 0)) && C.isObjCClassType(QualType(RHS, 0)) ||
      C.isObjCClassType(QualType(LHS, 0)) && C.isObjCIdType(QualType(RHS, 0)))
    return true;

  // Within a translation unit a tag type is only compatible with itself.  Self
  // equality is already handled by the time we get here.
  assert(LHS != RHS && "Self equality not handled!");
  return false;
}

bool ASTContext::pointerTypesAreCompatible(QualType lhs, QualType rhs) {
  // C99 6.7.5.1p2: For two pointer types to be compatible, both shall be 
  // identically qualified and both shall be pointers to compatible types.
  if (lhs.getCVRQualifiers() != rhs.getCVRQualifiers() ||
      lhs.getAddressSpace() != rhs.getAddressSpace())
    return false;
    
  QualType ltype = cast<PointerType>(lhs.getCanonicalType())->getPointeeType();
  QualType rtype = cast<PointerType>(rhs.getCanonicalType())->getPointeeType();
  
  return typesAreCompatible(ltype, rtype);
}

bool ASTContext::functionTypesAreCompatible(QualType lhs, QualType rhs) {
  const FunctionType *lbase = cast<FunctionType>(lhs.getCanonicalType());
  const FunctionType *rbase = cast<FunctionType>(rhs.getCanonicalType());
  const FunctionTypeProto *lproto = dyn_cast<FunctionTypeProto>(lbase);
  const FunctionTypeProto *rproto = dyn_cast<FunctionTypeProto>(rbase);

  // first check the return types (common between C99 and K&R).
  if (!typesAreCompatible(lbase->getResultType(), rbase->getResultType()))
    return false;

  if (lproto && rproto) { // two C99 style function prototypes
    unsigned lproto_nargs = lproto->getNumArgs();
    unsigned rproto_nargs = rproto->getNumArgs();
    
    if (lproto_nargs != rproto_nargs)
      return false;
      
    // both prototypes have the same number of arguments.
    if ((lproto->isVariadic() && !rproto->isVariadic()) ||
        (rproto->isVariadic() && !lproto->isVariadic()))
      return false;
      
    // The use of ellipsis agree...now check the argument types.
    for (unsigned i = 0; i < lproto_nargs; i++)
      // C99 6.7.5.3p15: ...and each parameter declared with qualified type
      // is taken as having the unqualified version of it's declared type.
      if (!typesAreCompatible(lproto->getArgType(i).getUnqualifiedType(), 
                              rproto->getArgType(i).getUnqualifiedType()))
        return false;
    return true;
  }
  
  if (!lproto && !rproto) // two K&R style function decls, nothing to do.
    return true;

  // we have a mixture of K&R style with C99 prototypes
  const FunctionTypeProto *proto = lproto ? lproto : rproto;
  if (proto->isVariadic())
    return false;
    
  // FIXME: Each parameter type T in the prototype must be compatible with the
  // type resulting from applying the usual argument conversions to T.
  return true;
}

// C99 6.7.5.2p6
static bool areCompatArrayTypes(ArrayType *LHS, ArrayType *RHS, ASTContext &C) {
  // Constant arrays must be the same size to be compatible.
  if (const ConstantArrayType* LCAT = dyn_cast<ConstantArrayType>(LHS))
    if (const ConstantArrayType* RCAT = dyn_cast<ConstantArrayType>(RHS))
      if (RCAT->getSize() != LCAT->getSize())
        return false;

  // Compatible arrays must have compatible element types
  return C.typesAreCompatible(LHS->getElementType(), RHS->getElementType());
}

/// areCompatVectorTypes - Return true if the two specified vector types are 
/// compatible.
static bool areCompatVectorTypes(const VectorType *LHS,
                                 const VectorType *RHS) {
  assert(LHS->isCanonical() && RHS->isCanonical());
  return LHS->getElementType() == RHS->getElementType() &&
  LHS->getNumElements() == RHS->getNumElements();
}

/// areCompatObjCInterfaces - Return true if the two interface types are
/// compatible for assignment from RHS to LHS.  This handles validation of any
/// protocol qualifiers on the LHS or RHS.
///
static bool areCompatObjCInterfaces(const ObjCInterfaceType *LHS, 
                                    const ObjCInterfaceType *RHS) {
  // Verify that the base decls are compatible: the RHS must be a subclass of
  // the LHS.
  if (!LHS->getDecl()->isSuperClassOf(RHS->getDecl()))
    return false;
  
  // RHS must have a superset of the protocols in the LHS.  If the LHS is not
  // protocol qualified at all, then we are good.
  if (!isa<ObjCQualifiedInterfaceType>(LHS))
    return true;
  
  // Okay, we know the LHS has protocol qualifiers.  If the RHS doesn't, then it
  // isn't a superset.
  if (!isa<ObjCQualifiedInterfaceType>(RHS))
    return true;  // FIXME: should return false!
  
  // Finally, we must have two protocol-qualified interfaces.
  const ObjCQualifiedInterfaceType *LHSP =cast<ObjCQualifiedInterfaceType>(LHS);
  const ObjCQualifiedInterfaceType *RHSP =cast<ObjCQualifiedInterfaceType>(RHS);
  ObjCQualifiedInterfaceType::qual_iterator LHSPI = LHSP->qual_begin();
  ObjCQualifiedInterfaceType::qual_iterator LHSPE = LHSP->qual_end();
  ObjCQualifiedInterfaceType::qual_iterator RHSPI = RHSP->qual_begin();
  ObjCQualifiedInterfaceType::qual_iterator RHSPE = RHSP->qual_end();
  
  // All protocols in LHS must have a presence in RHS.  Since the protocol lists
  // are both sorted alphabetically and have no duplicates, we can scan RHS and
  // LHS in a single parallel scan until we run out of elements in LHS.
  assert(LHSPI != LHSPE && "Empty LHS protocol list?");
  ObjCProtocolDecl *LHSProto = *LHSPI;
  
  while (RHSPI != RHSPE) {
    ObjCProtocolDecl *RHSProto = *RHSPI++;
    // If the RHS has a protocol that the LHS doesn't, ignore it.
    if (RHSProto != LHSProto)
      continue;
    
    // Otherwise, the RHS does have this element.
    ++LHSPI;
    if (LHSPI == LHSPE)
      return true;  // All protocols in LHS exist in RHS.
    
    LHSProto = *LHSPI;
  }
  
  // If we got here, we didn't find one of the LHS's protocols in the RHS list.
  return false;
}


/// typesAreCompatible - C99 6.7.3p9: For two qualified types to be compatible, 
/// both shall have the identically qualified version of a compatible type.
/// C99 6.2.7p1: Two types have compatible types if their types are the 
/// same. See 6.7.[2,3,5] for additional rules.
bool ASTContext::typesAreCompatible(QualType LHS_NC, QualType RHS_NC) {
  QualType LHS = LHS_NC.getCanonicalType();
  QualType RHS = RHS_NC.getCanonicalType();
  
  // C++ [expr]: If an expression initially has the type "reference to T", the
  // type is adjusted to "T" prior to any further analysis, the expression
  // designates the object or function denoted by the reference, and the
  // expression is an lvalue.
  if (ReferenceType *RT = dyn_cast<ReferenceType>(LHS))
    LHS = RT->getPointeeType();
  if (ReferenceType *RT = dyn_cast<ReferenceType>(RHS))
    RHS = RT->getPointeeType();
  
  // If two types are identical, they are compatible.
  if (LHS == RHS)
    return true;
  
  // If qualifiers differ, the types are different.
  unsigned LHSAS = LHS.getAddressSpace(), RHSAS = RHS.getAddressSpace();
  if (LHS.getCVRQualifiers() != RHS.getCVRQualifiers() || LHSAS != RHSAS)
    return false;
  
  // Strip off ASQual's if present.
  if (LHSAS) {
    LHS = LHS.getUnqualifiedType();
    RHS = RHS.getUnqualifiedType();
  }

  Type::TypeClass LHSClass = LHS->getTypeClass();
  Type::TypeClass RHSClass = RHS->getTypeClass();
  
  // We want to consider the two function types to be the same for these
  // comparisons, just force one to the other.
  if (LHSClass == Type::FunctionProto) LHSClass = Type::FunctionNoProto;
  if (RHSClass == Type::FunctionProto) RHSClass = Type::FunctionNoProto;

  // Same as above for arrays
  if (LHSClass == Type::VariableArray || LHSClass == Type::IncompleteArray)
    LHSClass = Type::ConstantArray;
  if (RHSClass == Type::VariableArray || RHSClass == Type::IncompleteArray)
    RHSClass = Type::ConstantArray;
  
  // Canonicalize ExtVector -> Vector.
  if (LHSClass == Type::ExtVector) LHSClass = Type::Vector;
  if (RHSClass == Type::ExtVector) RHSClass = Type::Vector;
  
  // Consider qualified interfaces and interfaces the same.
  if (LHSClass == Type::ObjCQualifiedInterface) LHSClass = Type::ObjCInterface;
  if (RHSClass == Type::ObjCQualifiedInterface) RHSClass = Type::ObjCInterface;
  
  // If the canonical type classes don't match.
  if (LHSClass != RHSClass) {
    // ID is compatible with all interface types.
    if (isa<ObjCInterfaceType>(LHS))
      return isObjCIdType(RHS);
    if (isa<ObjCInterfaceType>(RHS))
      return isObjCIdType(LHS);

    // ID is compatible with all qualified id types.
    if (isa<ObjCQualifiedIdType>(LHS)) {
      if (const PointerType *PT = RHS->getAsPointerType())
        return isObjCIdType(PT->getPointeeType());
    }
    if (isa<ObjCQualifiedIdType>(RHS)) {
      if (const PointerType *PT = LHS->getAsPointerType())
        return isObjCIdType(PT->getPointeeType());
    }    
    // C99 6.7.2.2p4: Each enumerated type shall be compatible with char,
    // a signed integer type, or an unsigned integer type. 
    if (LHS->isEnumeralType() && RHS->isIntegralType()) {
      EnumDecl* EDecl = cast<EnumType>(LHS)->getDecl();
      return EDecl->getIntegerType() == RHS;
    }
    if (RHS->isEnumeralType() && LHS->isIntegralType()) {
      EnumDecl* EDecl = cast<EnumType>(RHS)->getDecl();
      return EDecl->getIntegerType() == LHS;
    }

    return false;
  }
  
  // The canonical type classes match.
  switch (LHSClass) {
  case Type::ASQual:
  case Type::FunctionProto:
  case Type::VariableArray:
  case Type::IncompleteArray:
  case Type::Reference:
  case Type::ObjCQualifiedInterface:
    assert(0 && "Canonicalized away above");
  case Type::Pointer:
    return pointerTypesAreCompatible(LHS, RHS);
  case Type::ConstantArray:
    return areCompatArrayTypes(cast<ArrayType>(LHS), cast<ArrayType>(RHS),
                               *this);
  case Type::FunctionNoProto:
    return functionTypesAreCompatible(LHS, RHS);
  case Type::Tagged: // handle structures, unions
    return areCompatTagTypes(cast<TagType>(LHS), cast<TagType>(RHS), *this);
  case Type::Builtin:
    // Only exactly equal builtin types are compatible, which is tested above.
    return false;
  case Type::Vector:
    return areCompatVectorTypes(cast<VectorType>(LHS), cast<VectorType>(RHS));
  case Type::ObjCInterface:
    return areCompatObjCInterfaces(cast<ObjCInterfaceType>(LHS),
                                   cast<ObjCInterfaceType>(RHS));
  default:
    assert(0 && "unexpected type");
  }
  return true; // should never get here...
}

//===----------------------------------------------------------------------===//
//                         Integer Predicates
//===----------------------------------------------------------------------===//
unsigned ASTContext::getIntWidth(QualType T) {
  if (T == BoolTy)
    return 1;
  // At the moment, only bool has padding bits
  return (unsigned)getTypeSize(T);
}

QualType ASTContext::getCorrespondingUnsignedType(QualType T) {
  assert(T->isSignedIntegerType() && "Unexpected type");
  if (const EnumType* ETy = T->getAsEnumType())
    T = ETy->getDecl()->getIntegerType();
  const BuiltinType* BTy = T->getAsBuiltinType();
  assert (BTy && "Unexpected signed integer type");
  switch (BTy->getKind()) {
  case BuiltinType::Char_S:
  case BuiltinType::SChar:
    return UnsignedCharTy;
  case BuiltinType::Short:
    return UnsignedShortTy;
  case BuiltinType::Int:
    return UnsignedIntTy;
  case BuiltinType::Long:
    return UnsignedLongTy;
  case BuiltinType::LongLong:
    return UnsignedLongLongTy;
  default:
    assert(0 && "Unexpected signed integer type");
    return QualType();
  }
}


//===----------------------------------------------------------------------===//
//                         Serialization Support
//===----------------------------------------------------------------------===//

/// Emit - Serialize an ASTContext object to Bitcode.
void ASTContext::Emit(llvm::Serializer& S) const {
  S.Emit(LangOpts);
  S.EmitRef(SourceMgr);
  S.EmitRef(Target);
  S.EmitRef(Idents);
  S.EmitRef(Selectors);

  // Emit the size of the type vector so that we can reserve that size
  // when we reconstitute the ASTContext object.
  S.EmitInt(Types.size());
  
  for (std::vector<Type*>::const_iterator I=Types.begin(), E=Types.end(); 
                                          I!=E;++I)    
    (*I)->Emit(S);

  S.EmitOwnedPtr(TUDecl);

  // FIXME: S.EmitOwnedPtr(CFConstantStringTypeDecl);
}

ASTContext* ASTContext::Create(llvm::Deserializer& D) {
  
  // Read the language options.
  LangOptions LOpts;
  LOpts.Read(D);
  
  SourceManager &SM = D.ReadRef<SourceManager>();
  TargetInfo &t = D.ReadRef<TargetInfo>();
  IdentifierTable &idents = D.ReadRef<IdentifierTable>();
  SelectorTable &sels = D.ReadRef<SelectorTable>();

  unsigned size_reserve = D.ReadInt();
  
  ASTContext* A = new ASTContext(LOpts, SM, t, idents, sels, size_reserve);
  
  for (unsigned i = 0; i < size_reserve; ++i)
    Type::Create(*A,i,D);
  
  A->TUDecl = cast<TranslationUnitDecl>(D.ReadOwnedPtr<Decl>(*A));

  // FIXME: A->CFConstantStringTypeDecl = D.ReadOwnedPtr<RecordDecl>();
  
  return A;
}
