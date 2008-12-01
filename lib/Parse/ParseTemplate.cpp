//===--- ParseTemplate.cpp - Template Parsing -----------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file implements parsing of C++ templates.
//
//===----------------------------------------------------------------------===//

#include "clang/Parse/Parser.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Parse/DeclSpec.h"
#include "clang/Parse/Scope.h"

using namespace clang;

/// ParseTemplateDeclaration - Parse a template declaration, which includes 
/// the template parameter list and either a function of class declaration.
///
///       template-declaration: [C++ temp]
///         'export'[opt] 'template' '<' template-parameter-list '>' declaration
Parser::DeclTy *Parser::ParseTemplateDeclaration(unsigned Context) {
  assert((Tok.is(tok::kw_export) || Tok.is(tok::kw_template)) && 
	 "Token does not start a template declaration.");
  
  // Consume the optional export token, if it exists, followed by the
  // namespace token.
  bool isExported = false;
  if(Tok.is(tok::kw_export)) {
    SourceLocation ExportLoc = ConsumeToken();
    if(!Tok.is(tok::kw_template)) {
      Diag(Tok.getLocation(), diag::err_expected_template);
      return 0;
    }
    isExported = true;
  }
  SourceLocation TemplateLoc = ConsumeToken();
  
  // Try to parse the template parameters, and the declaration if successful.
  if(ParseTemplateParameters(0)) {
    // For some reason, this is generating a compiler error when parsing the
    // declaration. Apparently, ParseDeclaration doesn't want to match a
    // function-definition, but will match a function declaration.
    // TODO: ParseDeclarationOrFunctionDefinition
    return ParseDeclaration(Context);
  }
  return 0;
}

/// ParseTemplateParameters - Parses a template-parameter-list enclosed in
/// angle brackets.
bool Parser::ParseTemplateParameters(DeclTy* TmpDecl) {
  // Get the template parameter list.
  if(!Tok.is(tok::less)) {
    Diag(Tok.getLocation(), diag::err_expected_less_after) << "template";
    return false;
  }
  ConsumeToken();
  
  // Try to parse the template parameter list.
  if(ParseTemplateParameterList(0)) {
    if(!Tok.is(tok::greater)) {
      Diag(Tok.getLocation(), diag::err_expected_greater);
      return false;
    }
    ConsumeToken();
  }
  return true;
}

/// ParseTemplateParameterList - Parse a template parameter list. If
/// the parsing fails badly (i.e., closing bracket was left out), this
/// will try to put the token stream in a reasonable position (closing
/// a statement, etc.) and return false. 
///
///       template-parameter-list:    [C++ temp]
///         template-parameter
///         template-parameter-list ',' template-parameter
bool Parser::ParseTemplateParameterList(DeclTy* TmpDecl) {
  // FIXME: For now, this is just going to consume the template parameters.
  // Eventually, we should pass the template decl AST node as a parameter and
  // apply template parameters as we find them.
  while(1) {
    DeclTy* TmpParam = ParseTemplateParameter();
    if(!TmpParam) {
      // If we failed to parse a template parameter, skip until we find
      // a comma or closing brace.
      SkipUntil(tok::comma, tok::greater, true, true);
    }
    
    // Did we find a comma or the end of the template parmeter list?
    if(Tok.is(tok::comma)) {
      ConsumeToken();
    } else if(Tok.is(tok::greater)) {
      // Don't consume this... that's done by template parser.
      break;
    } else {
      // Somebody probably forgot to close the template. Skip ahead and
      // try to get out of the expression. This error is currently
      // subsumed by whatever goes on in ParseTemplateParameter.
      // TODO: This could match >>, and it would be nice to avoid those
      // silly errors with template <vec<T>>.
      // Diag(Tok.getLocation(), diag::err_expected_comma_greater);
      SkipUntil(tok::greater, true, true);
      return false;
    }
  }
  return true;
}

/// ParseTemplateParameter - Parse a template-parameter (C++ [temp.param]).
///
///       template-parameter: [C++ temp.param]
///         type-parameter
///         parameter-declaration
///
///       type-parameter: (see below)
///         'class' identifier[opt]
///         'class' identifier[opt] '=' type-id
///         'typename' identifier[opt]
///         'typename' identifier[opt] '=' type-id
///         'template' '<' template-parameter-list '>' 'class' identifier[opt]
///         'template' '<' template-parameter-list '>' 'class' identifier[opt] = id-expression
Parser::DeclTy *Parser::ParseTemplateParameter() {
  TryAnnotateCXXScopeToken();

  if(Tok.is(tok::kw_class) 
     || (Tok.is(tok::kw_typename) && 
	 NextToken().isNot(tok::annot_qualtypename))) {
    return ParseTypeParameter();
  } else if(Tok.is(tok::kw_template)) {
    return ParseTemplateTemplateParameter();
  } else {
    // If it's none of the above, then it must be a parameter declaration.
    // NOTE: This will pick up errors in the closure of the template parameter
    // list (e.g., template < ; Check here to implement >> style closures.
    return ParseNonTypeTemplateParameter();
  }
  return 0;
}

