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
  
namespace CodeGen {
  
/// CodeGenTypes - This class organizes the cross-module state that is used
/// while lowering AST types to LLVM types.
class CodeGenTypes {
  ASTContext &Context;
  TargetInfo &Target;
  llvm::Module& TheModule;
  
  llvm::DenseMap<const TagDecl*, llvm::Type*> TagDeclTypes;
public:
  CodeGenTypes(ASTContext &Ctx, llvm::Module &M);
  
  TargetInfo &getTarget() const { return Target; }
  
  const llvm::Type *ConvertType(QualType T);
  void DecodeArgumentTypes(const FunctionTypeProto &FTP, 
                           std::vector<const llvm::Type*> &ArgTys);
};

}  // end namespace CodeGen
}  // end namespace clang

#endif
