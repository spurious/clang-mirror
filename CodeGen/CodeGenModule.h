//===--- CodeGenModule.h - Per-Module state for LLVM CodeGen --------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file was developed by Chris Lattner and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This is the internal per-translation-unit state used for llvm translation. 
//
//===----------------------------------------------------------------------===//

#ifndef CODEGEN_CODEGENMODULE_H
#define CODEGEN_CODEGENMODULE_H

#include "CodeGenTypes.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringMap.h"

namespace llvm {
  class Module;
  class Constant;
  class Function;
  class GlobalVariable;
  class TargetData;
}

namespace clang {
  class ASTContext;
  class FunctionDecl;
  class Decl;
  class ValueDecl;
  class FileVarDecl;
    
namespace CodeGen {

/// CodeGenModule - This class organizes the cross-module state that is used
/// while generating LLVM code.
class CodeGenModule {
  ASTContext &Context;
  llvm::Module &TheModule;
  const llvm::TargetData &TheTargetData;
  CodeGenTypes Types;

  llvm::Function *MemCpyFn;
  llvm::DenseMap<const Decl*, llvm::Constant*> GlobalDeclMap;
  
  llvm::StringMap<llvm::Constant*> CFConstantStringMap;
  llvm::Constant *CFConstantStringClassRef;
  
  std::vector<llvm::Function *> BuiltinFunctions;
public:
  CodeGenModule(ASTContext &C, llvm::Module &M, const llvm::TargetData &TD);
  
  ASTContext &getContext() const { return Context; }
  llvm::Module &getModule() const { return TheModule; }
  CodeGenTypes &getTypes() { return Types; }
  
  llvm::Constant *GetAddrOfGlobalDecl(const ValueDecl *D);
  
  /// getBuiltinLibFunction - Given a builtin id for a function like
  /// "__builtin_fabsf", return a Function* for "fabsf".
  ///
  llvm::Function *getBuiltinLibFunction(unsigned BuiltinID);
  llvm::Constant *GetAddrOfConstantCFString(const std::string& str);
  llvm::Function *getMemCpyFn();
  
  
  void EmitFunction(const FunctionDecl *FD);
  void EmitGlobalVar(const FileVarDecl *D);
  void EmitGlobalVarDeclarator(const FileVarDecl *D);
  llvm::Constant *EmitGlobalInit(const FileVarDecl *D, 
                                 llvm::GlobalVariable *GV);
  
  void PrintStats() {}
};
}  // end namespace CodeGen
}  // end namespace clang

#endif
