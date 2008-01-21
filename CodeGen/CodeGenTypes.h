//===--- CodeGenTypes.h - Type translation for LLVM CodeGen -----*- C++ -*-===//
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

#ifndef CODEGEN_CODEGENTYPES_H
#define CODEGEN_CODEGENTYPES_H

#include "llvm/ADT/DenseMap.h"
#include <vector>

namespace llvm {
  class Module;
  class Type;
  class PATypeHolder;
  class TargetData;
}

namespace clang {
  class ASTContext;
  class TagDecl;
  class TargetInfo;
  class QualType;
  class Type;
  class FunctionTypeProto;
  class FieldDecl;
  class RecordDecl;

namespace CodeGen {
  class CodeGenTypes;

  /// CGRecordLayout - This class handles struct and union layout info while 
  /// lowering AST types to LLVM types.
  class CGRecordLayout {
    CGRecordLayout(); // DO NOT IMPLEMENT
  public:
    CGRecordLayout(llvm::Type *T) : STy(T) {
      // FIXME : Collect info about fields that requires adjustments 
      // (i.e. fields that do not directly map to llvm struct fields.)
    }

    /// getLLVMType - Return llvm type associated with this record.
    llvm::Type *getLLVMType() const {
      return STy;
    }

  private:
    llvm::Type *STy;
  };
  
/// CodeGenTypes - This class organizes the cross-module state that is used
/// while lowering AST types to LLVM types.
class CodeGenTypes {
  ASTContext &Context;
  TargetInfo &Target;
  llvm::Module& TheModule;
  const llvm::TargetData& TheTargetData;
  
  llvm::DenseMap<const TagDecl*, llvm::Type*> TagDeclTypes;

  /// CGRecordLayouts - This maps llvm struct type with corresponding 
  /// record layout info. 
  /// FIXME : If CGRecordLayout is less than 16 bytes then use 
  /// inline it in the map.
  llvm::DenseMap<const llvm::Type*, CGRecordLayout *> CGRecordLayouts;

  /// FieldInfo - This maps struct field with corresponding llvm struct type
  /// field no. This info is populated by record organizer.
  llvm::DenseMap<const FieldDecl *, unsigned> FieldInfo;

  class BitFieldInfo {
  public:
    explicit BitFieldInfo(unsigned B, unsigned S)
      : Begin(B), Size(S) {}

    unsigned Begin;
    unsigned Size;
  };
  llvm::DenseMap<const FieldDecl *, BitFieldInfo> BitFields;

  /// RecordTypesToResolve - This keeps track of record types that are not 
  /// yet incomplete. One llvm::OpaqueType is associated with each incomplete
  /// record.
  llvm::DenseMap<const RecordDecl *, llvm::Type *> RecordTypesToResolve;

  /// TypeHolderMap - This map keeps cache of llvm::Types (through PATypeHolder)
  /// and maps llvm::Types to corresponding clang::Type. llvm::PATypeHolder is
  /// used instead of llvm::Type because it allows us to bypass potential 
  /// dangling type pointers due to type refinement on llvm side.
  llvm::DenseMap<Type *, llvm::PATypeHolder> TypeHolderMap;

  /// ConvertNewType - Convert type T into a llvm::Type. Do not use this
  /// method directly because it does not do any type caching. This method
  /// is available only for ConvertType(). CovertType() is preferred
  /// interface to convert type T into a llvm::Type.
  const llvm::Type *ConvertNewType(QualType T);
public:
  CodeGenTypes(ASTContext &Ctx, llvm::Module &M, const llvm::TargetData &TD);
  ~CodeGenTypes();
  
  const llvm::TargetData &getTargetData() const { return TheTargetData; }
  TargetInfo &getTarget() const { return Target; }
  ASTContext &getContext() const { return Context; }

  /// ConvertType - Convert type T into a llvm::Type. Maintain and use
  /// type cache through TypeHolderMap.
  const llvm::Type *ConvertType(QualType T);
  
  /// ConvertTypeForMem - Convert type T into a llvm::Type. Maintain and use
  /// type cache through TypeHolderMap.  This differs from ConvertType in that
  /// it is used to convert to the memory representation for a type.  For
  /// example, the scalar representation for _Bool is i1, but the memory
  /// representation is usually i8 or i32, depending on the target.
  const llvm::Type *ConvertTypeForMem(QualType T);
  
  
  const CGRecordLayout *getCGRecordLayout(const llvm::Type*) const;
  
  /// getLLVMFieldNo - Return llvm::StructType element number
  /// that corresponds to the field FD.
  unsigned getLLVMFieldNo(const FieldDecl *FD);
    
public:  // These are internal details of CGT that shouldn't be used externally.
  void DecodeArgumentTypes(const FunctionTypeProto &FTP, 
                           std::vector<const llvm::Type*> &ArgTys);

  /// addFieldInfo - Assign field number to field FD.
  void addFieldInfo(const FieldDecl *FD, unsigned No);

  /// addBitFieldInfo - Assign a start bit and a size to field FD.
  void addBitFieldInfo(const FieldDecl *FD, unsigned Begin, unsigned Size);

  /// getBitFieldInfo - Return the BitFieldInfo  that corresponds to the field FD.
  BitFieldInfo getBitFieldInfo(const FieldDecl *FD);
};

}  // end namespace CodeGen
}  // end namespace clang

#endif
