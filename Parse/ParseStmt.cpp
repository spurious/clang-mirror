//===--- Statement.cpp - Statement and Block Parser -----------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file was developed by Chris Lattner and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the Statement and Block portions of the Parser
// interface.
//
//===----------------------------------------------------------------------===//

#include "clang/Parse/Parser.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Parse/Declarations.h"
using namespace llvm;
using namespace clang;

//===----------------------------------------------------------------------===//
// C99 6.8: Statements and Blocks.
//===----------------------------------------------------------------------===//

/// ParseStatementOrDeclaration - Read 'statement' or 'declaration'.
///       StatementOrDeclaration:
///         statement
///         declaration
///
///       statement:
///         labeled-statement
///         compound-statement
///         expression-statement
///         selection-statement
///         iteration-statement
///         jump-statement
/// [OBC]   objc-throw-statement         [TODO]
/// [OBC]   objc-try-catch-statement     [TODO]
/// [OBC]   objc-synchronized-statement  [TODO]
/// [GNU]   asm-statement                [TODO]
/// [OMP]   openmp-construct             [TODO]
///
///       labeled-statement:
///         identifier ':' statement
///         'case' constant-expression ':' statement
///         'default' ':' statement
///
///       selection-statement:
///         if-statement
///         switch-statement
///
///       iteration-statement:
///         while-statement
///         do-statement
///         for-statement
///
///       expression-statement:
///         expression[opt] ';'
///
///       jump-statement:
///         'goto' identifier ';'
///         'continue' ';'
///         'break' ';'
///         'return' expression[opt] ';'
/// [GNU]   'goto' '*' expression ';'
///
/// [OBC] objc-throw-statement:           [TODO]
/// [OBC]   '@' 'throw' expression ';'    [TODO]
/// [OBC]   '@' 'throw' ';'               [TODO]
/// 
void Parser::ParseStatementOrDeclaration(bool OnlyStatement) {
  const char *SemiError = 0;
  
ParseNextStatement:
  // Cases in this switch statement should fall through if the parser expects
  // the token to end in a semicolon (in which case SemiError should be set),
  // or they directly 'return;' if not.
  switch (Tok.getKind()) {
  case tok::identifier:             // C99 6.8.1: labeled-statement
    // identifier ':' statement
    // declaration                  (if !OnlyStatement)
    // expression[opt] ';'
    return ParseIdentifierStatement(OnlyStatement);

  default:
    if (!OnlyStatement && isDeclarationSpecifier()) {
      // TODO: warn/disable if declaration is in the middle of a block and !C99.
      ParseDeclaration(Declarator::BlockContext);
      return;
    } else if (Tok.getKind() == tok::r_brace) {
      Diag(Tok, diag::err_expected_statement);
    } else {
      // expression[opt] ';'
      ParseExpression();
    }
    return;
    
  case tok::kw_case:                // C99 6.8.1: labeled-statement
    ParseCaseStatement();
    if (Tok.getKind() == tok::r_brace) {
      Diag(Tok, diag::err_label_end_of_compound_statement);
      return;
    }
    OnlyStatement = true;
    goto ParseNextStatement;
  case tok::kw_default:             // C99 6.8.1: labeled-statement
    ParseDefaultStatement();
    if (Tok.getKind() == tok::r_brace) {
      Diag(Tok, diag::err_label_end_of_compound_statement);
      return;
    }
    OnlyStatement = true;
    goto ParseNextStatement;
    
  case tok::l_brace:                // C99 6.8.2: compound-statement
    ParseCompoundStatement();
    return;
  case tok::semi:                   // C99 6.8.3: expression[opt] ';'
    ConsumeToken();
    return;
    
  case tok::kw_if:                  // C99 6.8.4.1: if-statement
    ParseIfStatement();
    return;
  case tok::kw_switch:              // C99 6.8.4.2: switch-statement
    ParseSwitchStatement();
    return;
    
  case tok::kw_while:               // C99 6.8.5.1: while-statement
    ParseWhileStatement();
    return;
  case tok::kw_do:                  // C99 6.8.5.2: do-statement
    ParseDoStatement();
    SemiError = "do/while loop";
    break;
  case tok::kw_for:                 // C99 6.8.5.3: for-statement
    ParseForStatement();
    return;

  case tok::kw_goto:                // C99 6.8.6.1: goto-statement
    ParseGotoStatement();
    SemiError = "goto statement";
    break;
  case tok::kw_continue:            // C99 6.8.6.2: continue-statement
    ConsumeToken();  // eat the 'continue'.
    SemiError = "continue statement";
    break;
  case tok::kw_break:               // C99 6.8.6.3: break-statement
    ConsumeToken();  // eat the 'break'.
    SemiError = "break statement";
    break;
  case tok::kw_return:              // C99 6.8.6.4: return-statement
    ParseReturnStatement();
    SemiError = "return statement";
    break;
  }
  
  // If we reached this code, the statement must end in a semicolon.
  if (Tok.getKind() == tok::semi) {
    ConsumeToken();
  } else {
    Diag(Tok, diag::err_expected_semi_after, SemiError);
    SkipUntil(tok::semi);
  }
}

