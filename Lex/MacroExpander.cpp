//===--- TokenLexer.cpp - Lex from a token stream -------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the TokenLexer interface.
//
//===----------------------------------------------------------------------===//

#include "clang/Lex/MacroExpander.h"
#include "clang/Lex/MacroInfo.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/Diagnostic.h"
#include "llvm/ADT/SmallVector.h"
using namespace clang;

//===----------------------------------------------------------------------===//
// MacroArgs Implementation
//===----------------------------------------------------------------------===//

/// MacroArgs ctor function - This destroys the vector passed in.
MacroArgs *MacroArgs::create(const MacroInfo *MI,
                             const Token *UnexpArgTokens,
                             unsigned NumToks, bool VarargsElided) {
  assert(MI->isFunctionLike() &&
         "Can't have args for an object-like macro!");

  // Allocate memory for the MacroArgs object with the lexer tokens at the end.
  MacroArgs *Result = (MacroArgs*)malloc(sizeof(MacroArgs) +
                                         NumToks*sizeof(Token));
  // Construct the macroargs object.
  new (Result) MacroArgs(NumToks, VarargsElided);
  
  // Copy the actual unexpanded tokens to immediately after the result ptr.
  if (NumToks)
    memcpy(const_cast<Token*>(Result->getUnexpArgument(0)),
           UnexpArgTokens, NumToks*sizeof(Token));
  
  return Result;
}

/// destroy - Destroy and deallocate the memory for this object.
///
void MacroArgs::destroy() {
  // Run the dtor to deallocate the vectors.
  this->~MacroArgs();
  // Release the memory for the object.
  free(this);
}


/// getArgLength - Given a pointer to an expanded or unexpanded argument,
/// return the number of tokens, not counting the EOF, that make up the
/// argument.
unsigned MacroArgs::getArgLength(const Token *ArgPtr) {
  unsigned NumArgTokens = 0;
  for (; ArgPtr->isNot(tok::eof); ++ArgPtr)
    ++NumArgTokens;
  return NumArgTokens;
}


/// getUnexpArgument - Return the unexpanded tokens for the specified formal.
///
const Token *MacroArgs::getUnexpArgument(unsigned Arg) const {
  // The unexpanded argument tokens start immediately after the MacroArgs object
  // in memory.
  const Token *Start = (const Token *)(this+1);
  const Token *Result = Start;
  // Scan to find Arg.
  for (; Arg; ++Result) {
    assert(Result < Start+NumUnexpArgTokens && "Invalid arg #");
    if (Result->is(tok::eof))
      --Arg;
  }
  return Result;
}


/// ArgNeedsPreexpansion - If we can prove that the argument won't be affected
/// by pre-expansion, return false.  Otherwise, conservatively return true.
bool MacroArgs::ArgNeedsPreexpansion(const Token *ArgTok,
                                     Preprocessor &PP) const {
  // If there are no identifiers in the argument list, or if the identifiers are
  // known to not be macros, pre-expansion won't modify it.
  for (; ArgTok->isNot(tok::eof); ++ArgTok)
    if (IdentifierInfo *II = ArgTok->getIdentifierInfo()) {
      if (II->hasMacroDefinition() && PP.getMacroInfo(II)->isEnabled())
        // Return true even though the macro could be a function-like macro
        // without a following '(' token.
        return true;
    }
  return false;
}

