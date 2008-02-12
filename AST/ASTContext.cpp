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
    if (FunctionTypeProto *FT = dyn_cast<FunctionTypeProto>(Types.back())) {
      // Destroy the object, but don't call delete.  These are malloc'd.
      FT->~FunctionTypeProto();
      free(FT);
    } else {
      delete Types.back();
    }
    Types.pop_back();
  }
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
      switch (TT->getDecl()->getKind()) {
      default: assert(0 && "Unknown tagged type!");
      case Decl::Struct: ++NumTagStruct; break;
      case Decl::Union:  ++NumTagUnion; break;
      case Decl::Class:  ++NumTagClass; break; 
      case Decl::Enum:   ++NumTagEnum; break;
      }
    } else if (isa<ObjCInterfaceType>(T))
      ++NumObjCInterfaces;
    else if (isa<ObjCQualifiedInterfaceType>(T))
      ++NumObjCQualifiedInterfaces;
    else if (isa<ObjCQualifiedIdType>(T))
      ++NumObjCQualifiedIds;
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
  fprintf(stderr, "Total bytes = %d\n", int(NumBuiltin*sizeof(BuiltinType)+
    NumPointer*sizeof(PointerType)+NumArray*sizeof(ArrayType)+
    NumComplex*sizeof(ComplexType)+NumVector*sizeof(VectorType)+
    NumFunctionP*sizeof(FunctionTypeProto)+
    NumFunctionNP*sizeof(FunctionTypeNoProto)+
    NumTypeName*sizeof(TypedefType)+NumTagged*sizeof(TagType)));
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
  if (Target.isCharSigned(FullSourceLoc()))
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

/// getTypeSize - Return the size of the specified type, in bits.  This method
/// does not work on incomplete types.
std::pair<uint64_t, unsigned>
ASTContext::getTypeInfo(QualType T, SourceLocation L) {
  T = T.getCanonicalType();
  uint64_t Size;
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
    
    std::pair<uint64_t, unsigned> EltInfo = 
      getTypeInfo(CAT->getElementType(), L);
    Size = EltInfo.first*CAT->getSize().getZExtValue();
    Align = EltInfo.second;
    break;
  }
  case Type::OCUVector:
  case Type::Vector: {
    std::pair<uint64_t, unsigned> EltInfo = 
      getTypeInfo(cast<VectorType>(T)->getElementType(), L);
    Size = EltInfo.first*cast<VectorType>(T)->getNumElements();
    // FIXME: Vector alignment is not the alignment of its elements.
    Align = EltInfo.second;
    break;
  }

  case Type::Builtin: {
    // FIXME: need to use TargetInfo to derive the target specific sizes. This
    // implementation will suffice for play with vector support.
    const llvm::fltSemantics *F;
    switch (cast<BuiltinType>(T)->getKind()) {
    default: assert(0 && "Unknown builtin type!");
    case BuiltinType::Void:
      assert(0 && "Incomplete types have no size!");
    case BuiltinType::Bool:
      Target.getBoolInfo(Size, Align, getFullLoc(L));
      break;
    case BuiltinType::Char_S:
    case BuiltinType::Char_U:
    case BuiltinType::UChar:
    case BuiltinType::SChar:
      Target.getCharInfo(Size, Align, getFullLoc(L));
      break;
    case BuiltinType::UShort:
    case BuiltinType::Short:
      Target.getShortInfo(Size, Align, getFullLoc(L));
      break;
    case BuiltinType::UInt:
    case BuiltinType::Int:
      Target.getIntInfo(Size, Align, getFullLoc(L));
      break;
    case BuiltinType::ULong:
    case BuiltinType::Long:
      Target.getLongInfo(Size, Align, getFullLoc(L));
      break;
    case BuiltinType::ULongLong:
    case BuiltinType::LongLong:
      Target.getLongLongInfo(Size, Align, getFullLoc(L));
      break;
    case BuiltinType::Float:
      Target.getFloatInfo(Size, Align, F, getFullLoc(L));
      break;
    case BuiltinType::Double:
      Target.getDoubleInfo(Size, Align, F, getFullLoc(L));
      break;
    case BuiltinType::LongDouble:
      Target.getLongDoubleInfo(Size, Align, F, getFullLoc(L));
      break;
    }
    break;
  }
  case Type::ASQual:
    return getTypeInfo(cast<ASQualType>(T)->getBaseType(), L);
  case Type::ObjCQualifiedId:
    Target.getPointerInfo(Size, Align, getFullLoc(L));
    break;
  case Type::Pointer:
    Target.getPointerInfo(Size, Align, getFullLoc(L));
    break;
  case Type::Reference:
    // "When applied to a reference or a reference type, the result is the size
    // of the referenced type." C++98 5.3.3p2: expr.sizeof.
    // FIXME: This is wrong for struct layout: a reference in a struct has
    // pointer size.
    return getTypeInfo(cast<ReferenceType>(T)->getReferenceeType(), L);
    
  case Type::Complex: {
    // Complex types have the same alignment as their elements, but twice the
    // size.
    std::pair<uint64_t, unsigned> EltInfo = 
      getTypeInfo(cast<ComplexType>(T)->getElementType(), L);
    Size = EltInfo.first*2;
    Align = EltInfo.second;
    break;
  }
  case Type::Tagged:
    TagType *TT = cast<TagType>(T);
    if (RecordType *RT = dyn_cast<RecordType>(TT)) {
      const ASTRecordLayout &Layout = getASTRecordLayout(RT->getDecl(), L);
      Size = Layout.getSize();
      Align = Layout.getAlignment();
    } else if (EnumDecl *ED = dyn_cast<EnumDecl>(TT->getDecl())) {
      return getTypeInfo(ED->getIntegerType(), L);
    } else {
      assert(0 && "Unimplemented type sizes!");
    }
    break;
  }
  
  assert(Align && (Align & (Align-1)) == 0 && "Alignment must be power of 2");
  return std::make_pair(Size, Align);
}