/// ParseIdentifierStatement - Because we don't have two-token lookahead, we
/// have a bit of a quandry here.  Reading the identifier is necessary to see if
/// there is a ':' after it.  If there is, this is a label, regardless of what
/// else the identifier can mean.  If not, this is either part of a declaration
/// (if the identifier is a type-name) or part of an expression.
///
///       labeled-statement:
///         identifier ':' statement
///         declaration                  (if !OnlyStatement)
///         expression[opt] ';'
///
void Parser::ParseIdentifierStatement(bool OnlyStatement) {
  IdentifierInfo *II = Tok.getIdentifierInfo();
  assert(Tok.getKind() == tok::identifier && II && "Not an identifier!");

  ConsumeToken();  // eat the identifier.
  
  // identifier ':' statement
  if (Tok.getKind() == tok::colon) {
    ConsumeToken();
    ParseStatement();
    return;
  }
  
  // declaration
  if (!OnlyStatement && 0/*Is typedef name!*/) {
    // Handle this.  Warn/disable if in middle of block and !C99.
    DeclSpec DS;
    
    // FIXME: Add the typedef name to the start of the decl-specs.
    // ParseDeclarationSpecifiers will continue from there.
    ParseDeclarationSpecifiers(DS);

    // Parse all the declarators.
    Declarator DeclaratorInfo(DS, Declarator::BlockContext);
    ParseDeclarator(DeclaratorInfo);
    
    ParseInitDeclaratorListAfterFirstDeclarator(DeclaratorInfo);
    return;
  }
  
  // Otherwise, this is an expression.  Seed it with II.
  
  assert(0);
}

/// ParseCaseStatement
///       labeled-statement:
///         'case' constant-expression ':' statement
///
/// Note that this does not parse the 'statement' at the end.
///
void Parser::ParseCaseStatement() {
  assert(Tok.getKind() == tok::kw_case && "Not a case stmt!");
  ConsumeToken();  // eat the 'case'.

  ParseAssignmentExpression(); // Expr without commas.
  
  if (Tok.getKind() == tok::colon) {
    ConsumeToken();
  } else {
    Diag(Tok, diag::err_expected_colon_after, "'case'");
    SkipUntil(tok::colon);
  }
}

/// ParseDefaultStatement
///       labeled-statement:
///         'default' ':' statement
/// Note that this does not parse the 'statement' at the end.
///
void Parser::ParseDefaultStatement() {
  assert(Tok.getKind() == tok::kw_default && "Not a default stmt!");
  ConsumeToken();  // eat the 'default'.

  if (Tok.getKind() == tok::colon) {
    ConsumeToken();
  } else {
    Diag(Tok, diag::err_expected_colon_after, "'default'");
    SkipUntil(tok::colon);
  }
}


