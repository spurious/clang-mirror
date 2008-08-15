//===----- CGObjCRuntime.h - Interface to ObjC Runtimes ---------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This provides an abstract class for Objective-C code generation.  Concrete
// subclasses of this implement code generation for specific Objective-C
// runtime libraries.
//
//===----------------------------------------------------------------------===//

#ifndef CLANG_CODEGEN_OBCJRUNTIME_H
#define CLANG_CODEGEN_OBCJRUNTIME_H
#include "clang/Basic/IdentifierTable.h" // Selector
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/IRBuilder.h"
#include <string>

namespace llvm {
  class Constant;
  class Type;
  class Value;
  class Module;
  class Function;
}

namespace clang {
  class ObjCCategoryImplDecl;
  class ObjCImplementationDecl;
  class ObjCMethodDecl;
  class ObjCProtocolDecl;
  class Selector;

namespace CodeGen {
  class CodeGenModule;

//FIXME Several methods should be pure virtual but aren't to avoid the
//partially-implemented subclass breaking.

/// Implements runtime-specific code generation functions.
class CGObjCRuntime {
  typedef llvm::IRBuilder<> BuilderType;

public:
  virtual ~CGObjCRuntime();

  /// Generate the function required to register all Objective-C components in
  /// this compilation unit with the runtime library.
  virtual llvm::Function *ModuleInitFunction() = 0;

  /// Get a selector for the specified name and type values. The
  /// return value should have the LLVM type for pointer-to
  /// ASTContext::getObjCSelType().
  virtual llvm::Value *GetSelector(BuilderType &Builder,
                                   Selector Sel) = 0;

  /// Generate a constant string object.
  virtual llvm::Constant *GenerateConstantString(const std::string &String) = 0;

  /// Generate a category.  A category contains a list of methods (and
  /// accompanying metadata) and a list of protocols.
  virtual void GenerateCategory(const ObjCCategoryImplDecl *OCD) = 0;

  /// Generate a class stucture for this class.
  virtual void GenerateClass(const ObjCImplementationDecl *OID) = 0;
  
  /// Generate an Objective-C message send operation.
  virtual llvm::Value *GenerateMessageSend(BuilderType &Builder,
                                           const llvm::Type *ReturnTy,
                                           llvm::Value *Receiver,
                                           Selector Sel,
                                           llvm::Value** ArgV,
                                           unsigned ArgC) = 0;

  /// Generate an Objective-C message send operation to the super
  /// class.
  virtual llvm::Value *GenerateMessageSendSuper(llvm::IRBuilder<true> &Builder,
                                                const llvm::Type *ReturnTy,
                                                const char *SuperClassName,
                                                llvm::Value *Receiver,
                                                Selector Sel,
                                                llvm::Value** ArgV,
                                                unsigned ArgC) = 0;

  /// Emit the code to return the named protocol as an object, as in a
  /// @protocol expression.
  virtual llvm::Value *GenerateProtocolRef(llvm::IRBuilder<true> &Builder,
                                           const ObjCProtocolDecl *OPD) = 0;

  /// Generate the named protocol.  Protocols contain method metadata but no 
  /// implementations. 
  virtual void GenerateProtocol(const ObjCProtocolDecl *OPD) = 0;

  /// Generate a function preamble for a method with the specified
  /// types.  

  // FIXME: Current this just generates the Function definition, but
  // really this should also be generating the loads of the
  // parameters, as the runtime should have full control over how
  // parameters are passed.
  virtual llvm::Function *GenerateMethod(const ObjCMethodDecl *OMD) = 0;

  /// Look up the class for the specified name
  virtual llvm::Value *LookupClass(BuilderType &Builder, 
                                   llvm::Value *ClassName) = 0;

  /// If instance variable addresses are determined at runtime then this should
  /// return true, otherwise instance variables will be accessed directly from
  /// the structure.  If this returns true then @defs is invalid for this
  /// runtime and a warning should be generated.
  virtual bool LateBoundIVars() { return false; }
};

/// Creates an instance of an Objective-C runtime class.  
//TODO: This should include some way of selecting which runtime to target.
CGObjCRuntime *CreateGNUObjCRuntime(CodeGenModule &CGM);
CGObjCRuntime *CreateMacObjCRuntime(CodeGenModule &CGM);
}
}
#endif