/// getPreExpArgument - Return the pre-expanded form of the specified
/// argument.
const std::vector<Token> &
MacroArgs::getPreExpArgument(unsigned Arg, Preprocessor &PP) {
  assert(Arg < NumUnexpArgTokens && "Invalid argument number!");
  
  // If we have already computed this, return it.
  if (PreExpArgTokens.empty())
    PreExpArgTokens.resize(NumUnexpArgTokens);

  std::vector<Token> &Result = PreExpArgTokens[Arg];
  if (!Result.empty()) return Result;

  const Token *AT = getUnexpArgument(Arg);
  unsigned NumToks = getArgLength(AT)+1;  // Include the EOF.
  
  // Otherwise, we have to pre-expand this argument, populating Result.  To do
  // this, we set up a fake TokenLexer to lex from the unexpanded argument
  // list.  With this installed, we lex expanded tokens until we hit the EOF
  // token at the end of the unexp list.
  PP.EnterTokenStream(AT, NumToks);

  // Lex all of the macro-expanded tokens into Result.
  do {
    Result.push_back(Token());
    PP.Lex(Result.back());
  } while (Result.back().isNot(tok::eof));
  
  // Pop the token stream off the top of the stack.  We know that the internal
  // pointer inside of it is to the "end" of the token stream, but the stack
  // will not otherwise be popped until the next token is lexed.  The problem is
  // that the token may be lexed sometime after the vector of tokens itself is
  // destroyed, which would be badness.
  PP.RemoveTopOfLexerStack();
  return Result;
}


/// StringifyArgument - Implement C99 6.10.3.2p2, converting a sequence of
/// tokens into the literal string token that should be produced by the C #
/// preprocessor operator.
///
static Token StringifyArgument(const Token *ArgToks,
                                    Preprocessor &PP, bool Charify = false) {
  Token Tok;
  Tok.startToken();
  Tok.setKind(tok::string_literal);

  const Token *ArgTokStart = ArgToks;
  
  // Stringify all the tokens.
  std::string Result = "\"";
  // FIXME: Optimize this loop to not use std::strings.
  bool isFirst = true;
  for (; ArgToks->isNot(tok::eof); ++ArgToks) {
    const Token &Tok = *ArgToks;
    if (!isFirst && (Tok.hasLeadingSpace() || Tok.isAtStartOfLine()))
      Result += ' ';
    isFirst = false;
    
    // If this is a string or character constant, escape the token as specified
    // by 6.10.3.2p2.
    if (Tok.is(tok::string_literal) ||       // "foo"
        Tok.is(tok::wide_string_literal) ||  // L"foo"
        Tok.is(tok::char_constant)) {        // 'x' and L'x'.
      Result += Lexer::Stringify(PP.getSpelling(Tok));
    } else {
      // Otherwise, just append the token.
      Result += PP.getSpelling(Tok);
    }
  }
  
  // If the last character of the string is a \, and if it isn't escaped, this
  // is an invalid string literal, diagnose it as specified in C99.
  if (Result[Result.size()-1] == '\\') {
    // Count the number of consequtive \ characters.  If even, then they are
    // just escaped backslashes, otherwise it's an error.
    unsigned FirstNonSlash = Result.size()-2;
    // Guaranteed to find the starting " if nothing else.
    while (Result[FirstNonSlash] == '\\')
      --FirstNonSlash;
    if ((Result.size()-1-FirstNonSlash) & 1) {
      // Diagnose errors for things like: #define F(X) #X   /   F(\)
      PP.Diag(ArgToks[-1], diag::pp_invalid_string_literal);
      Result.erase(Result.end()-1);  // remove one of the \'s.
    }
  }
  Result += '"';
  
  // If this is the charify operation and the result is not a legal character
  // constant, diagnose it.
  if (Charify) {
    // First step, turn double quotes into single quotes:
    Result[0] = '\'';
    Result[Result.size()-1] = '\'';
    
    // Check for bogus character.
    bool isBad = false;
    if (Result.size() == 3) {
      isBad = Result[1] == '\'';   // ''' is not legal. '\' already fixed above.
    } else {
      isBad = (Result.size() != 4 || Result[1] != '\\');  // Not '\x'
    }
    
    if (isBad) {
      PP.Diag(ArgTokStart[0], diag::err_invalid_character_to_charify);
      Result = "' '";  // Use something arbitrary, but legal.
    }
  }
  
  Tok.setLength(Result.size());
  Tok.setLocation(PP.CreateString(&Result[0], Result.size()));
  return Tok;
}