/// getASTRecordLayout - Get or compute information about the layout of the
/// specified record (struct/union/class), which indicates its size and field
/// position information.
const ASTRecordLayout &ASTContext::getASTRecordLayout(const RecordDecl *D,
                                                      SourceLocation L) {
  assert(D->isDefinition() && "Cannot get layout of forward declarations!");
  
  // Look up this layout, if already laid out, return what we have.
  const ASTRecordLayout *&Entry = ASTRecordLayouts[D];
  if (Entry) return *Entry;
  
  // Allocate and assign into ASTRecordLayouts here.  The "Entry" reference can
  // be invalidated (dangle) if the ASTRecordLayouts hashtable is inserted into.
  ASTRecordLayout *NewEntry = new ASTRecordLayout();
  Entry = NewEntry;
  
  uint64_t *FieldOffsets = new uint64_t[D->getNumMembers()];
  uint64_t RecordSize = 0;
  unsigned RecordAlign = 8;  // Default alignment = 1 byte = 8 bits.

  if (D->getKind() != Decl::Union) {
    // Layout each field, for now, just sequentially, respecting alignment.  In
    // the future, this will need to be tweakable by targets.
    for (unsigned i = 0, e = D->getNumMembers(); i != e; ++i) {
      const FieldDecl *FD = D->getMember(i);
      uint64_t FieldSize;
      unsigned FieldAlign;
      if (FD->getType()->isIncompleteType()) {
        // This must be a flexible array member; we can't directly
        // query getTypeInfo about these, so we figure it out here.
        // Flexible array members don't have any size, but they
        // have to be aligned appropriately for their element type.
        const ArrayType* ATy = FD->getType()->getAsArrayType();
        FieldAlign = getTypeAlign(ATy->getElementType(), L);
        FieldSize = 0;
      } else {
        std::pair<uint64_t, unsigned> FieldInfo = getTypeInfo(FD->getType(), L);
        FieldSize = FieldInfo.first;
        FieldAlign = FieldInfo.second;
      }

      // Round up the current record size to the field's alignment boundary.
      RecordSize = (RecordSize+FieldAlign-1) & ~(FieldAlign-1);
      
      // Place this field at the current location.
      FieldOffsets[i] = RecordSize;
      
      // Reserve space for this field.
      RecordSize += FieldSize;
      
      // Remember max struct/class alignment.
      RecordAlign = std::max(RecordAlign, FieldAlign);
    }
    
    // Finally, round the size of the total struct up to the alignment of the
    // struct itself.
    RecordSize = (RecordSize+RecordAlign-1) & ~(RecordAlign-1);
  } else {
    // Union layout just puts each member at the start of the record.
    for (unsigned i = 0, e = D->getNumMembers(); i != e; ++i) {
      const FieldDecl *FD = D->getMember(i);
      std::pair<uint64_t, unsigned> FieldInfo = getTypeInfo(FD->getType(), L);
      uint64_t FieldSize = FieldInfo.first;
      unsigned FieldAlign = FieldInfo.second;
      
      // Round up the current record size to the field's alignment boundary.
      RecordSize = std::max(RecordSize, FieldSize);
      
      // Place this field at the start of the record.
      FieldOffsets[i] = 0;
      
      // Remember max struct/class alignment.
      RecordAlign = std::max(RecordAlign, FieldAlign);
    }
  }
  
  NewEntry->SetLayout(RecordSize, RecordAlign, FieldOffsets);
  return *NewEntry;
}

//===----------------------------------------------------------------------===//
//                   Type creation/memoization methods
//===----------------------------------------------------------------------===//

