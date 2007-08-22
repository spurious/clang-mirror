//===--- ParseObjc.cpp - Objective C Parsing ------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file was developed by Steve Naroff and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file implements the Objective-C portions of the Parser interface.
//
//===----------------------------------------------------------------------===//

#include "clang/Parse/Parser.h"
#include "clang/Parse/DeclSpec.h"
#include "clang/Basic/Diagnostic.h"
#include "llvm/ADT/SmallVector.h"
using namespace clang;


/// ParseExternalDeclaration:
///       external-declaration: [C99 6.9]
/// [OBJC]  objc-class-definition
/// [OBJC]  objc-class-declaration     [TODO]
/// [OBJC]  objc-alias-declaration     [TODO]
/// [OBJC]  objc-protocol-definition   [TODO]
/// [OBJC]  objc-method-definition     [TODO]
/// [OBJC]  '@' 'end'                  [TODO]
Parser::DeclTy *Parser::ParseObjCAtDirectives() {
  SourceLocation AtLoc = ConsumeToken(); // the "@"
  
  IdentifierInfo *II = Tok.getIdentifierInfo();
  switch (II ? II->getObjCKeywordID() : tok::objc_not_keyword) {
    case tok::objc_class:
      return ParseObjCAtClassDeclaration(AtLoc);
    case tok::objc_interface:
      return ParseObjCAtInterfaceDeclaration(AtLoc);
    case tok::objc_protocol:
      return ParseObjCAtProtocolDeclaration(AtLoc);
    case tok::objc_implementation:
      return ParseObjCAtImplementationDeclaration();
    case tok::objc_end:
      return ParseObjCAtEndDeclaration();
    case tok::objc_compatibility_alias:
      return ParseObjCAtAliasDeclaration();
    default:
      Diag(AtLoc, diag::err_unexpected_at);
      SkipUntil(tok::semi);
      return 0;
  }
}

///
/// objc-class-declaration: 
///    '@' 'class' identifier-list ';'
///  
Parser::DeclTy *Parser::ParseObjCAtClassDeclaration(SourceLocation atLoc) {
  ConsumeToken(); // the identifier "class"
  llvm::SmallVector<IdentifierInfo *, 8> ClassNames;
  
  while (1) {
    if (Tok.getKind() != tok::identifier) {
      Diag(Tok, diag::err_expected_ident);
      SkipUntil(tok::semi);
      return 0;
    }
    ClassNames.push_back(Tok.getIdentifierInfo());
    ConsumeToken();
    
    if (Tok.getKind() != tok::comma)
      break;
    
    ConsumeToken();
  }
  
  // Consume the ';'.
  if (ExpectAndConsume(tok::semi, diag::err_expected_semi_after, "@class"))
    return 0;
  
  return Actions.ParsedObjcClassDeclaration(CurScope,
                                            &ClassNames[0], ClassNames.size());
}