/// getStringifiedArgument - Compute, cache, and return the specified argument
/// that has been 'stringified' as required by the # operator.
const Token &MacroArgs::getStringifiedArgument(unsigned ArgNo,
                                                    Preprocessor &PP) {
  assert(ArgNo < NumUnexpArgTokens && "Invalid argument number!");
  if (StringifiedArgs.empty()) {
    StringifiedArgs.resize(getNumArguments());
    memset(&StringifiedArgs[0], 0,
           sizeof(StringifiedArgs[0])*getNumArguments());
  }
  if (StringifiedArgs[ArgNo].isNot(tok::string_literal))
    StringifiedArgs[ArgNo] = StringifyArgument(getUnexpArgument(ArgNo), PP);
  return StringifiedArgs[ArgNo];
}

//===----------------------------------------------------------------------===//
// TokenLexer Implementation
//===----------------------------------------------------------------------===//

/// Create a TokenLexer for the specified macro with the specified actual
/// arguments.  Note that this ctor takes ownership of the ActualArgs pointer.
void TokenLexer::Init(Token &Tok, MacroArgs *Actuals) {
  // If the client is reusing a TokenLexer, make sure to free any memory
  // associated with it.
  destroy();
  
  Macro = PP.getMacroInfo(Tok.getIdentifierInfo());
  ActualArgs = Actuals;
  CurToken = 0;
  InstantiateLoc = Tok.getLocation();
  AtStartOfLine = Tok.isAtStartOfLine();
  HasLeadingSpace = Tok.hasLeadingSpace();
  Tokens = &*Macro->tokens_begin();
  OwnsTokens = false;
  NumTokens = Macro->tokens_end()-Macro->tokens_begin();

  // If this is a function-like macro, expand the arguments and change
  // Tokens to point to the expanded tokens.
  if (Macro->isFunctionLike() && Macro->getNumArgs())
    ExpandFunctionArguments();
  
  // Mark the macro as currently disabled, so that it is not recursively
  // expanded.  The macro must be disabled only after argument pre-expansion of
  // function-like macro arguments occurs.
  Macro->DisableMacro();
}



/// Create a TokenLexer for the specified token stream.  This does not
/// take ownership of the specified token vector.
void TokenLexer::Init(const Token *TokArray, unsigned NumToks) {
  // If the client is reusing a TokenLexer, make sure to free any memory
  // associated with it.
  destroy();
  
  Macro = 0;
  ActualArgs = 0;
  Tokens = TokArray;
  OwnsTokens = false;
  NumTokens = NumToks;
  CurToken = 0;
  InstantiateLoc = SourceLocation();
  AtStartOfLine = false;
  HasLeadingSpace = false;
      
  // Set HasLeadingSpace/AtStartOfLine so that the first token will be
  // returned unmodified.
  if (NumToks != 0) {
    AtStartOfLine   = TokArray[0].isAtStartOfLine();
    HasLeadingSpace = TokArray[0].hasLeadingSpace();
  }
}


void TokenLexer::destroy() {
  // If this was a function-like macro that actually uses its arguments, delete
  // the expanded tokens.
  if (OwnsTokens) {
    delete [] Tokens;
    Tokens = 0;
  }
  
  // TokenLexer owns its formal arguments.
  if (ActualArgs) ActualArgs->destroy();
}

