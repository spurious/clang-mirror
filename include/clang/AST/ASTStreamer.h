//===--- AST.h - "Umbrella" header for AST library --------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file was developed by Chris Lattner and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the ASTStreamer interface.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_ASTSTREAMER_H
#define LLVM_CLANG_AST_ASTSTREAMER_H

namespace llvm {
namespace clang {
  class Preprocessor;
  class Decl;
  
  /// ASTStreamerTy - This is an opaque type used to reference ASTStreamer
  /// objects.
  typedef void ASTStreamerTy;
  
  /// ASTStreamer_Init - Create an ASTStreamer with the specified preprocessor
  /// and FileID.
  ASTStreamerTy *ASTStreamer_Init(Preprocessor &PP, unsigned MainFileID);
  
  /// ASTStreamer_ReadTopLevelDecl - Parse and return one top-level declaration. This
  /// returns null at end of file.
  Decl *ASTStreamer_ReadTopLevelDecl(ASTStreamerTy *Streamer);
  
  /// ASTStreamer_Terminate - Gracefully shut down the streamer.
  ///
  void ASTStreamer_Terminate(ASTStreamerTy *Streamer);
  
}  // end namespace clang
}  // end namespace llvm

#endif