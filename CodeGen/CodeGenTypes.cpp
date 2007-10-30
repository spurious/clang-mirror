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

using namespace clang;
using namespace CodeGen;

namespace {
  /// RecordOrganizer - This helper class, used by RecordLayoutInfo, layouts 
  /// structs and unions. It manages transient information used during layout.
  /// FIXME : At the moment assume 
  ///    - one to one mapping between AST FieldDecls and 
  ///      llvm::StructType elements.
  ///    - Ignore bit fields
  ///    - Ignore field aligments
  ///    - Ignore packed structs
  class RecordOrganizer {
  public:
    RecordOrganizer() : STy(NULL) {}
    
    /// addField - Add new field.
    void addField(const FieldDecl *FD);

    /// layoutStructFields - Do the actual work and lay out all fields. Create
    /// corresponding llvm struct type.  This should be invoked only after
    /// all fields are added.
    void layoutStructFields(CodeGenTypes &CGT);

    /// layoutUnionFields - Do the actual work and lay out all fields. Create
    /// corresponding llvm struct type.  This should be invoked only after
    /// all fields are added.
    void layoutUnionFields(CodeGenTypes &CGT);

    /// getLLVMType - Return associated llvm struct type. This may be NULL
    /// if fields are not laid out.
    llvm::Type *getLLVMType() const {
      return STy;
    }

    /// Clear private data so that this object can be reused.
    void clear();
  private:
    llvm::Type *STy;
    llvm::SmallVector<const FieldDecl *, 8> FieldDecls;
  };
}

CodeGenTypes::CodeGenTypes(ASTContext &Ctx, llvm::Module& M)
  : Context(Ctx), Target(Ctx.Target), TheModule(M) {
}

CodeGenTypes::~CodeGenTypes() {
  for(llvm::DenseMap<const llvm::Type *, RecordLayoutInfo *>::iterator
        I = RecordLayouts.begin(), E = RecordLayouts.end();
      I != E; ++I)
    delete I->second;
  RecordLayouts.clear();
}

/// ConvertType - Convert the specified type to its LLVM form.
const llvm::Type *CodeGenTypes::ConvertType(QualType T) {
  // See if type is already cached.
  llvm::DenseMap<Type *, llvm::PATypeHolder>::iterator
    I = TypeHolderMap.find(T.getTypePtr());
  if (I != TypeHolderMap.end())
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
    return llvm::PointerType::get(ConvertType(P.getPointeeType())); 
  }
  case Type::Reference: {
    const ReferenceType &R = cast<ReferenceType>(Ty);
    return llvm::PointerType::get(ConvertType(R.getReferenceeType()));
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
      const llvm::Type *RType = llvm::PointerType::get(ResultType);
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
    
    return llvm::FunctionType::get(ResultType, ArgTys, isVarArg, 0);
  }

  case Type::ObjcInterface:
    assert(0 && "FIXME: add missing functionality here");
    break;
      
  case Type::ObjcQualifiedInterface:
    assert(0 && "FIXME: add missing functionality here");
    break;

  case Type::Tagged:
    const TagType &TT = cast<TagType>(Ty);
    const TagDecl *TD = TT.getDecl();
    llvm::Type *&ResultType = TagDeclTypes[TD];
      
    if (ResultType)
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

      // Create new OpaqueType now for later use.
      // FIXME: This creates a lot of opaque types, most of them are not 
      // needed. Reevaluate this when performance analyis finds tons of 
      // opaque types.
      llvm::OpaqueType *OpaqueTy =  llvm::OpaqueType::get();
      RecordTypesToResolve[RD] = OpaqueTy;
      TypeHolderMap.insert(std::make_pair(T.getTypePtr(), 
                                          llvm::PATypeHolder(OpaqueTy)));

      // Layout fields.
      RecordOrganizer RO;
      for (unsigned i = 0, e = RD->getNumMembers(); i != e; ++i)
        RO.addField(RD->getMember(i));
      RO.layoutStructFields(*this);

      // Get llvm::StructType.
      RecordLayoutInfo *RLI = new RecordLayoutInfo(RO.getLLVMType());
      ResultType = RLI->getLLVMType();
      RecordLayouts[ResultType] = RLI;

      // Refine any OpaqueType associated with this RecordDecl.
      OpaqueTy->refineAbstractTypeTo(ResultType);
      OpaqueI = RecordTypesToResolve.find(RD);
      assert (OpaqueI != RecordTypesToResolve.end() 
              && "Expected RecordDecl in RecordTypesToResolve");
      RecordTypesToResolve.erase(OpaqueI);

      RO.clear();
    } else if (TD->getKind() == Decl::Union) {
      const RecordDecl *RD = cast<const RecordDecl>(TD);
      // Just use the largest element of the union, breaking ties with the
      // highest aligned member.

      if (RD->getNumMembers() != 0) {
        RecordOrganizer RO;
        for (unsigned i = 0, e = RD->getNumMembers(); i != e; ++i)
          RO.addField(RD->getMember(i));
        RO.layoutUnionFields(*this);

        // Get llvm::StructType.
        RecordLayoutInfo *RLI = new RecordLayoutInfo(RO.getLLVMType());
        ResultType = RLI->getLLVMType();
        RecordLayouts[ResultType] = RLI;
        RO.clear();
      } else {       
        std::vector<const llvm::Type*> Fields;
        ResultType = llvm::StructType::get(Fields);
      }
    } else {
      assert(0 && "FIXME: Implement tag decl kind!");
    }
          
    std::string TypeName(TD->getKindName());
    TypeName += '.';
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
      const llvm::Type *PtrTy = llvm::PointerType::get(Ty);
      TypeHolderMap.insert(std::make_pair(PTy.getTypePtr(), 
                                          llvm::PATypeHolder(PtrTy)));

      ArgTys.push_back(PtrTy);
    }
  }
}

