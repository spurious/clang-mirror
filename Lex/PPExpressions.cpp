//===--- PPExpressions.cpp - Preprocessor Expression Evaluation -----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file was developed by Chris Lattner and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the Preprocessor::EvaluateDirectiveExpression method,
// which parses and evaluates integer constant expressions for #if directives.
//
//===----------------------------------------------------------------------===//
//
// FIXME: implement testing for #assert's.
// FIXME: Track signed/unsigned correctly.
// FIXME: Track and report integer overflow correctly.
//
//===----------------------------------------------------------------------===//

#include "clang/Lex/Preprocessor.h"
#include "clang/Lex/MacroInfo.h"
#include "clang/Lex/LiteralSupport.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Basic/TokenKinds.h"
#include "clang/Basic/Diagnostic.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/SmallString.h"
using namespace llvm;
using namespace clang;

static bool EvaluateDirectiveSubExpr(APInt &LHS, unsigned MinPrec,
                                     LexerToken &PeekTok, Preprocessor &PP);

/// DefinedTracker - This struct is used while parsing expressions to keep track
/// of whether !defined(X) has been seen.
///
/// With this simple scheme, we handle the basic forms:
///    !defined(X)   and !defined X
/// but we also trivially handle (silly) stuff like:
///    !!!defined(X) and +!defined(X) and !+!+!defined(X) and !(defined(X)).
struct DefinedTracker {
  /// Each time a Value is evaluated, it returns information about whether the
  /// parsed value is of the form defined(X), !defined(X) or is something else.
  enum TrackerState {
    DefinedMacro,        // defined(X)
    NotDefinedMacro,     // !defined(X)
    Unknown              // Something else.
  } State;
  /// TheMacro - When the state is DefinedMacro or NotDefinedMacro, this
  /// indicates the macro that was checked.
  IdentifierInfo *TheMacro;
};



