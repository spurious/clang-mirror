//===--- Parser.h - C Language Parser ---------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file was developed by Chris Lattner and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the Parser interface.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_PARSE_PARSER_H
#define LLVM_CLANG_PARSE_PARSER_H

#include "clang/Lex/Preprocessor.h"
#include "clang/Parse/Action.h"

namespace clang {
  class DeclSpec;
  class ObjcDeclSpec;
  class Declarator;
  class AttributeList;
  class Scope;

/// Parser - This implements a parser for the C family of languages.  After
/// parsing units of the grammar, productions are invoked to handle whatever has
/// been read.
///
class Parser {
  Preprocessor &PP;
  
  /// Tok - The current token we are peeking head.  All parsing methods assume
  /// that this is valid.
  Token Tok;
  
  unsigned short ParenCount, BracketCount, BraceCount;

  /// Actions - These are the callbacks we invoke as we parse various constructs
  /// in the file.  This refers to the common base class between MinimalActions
  /// and SemaActions for those uses that don't matter.
  Action &Actions;
  
  Scope *CurScope;
  Diagnostic &Diags;
  
  /// ScopeCache - Cache scopes to reduce malloc traffic.
  enum { ScopeCacheSize = 16 };
  unsigned NumCachedScopes;
  Scope *ScopeCache[ScopeCacheSize];
public:
  Parser(Preprocessor &PP, Action &Actions);
  ~Parser();

  const LangOptions &getLang() const { return PP.getLangOptions(); }
  TargetInfo &getTargetInfo() const { return PP.getTargetInfo(); }
  Action &getActions() const { return Actions; }
  
  // Type forwarding.  All of these are statically 'void*', but they may all be
  // different actual classes based on the actions in place.
  typedef Action::ExprTy ExprTy;
  typedef Action::StmtTy StmtTy;
  typedef Action::DeclTy DeclTy;
  typedef Action::TypeTy TypeTy;
  
  // Parsing methods.
  
  /// ParseTranslationUnit - All in one method that initializes parses, and
  /// shuts down the parser.
  void ParseTranslationUnit();
  
  /// Initialize - Warm up the parser.
  ///
  void Initialize();
  
  /// ParseTopLevelDecl - Parse one top-level declaration, return whatever the
  /// action tells us to.  This returns true if the EOF was encountered.
  bool ParseTopLevelDecl(DeclTy*& Result);
  
  /// Finalize - Shut down the parser.
  ///
  void Finalize();
  
private:
  //===--------------------------------------------------------------------===//
  // Low-Level token peeking and consumption methods.
  //
  
  /// isTokenParen - Return true if the cur token is '(' or ')'.
  bool isTokenParen() const {
    return Tok.getKind() == tok::l_paren || Tok.getKind() == tok::r_paren;
  }
  /// isTokenBracket - Return true if the cur token is '[' or ']'.
  bool isTokenBracket() const {
    return Tok.getKind() == tok::l_square || Tok.getKind() == tok::r_square;
  }
  /// isTokenBrace - Return true if the cur token is '{' or '}'.
  bool isTokenBrace() const {
    return Tok.getKind() == tok::l_brace || Tok.getKind() == tok::r_brace;
  }
  
  /// isTokenStringLiteral - True if this token is a string-literal.
  ///
  bool isTokenStringLiteral() const {
    return Tok.getKind() == tok::string_literal ||
           Tok.getKind() == tok::wide_string_literal;
  }
  
  /// ConsumeToken - Consume the current 'peek token' and lex the next one.
  /// This does not work will all kinds of tokens: strings and specific other
  /// tokens must be consumed with custom methods below.  This returns the
  /// location of the consumed token.
  SourceLocation ConsumeToken() {
    assert(!isTokenStringLiteral() && !isTokenParen() && !isTokenBracket() &&
           !isTokenBrace() &&
           "Should consume special tokens with Consume*Token");
    SourceLocation L = Tok.getLocation();
    PP.Lex(Tok);
    return L;
  }
  
  /// ConsumeAnyToken - Dispatch to the right Consume* method based on the
  /// current token type.  This should only be used in cases where the type of
  /// the token really isn't known, e.g. in error recovery.
  SourceLocation ConsumeAnyToken() {
    if (isTokenParen())
      return ConsumeParen();
    else if (isTokenBracket())
      return ConsumeBracket();
    else if (isTokenBrace())
      return ConsumeBrace();
    else
      return ConsumeToken();
  }
  
  /// ConsumeParen - This consume method keeps the paren count up-to-date.
  ///
  SourceLocation ConsumeParen() {
    assert(isTokenParen() && "wrong consume method");
    if (Tok.getKind() == tok::l_paren)
      ++ParenCount;
    else if (ParenCount)
      --ParenCount;       // Don't let unbalanced )'s drive the count negative.
    SourceLocation L = Tok.getLocation();
    PP.Lex(Tok);
    return L;
  }
  
