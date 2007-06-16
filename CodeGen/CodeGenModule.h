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

namespace llvm {
  class Module;
  class Constant;
}

namespace clang {
  class ASTContext;
  class FunctionDecl;
  class Decl;
    
namespace CodeGen {

/// CodeGenModule - This class organizes the cross-module state that is used
/// while generating LLVM code.
class CodeGenModule {
  ASTContext &Context;
  llvm::Module &TheModule;
  
  //llvm::DenseMap<const Decl*, llvm::Constant*> GlobalDeclMap;
public:
  CodeGenModule(ASTContext &C, llvm::Module &M) : Context(C), TheModule(M) {}
  
  ASTContext &getContext() const { return Context; }
  llvm::Module &getModule() const { return TheModule; }
  
  void EmitFunction(FunctionDecl *FD);
  
  void PrintStats() {}
};
}  // end namespace CodeGen
}  // end namespace clang

#endif