/// EvaluateValue - Evaluate the token PeekTok (and any others needed) and
/// return the computed value in Result.  Return true if there was an error
/// parsing.  This function also returns information about the form of the
/// expression in DT.  See above for information on what DT means.
static bool EvaluateValue(APInt &Result, LexerToken &PeekTok,
                          DefinedTracker &DT, Preprocessor &PP) {
  Result = 0;
  DT.State = DefinedTracker::Unknown;
  
  // If this token's spelling is a pp-identifier, check to see if it is
  // 'defined' or if it is a macro.  Note that we check here because many
  // keywords are pp-identifiers, so we can't check the kind.
  if (IdentifierInfo *II = PeekTok.getIdentifierInfo()) {
    // If this identifier isn't 'defined' and it wasn't macro expanded, it turns
    // into a simple 0.
    if (II->getPPKeywordID() != tok::pp_defined) {
      Result = 0;
      PP.LexNonComment(PeekTok);
      return false;
    }

    // Handle "defined X" and "defined(X)".

    // Get the next token, don't expand it.
    PP.LexUnexpandedToken(PeekTok);

    // Two options, it can either be a pp-identifier or a (.
    bool InParens = false;
    if (PeekTok.getKind() == tok::l_paren) {
      // Found a paren, remember we saw it and skip it.
      InParens = true;
      PP.LexUnexpandedToken(PeekTok);
    }
    
    // If we don't have a pp-identifier now, this is an error.
    if ((II = PeekTok.getIdentifierInfo()) == 0) {
      PP.Diag(PeekTok, diag::err_pp_defined_requires_identifier);
      return true;
    }
    
    // Otherwise, we got an identifier, is it defined to something?
    Result = II->getMacroInfo() != 0;
    
    // If there is a macro, mark it used.
    if (Result != 0) {
      II->getMacroInfo()->setIsUsed(true);
      
      // If this is the first use of a target-specific macro, warn about it.
      if (II->getMacroInfo()->isTargetSpecific()) {
        // Don't warn on second use.
        II->getMacroInfo()->setIsTargetSpecific(false);
        PP.getTargetInfo().DiagnoseNonPortability(PeekTok.getLocation(),
                                                  diag::port_target_macro_use);
      }
    } else {
      // Use of a target-specific macro for some other target?  If so, warn.
      if (II->isOtherTargetMacro()) {
        II->setIsOtherTargetMacro(false);  // Don't warn on second use.
        PP.getTargetInfo().DiagnoseNonPortability(PeekTok.getLocation(),
                                                  diag::port_target_macro_use);
      }
    }

    // Consume identifier.
    PP.LexNonComment(PeekTok);

    // If we are in parens, ensure we have a trailing ).
    if (InParens) {
      if (PeekTok.getKind() != tok::r_paren) {
        PP.Diag(PeekTok, diag::err_pp_missing_rparen);
        return true;
      }
      // Consume the ).
      PP.LexNonComment(PeekTok);
    }
    
    // Success, remember that we saw defined(X).
    DT.State = DefinedTracker::DefinedMacro;
    DT.TheMacro = II;
    return false;
  }
  
  switch (PeekTok.getKind()) {
  default:  // Non-value token.
    PP.Diag(PeekTok, diag::err_pp_expr_bad_token);
    return true;
  case tok::eom:
  case tok::r_paren:
    // If there is no expression, report and exit.
    PP.Diag(PeekTok, diag::err_pp_expected_value_in_expr);
    return true;
  case tok::numeric_constant: {
    SmallString<64> IntegerBuffer;
    IntegerBuffer.resize(PeekTok.getLength());
    const char *ThisTokBegin = &IntegerBuffer[0];
    unsigned ActualLength = PP.getSpelling(PeekTok, ThisTokBegin);
    NumericLiteralParser Literal(ThisTokBegin, ThisTokBegin+ActualLength, 
                                 PeekTok.getLocation(), PP);
    if (Literal.hadError)
      return true; // a diagnostic was already reported.
    
    if (Literal.isFloatingLiteral()) {
      PP.Diag(PeekTok, diag::err_pp_illegal_floating_literal);
      return true;
    }
    assert(Literal.isIntegerLiteral() && "Unknown ppnumber");
    
    // FIXME: Handle overflow based on whether the value is signed.  If signed
    // and if the value is too large, emit a warning "integer constant is so
    // large that it is unsigned" e.g. 12345678901234567890.
    if (Literal.GetIntegerValue(Result))
      PP.Diag(PeekTok, diag::warn_integer_too_large);
    PP.LexNonComment(PeekTok);
    return false;
  }
  case tok::l_paren:
    PP.LexNonComment(PeekTok);  // Eat the (.
    // Parse the value and if there are any binary operators involved, parse
    // them.
    if (EvaluateValue(Result, PeekTok, DT, PP)) return true;

    // If this is a silly value like (X), which doesn't need parens, check for
    // !(defined X).
    if (PeekTok.getKind() == tok::r_paren) {
      // Just use DT unmodified as our result.
    } else {
      if (EvaluateDirectiveSubExpr(Result, 1, PeekTok, PP)) return true;
      
      if (PeekTok.getKind() != tok::r_paren) {
        PP.Diag(PeekTok, diag::err_pp_expected_rparen);
        return true;
      }
      DT.State = DefinedTracker::Unknown;
    }
    PP.LexNonComment(PeekTok);  // Eat the ).
    return false;
 
  case tok::plus:
    // Unary plus doesn't modify the value.
    PP.LexNonComment(PeekTok);
    return EvaluateValue(Result, PeekTok, DT, PP);
  case tok::minus:
    PP.LexNonComment(PeekTok);
    if (EvaluateValue(Result, PeekTok, DT, PP)) return true;
    Result = -Result;
    DT.State = DefinedTracker::Unknown;
    return false;
    
  case tok::tilde:
    PP.LexNonComment(PeekTok);
    if (EvaluateValue(Result, PeekTok, DT, PP)) return true;
    Result = ~Result;
    DT.State = DefinedTracker::Unknown;
    return false;
    
  case tok::exclaim:
    PP.LexNonComment(PeekTok);
    if (EvaluateValue(Result, PeekTok, DT, PP)) return true;
    Result = !Result;
    
    if (DT.State == DefinedTracker::DefinedMacro)
      DT.State = DefinedTracker::NotDefinedMacro;
    else if (DT.State == DefinedTracker::NotDefinedMacro)
      DT.State = DefinedTracker::DefinedMacro;
    return false;
    
  // FIXME: Handle #assert
  }
}