  /// ConsumeBracket - This consume method keeps the bracket count up-to-date.
  ///
  SourceLocation ConsumeBracket() {
    assert(isTokenBracket() && "wrong consume method");
    if (Tok.getKind() == tok::l_square)
      ++BracketCount;
    else if (BracketCount)
      --BracketCount;     // Don't let unbalanced ]'s drive the count negative.
    
    SourceLocation L = Tok.getLocation();
    PP.Lex(Tok);
    return L;
  }
      
  /// ConsumeBrace - This consume method keeps the brace count up-to-date.
  ///
  SourceLocation ConsumeBrace() {
    assert(isTokenBrace() && "wrong consume method");
    if (Tok.getKind() == tok::l_brace)
      ++BraceCount;
    else if (BraceCount)
      --BraceCount;     // Don't let unbalanced }'s drive the count negative.
    
    SourceLocation L = Tok.getLocation();
    PP.Lex(Tok);
    return L;
  }
  
  /// ConsumeStringToken - Consume the current 'peek token', lexing a new one
  /// and returning the token kind.  This method is specific to strings, as it
  /// handles string literal concatenation, as per C99 5.1.1.2, translation
  /// phase #6.
  SourceLocation ConsumeStringToken() {
    assert(isTokenStringLiteral() &&
           "Should only consume string literals with this method");
    SourceLocation L = Tok.getLocation();
    PP.Lex(Tok);
    return L;
  }
  
  /// MatchRHSPunctuation - For punctuation with a LHS and RHS (e.g. '['/']'),
  /// this helper function matches and consumes the specified RHS token if
  /// present.  If not present, it emits the specified diagnostic indicating
  /// that the parser failed to match the RHS of the token at LHSLoc.  LHSName
  /// should be the name of the unmatched LHS token.  This returns the location
  /// of the consumed token.
  SourceLocation MatchRHSPunctuation(tok::TokenKind RHSTok,
                                     SourceLocation LHSLoc);
  
  /// ExpectAndConsume - The parser expects that 'ExpectedTok' is next in the
  /// input.  If so, it is consumed and false is returned.
  ///
  /// If the input is malformed, this emits the specified diagnostic.  Next, if
  /// SkipToTok is specified, it calls SkipUntil(SkipToTok).  Finally, true is
  /// returned.
  bool ExpectAndConsume(tok::TokenKind ExpectedTok, unsigned Diag,
                        const char *DiagMsg = "",
                        tok::TokenKind SkipToTok = tok::unknown);

  //===--------------------------------------------------------------------===//
  // Scope manipulation
  
  /// EnterScope - Start a new scope.
  void EnterScope(unsigned ScopeFlags);
  
  /// ExitScope - Pop a scope off the scope stack.
  void ExitScope();

  //===--------------------------------------------------------------------===//
  // Diagnostic Emission and Error recovery.
    
  void Diag(SourceLocation Loc, unsigned DiagID,
            const std::string &Msg = std::string());
  void Diag(const Token &Tok, unsigned DiagID,
            const std::string &M = std::string()) {
    Diag(Tok.getLocation(), DiagID, M);
  }
  
  /// SkipUntil - Read tokens until we get to the specified token, then consume
  /// it (unless DontConsume is true).  Because we cannot guarantee that the
  /// token will ever occur, this skips to the next token, or to some likely
  /// good stopping point.  If StopAtSemi is true, skipping will stop at a ';'
  /// character.
  /// 
  /// If SkipUntil finds the specified token, it returns true, otherwise it
  /// returns false.  
  bool SkipUntil(tok::TokenKind T, bool StopAtSemi = true,
                 bool DontConsume = false) {
    return SkipUntil(&T, 1, StopAtSemi, DontConsume);
  }
  bool SkipUntil(tok::TokenKind T1, tok::TokenKind T2, bool StopAtSemi = true,
                 bool DontConsume = false) {
    tok::TokenKind TokArray[] = {T1, T2};
    return SkipUntil(TokArray, 2, StopAtSemi, DontConsume);
  }
  bool SkipUntil(const tok::TokenKind *Toks, unsigned NumToks,
                 bool StopAtSemi = true, bool DontConsume = false);
   
  //===--------------------------------------------------------------------===//
  // C99 6.9: External Definitions.
  DeclTy *ParseExternalDeclaration();
  DeclTy *ParseDeclarationOrFunctionDefinition();
  DeclTy *ParseFunctionDefinition(Declarator &D);
  void ParseKNRParamDeclarations(Declarator &D);
  void ParseSimpleAsm();
  void ParseAsmStringLiteral();