///
///   objc-interface:
///     objc-class-interface-attributes[opt] objc-class-interface
///     objc-category-interface
///
///   objc-class-interface:
///     '@' 'interface' identifier objc-superclass[opt] 
///       objc-protocol-refs[opt]
///       objc-class-instance-variables[opt] 
///       objc-interface-decl-list
///     @end
///
///   objc-category-interface:
///     '@' 'interface' identifier '(' identifier[opt] ')' 
///       objc-protocol-refs[opt]
///       objc-interface-decl-list
///     @end
///
///   objc-superclass:
///     ':' identifier
///
///   objc-class-interface-attributes:
///     __attribute__((visibility("default")))
///     __attribute__((visibility("hidden")))
///     __attribute__((deprecated))
///     __attribute__((unavailable))
///     __attribute__((objc_exception)) - used by NSException on 64-bit
///
Parser::DeclTy *Parser::ParseObjCAtInterfaceDeclaration(
  SourceLocation atLoc, AttributeList *attrList) {
  assert((Tok.getKind() == tok::identifier &&
          Tok.getIdentifierInfo()->getObjCKeywordID() == tok::objc_interface) &&
         "ParseObjCAtInterfaceDeclaration(): Expected @interface");
  ConsumeToken(); // the "interface" identifier
  
  if (Tok.getKind() != tok::identifier) {
    Diag(Tok, diag::err_expected_ident); // missing class or category name.
    return 0;
  }
  // We have a class or category name - consume it.
  IdentifierInfo *nameId = Tok.getIdentifierInfo();
  SourceLocation nameLoc = ConsumeToken();
  
  if (Tok.getKind() == tok::l_paren) { // we have a category
    SourceLocation lparenLoc = ConsumeParen();
    SourceLocation categoryLoc, rparenLoc;
    IdentifierInfo *categoryId = 0;
    
    // OBJC2: The cateogry name is optional (not an error).
    if (Tok.getKind() == tok::identifier) {
      categoryId = Tok.getIdentifierInfo();
      categoryLoc = ConsumeToken();
    }
    if (Tok.getKind() != tok::r_paren) {
      Diag(Tok, diag::err_expected_rparen);
      SkipUntil(tok::r_paren, false); // don't stop at ';'
      return 0;
    }
    rparenLoc = ConsumeParen();
    // Next, we need to check for any protocol references.
    if (Tok.getKind() == tok::less) {
      if (ParseObjCProtocolReferences())
        return 0;
    }
    if (attrList) // categories don't support attributes.
      Diag(Tok, diag::err_objc_no_attributes_on_category);
    
    ParseObjCInterfaceDeclList(0/*FIXME*/);

    // The @ sign was already consumed by ParseObjCInterfaceDeclList().
    if (Tok.getKind() == tok::identifier &&
        Tok.getIdentifierInfo()->getObjCKeywordID() == tok::objc_end) {
      ConsumeToken(); // the "end" identifier
      return 0;
    }
    Diag(Tok, diag::err_objc_missing_end);
    return 0;
  }
  // Parse a class interface.
  IdentifierInfo *superClassId = 0;
  SourceLocation superClassLoc;

  // FIXME: temporary hack to grok class names (until we have sema support).
  llvm::SmallVector<IdentifierInfo *, 1> ClassName;
  ClassName.push_back(nameId);
  Actions.ParsedObjcClassDeclaration(CurScope, &ClassName[0], 1);
  
  if (Tok.getKind() == tok::colon) { // a super class is specified.
    ConsumeToken();
    if (Tok.getKind() != tok::identifier) {
      Diag(Tok, diag::err_expected_ident); // missing super class name.
      return 0;
    }
    superClassId = Tok.getIdentifierInfo();
    superClassLoc = ConsumeToken();
  }
  // Next, we need to check for any protocol references.
  if (Tok.getKind() == tok::less) {
    if (ParseObjCProtocolReferences())
      return 0;
  }
  // FIXME: add Actions.StartObjCClassInterface(nameId, superClassId, ...)
  if (Tok.getKind() == tok::l_brace)
    ParseObjCClassInstanceVariables(0/*FIXME*/);

  ParseObjCInterfaceDeclList(0/*FIXME*/);

  // The @ sign was already consumed by ParseObjCInterfaceDeclList().
  if (Tok.getKind() == tok::identifier &&
      Tok.getIdentifierInfo()->getObjCKeywordID() == tok::objc_end) {
    ConsumeToken(); // the "end" identifier
    return 0;
  }
  Diag(Tok, diag::err_objc_missing_end);
  return 0;
}

