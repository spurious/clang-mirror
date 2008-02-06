//===--- ParseAST.cpp - Provide the clang::ParseAST method ----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the clang::ParseAST method.
//
//===----------------------------------------------------------------------===//

#include "clang/Sema/ParseAST.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/ASTConsumer.h"
#include "Sema.h"
#include "clang/Parse/Action.h"
#include "clang/Parse/Parser.h"
using namespace clang;

ASTConsumer::~ASTConsumer() {}

//===----------------------------------------------------------------------===//
// Public interface to the file
//===----------------------------------------------------------------------===//

/// ParseAST - Parse the entire file specified, notifying the ASTConsumer as
/// the file is parsed.  This takes ownership of the ASTConsumer and
/// ultimately deletes it.
void clang::ParseAST(Preprocessor &PP, ASTConsumer *Consumer, bool PrintStats) {
  // Collect global stats on Decls/Stmts (until we have a module streamer).
  if (PrintStats) {
    Decl::CollectingStats(true);
    Stmt::CollectingStats(true);
  }
  
  ASTContext Context(PP.getSourceManager(), PP.getTargetInfo(),
                     PP.getIdentifierTable(), PP.getSelectorTable());
  
  Parser P(PP, *new Sema(PP, Context));
  PP.EnterMainSourceFile();
    
  // Initialize the parser.
  P.Initialize();
  
  Consumer->Initialize(Context);
  
  Parser::DeclTy *ADecl;
  while (!P.ParseTopLevelDecl(ADecl)) {  // Not end of file.
    // If we got a null return and something *was* parsed, ignore it.  This
    // is due to a top-level semicolon, an action override, or a parse error
    // skipping something.
    if (ADecl)
      Consumer->HandleTopLevelDecl(static_cast<Decl*>(ADecl));
  };

  if (PrintStats) {
    fprintf(stderr, "\nSTATISTICS:\n");
    P.getActions().PrintStats();
    Context.PrintStats();
    Decl::PrintStats();
    Stmt::PrintStats();
    Consumer->PrintStats();
    
    Decl::CollectingStats(false);
    Stmt::CollectingStats(false);
  }
  
  delete Consumer;
}