  // Objective-C External Declarations
  DeclTy *ParseObjCAtDirectives(); 
  DeclTy *ParseObjCAtClassDeclaration(SourceLocation atLoc);
  DeclTy *ParseObjCAtInterfaceDeclaration(SourceLocation atLoc, 
                                          AttributeList *prefixAttrs = 0);
  void ParseObjCClassInstanceVariables(DeclTy *interfaceDecl, 
                                       SourceLocation atLoc);
  bool ParseObjCProtocolReferences(llvm::SmallVectorImpl<IdentifierInfo*> &,
                                   SourceLocation &endProtoLoc);
  void ParseObjCInterfaceDeclList(DeclTy *interfaceDecl,
				  tok::ObjCKeywordKind contextKey);
  DeclTy *ParseObjCAtProtocolDeclaration(SourceLocation atLoc);
  
  DeclTy *ObjcImpDecl;
  /// Vector is used to collect method decls for each @implementation
  llvm::SmallVector<DeclTy*, 32>  AllImplMethods;
  DeclTy *ParseObjCAtImplementationDeclaration(SourceLocation atLoc);
  DeclTy *ParseObjCAtEndDeclaration(SourceLocation atLoc);
  DeclTy *ParseObjCAtAliasDeclaration(SourceLocation atLoc);
  DeclTy *ParseObjCPropertySynthesize(SourceLocation atLoc);
  DeclTy *ParseObjCPropertyDynamic(SourceLocation atLoc);
  
  IdentifierInfo *ParseObjCSelector(SourceLocation &MethodLocation);
  // Definitions for Objective-c context sensitive keywords recognition.
  enum ObjCTypeQual {
    objc_in=0, objc_out, objc_inout, objc_oneway, objc_bycopy, objc_byref,
    objc_NumQuals
  };
  IdentifierInfo *ObjcTypeQuals[objc_NumQuals];
  // Definitions for ObjC2's @property attributes.
  enum ObjCPropertyAttr {
    objc_readonly=0, objc_getter, objc_setter, objc_assign, 
    objc_readwrite, objc_retain, objc_copy, objc_nonatomic, objc_NumAttrs
  };
  IdentifierInfo *ObjcPropertyAttrs[objc_NumAttrs];
  bool isObjCPropertyAttribute();

  TypeTy *ParseObjCTypeName(ObjcDeclSpec &DS);
  void ParseObjCMethodRequirement();
  DeclTy *ParseObjCMethodPrototype(DeclTy *classOrCat,
   	    tok::ObjCKeywordKind MethodImplKind = tok::objc_not_keyword);
  DeclTy *ParseObjCMethodDecl(SourceLocation mLoc, tok::TokenKind mType,
            tok::ObjCKeywordKind MethodImplKind = tok::objc_not_keyword);
  void ParseObjCPropertyAttribute(ObjcDeclSpec &DS);
  DeclTy *ParseObjCPropertyDecl(DeclTy *interfaceDecl, SourceLocation AtLoc);
  
  void ParseObjCInstanceMethodDefinition();
  void ParseObjCClassMethodDefinition();
  
  //===--------------------------------------------------------------------===//
  // C99 6.5: Expressions.

  typedef Action::ExprResult ExprResult;
  typedef Action::StmtResult StmtResult;
  
  ExprResult ParseExpression();
  ExprResult ParseConstantExpression();
  ExprResult ParseAssignmentExpression();  // Expr that doesn't include commas.
  
  ExprResult ParseExpressionWithLeadingIdentifier(const Token &Tok);
  ExprResult ParseExpressionWithLeadingAt(SourceLocation AtLoc);
  ExprResult ParseAssignmentExprWithLeadingIdentifier(const Token &Tok);
  ExprResult ParseAssignmentExpressionWithLeadingStar(const Token &Tok);

  ExprResult ParseRHSOfBinaryExpression(ExprResult LHS, unsigned MinPrec);
  ExprResult ParseCastExpression(bool isUnaryExpression);
  ExprResult ParsePostfixExpressionSuffix(ExprResult LHS);
  ExprResult ParseSizeofAlignofExpression();
  ExprResult ParseBuiltinPrimaryExpression();
  
  /// ParenParseOption - Control what ParseParenExpression will parse.
  enum ParenParseOption {
    SimpleExpr,      // Only parse '(' expression ')'
    CompoundStmt,    // Also allow '(' compound-statement ')'
    CompoundLiteral, // Also allow '(' type-name ')' '{' ... '}'
    CastExpr         // Also allow '(' type-name ')' <anything>
  };
  ExprResult ParseParenExpression(ParenParseOption &ExprType, TypeTy *&CastTy,
                                  SourceLocation &RParenLoc);
  