/// Expand the arguments of a function-like macro so that we can quickly
/// return preexpanded tokens from Tokens.
void TokenLexer::ExpandFunctionArguments() {
  llvm::SmallVector<Token, 128> ResultToks;
  
  // Loop through 'Tokens', expanding them into ResultToks.  Keep
  // track of whether we change anything.  If not, no need to keep them.  If so,
  // we install the newly expanded sequence as the new 'Tokens' list.
  bool MadeChange = false;
  
  // NextTokGetsSpace - When this is true, the next token appended to the
  // output list will get a leading space, regardless of whether it had one to
  // begin with or not.  This is used for placemarker support.
  bool NextTokGetsSpace = false;
  
  for (unsigned i = 0, e = NumTokens; i != e; ++i) {
    // If we found the stringify operator, get the argument stringified.  The
    // preprocessor already verified that the following token is a macro name
    // when the #define was parsed.
    const Token &CurTok = Tokens[i];
    if (CurTok.is(tok::hash) || CurTok.is(tok::hashat)) {
      int ArgNo = Macro->getArgumentNum(Tokens[i+1].getIdentifierInfo());
      assert(ArgNo != -1 && "Token following # is not an argument?");
    
      Token Res;
      if (CurTok.is(tok::hash))  // Stringify
        Res = ActualArgs->getStringifiedArgument(ArgNo, PP);
      else {
        // 'charify': don't bother caching these.
        Res = StringifyArgument(ActualArgs->getUnexpArgument(ArgNo), PP, true);
      }
      
      // The stringified/charified string leading space flag gets set to match
      // the #/#@ operator.
      if (CurTok.hasLeadingSpace() || NextTokGetsSpace)
        Res.setFlag(Token::LeadingSpace);
      
      ResultToks.push_back(Res);
      MadeChange = true;
      ++i;  // Skip arg name.
      NextTokGetsSpace = false;
      continue;
    }
    
    // Otherwise, if this is not an argument token, just add the token to the
    // output buffer.
    IdentifierInfo *II = CurTok.getIdentifierInfo();
    int ArgNo = II ? Macro->getArgumentNum(II) : -1;
    if (ArgNo == -1) {
      // This isn't an argument, just add it.
      ResultToks.push_back(CurTok);

      if (NextTokGetsSpace) {
        ResultToks.back().setFlag(Token::LeadingSpace);
        NextTokGetsSpace = false;
      }
      continue;
    }
      
    // An argument is expanded somehow, the result is different than the
    // input.
    MadeChange = true;

    // Otherwise, this is a use of the argument.  Find out if there is a paste
    // (##) operator before or after the argument.
    bool PasteBefore = 
      !ResultToks.empty() && ResultToks.back().is(tok::hashhash);
    bool PasteAfter = i+1 != e && Tokens[i+1].is(tok::hashhash);
    
    // If it is not the LHS/RHS of a ## operator, we must pre-expand the
    // argument and substitute the expanded tokens into the result.  This is
    // C99 6.10.3.1p1.
    if (!PasteBefore && !PasteAfter) {
      const Token *ResultArgToks;

      // Only preexpand the argument if it could possibly need it.  This
      // avoids some work in common cases.
      const Token *ArgTok = ActualArgs->getUnexpArgument(ArgNo);
      if (ActualArgs->ArgNeedsPreexpansion(ArgTok, PP))
        ResultArgToks = &ActualArgs->getPreExpArgument(ArgNo, PP)[0];
      else
        ResultArgToks = ArgTok;  // Use non-preexpanded tokens.
      
      // If the arg token expanded into anything, append it.
      if (ResultArgToks->isNot(tok::eof)) {
        unsigned FirstResult = ResultToks.size();
        unsigned NumToks = MacroArgs::getArgLength(ResultArgToks);
        ResultToks.append(ResultArgToks, ResultArgToks+NumToks);
      
        // If any tokens were substituted from the argument, the whitespace
        // before the first token should match the whitespace of the arg
        // identifier.
        ResultToks[FirstResult].setFlagValue(Token::LeadingSpace,
                                             CurTok.hasLeadingSpace() ||
                                             NextTokGetsSpace);
        NextTokGetsSpace = false;
      } else {
        // If this is an empty argument, and if there was whitespace before the
        // formal token, make sure the next token gets whitespace before it.
        NextTokGetsSpace = CurTok.hasLeadingSpace();
      }
      continue;
    }
    
    // Okay, we have a token that is either the LHS or RHS of a paste (##)
    // argument.  It gets substituted as its non-pre-expanded tokens.
    const Token *ArgToks = ActualArgs->getUnexpArgument(ArgNo);
    unsigned NumToks = MacroArgs::getArgLength(ArgToks);
    if (NumToks) {  // Not an empty argument?
      // If this is the GNU ", ## __VA_ARG__" extension, and we just learned
      // that __VA_ARG__ expands to multiple tokens, avoid a pasting error when
      // the expander trys to paste ',' with the first token of the __VA_ARG__
      // expansion.
      if (PasteBefore && ResultToks.size() >= 2 &&
          ResultToks[ResultToks.size()-2].is(tok::comma) &&
          (unsigned)ArgNo == Macro->getNumArgs()-1 &&
          Macro->isVariadic()) {
        // Remove the paste operator, report use of the extension.
        PP.Diag(ResultToks.back().getLocation(), diag::ext_paste_comma);
        ResultToks.pop_back();
      }
      
      ResultToks.append(ArgToks, ArgToks+NumToks);
      
      // If the next token was supposed to get leading whitespace, ensure it has
      // it now.
      if (NextTokGetsSpace) {
        ResultToks[ResultToks.size()-NumToks].setFlag(Token::LeadingSpace);
        NextTokGetsSpace = false;
      }
      continue;
    }
    
    // If an empty argument is on the LHS or RHS of a paste, the standard (C99
    // 6.10.3.3p2,3) calls for a bunch of placemarker stuff to occur.  We
    // implement this by eating ## operators when a LHS or RHS expands to
    // empty.
    NextTokGetsSpace |= CurTok.hasLeadingSpace();
    if (PasteAfter) {
      // Discard the argument token and skip (don't copy to the expansion
      // buffer) the paste operator after it.
      NextTokGetsSpace |= Tokens[i+1].hasLeadingSpace();
      ++i;
      continue;
    }
    
    // If this is on the RHS of a paste operator, we've already copied the
    // paste operator to the ResultToks list.  Remove it.
    assert(PasteBefore && ResultToks.back().is(tok::hashhash));
    NextTokGetsSpace |= ResultToks.back().hasLeadingSpace();
    ResultToks.pop_back();
    
    // If this is the __VA_ARGS__ token, and if the argument wasn't provided,
    // and if the macro had at least one real argument, and if the token before
    // the ## was a comma, remove the comma.
    if ((unsigned)ArgNo == Macro->getNumArgs()-1 && // is __VA_ARGS__
        ActualArgs->isVarargsElidedUse() &&       // Argument elided.
        !ResultToks.empty() && ResultToks.back().is(tok::comma)) {
      // Never add a space, even if the comma, ##, or arg had a space.
      NextTokGetsSpace = false;
      // Remove the paste operator, report use of the extension.
      PP.Diag(ResultToks.back().getLocation(), diag::ext_paste_comma);
      ResultToks.pop_back();
    }
    continue;
  }
  
  // If anything changed, install this as the new Tokens list.
  if (MadeChange) {
    // This is deleted in the dtor.
    NumTokens = ResultToks.size();
    Token *Res = new Token[ResultToks.size()];
    if (NumTokens)
      memcpy(Res, &ResultToks[0], NumTokens*sizeof(Token));
    Tokens = Res;
    OwnsTokens = true;
  }
}