QualType ASTContext::getASQualType(QualType T, unsigned AddressSpace) {
  // Check if we've already instantiated an address space qual'd type of this type.
  llvm::FoldingSetNodeID ID;
  ASQualType::Profile(ID, T, AddressSpace);      
  void *InsertPos = 0;
  if (ASQualType *ASQy = ASQualTypes.FindNodeOrInsertPos(ID, InsertPos))
    return QualType(ASQy, 0);
    
  // If the base type isn't canonical, this won't be a canonical type either,
  // so fill in the canonical type field.
  QualType Canonical;
  if (!T->isCanonical()) {
    Canonical = getASQualType(T.getCanonicalType(), AddressSpace);
    
    // Get the new insert position for the node we care about.
    ASQualType *NewIP = ASQualTypes.FindNodeOrInsertPos(ID, InsertPos);
    assert(NewIP == 0 && "Shouldn't be in the map!");
  }
  ASQualType *New = new ASQualType(T, Canonical, AddressSpace);
  ASQualTypes.InsertNode(New, InsertPos);
  Types.push_back(New);
  return QualType(New, 0);
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
    Canonical = getComplexType(T.getCanonicalType());
    
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
    Canonical = getPointerType(T.getCanonicalType());
   
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
    Canonical = getReferenceType(T.getCanonicalType());
   
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
    Canonical = getConstantArrayType(EltTy.getCanonicalType(), ArySize, 
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
  if (NumElts) {
    // Since we don't unique expressions, it isn't possible to unique VLA's
    // that have an expression provided for their size.
    
    VariableArrayType *New = new VariableArrayType(EltTy, QualType(), NumElts, 
                                                   ASM, EltTypeQuals);
    
    CompleteVariableArrayTypes.push_back(New);
    Types.push_back(New);
    return QualType(New, 0);
  }
  else {
    // No size is provided for the VLA.  These we can unique.
    llvm::FoldingSetNodeID ID;
    VariableArrayType::Profile(ID, EltTy);
    
    void *InsertPos = 0;
    if (VariableArrayType *ATP = 
         IncompleteVariableArrayTypes.FindNodeOrInsertPos(ID, InsertPos))
      return QualType(ATP, 0);
    
    // If the element type isn't canonical, this won't be a canonical type
    // either, so fill in the canonical type field.
    QualType Canonical;
    
    if (!EltTy->isCanonical()) {
      Canonical = getVariableArrayType(EltTy.getCanonicalType(), NumElts,
                                       ASM, EltTypeQuals);
      
      // Get the new insert position for the node we care about.
      VariableArrayType *NewIP =
        IncompleteVariableArrayTypes.FindNodeOrInsertPos(ID, InsertPos);
      
      assert(NewIP == 0 && "Shouldn't be in the map!");
    }
    
    VariableArrayType *New = new VariableArrayType(EltTy, QualType(), NumElts, 
                                                   ASM, EltTypeQuals);
    
    IncompleteVariableArrayTypes.InsertNode(New, InsertPos);
    Types.push_back(New);
    return QualType(New, 0);            
  }
}

/// getVectorType - Return the unique reference to a vector type of
/// the specified element type and size. VectorType must be a built-in type.
QualType ASTContext::getVectorType(QualType vecType, unsigned NumElts) {
  BuiltinType *baseType;
  
  baseType = dyn_cast<BuiltinType>(vecType.getCanonicalType().getTypePtr());
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
    Canonical = getVectorType(vecType.getCanonicalType(), NumElts);
    
    // Get the new insert position for the node we care about.
    VectorType *NewIP = VectorTypes.FindNodeOrInsertPos(ID, InsertPos);
    assert(NewIP == 0 && "Shouldn't be in the map!");
  }
  VectorType *New = new VectorType(vecType, NumElts, Canonical);
  VectorTypes.InsertNode(New, InsertPos);
  Types.push_back(New);
  return QualType(New, 0);
}