/// getLLVMFieldNo - Return llvm::StructType element number
/// that corresponds to the field FD.
unsigned CodeGenTypes::getLLVMFieldNo(const FieldDecl *FD) {
  llvm::DenseMap<const FieldDecl *, unsigned>::iterator
    I = FieldInfo.find(FD);
  assert (I != FieldInfo.end()  && "Unable to find field info");
  return I->second;
}

/// addFieldInfo - Assign field number to field FD.
void CodeGenTypes::addFieldInfo(const FieldDecl *FD, unsigned No) {
  FieldInfo[FD] = No;
}

/// getRecordLayoutInfo - Return record layout info for the given llvm::Type.
const RecordLayoutInfo *
CodeGenTypes::getRecordLayoutInfo(const llvm::Type* Ty) const {
  llvm::DenseMap<const llvm::Type*, RecordLayoutInfo *>::iterator I
    = RecordLayouts.find(Ty);
  assert (I != RecordLayouts.end() 
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
void RecordOrganizer::layoutStructFields(CodeGenTypes &CGT) {
  // FIXME : Use SmallVector
  std::vector<const llvm::Type*> Fields;
  unsigned FieldNo = 0;
  for (llvm::SmallVector<const FieldDecl *, 8>::iterator I = FieldDecls.begin(),
         E = FieldDecls.end(); I != E; ++I) {
    const FieldDecl *FD = *I;
    const llvm::Type *Ty = CGT.ConvertType(FD->getType());

    // FIXME : Layout FieldDecl FD

    Fields.push_back(Ty);
    CGT.addFieldInfo(FD, FieldNo++);
  }
  STy = llvm::StructType::get(Fields);
}

/// layoutUnionFields - Do the actual work and lay out all fields. Create
/// corresponding llvm struct type.  This should be invoked only after
/// all fields are added.
void RecordOrganizer::layoutUnionFields(CodeGenTypes &CGT) {
 
  unsigned PrimaryEltNo = 0;
  std::pair<uint64_t, unsigned> PrimaryElt =
    CGT.getContext().getTypeInfo(FieldDecls[0]->getType(), SourceLocation());
  CGT.addFieldInfo(FieldDecls[0], 0);

  unsigned Size = FieldDecls.size();
  for(unsigned i = 1; i != Size; ++i) {
    const FieldDecl *FD = FieldDecls[i];
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
    CGT.addFieldInfo(FD, 0);
  }

  std::vector<const llvm::Type*> Fields;
  const llvm::Type *Ty = CGT.ConvertType(FieldDecls[PrimaryEltNo]->getType());
  Fields.push_back(Ty);
  STy = llvm::StructType::get(Fields);
}

/// Clear private data so that this object can be reused.
void RecordOrganizer::clear() {
  STy = NULL;
  FieldDecls.clear();
}