///   objc-interface-decl-list:
///     empty
///     objc-interface-decl-list objc-property-decl [OBJC2]
///     objc-interface-decl-list objc-method-requirement [OBJC2]
///     objc-interface-decl-list objc-method-proto
///     objc-interface-decl-list declaration
///     objc-interface-decl-list ';'
///
///   objc-method-requirement: [OBJC2]
///     @required
///     @optional
///
void Parser::ParseObjCInterfaceDeclList(DeclTy *interfaceDecl) {
  while (1) {
    if (Tok.getKind() == tok::at) {
      SourceLocation AtLoc = ConsumeToken(); // the "@"
      tok::ObjCKeywordKind ocKind = Tok.getIdentifierInfo()->getObjCKeywordID();
      
      if (ocKind == tok::objc_end) { // terminate list
        return;
      } else if (ocKind == tok::objc_required) { // protocols only
        ConsumeToken();
        continue;
      } else if (ocKind == tok::objc_optional) { // protocols only
        ConsumeToken();
        continue;
      } else if (ocKind == tok::objc_property) {
        ParseObjCPropertyDecl(AtLoc);
        continue;
      } else {
        Diag(Tok, diag::err_objc_illegal_interface_qual);
        ConsumeToken();
      }
    }
    if (Tok.getKind() == tok::minus || Tok.getKind() == tok::plus) {
      ParseObjCMethodPrototype();
      continue;
    }
    if (Tok.getKind() == tok::semi)
      ConsumeToken();
    else if (Tok.getKind() == tok::eof)
      return;
    else 
      // FIXME: as the name implies, this rule allows function definitions.
      // We could pass a flag or check for functions during semantic analysis.
      ParseDeclarationOrFunctionDefinition();
  }
}

void Parser::ParseObjCPropertyDecl(SourceLocation atLoc) {
  assert(0 && "Unimp");
}

///   objc-methodproto:
///     objc-instance-method objc-method-decl objc-method-attributes[opt] ';'
///     objc-class-method objc-method-decl objc-method-attributes[opt] ';'
///
///   objc-instance-method: '-'
///   objc-class-method: '+'
///
///   objc-method-attributes:         [OBJC2]
///     __attribute__((deprecated))
///
void Parser::ParseObjCMethodPrototype() {
  assert((Tok.getKind() == tok::minus || Tok.getKind() == tok::plus) && 
         "expected +/-");

  tok::TokenKind methodType = Tok.getKind();  
  SourceLocation methodLoc = ConsumeToken();
  
  // FIXME: deal with "context sensitive" protocol qualifiers in prototypes
  ParseObjCMethodDecl(methodType, methodLoc);
  
  // If attributes exist after the method, parse them.
  if (Tok.getKind() == tok::kw___attribute)
    ParseAttributes();
    
  // Consume the ';'.
  ExpectAndConsume(tok::semi, diag::err_expected_semi_after, "method proto");
}

///   objc-selector:
///     identifier
///     one of
///       enum struct union if else while do for switch case default
///       break continue return goto asm sizeof typeof __alignof
///       unsigned long const short volatile signed restrict _Complex
///       in out inout bycopy byref oneway int char float double void _Bool
///
IdentifierInfo *Parser::ParseObjCSelector() {
  tok::TokenKind tKind = Tok.getKind();
  IdentifierInfo *II = 0;
  
  if (tKind == tok::identifier || 
      (tKind >= tok::kw_auto && tKind <= tok::kw__Complex)) {
    // FIXME: make sure the list of keywords jives with gcc. For example,
    // the above test does not include in/out/inout/bycopy/byref/oneway.
    II = Tok.getIdentifierInfo();
    ConsumeToken();
  } 
  return II;
}

///   objc-type-qualifier: one of
///     in out inout bycopy byref oneway
///
///   FIXME: remove the string compares...
bool Parser::isObjCTypeQualifier() {
  if (Tok.getKind() == tok::identifier) {
    const char *qual = Tok.getIdentifierInfo()->getName();
    return (strcmp(qual, "in") == 0) || (strcmp(qual, "out") == 0) ||
           (strcmp(qual, "inout") == 0) || (strcmp(qual, "oneway") == 0) ||
           (strcmp(qual, "bycopy") == 0) || (strcmp(qual, "byref") == 0);
  }
  return false;
}

