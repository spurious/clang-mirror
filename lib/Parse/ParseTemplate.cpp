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
  
  // Enter template-parameter scope.
  ParseScope TemplateParmScope(this, Scope::TemplateParamScope);

  // Try to parse the template parameters, and the declaration if
  // successful.
  DeclTy *TemplateDecl = 0;
  if (Tok.is(tok::less) && NextToken().is(tok::greater)) {
    // This is a template specialization. Just consume the angle
    // brackets and parse the declaration or function definition that
    // follows.
    // FIXME: Record somehow that we're in an explicit specialization.
    ConsumeToken();
    ConsumeToken();
    TemplateParmScope.Exit();
    TemplateDecl = ParseDeclarationOrFunctionDefinition();
  } else {
    if(ParseTemplateParameters(0))
      TemplateDecl = ParseDeclarationOrFunctionDefinition();
  }

  return TemplateDecl;
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
  assert((Tok.is(tok::kw_class) || Tok.is(tok::kw_typename)) &&
	 "A type-parameter starts with 'class' or 'typename'");

  // Consume the 'class' or 'typename' keyword.
  bool TypenameKeyword = Tok.is(tok::kw_typename);
  SourceLocation KeyLoc = ConsumeToken();

  // Grab the template parameter name (if given)
  SourceLocation NameLoc;
  IdentifierInfo* ParamName = 0;
  if(Tok.is(tok::identifier)) {
    ParamName = Tok.getIdentifierInfo();
    NameLoc = ConsumeToken();
  } else if(Tok.is(tok::equal) || Tok.is(tok::comma) ||
	    Tok.is(tok::greater)) {
    // Unnamed template parameter. Don't have to do anything here, just
    // don't consume this token.
  } else {
    Diag(Tok.getLocation(), diag::err_expected_ident);
    return 0;
  }
  
  DeclTy *TypeParam = Actions.ActOnTypeParameter(CurScope, TypenameKeyword, 
						 KeyLoc, ParamName, NameLoc);

  // Grab a default type id (if given).
  if(Tok.is(tok::equal)) {
    SourceLocation EqualLoc = ConsumeToken();
    TypeTy *DefaultType = ParseTypeName();
    if(DefaultType)
      Actions.ActOnTypeParameterDefault(TypeParam, DefaultType);
  }
  
  return TypeParam;
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
  OwningExprResult DefaultExpr(Actions);
  if(Tok.is(tok::equal)) {
    ConsumeToken();
    DefaultExpr = ParseCXXIdExpression();
    if(DefaultExpr.isInvalid()) {
      return 0;
    }
  }

  // FIXME: Add an action for template template parameters.
  return 0;
}

/// ParseNonTypeTemplateParameter - Handle the parsing of non-type
/// template parameters (e.g., in "template<int Size> class array;"). 
///
///       template-parameter:
///         ...
///         parameter-declaration
///
/// NOTE: It would be ideal to simply call out to ParseParameterDeclaration(),
/// but that didn't work out to well. Instead, this tries to recrate the basic
/// parsing of parameter declarations, but tries to constrain it for template
/// parameters.
/// FIXME: We need to make a ParseParameterDeclaration that works for
/// non-type template parameters and normal function parameters.
Parser::DeclTy* Parser::ParseNonTypeTemplateParameter() {
  SourceLocation StartLoc = Tok.getLocation();

  // Parse the declaration-specifiers (i.e., the type).
  // FIXME: The type should probably be restricted in some way... Not all
  // declarators (parts of declarators?) are accepted for parameters.
  DeclSpec DS;
  ParseDeclarationSpecifiers(DS);

  // Parse this as a typename.
  Declarator ParamDecl(DS, Declarator::TemplateParamContext);
  ParseDeclarator(ParamDecl);
  if(DS.getTypeSpecType() == DeclSpec::TST_unspecified && !DS.getTypeRep()) {
    // This probably shouldn't happen - and it's more of a Sema thing, but
    // basically we didn't parse the type name because we couldn't associate
    // it with an AST node. we should just skip to the comma or greater.
    // TODO: This is currently a placeholder for some kind of Sema Error.
    Diag(Tok.getLocation(), diag::err_parse_error);
    SkipUntil(tok::comma, tok::greater, true, true);
    return 0;
  }

  // Create the parameter. 
  DeclTy *Param = Actions.ActOnNonTypeTemplateParameter(CurScope, ParamDecl);

  // Is there a default value? Parsing this can be fairly annoying because
  // we have to stop on the first non-nested (paren'd) '>' as the closure
  // for the template parameter list. Or a ','.
  if(Tok.is(tok::equal)) {
    // TODO: Implement default non-type values.
    SkipUntil(tok::comma, tok::greater, true, true);
  }
  
  return Param;
}