  ExprResult ParseSimpleParenExpression() {  // Parse SimpleExpr only.
    SourceLocation RParenLoc;
    return ParseSimpleParenExpression(RParenLoc);
  }
  ExprResult ParseSimpleParenExpression(SourceLocation &RParenLoc) {
    ParenParseOption Op = SimpleExpr;
    TypeTy *CastTy;
    return ParseParenExpression(Op, CastTy, RParenLoc);
  }
  ExprResult ParseStringLiteralExpression();
  
  //===--------------------------------------------------------------------===//
  // C++ 5.2p1: C++ Casts
  ExprResult ParseCXXCasts();

  //===--------------------------------------------------------------------===//
  // C++ 2.13.5: C++ Boolean Literals
  ExprResult ParseCXXBoolLiteral();

  //===--------------------------------------------------------------------===//
  // C99 6.7.8: Initialization.
  ExprResult ParseInitializer();
  ExprResult ParseInitializerWithPotentialDesignator();
  
  //===--------------------------------------------------------------------===//
  // Objective-C Expressions
  ExprResult ParseObjCAtExpression(SourceLocation AtLocation);
  ExprResult ParseObjCStringLiteral(SourceLocation AtLoc);
  ExprResult ParseObjCEncodeExpression(SourceLocation AtLoc);
  ExprResult ParseObjCSelectorExpression(SourceLocation AtLoc);
  ExprResult ParseObjCProtocolExpression(SourceLocation AtLoc);
  ExprResult ParseObjCMessageExpression();

  //===--------------------------------------------------------------------===//
  // C99 6.8: Statements and Blocks.
  
  StmtResult ParseStatement() { return ParseStatementOrDeclaration(true); }
  StmtResult ParseStatementOrDeclaration(bool OnlyStatement = false);
  StmtResult ParseIdentifierStatement(bool OnlyStatement);
  StmtResult ParseCaseStatement();
  StmtResult ParseDefaultStatement();
  StmtResult ParseCompoundStatement(bool isStmtExpr = false);
  StmtResult ParseCompoundStatementBody(bool isStmtExpr = false);
  StmtResult ParseIfStatement();
  StmtResult ParseSwitchStatement();
  StmtResult ParseWhileStatement();
  StmtResult ParseDoStatement();
  StmtResult ParseForStatement();
  StmtResult ParseGotoStatement();
  StmtResult ParseContinueStatement();
  StmtResult ParseBreakStatement();
  StmtResult ParseReturnStatement();
  StmtResult ParseAsmStatement();
  StmtResult ParseObjCTryStmt(SourceLocation atLoc);
  StmtResult ParseObjCThrowStmt(SourceLocation atLoc);
  void ParseAsmOperandsOpt();

  //===--------------------------------------------------------------------===//
  // C99 6.7: Declarations.
  
  DeclTy *ParseDeclaration(unsigned Context);
  DeclTy *ParseSimpleDeclaration(unsigned Context);
  DeclTy *ParseInitDeclaratorListAfterFirstDeclarator(Declarator &D);
  DeclTy *ParseFunctionStatementBody(DeclTy *Decl, 
                                     SourceLocation L, SourceLocation R);
  void ParseDeclarationSpecifiers(DeclSpec &DS);
  void ParseSpecifierQualifierList(DeclSpec &DS);
  
  void ParseObjcTypeQualifierList(ObjcDeclSpec &DS);

  bool ParseTag(DeclTy *&Decl, unsigned TagType, SourceLocation StartLoc);
  void ParseEnumSpecifier(DeclSpec &DS);
  void ParseEnumBody(SourceLocation StartLoc, DeclTy *TagDecl);
  void ParseStructUnionSpecifier(DeclSpec &DS);
  void ParseStructUnionBody(SourceLocation StartLoc, unsigned TagType,
                            DeclTy *TagDecl);
  void ParseStructDeclaration(DeclTy *TagDecl,
                              llvm::SmallVectorImpl<DeclTy*> &FieldDecls);
                              
  bool isDeclarationSpecifier() const;
  bool isTypeSpecifierQualifier() const;

  TypeTy *ParseTypeName();
  AttributeList *ParseAttributes();
  void ParseTypeofSpecifier(DeclSpec &DS);
  
  /// ParseDeclarator - Parse and verify a newly-initialized declarator.
  void ParseDeclarator(Declarator &D);
  void ParseDeclaratorInternal(Declarator &D);
  void ParseTypeQualifierListOpt(DeclSpec &DS);
  void ParseDirectDeclarator(Declarator &D);
  void ParseParenDeclarator(Declarator &D);
  void ParseBracketDeclarator(Declarator &D);
  
  //===--------------------------------------------------------------------===//
  // C++ 7: Declarations [dcl.dcl]
  
  DeclTy *ParseNamespace(unsigned Context);

};

}  // end namespace clang

#endif