///   objc-type-name:
///     '(' objc-type-qualifiers[opt] type-name ')'
///     '(' objc-type-qualifiers[opt] ')'
///
///   objc-type-qualifiers:
///     objc-type-qualifier
///     objc-type-qualifiers objc-type-qualifier
///
void Parser::ParseObjCTypeName() {
  assert(Tok.getKind() == tok::l_paren && "expected (");
  
  SourceLocation LParenLoc = ConsumeParen(), RParenLoc;
  
  while (isObjCTypeQualifier())
    ConsumeToken();

  if (isTypeSpecifierQualifier()) {
    //TypeTy *Ty = ParseTypeName();
    //assert(Ty && "Parser::ParseObjCTypeName(): missing type");
    ParseTypeName(); // FIXME: when sema support is added.
  }
  if (Tok.getKind() != tok::r_paren) {
    MatchRHSPunctuation(tok::r_paren, LParenLoc);
    return;
  }
  RParenLoc = ConsumeParen();
}

///   objc-method-decl:
///     objc-selector
///     objc-keyword-selector objc-parmlist[opt]
///     objc-type-name objc-selector
///     objc-type-name objc-keyword-selector objc-parmlist[opt]
///
///   objc-keyword-selector:
///     objc-keyword-decl 
///     objc-keyword-selector objc-keyword-decl
///
///   objc-keyword-decl:
///     objc-selector ':' objc-type-name objc-keyword-attributes[opt] identifier
///     objc-selector ':' objc-keyword-attributes[opt] identifier
///     ':' objc-type-name objc-keyword-attributes[opt] identifier
///     ':' objc-keyword-attributes[opt] identifier
///
///   objc-parmlist:
///     objc-parms objc-ellipsis[opt]
///
///   objc-parms:
///     objc-parms , parameter-declaration
///
///   objc-ellipsis:
///     , ...
///
///   objc-keyword-attributes:         [OBJC2]
///     __attribute__((unused))
///
void Parser::ParseObjCMethodDecl(tok::TokenKind mType, SourceLocation mLoc) {

  // Parse the return type.
  if (Tok.getKind() == tok::l_paren)
    ParseObjCTypeName();
  IdentifierInfo *selIdent = ParseObjCSelector();
  
  if (Tok.getKind() == tok::colon) {
    IdentifierInfo *keywordSelector = selIdent;
    while (1) {
      // Each iteration parses a single keyword argument.
      if (Tok.getKind() != tok::colon) {
        Diag(Tok, diag::err_expected_colon);
        break;
      }
      ConsumeToken(); // Eat the ':'.
      if (Tok.getKind() == tok::l_paren) // Parse the argument type.
        ParseObjCTypeName();

      // If attributes exist before the argument name, parse them.
      if (Tok.getKind() == tok::kw___attribute)
        ParseAttributes();

      if (Tok.getKind() != tok::identifier) {
        Diag(Tok, diag::err_expected_ident); // missing argument name.
        break;
      }
      ConsumeToken(); // Eat the identifier.
      // FIXME: add Actions.BuildObjCKeyword()
      
      keywordSelector = ParseObjCSelector();
      if (!keywordSelector && Tok.getKind() != tok::colon)
        break;
      // We have a selector or a colon, continue parsing.
    }
    // Parse the (optional) parameter list.
    while (Tok.getKind() == tok::comma) {
      ConsumeToken();
      if (Tok.getKind() == tok::ellipsis) {
        ConsumeToken();
        break;
      }
      ParseDeclaration(Declarator::PrototypeContext);
    }
  } else if (!selIdent) {
    Diag(Tok, diag::err_expected_ident); // missing selector name.
  }
  // FIXME: add Actions.BuildMethodSignature().
}

///   objc-protocol-refs:
///     '<' identifier-list '>'
///
bool Parser::ParseObjCProtocolReferences() {
  assert(Tok.getKind() == tok::less && "expected <");
  
  ConsumeToken(); // the "<"
  llvm::SmallVector<IdentifierInfo *, 8> ProtocolRefs;
  
  while (1) {
    if (Tok.getKind() != tok::identifier) {
      Diag(Tok, diag::err_expected_ident);
      SkipUntil(tok::greater);
      return true;
    }
    ProtocolRefs.push_back(Tok.getIdentifierInfo());
    ConsumeToken();
    
    if (Tok.getKind() != tok::comma)
      break;
    ConsumeToken();
  }
  // Consume the '>'.
  return ExpectAndConsume(tok::greater, diag::err_expected_greater);
}