/// ParseCompoundStatement - Parse a "{}" block.
///
///       compound-statement: [C99 6.8.2]
///         { block-item-list[opt] }
/// [GNU]   { label-declarations block-item-list } [TODO]
///
///       block-item-list:
///         block-item
///         block-item-list block-item
///
///       block-item:
///         declaration
/// [GNU]   '__extension__' declaration [TODO]
///         statement
/// [OMP]   openmp-directive            [TODO]
///
/// [GNU] label-declarations:
/// [GNU]   label-declaration
/// [GNU]   label-declarations label-declaration
///
/// [GNU] label-declaration:
/// [GNU]   '__label__' identifier-list ';'
///
/// [OMP] openmp-directive:             [TODO]
/// [OMP]   barrier-directive
/// [OMP]   flush-directive
void Parser::ParseCompoundStatement() {
  assert(Tok.getKind() == tok::l_brace && "Not a compount stmt!");
  ConsumeBrace();  // eat the '{'.
  
  while (Tok.getKind() != tok::r_brace && Tok.getKind() != tok::eof)
    ParseStatementOrDeclaration(false);
  
  // We broke out of the while loop because we found a '}' or EOF.
  if (Tok.getKind() == tok::r_brace)
    ConsumeBrace();
  else
    Diag(Tok, diag::err_expected_rbrace);
}

/// ParseIfStatement
///       if-statement: [C99 6.8.4.1]
///         'if' '(' expression ')' statement
///         'if' '(' expression ')' statement 'else' statement
void Parser::ParseIfStatement() {
  assert(Tok.getKind() == tok::kw_if && "Not an if stmt!");
  ConsumeToken();  // eat the 'if'.

  if (Tok.getKind() != tok::l_paren) {
    Diag(Tok, diag::err_expected_lparen_after, "if");
    SkipUntil(tok::semi);
    return;
  }
  
  // Parse the condition.
  ParseParenExpression();
  
  // Read the if condition.
  ParseStatement();
  
  // If it has an else, parse it.
  if (Tok.getKind() == tok::kw_else) {
    ConsumeToken();
    ParseStatement();
  }
}

/// ParseSwitchStatement
///       switch-statement:
///         'switch' '(' expression ')' statement
void Parser::ParseSwitchStatement() {
  assert(Tok.getKind() == tok::kw_switch && "Not a switch stmt!");
  ConsumeToken();  // eat the 'switch'.

  if (Tok.getKind() != tok::l_paren) {
    Diag(Tok, diag::err_expected_lparen_after, "switch");
    SkipUntil(tok::semi);
    return;
  }
  
  // Parse the condition.
  ParseParenExpression();
  
  // Read the body statement.
  ParseStatement();
}

/// ParseWhileStatement
///       while-statement: [C99 6.8.5.1]
///         'while' '(' expression ')' statement
void Parser::ParseWhileStatement() {
  assert(Tok.getKind() == tok::kw_while && "Not a while stmt!");
  ConsumeToken();  // eat the 'while'.
  
  if (Tok.getKind() != tok::l_paren) {
    Diag(Tok, diag::err_expected_lparen_after, "while");
    SkipUntil(tok::semi);
    return;
  }
  
  // Parse the condition.
  ParseParenExpression();
  
  // Read the body statement.
  ParseStatement();
}

/// ParseDoStatement
///       do-statement: [C99 6.8.5.2]
///         'do' statement 'while' '(' expression ')' ';'
/// Note: this lets the caller parse the end ';'.
void Parser::ParseDoStatement() {
  assert(Tok.getKind() == tok::kw_do && "Not a do stmt!");
  SourceLocation DoLoc = Tok.getLocation();
  ConsumeToken();  // eat the 'do'.
  
  // Read the body statement.
  ParseStatement();

  if (Tok.getKind() != tok::kw_while) {
    Diag(Tok, diag::err_expected_while);
    Diag(DoLoc, diag::err_matching);
    SkipUntil(tok::semi);
    return;
  }
  ConsumeToken();
  
  if (Tok.getKind() != tok::l_paren) {
    Diag(Tok, diag::err_expected_lparen_after, "do/while");
    SkipUntil(tok::semi);
    return;
  }
  
  // Parse the condition.
  ParseParenExpression();
}