/// Lex - Lex and return a token from this macro stream.
///
void TokenLexer::Lex(Token &Tok) {
  // Lexing off the end of the macro, pop this macro off the expansion stack.
  if (isAtEnd()) {
    // If this is a macro (not a token stream), mark the macro enabled now
    // that it is no longer being expanded.
    if (Macro) Macro->EnableMacro();

    // Pop this context off the preprocessors lexer stack and get the next
    // token.  This will delete "this" so remember the PP instance var.
    Preprocessor &PPCache = PP;
    if (PP.HandleEndOfMacro(Tok))
      return;

    // HandleEndOfMacro may not return a token.  If it doesn't, lex whatever is
    // next.
    return PPCache.Lex(Tok);
  }
  
  // If this is the first token of the expanded result, we inherit spacing
  // properties later.
  bool isFirstToken = CurToken == 0;
  
  // Get the next token to return.
  Tok = Tokens[CurToken++];
  
  // If this token is followed by a token paste (##) operator, paste the tokens!
  if (!isAtEnd() && Tokens[CurToken].is(tok::hashhash))
    if (PasteTokens(Tok)) {
      // When handling the microsoft /##/ extension, the final token is
      // returned by PasteTokens, not the pasted token.
      return;
    }

  // The token's current location indicate where the token was lexed from.  We
  // need this information to compute the spelling of the token, but any
  // diagnostics for the expanded token should appear as if they came from
  // InstantiationLoc.  Pull this information together into a new SourceLocation
  // that captures all of this.
  if (InstantiateLoc.isValid()) {   // Don't do this for token streams.
    SourceManager &SrcMgr = PP.getSourceManager();
    Tok.setLocation(SrcMgr.getInstantiationLoc(Tok.getLocation(), 
                                               InstantiateLoc));
  }
  
  // If this is the first token, set the lexical properties of the token to
  // match the lexical properties of the macro identifier.
  if (isFirstToken) {
    Tok.setFlagValue(Token::StartOfLine , AtStartOfLine);
    Tok.setFlagValue(Token::LeadingSpace, HasLeadingSpace);
  }
  
  // Handle recursive expansion!
  if (Tok.getIdentifierInfo())
    return PP.HandleIdentifier(Tok);

  // Otherwise, return a normal token.
}