/// getPrecedence - Return the precedence of the specified binary operator
/// token.  This returns:
///   ~0 - Invalid token.
///   14 - *,/,%
///   13 - -,+
///   12 - <<,>>
///   11 - >=, <=, >, <
///   10 - ==, !=
///    9 - &
///    8 - ^
///    7 - |
///    6 - &&
///    5 - ||
///    4 - ?
///    3 - :
///    0 - eom, )
static unsigned getPrecedence(tok::TokenKind Kind) {
  switch (Kind) {
  default: return ~0U;
  case tok::percent:
  case tok::slash:
  case tok::star:                 return 14;
  case tok::plus:
  case tok::minus:                return 13;
  case tok::lessless:
  case tok::greatergreater:       return 12;
  case tok::lessequal:
  case tok::less:
  case tok::greaterequal:
  case tok::greater:              return 11;
  case tok::exclaimequal:
  case tok::equalequal:           return 10;
  case tok::amp:                  return 9;
  case tok::caret:                return 8;
  case tok::pipe:                 return 7;
  case tok::ampamp:               return 6;
  case tok::pipepipe:             return 5;
  case tok::question:             return 4;
  case tok::colon:                return 3;
  case tok::comma:                return 2;
  case tok::r_paren:              return 0;   // Lowest priority, end of expr.
  case tok::eom:                  return 0;   // Lowest priority, end of macro.
  }
}


/// EvaluateDirectiveSubExpr - Evaluate the subexpression whose first token is
/// PeekTok, and whose precedence is PeekPrec.
static bool EvaluateDirectiveSubExpr(APInt &LHS, unsigned MinPrec,
                                     LexerToken &PeekTok, Preprocessor &PP) {
  unsigned PeekPrec = getPrecedence(PeekTok.getKind());
  // If this token isn't valid, report the error.
  if (PeekPrec == ~0U) {
    PP.Diag(PeekTok, diag::err_pp_expr_bad_token);
    return true;
  }
  
  while (1) {
    // If this token has a lower precedence than we are allowed to parse, return
    // it so that higher levels of the recursion can parse it.
    if (PeekPrec < MinPrec)
      return false;
    
    tok::TokenKind Operator = PeekTok.getKind();

    // Consume the operator, saving the operator token for error reporting.
    LexerToken OpToken = PeekTok;
    PP.LexNonComment(PeekTok);

    APInt RHS(LHS.getBitWidth(), 0);
    // Parse the RHS of the operator.
    DefinedTracker DT;
    if (EvaluateValue(RHS, PeekTok, DT, PP)) return true;

    // Remember the precedence of this operator and get the precedence of the
    // operator immediately to the right of the RHS.
    unsigned ThisPrec = PeekPrec;
    PeekPrec = getPrecedence(PeekTok.getKind());

    // If this token isn't valid, report the error.
    if (PeekPrec == ~0U) {
      PP.Diag(PeekTok, diag::err_pp_expr_bad_token);
      return true;
    }
    
    bool isRightAssoc = Operator == tok::question;
    
    // Get the precedence of the operator to the right of the RHS.  If it binds
    // more tightly with RHS than we do, evaluate it completely first.
    if (ThisPrec < PeekPrec ||
        (ThisPrec == PeekPrec && isRightAssoc)) {
      if (EvaluateDirectiveSubExpr(RHS, ThisPrec+1, PeekTok, PP))
        return true;
      PeekPrec = getPrecedence(PeekTok.getKind());
    }
    assert(PeekPrec <= ThisPrec && "Recursion didn't work!");
    
    switch (Operator) {
    default: assert(0 && "Unknown operator token!");
    case tok::percent:
      if (RHS == 0) {
        PP.Diag(OpToken, diag::err_pp_remainder_by_zero);
        return true;
      }
      // FIXME: sign.
      LHS = LHS.urem(RHS);
      break;
    case tok::slash:
      if (RHS == 0) {
        PP.Diag(OpToken, diag::err_pp_division_by_zero);
        return true;
      }
      // FIXME: sign.
      LHS = LHS.udiv(RHS);
      break;
    case tok::star :           LHS *= RHS; break;
    case tok::lessless:
      // FIXME: shift amt overflow?
      // FIXME: Don't use getZExtValue.
      LHS = LHS << RHS.getZExtValue();
      break;
    case tok::greatergreater:
      // FIXME: signed vs unsigned
      // FIXME: Don't use getZExtValue.
      LHS = LHS.ashr(RHS.getZExtValue());
      break;
    case tok::plus :           LHS += RHS; break;
    case tok::minus:           LHS -= RHS; break;
    case tok::lessequal:
      // FIXME: signed vs unsigned
      LHS = LHS.sle(RHS);
      break;
    case tok::less:
      // FIXME: signed vs unsigned
      LHS = LHS.slt(RHS);
      break;
    case tok::greaterequal:
      // FIXME: signed vs unsigned
      LHS = LHS.sge(RHS);
      break;
    case tok::greater:
      // FIXME: signed vs unsigned
      LHS = LHS.sgt(RHS);
      break;
    case tok::exclaimequal:    LHS = LHS != RHS; break;
    case tok::equalequal:      LHS = LHS == RHS; break;
    case tok::amp:             LHS &= RHS; break;
    case tok::caret:           LHS ^= RHS; break;
    case tok::pipe:            LHS |= RHS; break;
    case tok::ampamp:          LHS = LHS != 0 && RHS != 0; break;
    case tok::pipepipe:        LHS = LHS != 0 || RHS != 0; break;
    case tok::comma:
      PP.Diag(OpToken, diag::ext_pp_comma_expr);
      LHS = RHS; // LHS = LHS,RHS -> RHS.
      break; 
    case tok::question: {
      // Parse the : part of the expression.
      if (PeekTok.getKind() != tok::colon) {
        PP.Diag(OpToken, diag::err_pp_question_without_colon);
        return true;
      }
      // Consume the :.
      PP.LexNonComment(PeekTok);

      // Evaluate the value after the :.
      APInt AfterColonVal(LHS.getBitWidth(), 0);
      DefinedTracker DT;
      if (EvaluateValue(AfterColonVal, PeekTok, DT, PP)) return true;

      // Parse anything after the : RHS that has a higher precedence than ?.
      if (EvaluateDirectiveSubExpr(AfterColonVal, ThisPrec+1,
                                   PeekTok, PP))
        return true;
      
      // Now that we have the condition, the LHS and the RHS of the :, evaluate.
      LHS = LHS != 0 ? RHS : AfterColonVal;
      
      // Figure out the precedence of the token after the : part.
      PeekPrec = getPrecedence(PeekTok.getKind());
      break;
    }
    case tok::colon:
      // Don't allow :'s to float around without being part of ?: exprs.
      PP.Diag(OpToken, diag::err_pp_colon_without_question);
      return true;
    }
  }
  
  return false;
}