/// ParseForStatement
///       for-statement: [C99 6.8.5.3]
///         'for' '(' expr[opt] ';' expr[opt] ';' expr[opt] ')' statement
///         'for' '(' declaration expr[opt] ';' expr[opt] ')' statement
void Parser::ParseForStatement() {
  assert(Tok.getKind() == tok::kw_for && "Not a for stmt!");
  SourceLocation ForLoc = Tok.getLocation();
  ConsumeToken();  // eat the 'for'.
  
  if (Tok.getKind() != tok::l_paren) {
    Diag(Tok, diag::err_expected_lparen_after, "for");
    SkipUntil(tok::semi);
    return;
  }

  SourceLocation LParenLoc = Tok.getLocation();
  ConsumeParen();
  
  // Parse the first part of the for specifier.
  if (Tok.getKind() == tok::semi) {  // for (;
    // no first part, eat the ';'.
    ConsumeToken();
  } else if (isDeclarationSpecifier()) {  // for (int X = 4;
    // Parse declaration, which eats the ';'.
    if (!getLang().C99)   // Use of C99-style for loops in C90 mode?
      Diag(Tok, diag::ext_c99_variable_decl_in_for_loop);
    ParseDeclaration(Declarator::ForContext);
  } else {
    ParseExpression();
  
    if (Tok.getKind() == tok::semi) {
      ConsumeToken();
    } else {
      Diag(Tok, diag::err_expected_semi_for);
      Diag(ForLoc, diag::err_matching);
      SkipUntil(tok::semi);
    }
  }
  
  // Parse the second part of the for specifier.
  if (Tok.getKind() == tok::semi) {  // for (...;;
    // no second part.
  } else {
    ParseExpression();
  }
  
  if (Tok.getKind() == tok::semi) {
    ConsumeToken();
  } else {
    Diag(Tok, diag::err_expected_semi_for);
    Diag(ForLoc, diag::err_matching);
    SkipUntil(tok::semi);
  }
  
  // Parse the third part of the for specifier.
  if (Tok.getKind() == tok::r_paren) {  // for (...;...;)
    // no third part.
  } else {
    ParseExpression();
  }
  
  if (Tok.getKind() == tok::r_paren) {
    ConsumeParen();
  } else {
    Diag(Tok, diag::err_expected_rparen);
    Diag(LParenLoc, diag::err_matching);
    SkipUntil(tok::r_paren);
    return;
  }
  
  // Read the body statement.
  ParseStatement();
}

/// ParseGotoStatement
///       jump-statement:
///         'goto' identifier ';'
/// [GNU]   'goto' '*' expression ';'
///
/// Note: this lets the caller parse the end ';'.
///
void Parser::ParseGotoStatement() {
  assert(Tok.getKind() == tok::kw_goto && "Not a goto stmt!");
  ConsumeToken();  // eat the 'goto'.
  
  if (Tok.getKind() == tok::identifier) {
    ConsumeToken();
  } else if (Tok.getKind() == tok::star && !getLang().NoExtensions) {
    // GNU indirect goto extension.
    Diag(Tok, diag::ext_gnu_indirect_goto);
    ConsumeToken();
    ParseExpression();
  }
}

/// ParseReturnStatement
///       jump-statement:
///         'return' expression[opt] ';'
void Parser::ParseReturnStatement() {
  assert(Tok.getKind() == tok::kw_return && "Not a return stmt!");
  ConsumeToken();  // eat the 'return'.
  
  if (Tok.getKind() != tok::semi)
    ParseExpression();
}
