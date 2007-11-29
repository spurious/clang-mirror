//===--- ASTStreamer.cpp - Provide streaming interface to ASTs ------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file was developed by Chris Lattner and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the ASTStreamer interface.
//
//===----------------------------------------------------------------------===//

#include "clang/Sema/ASTStreamer.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/ASTConsumer.h"
#include "Sema.h"
#include "clang/Parse/Action.h"
#include "clang/Parse/Parser.h"
using namespace clang;

ASTConsumer::~ASTConsumer() {}

namespace {
  class ASTStreamer {
    Parser P;
  public:
    ASTStreamer(Preprocessor &pp, ASTContext &ctxt, unsigned MainFileID)
      : P(pp, *new Sema(pp, ctxt)) {
      pp.EnterMainSourceFile(MainFileID);
      
      // Initialize the parser.
      P.Initialize();
    }
    
    /// ReadTopLevelDecl - Parse and return the next top-level declaration.
    Decl *ReadTopLevelDecl();
    
    void PrintStats() const;

    ~ASTStreamer() {
      P.Finalize();
      delete &P.getActions();
    }
  };
}

/// ReadTopLevelDecl - Parse and return the next top-level declaration.
///
Decl *ASTStreamer::ReadTopLevelDecl() {
  Parser::DeclTy *Result;
  
  do {
    if (P.ParseTopLevelDecl(Result))
      return 0;  // End of file.
    
    // If we got a null return and something *was* parsed, try again.  This
    // is due to a top-level semicolon, an action override, or a parse error
    // skipping something.
  } while (Result == 0);
  
  return static_cast<Decl*>(Result);
}

void ASTStreamer::PrintStats() const {
  P.getActions().PrintStats();
}

//===----------------------------------------------------------------------===//
// Public interface to the file
//===----------------------------------------------------------------------===//

/// ParseAST - Parse the entire file specified, notifying the ASTConsumer as
/// the file is parsed.  This takes ownership of the ASTConsumer and
/// ultimately deletes it.
void clang::ParseAST(Preprocessor &PP, unsigned MainFileID, 
                     ASTConsumer *Consumer, bool PrintStats) {
  // Collect global stats on Decls/Stmts (until we have a module streamer).
  if (PrintStats) {
    Decl::CollectingStats(true);
    Stmt::CollectingStats(true);
  }
  
  ASTContext Context(PP.getSourceManager(), PP.getTargetInfo(),
                     PP.getIdentifierTable(), PP.getSelectorTable());
  
  ASTStreamer Streamer(PP, Context, MainFileID);
  
  Consumer->Initialize(Context, MainFileID);
  
  while (Decl *D = Streamer.ReadTopLevelDecl())
    Consumer->HandleTopLevelDecl(D);

  if (PrintStats) {
    fprintf(stderr, "\nSTATISTICS:\n");
    Streamer.PrintStats();
    Context.PrintStats();
    Decl::PrintStats();
    Stmt::PrintStats();
    Consumer->PrintStats();
    
    Decl::CollectingStats(false);
    Stmt::CollectingStats(false);
  }
  
  delete Consumer;
}