///   objc-class-instance-variables:
///     '{' objc-instance-variable-decl-list[opt] '}'
///
///   objc-instance-variable-decl-list:
///     objc-visibility-spec
///     objc-instance-variable-decl ';'
///     ';'
///     objc-instance-variable-decl-list objc-visibility-spec
///     objc-instance-variable-decl-list objc-instance-variable-decl ';'
///     objc-instance-variable-decl-list ';'
///
///   objc-visibility-spec:
///     @private
///     @protected
///     @public
///     @package [OBJC2]
///
///   objc-instance-variable-decl:
///     struct-declaration 
///
void Parser::ParseObjCClassInstanceVariables(DeclTy *interfaceDecl) {
  assert(Tok.getKind() == tok::l_brace && "expected {");
  
  SourceLocation LBraceLoc = ConsumeBrace(); // the "{"
  llvm::SmallVector<DeclTy*, 32> IvarDecls;
  
  // While we still have something to read, read the instance variables.
  while (Tok.getKind() != tok::r_brace && 
         Tok.getKind() != tok::eof) {
    // Each iteration of this loop reads one objc-instance-variable-decl.
    
    // Check for extraneous top-level semicolon.
    if (Tok.getKind() == tok::semi) {
      Diag(Tok, diag::ext_extra_struct_semi);
      ConsumeToken();
      continue;
    }
    // Set the default visibility to private.
    tok::ObjCKeywordKind visibility = tok::objc_private;
    if (Tok.getKind() == tok::at) { // parse objc-visibility-spec
      ConsumeToken(); // eat the @ sign
      IdentifierInfo *specId = Tok.getIdentifierInfo();
      switch (specId->getObjCKeywordID()) {
      case tok::objc_private:
      case tok::objc_public:
      case tok::objc_protected:
      case tok::objc_package:
        visibility = specId->getObjCKeywordID();
        ConsumeToken();
        continue; 
      default:
        Diag(Tok, diag::err_objc_illegal_visibility_spec);
        ConsumeToken();
        continue;
      }
    }
    ParseStructDeclaration(interfaceDecl, IvarDecls);

    if (Tok.getKind() == tok::semi) {
      ConsumeToken();
    } else if (Tok.getKind() == tok::r_brace) {
      Diag(Tok.getLocation(), diag::ext_expected_semi_decl_list);
      break;
    } else {
      Diag(Tok, diag::err_expected_semi_decl_list);
      // Skip to end of block or statement
      SkipUntil(tok::r_brace, true, true);
    }
  }
  MatchRHSPunctuation(tok::r_brace, LBraceLoc);
  return;
}

///   objc-protocol-declaration:
///     objc-protocol-definition
///     objc-protocol-forward-reference
///
///   objc-protocol-definition:
///     @protocol identifier 
///       objc-protocol-refs[opt] 
///       objc-methodprotolist 
///     @end
///
///   objc-protocol-forward-reference:
///     @protocol identifier-list ';'
///
///   "@protocol identifier ;" should be resolved as "@protocol
///   identifier-list ;": objc-methodprotolist may not start with a
///   semicolon in the first alternative if objc-protocol-refs are omitted.