/// AnnotateTemplateIdToken - The current token is an identifier that
/// refers to the template declaration Template, and is followed by a
/// '<'. Turn this template-id into a template-id annotation token.
void Parser::AnnotateTemplateIdToken(DeclTy *Template, const CXXScopeSpec *SS) {
  assert(getLang().CPlusPlus && "Can only annotate template-ids in C++");
  assert(Template && Tok.is(tok::identifier) && NextToken().is(tok::less) &&
         "Parser isn't at the beginning of a template-id");

  // Consume the template-name.
  SourceLocation TemplateNameLoc = ConsumeToken();

  // Consume the '<'.
  SourceLocation LAngleLoc = ConsumeToken();

  // Parse the optional template-argument-list.
  TemplateArgList TemplateArgs;
  if (Tok.isNot(tok::greater) && ParseTemplateArgumentList(TemplateArgs)) {
    // Try to find the closing '>'.
    SkipUntil(tok::greater, true, true);

    // FIXME: What's our recovery strategy for failed template-argument-lists?
    return;
  }

  if (Tok.isNot(tok::greater))
    return;

  // Determine the location of the '>'. We won't actually consume this
  // token, because we'll be replacing it with the template-id.
  SourceLocation RAngleLoc = Tok.getLocation();
  
  Tok.setKind(tok::annot_template_id);
  Tok.setAnnotationEndLoc(RAngleLoc);
  Tok.setLocation(TemplateNameLoc);
  if (SS && SS->isNotEmpty())
    Tok.setLocation(SS->getBeginLoc());

  TemplateIdAnnotation *TemplateId 
    = (TemplateIdAnnotation *)malloc(sizeof(TemplateIdAnnotation) + 
                                  sizeof(TemplateArgTy*) * TemplateArgs.size());
  TemplateId->TemplateNameLoc = TemplateNameLoc;
  TemplateId->Template = Template;
  TemplateId->LAngleLoc = LAngleLoc;
  TemplateId->NumArgs = TemplateArgs.size();
  TemplateArgTy **Args = (TemplateArgTy**)(TemplateId + 1);
  for (unsigned Arg = 0, ArgEnd = TemplateArgs.size(); Arg != ArgEnd; ++Arg)
    Args[Arg] = TemplateArgs[Arg];
  Tok.setAnnotationValue(TemplateId);

  // In case the tokens were cached, have Preprocessor replace them with the
  // annotation token.
  PP.AnnotateCachedTokens(Tok);
}

/// ParseTemplateArgument - Parse a C++ template argument (C++ [temp.names]).
///
///       template-argument: [C++ 14.2]
///         assignment-expression
///         type-id
///         id-expression
Parser::OwningTemplateArgResult Parser::ParseTemplateArgument() {
  // FIXME: Implement this!
  return TemplateArgError();
}

/// ParseTemplateArgumentList - Parse a C++ template-argument-list
/// (C++ [temp.names]). Returns true if there was an error.
///
///       template-argument-list: [C++ 14.2]
///         template-argument
///         template-argument-list ',' template-argument
bool Parser::ParseTemplateArgumentList(TemplateArgList &TemplateArgs) {
  while (true) {
    OwningTemplateArgResult Arg = ParseTemplateArgument();
    if (Arg.isInvalid()) {
      SkipUntil(tok::comma, tok::greater, true, true);
      return true;
    }
    else
      TemplateArgs.push_back(Arg.release());
    
    // If the next token is a comma, consume it and keep reading
    // arguments.
    if (Tok.isNot(tok::comma)) break;

    // Consume the comma.
    ConsumeToken();
  }

  return Tok.isNot(tok::greater);
}

