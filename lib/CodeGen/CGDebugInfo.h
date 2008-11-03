//===--- CGDebugInfo.h - DebugInfo for LLVM CodeGen -------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This is the source level debug info generator for llvm translation. 
//
//===----------------------------------------------------------------------===//

#ifndef CLANG_CODEGEN_CGDEBUGINFO_H
#define CLANG_CODEGEN_CGDEBUGINFO_H

#include "clang/AST/Type.h"
#include "clang/Basic/SourceLocation.h"
#include <map>
#include <vector>

#include "CGBuilder.h"

namespace llvm {
  class Function;
  class DISerializer;
  class CompileUnitDesc;
  class BasicBlock;
  class AnchorDesc;
  class DebugInfoDesc;
  class Value;
  class TypeDesc;
  class VariableDesc;
  class SubprogramDesc;
  class GlobalVariable;
  class GlobalVariableDesc;
  class EnumeratorDesc;
  class SubrangeDesc;
}

namespace clang {
  class FunctionDecl;
  class VarDecl;

namespace CodeGen {
  class CodeGenModule;

/// CGDebugInfo - This class gathers all debug information during compilation 
/// and is responsible for emitting to llvm globals or pass directly to 
/// the backend.
class CGDebugInfo {
private:
  CodeGenModule *M;
  llvm::DISerializer *SR;
  SourceLocation CurLoc;
  SourceLocation PrevLoc;

  /// CompileUnitCache - Cache of previously constructed CompileUnits.
  std::map<const FileEntry*, llvm::CompileUnitDesc *> CompileUnitCache;

  /// TypeCache - Cache of previously constructed Types.
  std::map<void *, llvm::TypeDesc *> TypeCache;
  
  llvm::Function *StopPointFn;
  llvm::Function *FuncStartFn;
  llvm::Function *DeclareFn;
  llvm::Function *RegionStartFn;
  llvm::Function *RegionEndFn;
  llvm::AnchorDesc *CompileUnitAnchor;
  llvm::AnchorDesc *SubprogramAnchor;
  llvm::AnchorDesc *GlobalVariableAnchor;
  std::vector<llvm::DebugInfoDesc *> RegionStack;
  std::vector<llvm::VariableDesc *> VariableDescList;
  std::vector<llvm::GlobalVariableDesc *> GlobalVarDescList;
  std::vector<llvm::EnumeratorDesc *> EnumDescList;
  std::vector<llvm::SubrangeDesc *> SubrangeDescList;
  llvm::SubprogramDesc *Subprogram;

  /// Helper functions for getOrCreateType.
  llvm::TypeDesc *getOrCreateCVRType(QualType type, 
                                     llvm::CompileUnitDesc *unit);
  llvm::TypeDesc *getOrCreateBuiltinType(QualType type, 
                                         llvm::CompileUnitDesc *unit);
  llvm::TypeDesc *getOrCreateTypedefType(QualType type, 
                                         llvm::CompileUnitDesc *unit);
  llvm::TypeDesc *getOrCreatePointerType(QualType type, 
                                         llvm::CompileUnitDesc *unit);
  llvm::TypeDesc *getOrCreateFunctionType(QualType type, 
                                          llvm::CompileUnitDesc *unit);
  void getOrCreateRecordType(QualType type,
                             llvm::CompileUnitDesc *unit,
                             llvm::TypeDesc *&Slot);
  llvm::TypeDesc *getOrCreateEnumType(QualType type,
                                      llvm::CompileUnitDesc *unit);
  void getOrCreateTaggedType(QualType type,
                             llvm::CompileUnitDesc *unit,
                             llvm::TypeDesc *&Slot);
  llvm::TypeDesc *getOrCreateArrayType(QualType type,
                                       llvm::CompileUnitDesc *unit);

public:
  CGDebugInfo(CodeGenModule *m);
  ~CGDebugInfo();

  /// setLocation - Update the current source location. If \arg loc is
  /// invalid it is ignored.
  void setLocation(SourceLocation loc);

  /// EmitStopPoint - Emit a call to llvm.dbg.stoppoint to indicate a change of
  /// source line.
  void EmitStopPoint(llvm::Function *Fn, CGBuilderTy &Builder);

  /// EmitFunctionStart - Emit a call to llvm.dbg.function.start to indicate
  /// start of a new function.
  void EmitFunctionStart(const char *Name, QualType ReturnType,
                         llvm::Function *Fn, CGBuilderTy &Builder);
  
  /// EmitRegionStart - Emit a call to llvm.dbg.region.start to indicate start
  /// of a new block.  
  void EmitRegionStart(llvm::Function *Fn, CGBuilderTy &Builder);
  
  /// EmitRegionEnd - Emit call to llvm.dbg.region.end to indicate end of a 
  /// block.
  void EmitRegionEnd(llvm::Function *Fn, CGBuilderTy &Builder);

  /// EmitDeclare - Emit call to llvm.dbg.declare for a variable declaration.
  void EmitDeclare(const VarDecl *decl, unsigned Tag, llvm::Value *AI,
                   CGBuilderTy &Builder);

  /// EmitGlobalVariable - Emit information about a global variable.
  void EmitGlobalVariable(llvm::GlobalVariable *GV, const VarDecl *decl);
 
  
private:
  
  /// getOrCreateCompileUnit - Get the compile unit from the cache or create a
  /// new one if necessary.
  llvm::CompileUnitDesc *getOrCreateCompileUnit(SourceLocation loc);

  /// getOrCreateType - Get the type from the cache or create a new type if
  /// necessary.
  llvm::TypeDesc *getOrCreateType(QualType type, llvm::CompileUnitDesc *unit);

  /// getCastValueFor - Return a llvm representation for a given debug
  /// information descriptor cast to an empty struct pointer.
  llvm::Value *getCastValueFor(llvm::DebugInfoDesc *DD);

  /// getValueFor - Return a llvm representation for a given debug information
  /// descriptor.
  llvm::Value *getValueFor(llvm::DebugInfoDesc *DD);
};
} // namespace CodeGen
} // namespace clang

#endif