/// PasteTokens - Tok is the LHS of a ## operator, and CurToken is the ##
/// operator.  Read the ## and RHS, and paste the LHS/RHS together.  If there
/// are is another ## after it, chomp it iteratively.  Return the result as Tok.
/// If this returns true, the caller should immediately return the token.
bool TokenLexer::PasteTokens(Token &Tok) {
  llvm::SmallVector<char, 128> Buffer;
  do {
    // Consume the ## operator.
    SourceLocation PasteOpLoc = Tokens[CurToken].getLocation();
    ++CurToken;
    assert(!isAtEnd() && "No token on the RHS of a paste operator!");
  
    // Get the RHS token.
    const Token &RHS = Tokens[CurToken];
  
    bool isInvalid = false;

    // Allocate space for the result token.  This is guaranteed to be enough for
    // the two tokens and a null terminator.
    Buffer.resize(Tok.getLength() + RHS.getLength() + 1);
    
    // Get the spelling of the LHS token in Buffer.
    const char *BufPtr = &Buffer[0];
    unsigned LHSLen = PP.getSpelling(Tok, BufPtr);
    if (BufPtr != &Buffer[0])   // Really, we want the chars in Buffer!
      memcpy(&Buffer[0], BufPtr, LHSLen);
    
    BufPtr = &Buffer[LHSLen];
    unsigned RHSLen = PP.getSpelling(RHS, BufPtr);
    if (BufPtr != &Buffer[LHSLen])   // Really, we want the chars in Buffer!
      memcpy(&Buffer[LHSLen], BufPtr, RHSLen);
    
    // Add null terminator.
    Buffer[LHSLen+RHSLen] = '\0';
    
    // Trim excess space.
    Buffer.resize(LHSLen+RHSLen+1);
    
    // Plop the pasted result (including the trailing newline and null) into a
    // scratch buffer where we can lex it.
    SourceLocation ResultTokLoc = PP.CreateString(&Buffer[0], Buffer.size());
    
    // Lex the resultant pasted token into Result.
    Token Result;
    
    // Avoid testing /*, as the lexer would think it is the start of a comment
    // and emit an error that it is unterminated.
    if (Tok.is(tok::slash) && RHS.is(tok::star)) {
      isInvalid = true;
    } else if (Tok.is(tok::identifier) && RHS.is(tok::identifier)) {
      // Common paste case: identifier+identifier = identifier.  Avoid creating
      // a lexer and other overhead.
      PP.IncrementPasteCounter(true);
      Result.startToken();
      Result.setKind(tok::identifier);
      Result.setLocation(ResultTokLoc);
      Result.setLength(LHSLen+RHSLen);
    } else {
      PP.IncrementPasteCounter(false);
      
      // Make a lexer to lex this string from.
      SourceManager &SourceMgr = PP.getSourceManager();
      const char *ResultStrData = SourceMgr.getCharacterData(ResultTokLoc);
      
      // Make a lexer object so that we lex and expand the paste result.
      Lexer *TL = new Lexer(ResultTokLoc, PP, ResultStrData, 
                            ResultStrData+LHSLen+RHSLen /*don't include null*/);
      
      // Lex a token in raw mode.  This way it won't look up identifiers
      // automatically, lexing off the end will return an eof token, and
      // warnings are disabled.  This returns true if the result token is the
      // entire buffer.
      bool IsComplete = TL->LexRawToken(Result);
      
      // If we got an EOF token, we didn't form even ONE token.  For example, we
      // did "/ ## /" to get "//".
      IsComplete &= Result.isNot(tok::eof);
      isInvalid = !IsComplete;
      
      // We're now done with the temporary lexer.
      delete TL;
    }
    
    // If pasting the two tokens didn't form a full new token, this is an error.
    // This occurs with "x ## +"  and other stuff.  Return with Tok unmodified
    // and with RHS as the next token to lex.
    if (isInvalid) {
      // Test for the Microsoft extension of /##/ turning into // here on the
      // error path.
      if (PP.getLangOptions().Microsoft && Tok.is(tok::slash) && 
          RHS.is(tok::slash)) {
        HandleMicrosoftCommentPaste(Tok);
        return true;
      } else {
        // TODO: If not in assembler language mode.
        PP.Diag(PasteOpLoc, diag::err_pp_bad_paste, 
                std::string(Buffer.begin(), Buffer.end()-1));
        return false;
      }
    }
    
    // Turn ## into 'unknown' to avoid # ## # from looking like a paste
    // operator.
    if (Result.is(tok::hashhash))
      Result.setKind(tok::unknown);
    // FIXME: Turn __VA_ARGS__ into "not a token"?
    
    // Transfer properties of the LHS over the the Result.
    Result.setFlagValue(Token::StartOfLine , Tok.isAtStartOfLine());
    Result.setFlagValue(Token::LeadingSpace, Tok.hasLeadingSpace());
    
    // Finally, replace LHS with the result, consume the RHS, and iterate.
    ++CurToken;
    Tok = Result;
  } while (!isAtEnd() && Tokens[CurToken].is(tok::hashhash));
  
  // Now that we got the result token, it will be subject to expansion.  Since
  // token pasting re-lexes the result token in raw mode, identifier information
  // isn't looked up.  As such, if the result is an identifier, look up id info.
  if (Tok.is(tok::identifier)) {
    // Look up the identifier info for the token.  We disabled identifier lookup
    // by saying we're skipping contents, so we need to do this manually.
    Tok.setIdentifierInfo(PP.LookUpIdentifierInfo(Tok));
  }
  return false;
}

/// isNextTokenLParen - If the next token lexed will pop this macro off the
/// expansion stack, return 2.  If the next unexpanded token is a '(', return
/// 1, otherwise return 0.
unsigned TokenLexer::isNextTokenLParen() const {
  // Out of tokens?
  if (isAtEnd())
    return 2;
  return Tokens[CurToken].is(tok::l_paren);
}


/// HandleMicrosoftCommentPaste - In microsoft compatibility mode, /##/ pastes
/// together to form a comment that comments out everything in the current
/// macro, other active macros, and anything left on the current physical
/// source line of the instantiated buffer.  Handle this by returning the
/// first token on the next line.
void TokenLexer::HandleMicrosoftCommentPaste(Token &Tok) {
  // We 'comment out' the rest of this macro by just ignoring the rest of the
  // tokens that have not been lexed yet, if any.
  
  // Since this must be a macro, mark the macro enabled now that it is no longer
  // being expanded.
  assert(Macro && "Token streams can't paste comments");
  Macro->EnableMacro();
  
  PP.HandleMicrosoftCommentPaste(Tok);
}