Parser::DeclTy *Parser::ParseObjCAtProtocolDeclaration(SourceLocation AtLoc) {
  assert((Tok.getKind() == tok::identifier &&
          Tok.getIdentifierInfo()->getObjCKeywordID() == tok::objc_protocol) &&
         "ParseObjCAtProtocolDeclaration(): Expected @protocol");
  ConsumeToken(); // the "protocol" identifier
  
  if (Tok.getKind() != tok::identifier) {
    Diag(Tok, diag::err_expected_ident); // missing protocol name.
    return 0;
  }
  // Save the protocol name, then consume it.
  IdentifierInfo *protocolName = Tok.getIdentifierInfo();
  SourceLocation nameLoc = ConsumeToken();
  
  if (Tok.getKind() == tok::semi) { // forward declaration.
    ConsumeToken();
    return 0; // FIXME: add protocolName
  }
  if (Tok.getKind() == tok::comma) { // list of forward declarations.
    // Parse the list of forward declarations.
    llvm::SmallVector<IdentifierInfo *, 8> ProtocolRefs;
    ProtocolRefs.push_back(protocolName);
    
    while (1) {
      ConsumeToken(); // the ','
      if (Tok.getKind() != tok::identifier) {
        Diag(Tok, diag::err_expected_ident);
        SkipUntil(tok::semi);
        return 0;
      }
      ProtocolRefs.push_back(Tok.getIdentifierInfo());
      ConsumeToken(); // the identifier
      
      if (Tok.getKind() != tok::comma)
        break;
    }
    // Consume the ';'.
    if (ExpectAndConsume(tok::semi, diag::err_expected_semi_after, "@protocol"))
      return 0;
    return 0; // FIXME
  }
  // Last, and definitely not least, parse a protocol declaration.
  if (Tok.getKind() == tok::less) {
    if (ParseObjCProtocolReferences())
      return 0;
  }
  ParseObjCInterfaceDeclList(0/*FIXME*/);

  // The @ sign was already consumed by ParseObjCInterfaceDeclList().
  if (Tok.getKind() == tok::identifier &&
      Tok.getIdentifierInfo()->getObjCKeywordID() == tok::objc_end) {
    ConsumeToken(); // the "end" identifier
    return 0;
  }
  Diag(Tok, diag::err_objc_missing_end);
  return 0;
}

///   objc-implementation:
///     objc-class-implementation-prologue
///     objc-category-implementation-prologue
///
///   objc-class-implementation-prologue:
///     @implementation identifier objc-superclass[opt]
///       objc-class-instance-variables[opt]
///
///   objc-category-implementation-prologue:
///     @implementation identifier ( identifier )

Parser::DeclTy *Parser::ParseObjCAtImplementationDeclaration() {
  assert(0 && "Unimp");
  return 0;
}
Parser::DeclTy *Parser::ParseObjCAtEndDeclaration() {
  assert(0 && "Unimp");
  return 0;
}
Parser::DeclTy *Parser::ParseObjCAtAliasDeclaration() {
  assert(0 && "Unimp");
  return 0;
}

void Parser::ParseObjCInstanceMethodDefinition() {
  assert(0 && "Parser::ParseObjCInstanceMethodDefinition():: Unimp");
}

void Parser::ParseObjCClassMethodDefinition() {
  assert(0 && "Parser::ParseObjCClassMethodDefinition():: Unimp");
}

Parser::ExprResult Parser::ParseObjCExpression() {
  SourceLocation AtLoc = ConsumeToken(); // the "@"

  switch (Tok.getKind()) {
    case tok::string_literal:    // primary-expression: string-literal
    case tok::wide_string_literal:
      return ParseObjCStringLiteral();
    case tok::objc_encode:
      return ParseObjCEncodeExpression();
      break;
    default:
      Diag(AtLoc, diag::err_unexpected_at);
      SkipUntil(tok::semi);
      break;
  }
  
  return 0;
}

Parser::ExprResult Parser::ParseObjCStringLiteral() {
  ExprResult Res = ParseStringLiteralExpression();

  if (Res.isInvalid) return Res;

  return Actions.ParseObjCStringLiteral(Res.Val);
}

///    objc-encode-expression:
///      @encode ( type-name )
Parser::ExprResult Parser::ParseObjCEncodeExpression() {
  assert(Tok.getIdentifierInfo()->getObjCKeywordID() == tok::objc_encode && 
         "Not an @encode expression!");
  
  SourceLocation EncLoc = ConsumeToken();
  
  if (Tok.getKind() != tok::l_paren) {
    Diag(Tok, diag::err_expected_lparen_after, "@encode");
    return true;
  }
   
  SourceLocation LParenLoc = ConsumeParen();
  
  TypeTy *Ty = ParseTypeName();
  
  if (Tok.getKind() != tok::r_paren) {
    Diag(Tok, diag::err_expected_rparen);
    return true;
  }
   
  return Actions.ParseObjCEncodeExpression(EncLoc, LParenLoc, Ty, 
                                           ConsumeParen());
}