/// ParseTypeParameter - Parse a template type parameter (C++ [temp.param]).
/// Other kinds of template parameters are parsed in
/// ParseTemplateTemplateParameter and ParseNonTypeTemplateParameter.
///
///       type-parameter:     [C++ temp.param]
///         'class' identifier[opt]
///         'class' identifier[opt] '=' type-id
///         'typename' identifier[opt]
///         'typename' identifier[opt] '=' type-id
Parser::DeclTy *Parser::ParseTypeParameter() {
  SourceLocation keyLoc = ConsumeToken();

  // Grab the template parameter name (if given)
  IdentifierInfo* paramName = 0;
  if(Tok.is(tok::identifier)) {
    paramName = Tok.getIdentifierInfo();
    ConsumeToken();
  } else if(Tok.is(tok::equal) || Tok.is(tok::comma) ||
	    Tok.is(tok::greater)) {
    // Unnamed template parameter. Don't have to do anything here, just
    // don't consume this token.
  } else {
    Diag(Tok.getLocation(), diag::err_expected_ident);
    return 0;
  }
  
  // Grab a default type id (if given).
  TypeTy* defaultType = 0;
  if(Tok.is(tok::equal)) {
    ConsumeToken();
    defaultType = ParseTypeName();
    if(!defaultType)
      return 0;
  }
  
  // FIXME: Add an action for type parameters.
  return 0;
}

/// ParseTemplateTemplateParameter - Handle the parsing of template
/// template parameters. 
///
///       type-parameter:    [C++ temp.param]
///         'template' '<' template-parameter-list '>' 'class' identifier[opt]
///         'template' '<' template-parameter-list '>' 'class' identifier[opt] = id-expression
Parser::DeclTy* Parser::ParseTemplateTemplateParameter() {
  assert(Tok.is(tok::kw_template) && "Expected 'template' keyword");

  // Handle the template <...> part.
  SourceLocation TemplateLoc = ConsumeToken();
  if(!ParseTemplateParameters(0)) {
    return 0;
  }

  // Generate a meaningful error if the user forgot to put class before the
  // identifier, comma, or greater.
  if(!Tok.is(tok::kw_class)) {
    Diag(Tok.getLocation(), diag::err_expected_class_before) 
      << PP.getSpelling(Tok);
    return 0;
  }
  SourceLocation ClassLoc = ConsumeToken();

  // Get the identifier, if given.
  IdentifierInfo* ident = 0;
  if(Tok.is(tok::identifier)) {
    ident = Tok.getIdentifierInfo();
    ConsumeToken();
  } else if(Tok.is(tok::equal) || Tok.is(tok::comma) || Tok.is(tok::greater)) {
    // Unnamed template parameter. Don't have to do anything here, just
    // don't consume this token.
  } else {
    Diag(Tok.getLocation(), diag::err_expected_ident);
    return 0;
  }

  // Get the a default value, if given.
  ExprResult defaultExpr;
  if(Tok.is(tok::equal)) {
    ConsumeToken();
    defaultExpr = ParseCXXIdExpression();
    if(defaultExpr.isInvalid) {
      return 0;
    }
  }

  // FIXME: Add an action for template template parameters.
  return 0;
}

/// ParseNonTypeTemplateParameter - Handle the parsing of non-type
/// template parameters (e.g., in "template<int Size> class array;"). 

///       template-parameter:
///         ...
///         parameter-declaration
///
/// NOTE: It would be ideal to simply call out to ParseParameterDeclaration(),
/// but that didn't work out to well. Instead, this tries to recrate the basic
/// parsing of parameter declarations, but tries to constrain it for template
/// parameters.
/// FIXME: We need to make ParseParameterDeclaration work for non-type 
/// template parameters, too.
Parser::DeclTy* Parser::ParseNonTypeTemplateParameter()
{
  SourceLocation startLoc = Tok.getLocation();

  // Parse the declaration-specifiers (i.e., the type).
  // FIXME:: The type should probably be restricted in some way... Not all
  // declarators (parts of declarators?) are accepted for parameters.
  DeclSpec ds;
  ParseDeclarationSpecifiers(ds);

  // Parse this as a typename.
  Declarator decl(ds, Declarator::TypeNameContext);
  ParseDeclarator(decl);
  if(ds.getTypeSpecType() == DeclSpec::TST_unspecified && !ds.getTypeRep()) {
    // This probably shouldn't happen - and it's more of a Sema thing, but
    // basically we didn't parse the type name because we couldn't associate
    // it with an AST node. we should just skip to the comma or greater.
    // TODO: This is currently a placeholder for some kind of Sema Error.
    Diag(Tok.getLocation(), diag::err_parse_error);
    SkipUntil(tok::comma, tok::greater, true, true);
    return 0;
  }

  // If there's an identifier after the typename, parse that as part of the
  // declarator - or something.
  if(Tok.is(tok::identifier)) {
    ConsumeToken();
  }

  // Is there a default value? Parsing this can be fairly annoying because
  // we have to stop on the first non-nested (paren'd) '>' as the closure
  // for the template parameter list. Or a ','.
  if(Tok.is(tok::equal)) {
    // TODO: Implement default non-type values.
    SkipUntil(tok::comma, tok::greater, true, true);
  }
  
  // FIXME: Add an action for non-type template parameters.
  return 0;
}
