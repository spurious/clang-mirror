//===--- MacroExpander.cpp - Lex from a macro expansion -------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file was developed by Chris Lattner and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the MacroExpander interface.
//
//===----------------------------------------------------------------------===//

#include "clang/Lex/MacroExpander.h"
#include "clang/Lex/MacroInfo.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Basic/SourceManager.h"
using namespace llvm;
using namespace clang;

//===----------------------------------------------------------------------===//
// MacroFormalArgs Implementation
//===----------------------------------------------------------------------===//

MacroFormalArgs::MacroFormalArgs(const MacroInfo *MI) {
  assert(MI->isFunctionLike() &&
         "Can't have formal args for an object-like macro!");
  // Reserve space for arguments to avoid reallocation.
  unsigned NumArgs = MI->getNumArgs();
  if (MI->isC99Varargs() || MI->isGNUVarargs())
    NumArgs += 3;    // Varargs can have more than this, just some guess.
  
  ArgTokens.reserve(NumArgs);
}

//===----------------------------------------------------------------------===//
// MacroExpander Implementation
//===----------------------------------------------------------------------===//

MacroExpander::MacroExpander(LexerToken &Tok, MacroFormalArgs *Formals,
                             Preprocessor &pp)
  : Macro(*Tok.getIdentifierInfo()->getMacroInfo()), FormalArgs(Formals),
    PP(pp), CurToken(0),
    InstantiateLoc(Tok.getLocation()),
    AtStartOfLine(Tok.isAtStartOfLine()),
    HasLeadingSpace(Tok.hasLeadingSpace()) {
}


/// Lex - Lex and return a token from this macro stream.
///
void MacroExpander::Lex(LexerToken &Tok) {
  // Lexing off the end of the macro, pop this macro off the expansion stack.
  if (CurToken == Macro.getNumTokens())
    return PP.HandleEndOfMacro(Tok);
  
  // Get the next token to return.
  Tok = Macro.getReplacementToken(CurToken++);

  // The token's current location indicate where the token was lexed from.  We
  // need this information to compute the spelling of the token, but any
  // diagnostics for the expanded token should appear as if they came from
  // InstantiationLoc.  Pull this information together into a new SourceLocation
  // that captures all of this.
  Tok.SetLocation(PP.getSourceManager().getInstantiationLoc(Tok.getLocation(),
                                                            InstantiateLoc));

  // If this is the first token, set the lexical properties of the token to
  // match the lexical properties of the macro identifier.
  if (CurToken == 1) {
    Tok.SetFlagValue(LexerToken::StartOfLine , AtStartOfLine);
    Tok.SetFlagValue(LexerToken::LeadingSpace, HasLeadingSpace);
  }
  
  // Handle recursive expansion!
  if (Tok.getIdentifierInfo())
    return PP.HandleIdentifier(Tok);

  // Otherwise, return a normal token.
}

/// NextTokenIsKnownNotLParen - If the next token lexed will pop this macro
/// off the expansion stack, return false and set RanOffEnd to true.
/// Otherwise, return true if we know for sure that the next token returned
/// will not be a '(' token.  Return false if it is a '(' token or if we are
/// not sure.  This is used when determining whether to expand a function-like
/// macro.
bool MacroExpander::NextTokenIsKnownNotLParen(bool &RanOffEnd) const {
  // Out of tokens?
  if (CurToken == Macro.getNumTokens()) {
    RanOffEnd = true;
    return false;
  }

  return Macro.getReplacementToken(CurToken).getKind() != tok::l_paren;
}