/// EvaluateDirectiveExpression - Evaluate an integer constant expression that
/// may occur after a #if or #elif directive.  If the expression is equivalent
/// to "!defined(X)" return X in IfNDefMacro.
bool Preprocessor::
EvaluateDirectiveExpression(IdentifierInfo *&IfNDefMacro) {
  // Peek ahead one token.
  LexerToken Tok;
  Lex(Tok);
  
  // C99 6.10.1p3 - All expressions are evaluated as intmax_t or uintmax_t.
  unsigned BitWidth = getTargetInfo().getIntMaxTWidth(Tok.getLocation());
  APInt ResVal(BitWidth, 0);
  DefinedTracker DT;
  if (EvaluateValue(ResVal, Tok, DT, *this)) {
    // Parse error, skip the rest of the macro line.
    if (Tok.getKind() != tok::eom)
      DiscardUntilEndOfDirective();
    return false;
  }
  
  // If we are at the end of the expression after just parsing a value, there
  // must be no (unparenthesized) binary operators involved, so we can exit
  // directly.
  if (Tok.getKind() == tok::eom) {
    // If the expression we parsed was of the form !defined(macro), return the
    // macro in IfNDefMacro.
    if (DT.State == DefinedTracker::NotDefinedMacro)
      IfNDefMacro = DT.TheMacro;
    
    return ResVal != 0;
  }
  
  // Otherwise, we must have a binary operator (e.g. "#if 1 < 2"), so parse the
  // operator and the stuff after it.
  if (EvaluateDirectiveSubExpr(ResVal, 1, Tok, *this)) {
    // Parse error, skip the rest of the macro line.
    if (Tok.getKind() != tok::eom)
      DiscardUntilEndOfDirective();
    return false;
  }
  
  // If we aren't at the tok::eom token, something bad happened, like an extra
  // ')' token.
  if (Tok.getKind() != tok::eom) {
    Diag(Tok, diag::err_pp_expected_eol);
    DiscardUntilEndOfDirective();
  }
  
  return ResVal != 0;
}