/// getOCUVectorType - Return the unique reference to an OCU vector type of
/// the specified element type and size. VectorType must be a built-in type.
QualType ASTContext::getOCUVectorType(QualType vecType, unsigned NumElts) {
  BuiltinType *baseType;
  
  baseType = dyn_cast<BuiltinType>(vecType.getCanonicalType().getTypePtr());
  assert(baseType != 0 && "getOCUVectorType(): Expecting a built-in type");
         
  // Check if we've already instantiated a vector of this type.
  llvm::FoldingSetNodeID ID;
  VectorType::Profile(ID, vecType, NumElts, Type::OCUVector);      
  void *InsertPos = 0;
  if (VectorType *VTP = VectorTypes.FindNodeOrInsertPos(ID, InsertPos))
    return QualType(VTP, 0);

  // If the element type isn't canonical, this won't be a canonical type either,
  // so fill in the canonical type field.
  QualType Canonical;
  if (!vecType->isCanonical()) {
    Canonical = getOCUVectorType(vecType.getCanonicalType(), NumElts);
    
    // Get the new insert position for the node we care about.
    VectorType *NewIP = VectorTypes.FindNodeOrInsertPos(ID, InsertPos);
    assert(NewIP == 0 && "Shouldn't be in the map!");
  }
  OCUVectorType *New = new OCUVectorType(vecType, NumElts, Canonical);
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
    Canonical = getFunctionTypeNoProto(ResultTy.getCanonicalType());
    
    // Get the new insert position for the node we care about.
    FunctionTypeNoProto *NewIP =
      FunctionTypeNoProtos.FindNodeOrInsertPos(ID, InsertPos);
    assert(NewIP == 0 && "Shouldn't be in the map!");
  }
  
  FunctionTypeNoProto *New = new FunctionTypeNoProto(ResultTy, Canonical);
  Types.push_back(New);
  FunctionTypeProtos.InsertNode(New, InsertPos);
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
      CanonicalArgs.push_back(ArgArray[i].getCanonicalType());
    
    Canonical = getFunctionType(ResultTy.getCanonicalType(),
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

/// getTypedefType - Return the unique reference to the type for the
/// specified typename decl.
QualType ASTContext::getTypedefType(TypedefDecl *Decl) {
  if (Decl->TypeForDecl) return QualType(Decl->TypeForDecl, 0);
  
  QualType Canonical = Decl->getUnderlyingType().getCanonicalType();
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

/// getObjCQualifiedInterfaceType - Return a 
/// ObjCQualifiedInterfaceType type for the given interface decl and
/// the conforming protocol list.
QualType ASTContext::getObjCQualifiedInterfaceType(ObjCInterfaceDecl *Decl,
                       ObjCProtocolDecl **Protocols, unsigned NumProtocols) {
  llvm::FoldingSetNodeID ID;
  ObjCQualifiedInterfaceType::Profile(ID, Protocols, NumProtocols);
  
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

/// getObjCQualifiedIdType - Return a 
/// getObjCQualifiedIdType type for the 'id' decl and
/// the conforming protocol list.
QualType ASTContext::getObjCQualifiedIdType(QualType idType,
                                            ObjCProtocolDecl **Protocols, 
                                            unsigned NumProtocols) {
  llvm::FoldingSetNodeID ID;
  ObjCQualifiedIdType::Profile(ID, Protocols, NumProtocols);
  
  void *InsertPos = 0;
  if (ObjCQualifiedIdType *QT =
      ObjCQualifiedIdTypes.FindNodeOrInsertPos(ID, InsertPos))
    return QualType(QT, 0);
  
  // No Match;
  QualType Canonical;
  if (!idType->isCanonical()) {
    Canonical = getObjCQualifiedIdType(idType.getCanonicalType(), 
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
  QualType Canonical = tofExpr->getType().getCanonicalType();
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
  QualType Canonical = tofType.getCanonicalType();
  TypeOfType *tot = new TypeOfType(tofType, Canonical);
  Types.push_back(tot);
  return QualType(tot, 0);
}

/// getTagDeclType - Return the unique reference to the type for the
/// specified TagDecl (struct/union/class/enum) decl.
QualType ASTContext::getTagDeclType(TagDecl *Decl) {
  assert (Decl);

  // The decl stores the type cache.
  if (Decl->TypeForDecl) return QualType(Decl->TypeForDecl, 0);
  
  TagType* T = new TagType(Decl, QualType());
  Types.push_back(T);  
  Decl->TypeForDecl = T;

  return QualType(T, 0);
}

/// getSizeType - Return the unique type for "size_t" (C99 7.17), the result 
/// of the sizeof operator (C99 6.5.3.4p4). The value is target dependent and 
/// needs to agree with the definition in <stddef.h>. 
QualType ASTContext::getSizeType() const {
  // On Darwin, size_t is defined as a "long unsigned int". 
  // FIXME: should derive from "Target".
  return UnsignedLongTy; 
}

/// getPointerDiffType - Return the unique type for "ptrdiff_t" (ref?)
/// defined in <stddef.h>. Pointer - pointer requires this (C99 6.5.6p9).
QualType ASTContext::getPointerDiffType() const {
  // On Darwin, ptrdiff_t is defined as a "int". This seems like a bug...
  // FIXME: should derive from "Target".
  return IntTy; 
}

/// getIntegerRank - Return an integer conversion rank (C99 6.3.1.1p1). This
/// routine will assert if passed a built-in type that isn't an integer or enum.
static int getIntegerRank(QualType t) {
  if (const TagType *TT = dyn_cast<TagType>(t.getCanonicalType())) {
    assert(TT->getDecl()->getKind() == Decl::Enum && "not an int or enum");
    return 4;
  }
  
  const BuiltinType *BT = t.getCanonicalType()->getAsBuiltinType();
  switch (BT->getKind()) {
  default:
    assert(0 && "getIntegerRank(): not a built-in integer");
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

/// getFloatingRank - Return a relative rank for floating point types.
/// This routine will assert if passed a built-in type that isn't a float.
static int getFloatingRank(QualType T) {
  T = T.getCanonicalType();
  if (const ComplexType *CT = T->getAsComplexType())
    return getFloatingRank(CT->getElementType());
  
  switch (T->getAsBuiltinType()->getKind()) {
  default:  assert(0 && "getFloatingRank(): not a floating type");
  case BuiltinType::Float:      return FloatRank;
  case BuiltinType::Double:     return DoubleRank;
  case BuiltinType::LongDouble: return LongDoubleRank;
  }
}

/// getFloatingTypeOfSizeWithinDomain - Returns a real floating 
/// point or a complex type (based on typeDomain/typeSize). 
/// 'typeDomain' is a real floating point or complex type.
/// 'typeSize' is a real floating point or complex type.
QualType ASTContext::getFloatingTypeOfSizeWithinDomain(
  QualType typeSize, QualType typeDomain) const {
  if (typeDomain->isComplexType()) {
    switch (getFloatingRank(typeSize)) {
    default: assert(0 && "getFloatingRank(): illegal value for rank");
    case FloatRank:      return FloatComplexTy;
    case DoubleRank:     return DoubleComplexTy;
    case LongDoubleRank: return LongDoubleComplexTy;
    }
  }
  if (typeDomain->isRealFloatingType()) {
    switch (getFloatingRank(typeSize)) {
    default: assert(0 && "getFloatingRank(): illegal value for rank");
    case FloatRank:      return FloatTy;
    case DoubleRank:     return DoubleTy;
    case LongDoubleRank: return LongDoubleTy;
    }
  }
  assert(0 && "getFloatingTypeOfSizeWithinDomain(): illegal domain");
  //an invalid return value, but the assert
  //will ensure that this code is never reached.
  return VoidTy;
}

/// compareFloatingType - Handles 3 different combos: 
/// float/float, float/complex, complex/complex. 
/// If lt > rt, return 1. If lt == rt, return 0. If lt < rt, return -1. 
int ASTContext::compareFloatingType(QualType lt, QualType rt) {
  if (getFloatingRank(lt) == getFloatingRank(rt))
    return 0;
  if (getFloatingRank(lt) > getFloatingRank(rt))
    return 1;
  return -1;
}

// maxIntegerType - Returns the highest ranked integer type. Handles 3 case:
// unsigned/unsigned, signed/signed, signed/unsigned. C99 6.3.1.8p1.
QualType ASTContext::maxIntegerType(QualType lhs, QualType rhs) {
  if (lhs == rhs) return lhs;
  
  bool t1Unsigned = lhs->isUnsignedIntegerType();
  bool t2Unsigned = rhs->isUnsignedIntegerType();
  
  if ((t1Unsigned && t2Unsigned) || (!t1Unsigned && !t2Unsigned))
    return getIntegerRank(lhs) >= getIntegerRank(rhs) ? lhs : rhs; 
  
  // We have two integer types with differing signs
  QualType unsignedType = t1Unsigned ? lhs : rhs;
  QualType signedType = t1Unsigned ? rhs : lhs;
  
  if (getIntegerRank(unsignedType) >= getIntegerRank(signedType))
    return unsignedType;
  else {
    // FIXME: Need to check if the signed type can represent all values of the 
    // unsigned type. If it can, then the result is the signed type. 
    // If it can't, then the result is the unsigned version of the signed type.  
    // Should probably add a helper that returns a signed integer type from 
    // an unsigned (and vice versa). C99 6.3.1.8.
    return signedType; 
  }
}

// getCFConstantStringType - Return the type used for constant CFStrings. 
QualType ASTContext::getCFConstantStringType() {
  if (!CFConstantStringTypeDecl) {
    CFConstantStringTypeDecl = new RecordDecl(Decl::Struct, SourceLocation(), 
                                              &Idents.get("NSConstantString"),
                                              0);
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
      FieldDecls[i] = new FieldDecl(SourceLocation(), 0, FieldTypes[i]);
  
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
  SourceLocation Loc;
  uint64_t sz = getTypeSize(type, Loc);
  
  // Make all integer and enum types at least as large as an int
  if (sz > 0 && type->isIntegralType())
    sz = std::max(sz, getTypeSize(IntTy, Loc));
  // Treat arrays as pointers, since that's how they're passed in.
  else if (type->isArrayType())
    sz = getTypeSize(VoidPtrTy, Loc);
  return sz / getTypeSize(CharTy, Loc);
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
  int PtrSize = getTypeSize(VoidPtrTy, Loc) / getTypeSize(CharTy, Loc);
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
    case BuiltinType::Void:
      encoding = 'v';
      break;
    case BuiltinType::Bool:
      encoding = 'B';
      break;
    case BuiltinType::Char_U:
    case BuiltinType::UChar:
      encoding = 'C';
      break;
    case BuiltinType::UShort:
      encoding = 'S';
      break;
    case BuiltinType::UInt:
      encoding = 'I';
      break;
    case BuiltinType::ULong:
      encoding = 'L';
      break;
    case BuiltinType::ULongLong:
      encoding = 'Q';
      break;
    case BuiltinType::Char_S:
    case BuiltinType::SChar:
      encoding = 'c';
      break;
    case BuiltinType::Short:
      encoding = 's';
      break;
    case BuiltinType::Int:
      encoding = 'i';
      break;
    case BuiltinType::Long:
      encoding = 'l';
      break;
    case BuiltinType::LongLong:
      encoding = 'q';
      break;
    case BuiltinType::Float:
      encoding = 'f';
      break;
    case BuiltinType::Double:
      encoding = 'd';
      break;
    case BuiltinType::LongDouble:
      encoding = 'd';
      break;
    default:
      assert(0 && "Unhandled builtin type kind");          
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

bool ASTContext::builtinTypesAreCompatible(QualType lhs, QualType rhs) {
  const BuiltinType *lBuiltin = lhs->getAsBuiltinType();
  const BuiltinType *rBuiltin = rhs->getAsBuiltinType();
  
  return lBuiltin->getKind() == rBuiltin->getKind();
}

/// objcTypesAreCompatible - This routine is called when two types
/// are of different class; one is interface type or is 
/// a qualified interface type and the other type is of a different class.
/// Example, II or II<P>. 
bool ASTContext::objcTypesAreCompatible(QualType lhs, QualType rhs) {
  if (lhs->isObjCInterfaceType() && isObjCIdType(rhs))
    return true;
  else if (isObjCIdType(lhs) && rhs->isObjCInterfaceType())
    return true;
  if (ObjCInterfaceType *lhsIT = 
      dyn_cast<ObjCInterfaceType>(lhs.getCanonicalType().getTypePtr())) {
    ObjCQualifiedInterfaceType *rhsQI = 
      dyn_cast<ObjCQualifiedInterfaceType>(rhs.getCanonicalType().getTypePtr());
    return rhsQI && (lhsIT->getDecl() == rhsQI->getDecl());
  }
  else if (ObjCInterfaceType *rhsIT = 
           dyn_cast<ObjCInterfaceType>(rhs.getCanonicalType().getTypePtr())) {
    ObjCQualifiedInterfaceType *lhsQI = 
    dyn_cast<ObjCQualifiedInterfaceType>(lhs.getCanonicalType().getTypePtr());
    return lhsQI && (rhsIT->getDecl() == lhsQI->getDecl());
  }
  return false;
}

/// Check that 'lhs' and 'rhs' are compatible interface types. Both types
/// must be canonical types.
bool ASTContext::interfaceTypesAreCompatible(QualType lhs, QualType rhs) {
  assert (lhs->isCanonical() &&
          "interfaceTypesAreCompatible strip typedefs of lhs");
  assert (rhs->isCanonical() &&
          "interfaceTypesAreCompatible strip typedefs of rhs");
  if (lhs == rhs)
    return true;
  ObjCInterfaceType *lhsIT = cast<ObjCInterfaceType>(lhs.getTypePtr());
  ObjCInterfaceType *rhsIT = cast<ObjCInterfaceType>(rhs.getTypePtr());
  ObjCInterfaceDecl *rhsIDecl = rhsIT->getDecl();
  ObjCInterfaceDecl *lhsIDecl = lhsIT->getDecl();
  // rhs is derived from lhs it is OK; else it is not OK.
  while (rhsIDecl != NULL) {
    if (rhsIDecl == lhsIDecl)
      return true;
    rhsIDecl = rhsIDecl->getSuperClass();
  }
  return false;
}

bool ASTContext::QualifiedInterfaceTypesAreCompatible(QualType lhs, 
                                                      QualType rhs) {
  ObjCQualifiedInterfaceType *lhsQI = 
    dyn_cast<ObjCQualifiedInterfaceType>(lhs.getCanonicalType().getTypePtr());
  assert(lhsQI && "QualifiedInterfaceTypesAreCompatible - bad lhs type");
  ObjCQualifiedInterfaceType *rhsQI = 
    dyn_cast<ObjCQualifiedInterfaceType>(rhs.getCanonicalType().getTypePtr());
  assert(rhsQI && "QualifiedInterfaceTypesAreCompatible - bad rhs type");
  if (!interfaceTypesAreCompatible(
         getObjCInterfaceType(lhsQI->getDecl()).getCanonicalType(), 
         getObjCInterfaceType(rhsQI->getDecl()).getCanonicalType()))
    return false;
  /* All protocols in lhs must have a presense in rhs. */
  for (unsigned i =0; i < lhsQI->getNumProtocols(); i++) {
    bool match = false;
    ObjCProtocolDecl *lhsProto = lhsQI->getProtocols(i);
    for (unsigned j = 0; j < rhsQI->getNumProtocols(); j++) {
      ObjCProtocolDecl *rhsProto = rhsQI->getProtocols(j);
      if (lhsProto == rhsProto) {
        match = true;
        break;
      }
    }
    if (!match)
      return false;
  }
  return true;
}

/// ProtocolCompatibleWithProtocol - return 'true' if 'lProto' is in the
/// inheritance hierarchy of 'rProto'.
static bool ProtocolCompatibleWithProtocol(ObjCProtocolDecl *lProto,
                                           ObjCProtocolDecl *rProto) {
  if (lProto == rProto)
    return true;
  ObjCProtocolDecl** RefPDecl = rProto->getReferencedProtocols();
  for (unsigned i = 0; i < rProto->getNumReferencedProtocols(); i++)
    if (ProtocolCompatibleWithProtocol(lProto, RefPDecl[i]))
      return true;
  return false;
}

/// ClassImplementsProtocol - Checks that 'lProto' protocol
/// has been implemented in IDecl class, its super class or categories (if
/// lookupCategory is true). 
static bool ClassImplementsProtocol(ObjCProtocolDecl *lProto,
                                    ObjCInterfaceDecl *IDecl, 
                                    bool lookupCategory) {
  
  // 1st, look up the class.
  ObjCProtocolDecl **protoList = IDecl->getReferencedProtocols();
  for (unsigned i = 0; i < IDecl->getNumIntfRefProtocols(); i++) {
    if (ProtocolCompatibleWithProtocol(lProto, protoList[i]))
      return true;
  }
  
  // 2nd, look up the category.
  if (lookupCategory)
    for (ObjCCategoryDecl *CDecl = IDecl->getCategoryList(); CDecl;
         CDecl = CDecl->getNextClassCategory()) {
      protoList = CDecl->getReferencedProtocols();
      for (unsigned i = 0; i < CDecl->getNumReferencedProtocols(); i++) {
        if (ProtocolCompatibleWithProtocol(lProto, protoList[i]))
          return true;
      }
    }
  
  // 3rd, look up the super class(s)
  if (IDecl->getSuperClass())
    return 
      ClassImplementsProtocol(lProto, IDecl->getSuperClass(), lookupCategory);
  
  return false;
}
                                           
/// ObjCQualifiedIdTypesAreCompatible - Compares two types, at least
/// one of which is a protocol qualified 'id' type. When 'compare'
/// is true it is for comparison; when false, for assignment/initialization.
bool ASTContext::ObjCQualifiedIdTypesAreCompatible(QualType lhs, 
                                                   QualType rhs,
                                                   bool compare) {
  // match id<P..> with an 'id' type in all cases.
  if (const PointerType *PT = lhs->getAsPointerType()) {
    QualType PointeeTy = PT->getPointeeType();
    if (isObjCIdType(PointeeTy) || PointeeTy->isVoidType())
      return true;
        
  }
  else if (const PointerType *PT = rhs->getAsPointerType()) {
    QualType PointeeTy = PT->getPointeeType();
    if (isObjCIdType(PointeeTy) || PointeeTy->isVoidType())
      return true;
    
  }
  
  ObjCQualifiedInterfaceType *lhsQI = 0;
  ObjCQualifiedInterfaceType *rhsQI = 0;
  ObjCInterfaceDecl *lhsID = 0;
  ObjCInterfaceDecl *rhsID = 0;
  ObjCQualifiedIdType *lhsQID = dyn_cast<ObjCQualifiedIdType>(lhs);
  ObjCQualifiedIdType *rhsQID = dyn_cast<ObjCQualifiedIdType>(rhs);
  
  if (lhsQID) {
    if (!rhsQID && rhs->getTypeClass() == Type::Pointer) {
      QualType rtype = 
        cast<PointerType>(rhs.getCanonicalType())->getPointeeType();
      rhsQI = 
        dyn_cast<ObjCQualifiedInterfaceType>(
          rtype.getCanonicalType().getTypePtr());
      if (!rhsQI) {
        ObjCInterfaceType *IT = dyn_cast<ObjCInterfaceType>(
                                  rtype.getCanonicalType().getTypePtr());
        if (IT)
          rhsID = IT->getDecl();
      }
    }
    if (!rhsQI && !rhsQID && !rhsID)
      return false;
    
    unsigned numRhsProtocols = 0;
    ObjCProtocolDecl **rhsProtoList = 0;
    if (rhsQI) {
      numRhsProtocols = rhsQI->getNumProtocols();
      rhsProtoList = rhsQI->getReferencedProtocols();
    }
    else if (rhsQID) {
      numRhsProtocols = rhsQID->getNumProtocols();
      rhsProtoList = rhsQID->getReferencedProtocols();
    }
    
    for (unsigned i =0; i < lhsQID->getNumProtocols(); i++) {
      ObjCProtocolDecl *lhsProto = lhsQID->getProtocols(i);
      bool match = false;

      // when comparing an id<P> on lhs with a static type on rhs,
      // see if static class implements all of id's protocols, directly or
      // through its super class and categories.
      if (rhsID) {
        if (ClassImplementsProtocol(lhsProto, rhsID, true))
          match = true;
      }
      else for (unsigned j = 0; j < numRhsProtocols; j++) {
        ObjCProtocolDecl *rhsProto = rhsProtoList[j];
        if (ProtocolCompatibleWithProtocol(lhsProto, rhsProto) ||
            compare && ProtocolCompatibleWithProtocol(rhsProto, lhsProto)) {
          match = true;
          break;
        }
      }
      if (!match)
        return false;
    }    
  }
  else if (rhsQID) {
    if (!lhsQID && lhs->getTypeClass() == Type::Pointer) {
      QualType ltype = 
      cast<PointerType>(lhs.getCanonicalType())->getPointeeType();
      lhsQI = 
      dyn_cast<ObjCQualifiedInterfaceType>(
        ltype.getCanonicalType().getTypePtr());
      if (!lhsQI) {
        ObjCInterfaceType *IT = dyn_cast<ObjCInterfaceType>(
                                  ltype.getCanonicalType().getTypePtr());
        if (IT)
          lhsID = IT->getDecl();
      }
    }
    if (!lhsQI && !lhsQID && !lhsID)
      return false;
    
    unsigned numLhsProtocols = 0;
    ObjCProtocolDecl **lhsProtoList = 0;
    if (lhsQI) {
      numLhsProtocols = lhsQI->getNumProtocols();
      lhsProtoList = lhsQI->getReferencedProtocols();
    }
    else if (lhsQID) {
      numLhsProtocols = lhsQID->getNumProtocols();
      lhsProtoList = lhsQID->getReferencedProtocols();
    }    
    bool match = false;
    // for static type vs. qualified 'id' type, check that class implements
    // one of 'id's protocols.
    if (lhsID) {
      for (unsigned j = 0; j < rhsQID->getNumProtocols(); j++) {
        ObjCProtocolDecl *rhsProto = rhsQID->getProtocols(j);
        if (ClassImplementsProtocol(rhsProto, lhsID, compare)) {
          match = true;
          break;
        }
      }
    }    
    else for (unsigned i =0; i < numLhsProtocols; i++) {
      match = false;
      ObjCProtocolDecl *lhsProto = lhsProtoList[i];
      for (unsigned j = 0; j < rhsQID->getNumProtocols(); j++) {
        ObjCProtocolDecl *rhsProto = rhsQID->getProtocols(j);
        if (ProtocolCompatibleWithProtocol(lhsProto, rhsProto) ||
          compare && ProtocolCompatibleWithProtocol(rhsProto, lhsProto)) {
          match = true;
          break;
        }
      }
    }
    if (!match)
      return false;
  }
  return true;
}

bool ASTContext::vectorTypesAreCompatible(QualType lhs, QualType rhs) {
  const VectorType *lVector = lhs->getAsVectorType();
  const VectorType *rVector = rhs->getAsVectorType();
  
  if ((lVector->getElementType().getCanonicalType() ==
      rVector->getElementType().getCanonicalType()) &&
      (lVector->getNumElements() == rVector->getNumElements()))
    return true;
  return false;
}

// C99 6.2.7p1: If both are complete types, then the following additional
// requirements apply...FIXME (handle compatibility across source files).
bool ASTContext::tagTypesAreCompatible(QualType lhs, QualType rhs) {
  TagDecl *ldecl = cast<TagType>(lhs.getCanonicalType())->getDecl();
  TagDecl *rdecl = cast<TagType>(rhs.getCanonicalType())->getDecl();
  
  if (ldecl->getKind() == Decl::Struct && rdecl->getKind() == Decl::Struct) {
    if (ldecl->getIdentifier() == rdecl->getIdentifier())
      return true;
  }
  if (ldecl->getKind() == Decl::Union && rdecl->getKind() == Decl::Union) {
    if (ldecl->getIdentifier() == rdecl->getIdentifier())
      return true;
  }
  // "Class" and "id" are compatible built-in structure types.
  if (isObjCIdType(lhs) && isObjCClassType(rhs) ||
      isObjCClassType(lhs) && isObjCIdType(rhs))
    return true;
  return false;
}

bool ASTContext::pointerTypesAreCompatible(QualType lhs, QualType rhs) {
  // C99 6.7.5.1p2: For two pointer types to be compatible, both shall be 
  // identically qualified and both shall be pointers to compatible types.
  if (lhs.getQualifiers() != rhs.getQualifiers())
    return false;
    
  QualType ltype = cast<PointerType>(lhs.getCanonicalType())->getPointeeType();
  QualType rtype = cast<PointerType>(rhs.getCanonicalType())->getPointeeType();
  
  return typesAreCompatible(ltype, rtype);
}

// C++ 5.17p6: When the left operand of an assignment operator denotes a
// reference to T, the operation assigns to the object of type T denoted by the
// reference.
bool ASTContext::referenceTypesAreCompatible(QualType lhs, QualType rhs) {
  QualType ltype = lhs;

  if (lhs->isReferenceType())
    ltype = cast<ReferenceType>(lhs.getCanonicalType())->getReferenceeType();

  QualType rtype = rhs;

  if (rhs->isReferenceType())
    rtype = cast<ReferenceType>(rhs.getCanonicalType())->getReferenceeType();

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

bool ASTContext::arrayTypesAreCompatible(QualType lhs, QualType rhs) {
  // Compatible arrays must have compatible element types
  QualType ltype = lhs->getAsArrayType()->getElementType();
  QualType rtype = rhs->getAsArrayType()->getElementType();

  if (!typesAreCompatible(ltype, rtype))
    return false;

  // Compatible arrays must be the same size
  if (const ConstantArrayType* LCAT = lhs->getAsConstantArrayType())
    if (const ConstantArrayType* RCAT = rhs->getAsConstantArrayType())
      return RCAT->getSize() == LCAT->getSize();

  return true;
}

/// typesAreCompatible - C99 6.7.3p9: For two qualified types to be compatible, 
/// both shall have the identically qualified version of a compatible type.
/// C99 6.2.7p1: Two types have compatible types if their types are the 
/// same. See 6.7.[2,3,5] for additional rules.
bool ASTContext::typesAreCompatible(QualType lhs, QualType rhs) {
  if (lhs.getQualifiers() != rhs.getQualifiers())
    return false;

  QualType lcanon = lhs.getCanonicalType();
  QualType rcanon = rhs.getCanonicalType();

  // If two types are identical, they are are compatible
  if (lcanon == rcanon)
    return true;

  // C++ [expr]: If an expression initially has the type "reference to T", the
  // type is adjusted to "T" prior to any further analysis, the expression
  // designates the object or function denoted by the reference, and the
  // expression is an lvalue.
  if (ReferenceType *RT = dyn_cast<ReferenceType>(lcanon))
    lcanon = RT->getReferenceeType();
  if (ReferenceType *RT = dyn_cast<ReferenceType>(rcanon))
    rcanon = RT->getReferenceeType();
  
  Type::TypeClass LHSClass = lcanon->getTypeClass();
  Type::TypeClass RHSClass = rcanon->getTypeClass();
  
  // We want to consider the two function types to be the same for these
  // comparisons, just force one to the other.
  if (LHSClass == Type::FunctionProto) LHSClass = Type::FunctionNoProto;
  if (RHSClass == Type::FunctionProto) RHSClass = Type::FunctionNoProto;

  // Same as above for arrays
  if (LHSClass == Type::VariableArray) LHSClass = Type::ConstantArray;
  if (RHSClass == Type::VariableArray) RHSClass = Type::ConstantArray;
  
  // If the canonical type classes don't match...
  if (LHSClass != RHSClass) {
    // For Objective-C, it is possible for two types to be compatible
    // when their classes don't match (when dealing with "id"). If either type
    // is an interface, we defer to objcTypesAreCompatible(). 
    if (lcanon->isObjCInterfaceType() || rcanon->isObjCInterfaceType())
      return objcTypesAreCompatible(lcanon, rcanon);
      
    // C99 6.7.2.2p4: Each enumerated type shall be compatible with char,
    // a signed integer type, or an unsigned integer type. 
    // FIXME: need to check the size and ensure it's the same.
    if ((lcanon->isEnumeralType() && rcanon->isIntegralType()) ||
        (rcanon->isEnumeralType() && lcanon->isIntegralType()))
      return true;

    return false;
  }
  // The canonical type classes match.
  switch (LHSClass) {
  case Type::FunctionProto: assert(0 && "Canonicalized away above");
  case Type::Pointer:
    return pointerTypesAreCompatible(lcanon, rcanon);
  case Type::ConstantArray:
  case Type::VariableArray:
    return arrayTypesAreCompatible(lcanon, rcanon);
  case Type::FunctionNoProto:
    return functionTypesAreCompatible(lcanon, rcanon);
  case Type::Tagged: // handle structures, unions
    return tagTypesAreCompatible(lcanon, rcanon);
  case Type::Builtin:
    return builtinTypesAreCompatible(lcanon, rcanon); 
  case Type::ObjCInterface:
    return interfaceTypesAreCompatible(lcanon, rcanon); 
  case Type::Vector:
  case Type::OCUVector:
    return vectorTypesAreCompatible(lcanon, rcanon);
  case Type::ObjCQualifiedInterface:
    return QualifiedInterfaceTypesAreCompatible(lcanon, rcanon);
  default:
    assert(0 && "unexpected type");
  }
  return true; // should never get here...
}

/// Emit - Serialize an ASTContext object to Bitcode.
void ASTContext::Emit(llvm::Serializer& S) const {
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

  // FIXME: S.EmitOwnedPtr(CFConstantStringTypeDecl);
}

ASTContext* ASTContext::Create(llvm::Deserializer& D) {
  SourceManager &SM = D.ReadRef<SourceManager>();
  TargetInfo &t = D.ReadRef<TargetInfo>();
  IdentifierTable &idents = D.ReadRef<IdentifierTable>();
  SelectorTable &sels = D.ReadRef<SelectorTable>();
  
  unsigned size_reserve = D.ReadInt();
  
  ASTContext* A = new ASTContext(SM,t,idents,sels,size_reserve);
  
  for (unsigned i = 0; i < size_reserve; ++i)
    Type::Create(*A,i,D);

  // FIXME: A->CFConstantStringTypeDecl = D.ReadOwnedPtr<RecordDecl>();
  
  return A;
}
