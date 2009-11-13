//===-- CompilerInstance.h - Clang Compiler Instance ------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_FRONTEND_COMPILERINSTANCE_H_
#define LLVM_CLANG_FRONTEND_COMPILERINSTANCE_H_

#include "clang/Frontend/CompilerInvocation.h"
#include "llvm/ADT/OwningPtr.h"

namespace llvm {
class LLVMContext;
}

namespace clang {
class Diagnostic;
class DiagnosticClient;
class FileManager;
class SourceManager;
class TargetInfo;

/// CompilerInstance - Helper class for managing a single instance of the Clang
/// compiler.
///
/// The CompilerInstance serves two purposes:
///  (1) It manages the various objects which are necessary to run the compiler,
///      for example the preprocessor, the target information, and the AST
///      context.
///  (2) It provides utility routines for constructing and manipulating the
///      common Clang objects.
///
/// The compiler instance generally owns the instance of all the objects that it
/// manages. However, clients can still share objects by manually setting the
/// object and retaking ownership prior to destroying the CompilerInstance.
///
/// The compiler instance is intended to simplify clients, but not to lock them
/// in to the compiler instance for everything. When possible, utility functions
/// come in two forms; a short form that reuses the CompilerInstance objects,
/// and a long form that takes explicit instances of any required objects.
class CompilerInstance {
  /// The LLVM context used for this instance.
  llvm::LLVMContext *LLVMContext;
  bool OwnsLLVMContext;

  /// The options used in this compiler instance.
  CompilerInvocation Invocation;

  /// The diagnostics engine instance.
  llvm::OwningPtr<Diagnostic> Diagnostics;

  /// The diagnostics client instance.
  llvm::OwningPtr<DiagnosticClient> DiagClient;

  /// The target being compiled for.
  llvm::OwningPtr<TargetInfo> Target;

  /// The file manager.
  llvm::OwningPtr<FileManager> FileMgr;

  /// The source manager.
  llvm::OwningPtr<SourceManager> SourceMgr;

public:
  /// Create a new compiler instance with the given LLVM context, optionally
  /// taking ownership of it.
  CompilerInstance(llvm::LLVMContext *_LLVMContext = 0,
                   bool _OwnsLLVMContext = true);
  ~CompilerInstance();

  /// @name LLVM Context
  /// {

  llvm::LLVMContext &getLLVMContext() { return *LLVMContext; }

  /// setLLVMContext - Replace the current LLVM context and take ownership of
  /// \arg Value.
  void setLLVMContext(llvm::LLVMContext *Value, bool TakeOwnership = true) {
    LLVMContext = Value;
    OwnsLLVMContext = TakeOwnership;
  }

  /// }
  /// @name Compiler Invocation and Options
  /// {

  CompilerInvocation &getInvocation() { return Invocation; }
  const CompilerInvocation &getInvocation() const { return Invocation; }
  void setInvocation(const CompilerInvocation &Value) { Invocation = Value; }

  /// }
  /// @name Forwarding Methods
  /// {

  AnalyzerOptions &getAnalyzerOpts() {
    return Invocation.getAnalyzerOpts();
  }
  const AnalyzerOptions &getAnalyzerOpts() const {
    return Invocation.getAnalyzerOpts();
  }

  CodeGenOptions &getCodeGenOpts() {
    return Invocation.getCodeGenOpts();
  }
  const CodeGenOptions &getCodeGenOpts() const {
    return Invocation.getCodeGenOpts();
  }

  DependencyOutputOptions &getDependencyOutputOpts() {
    return Invocation.getDependencyOutputOpts();
  }
  const DependencyOutputOptions &getDependencyOutputOpts() const {
    return Invocation.getDependencyOutputOpts();
  }

  DiagnosticOptions &getDiagnosticOpts() {
    return Invocation.getDiagnosticOpts();
  }
  const DiagnosticOptions &getDiagnosticOpts() const {
    return Invocation.getDiagnosticOpts();
  }

  FrontendOptions &getFrontendOpts() {
    return Invocation.getFrontendOpts();
  }
  const FrontendOptions &getFrontendOpts() const {
    return Invocation.getFrontendOpts();
  }

  HeaderSearchOptions &getHeaderSearchOpts() {
    return Invocation.getHeaderSearchOpts();
  }
  const HeaderSearchOptions &getHeaderSearchOpts() const {
    return Invocation.getHeaderSearchOpts();
  }

  LangOptions &getLangOpts() {
    return Invocation.getLangOpts();
  }
  const LangOptions &getLangOpts() const {
    return Invocation.getLangOpts();
  }

  PreprocessorOptions &getPreprocessorOpts() {
    return Invocation.getPreprocessorOpts();
  }
  const PreprocessorOptions &getPreprocessorOpts() const {
    return Invocation.getPreprocessorOpts();
  }

  PreprocessorOutputOptions &getPreprocessorOutputOpts() {
    return Invocation.getPreprocessorOutputOpts();
  }
  const PreprocessorOutputOptions &getPreprocessorOutputOpts() const {
    return Invocation.getPreprocessorOutputOpts();
  }

  /// }
  /// @name Diagnostics Engine
  /// {

  Diagnostic &getDiagnostics() const { return *Diagnostics; }

  /// takeDiagnostics - Remove the current diagnostics engine and give ownership
  /// to the caller.
  Diagnostic *takeDiagnostics() { return Diagnostics.take(); }

  /// setDiagnostics - Replace the current diagnostics engine; the compiler
  /// instance takes ownership of \arg Value.
  void setDiagnostics(Diagnostic *Value) { Diagnostics.reset(Value); }

  DiagnosticClient &getDiagnosticClient() const { return *DiagClient; }

  /// takeDiagnosticClient - Remove the current diagnostics client and give
  /// ownership to the caller.
  DiagnosticClient *takeDiagnosticClient() { return DiagClient.take(); }

  /// setDiagnosticClient - Replace the current diagnostics client; the compiler
  /// instance takes ownership of \arg Value.
  void setDiagnosticClient(DiagnosticClient *Value) {
    DiagClient.reset(Value);
  }

  /// }
  /// @name Target Info
  /// {

  TargetInfo &getTarget() const { return *Target; }

  /// takeTarget - Remove the current diagnostics engine and give ownership
  /// to the caller.
  TargetInfo *takeTarget() { return Target.take(); }

  /// setTarget - Replace the current diagnostics engine; the compiler
  /// instance takes ownership of \arg Value.
  void setTarget(TargetInfo *Value) { Target.reset(Value); }

  /// }
  /// @name File Manager
  /// {

  FileManager &getFileManager() const { return *FileMgr; }

  /// takeFileManager - Remove the current file manager and give ownership to
  /// the caller.
  FileManager *takeFileManager() { return FileMgr.take(); }

  /// setFileManager - Replace the current file manager; the compiler instance
  /// takes ownership of \arg Value.
  void setFileManager(FileManager *Value) { FileMgr.reset(Value); }

  /// }
  /// @name Source Manager
  /// {

  SourceManager &getSourceManager() const { return *SourceMgr; }

  /// takeSourceManager - Remove the current source manager and give ownership
  /// to the caller.
  SourceManager *takeSourceManager() { return SourceMgr.take(); }

  /// setSourceManager - Replace the current source manager; the compiler
  /// instance takes ownership of \arg Value.
  void setSourceManager(SourceManager *Value) { SourceMgr.reset(Value); }

  /// }
  /// @name Construction Utility Methods
  /// {

  /// Create the file manager and replace any existing one with it.
  void createFileManager();

  /// Create the source manager and replace any existing one with it.
  void createSourceManager();

  /// }
};

} // end namespace clang

#endif
