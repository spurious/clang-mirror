//===--- CodeGenTypes.h - Type translation for LLVM CodeGen -----*- C++ -*-===//
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

#ifndef CODEGEN_CODEGENTYPES_H
#define CODEGEN_CODEGENTYPES_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include <vector>

namespace llvm {
  class Module;
  class Type;
}

namespace clang {
  class ASTContext;
  class TagDecl;
  class TargetInfo;
  class QualType;
  class FunctionTypeProto;
  class FieldDecl;
  class RecordDecl;

namespace CodeGen {
  class CodeGenTypes;

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

    /// layoutFields - Do the actual work and lay out all fields. Create
    /// corresponding llvm struct type.  This should be invoked only after
    /// all fields are added.
    void layoutFields(CodeGenTypes &CGT);

    /// getLLVMType - Return associated llvm struct type. This may be NULL
    /// if fields are not laid out.
    llvm::Type *getLLVMType() {
      return STy;
    }

  private:
    llvm::Type *STy;
    llvm::SmallVector<const FieldDecl *, 8> FieldDecls;
  };

  /// RecordLayoutInfo - This class handles struct and union layout info while 
  /// lowering AST types to LLVM types.
  class RecordLayoutInfo {
    RecordLayoutInfo(); // DO NOT IMPLEMENT
  public:
    RecordLayoutInfo(RecordOrganizer *RO);

    /// getLLVMType - Return llvm type associated with this record.
    llvm::Type *getLLVMType() {
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
  
  llvm::DenseMap<const TagDecl*, llvm::Type*> TagDeclTypes;

  /// RecordLayouts - This maps llvm struct type with corresponding 
  /// record layout info. 
  /// FIXME : If RecordLayoutInfo is less than 16 bytes then use 
  /// inline it in the map.
  llvm::DenseMap<const llvm::Type*, RecordLayoutInfo *> RecordLayouts;

  /// FieldInfo - This maps struct field with corresponding llvm struct type
  /// field no. This info is populated by record organizer.
  llvm::DenseMap<const FieldDecl *, unsigned> FieldInfo;

  /// RecordTypesToResolve - This keeps track of record types that are not 
  /// yet incomplete. One llvm::OpaqueType is associated with each incomplete
  /// record.
  llvm::DenseMap<const RecordDecl *, llvm::Type *> RecordTypesToResolve;

public:
  CodeGenTypes(ASTContext &Ctx, llvm::Module &M);
  ~CodeGenTypes();
  
  TargetInfo &getTarget() const { return Target; }
  
  const llvm::Type *ConvertType(QualType T);
  void DecodeArgumentTypes(const FunctionTypeProto &FTP, 
                           std::vector<const llvm::Type*> &ArgTys);

  RecordLayoutInfo *getRecordLayoutInfo(const llvm::Type*);
  
  /// getLLVMFieldNo - Return llvm::StructType element number
  /// that corresponds to the field FD.
  unsigned getLLVMFieldNo(const FieldDecl *FD);

  /// addFieldInfo - Assign field number to field FD.
  void addFieldInfo(const FieldDecl *FD, unsigned No);
};

}  // end namespace CodeGen
}  // end namespace clang

#endif
