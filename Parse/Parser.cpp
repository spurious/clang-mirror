//===--- Parse.cpp - C Language Family Parser -----------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file was developed by Chris Lattner and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file implements the Parser interfaces.
//
//===----------------------------------------------------------------------===//

#include "clang/Parse/Parser.h"
#include "clang/Parse/Declarations.h"
#include "clang/Parse/Scope.h"
using namespace llvm;
using namespace clang;

Parser::Parser(Preprocessor &pp, ParserActions &actions)
  : PP(pp), Actions(actions), Diags(PP.getDiagnostics()) {
  // Create the global scope, install it as the current scope.
  CurScope = new Scope(0);
  Tok.SetKind(tok::eof);
}

Parser::~Parser() {
  delete CurScope;
}


void Parser::Diag(SourceLocation Loc, unsigned DiagID,
                  const std::string &Msg) {
  Diags.Report(Loc, DiagID, Msg);
}

//===----------------------------------------------------------------------===//
// C99 6.9: External Definitions.
//===----------------------------------------------------------------------===//

/// ParseTranslationUnit:
///       translation-unit: [C99 6.9]
///         external-declaration 
///         translation-unit external-declaration 
void Parser::ParseTranslationUnit() {

  if (Tok.getKind() == tok::eof)  // Empty source file is an extension.
    Diag(diag::ext_empty_source_file);
  
  while (Tok.getKind() != tok::eof)
    ParseExternalDeclaration();
}

/// ParseExternalDeclaration:
///       external-declaration: [C99 6.9]
///         function-definition        [TODO]
///         declaration                [TODO]
/// [EXT]   ';'
/// [GNU]   asm-definition             [TODO]
/// [GNU]   __extension__ external-declaration     [TODO]
/// [OBJC]  objc-class-definition      [TODO]
/// [OBJC]  objc-class-declaration     [TODO]
/// [OBJC]  objc-alias-declaration     [TODO]
/// [OBJC]  objc-protocol-definition   [TODO]
/// [OBJC]  objc-method-definition     [TODO]
/// [OBJC]  @end                       [TODO]
///
void Parser::ParseExternalDeclaration() {
  switch (Tok.getKind()) {
  case tok::semi:
    Diag(diag::ext_top_level_semi);
    ConsumeToken();
    break;
  default:
    // We can't tell whether this is a function-definition or declaration yet.
    ParseDeclarationOrFunctionDefinition();
    break;
  }
}

/// ParseDeclarationOrFunctionDefinition - Parse either a function-definition or
/// a declaration.  We can't tell which we have until we read up to the
/// compound-statement in function-definition.
///
///       function-definition: [C99 6.9.1]
///         declaration-specifiers[opt] declarator declaration-list[opt] 
///                 compound-statement                           [TODO]
///       declaration: [C99 6.7]
///         declaration-specifiers init-declarator-list[opt] ';' [TODO]
/// [!C99]  init-declarator-list ';'                             [TODO]
/// [OMP]   threadprivate-directive                              [TODO]
///
///       init-declarator-list: [C99 6.7]
///         init-declarator
///         init-declarator-list ',' init-declarator
///       init-declarator: [C99 6.7]
///         declarator
///         declarator '=' initializer
///
void Parser::ParseDeclarationOrFunctionDefinition() {
  // Parse the common declaration-specifiers piece.
  // NOTE: this can not be missing for C99 'declaration's.
  DeclSpec DS;
  ParseDeclarationSpecifiers(DS);

  // C99 6.7.2.3p6: Handle "struct-or-union identifier;", "enum { X };"
  if (Tok.getKind() == tok::semi)
    assert(0 && "Unimp!");
  
  
  // Parse the declarator.
  {
    Declarator DeclaratorInfo(DS, Declarator::FileContext);
    ParseDeclarator(DeclaratorInfo);

    // If the declarator was a function type... handle it.

    // must be: decl-spec[opt] declarator init-declarator-list
    // Parse declarator '=' initializer.
    if (Tok.getKind() == tok::equal)
      assert(0 && "cannot handle initializer yet!");
  }

  while (Tok.getKind() == tok::comma) {
    // Consume the comma.
    ConsumeToken();
    
    // Parse the declarator.
    Declarator DeclaratorInfo(DS, Declarator::FileContext);
    ParseDeclarator(DeclaratorInfo);
    
    // declarator '=' initializer
    if (Tok.getKind() == tok::equal)
      assert(0 && "cannot handle initializer yet!");
    
    
  }
  
  if (Tok.getKind() == tok::semi) {
    ConsumeToken();
  } else {
    Diag(Tok, diag::err_parse_error);
    // FIXME: skip to end of block or statement
    while (Tok.getKind() != tok::semi && Tok.getKind() != tok::eof)
      ConsumeToken();
    if (Tok.getKind() == tok::semi)
      ConsumeToken();
  }
}

