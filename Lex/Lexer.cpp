//===--- Lexer.cpp - C Language Family Lexer ------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file was developed by Chris Lattner and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file implements the Lexer and LexerToken interfaces.
//
//===----------------------------------------------------------------------===//
//
// TODO: GCC Diagnostics emitted by the lexer:
// PEDWARN: (form feed|vertical tab) in preprocessing directive
//
// Universal characters, unicode, char mapping:
// WARNING: `%.*s' is not in NFKC
// WARNING: `%.*s' is not in NFC
//
// Other:
// TODO: Options to support:
//    -fexec-charset,-fwide-exec-charset
//
//===----------------------------------------------------------------------===//

#include "clang/Lex/Lexer.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/SourceBuffer.h"
#include "clang/Basic/SourceLocation.h"
#include "llvm/Config/alloca.h"
#include <cctype>
#include <iostream>
using namespace llvm;
using namespace clang;

static void InitCharacterInfo();

Lexer::Lexer(const SourceBuffer *File, unsigned fileid, Preprocessor &pp,
             const char *BufStart, const char *BufEnd)
  : BufferEnd(BufEnd ? BufEnd : File->getBufferEnd()),
    InputFile(File), CurFileID(fileid), PP(pp), Features(PP.getLangOptions()) {
  Is_PragmaLexer = false;
  IsMainFile = false;
  InitCharacterInfo();
      
  assert(BufferEnd[0] == 0 &&
         "We assume that the input buffer has a null character at the end"
         " to simplify lexing!");
    
  BufferPtr = BufStart ? BufStart : File->getBufferStart();

  // Start of the file is a start of line.
  IsAtStartOfLine = true;

  // We are not after parsing a #.
  ParsingPreprocessorDirective = false;

  // We are not after parsing #include.
  ParsingFilename = false;

  // We are not in raw mode.  Raw mode disables diagnostics and interpretation
  // of tokens (e.g. identifiers, thus disabling macro expansion).  It is used
  // to quickly lex the tokens of the buffer, e.g. when handling a "#if 0" block
  // or otherwise skipping over tokens.
  LexingRawMode = false;
}

/// Stringify - Convert the specified string into a C string, with surrounding
/// ""'s, and with escaped \ and " characters.
std::string Lexer::Stringify(const std::string &Str, bool Charify) {
  std::string Result = Str;
  char Quote = Charify ? '\'' : '"';
  for (unsigned i = 0, e = Result.size(); i != e; ++i) {
    if (Result[i] == '\\' || Result[i] == Quote) {
      Result.insert(Result.begin()+i, '\\');
      ++i; ++e;
    }
  }
  return Result;
}


//===----------------------------------------------------------------------===//
// Character information.
//===----------------------------------------------------------------------===//

static unsigned char CharInfo[256];

enum {
  CHAR_HORZ_WS  = 0x01,  // ' ', '\t', '\f', '\v'.  Note, no '\0'
  CHAR_VERT_WS  = 0x02,  // '\r', '\n'
  CHAR_LETTER   = 0x04,  // a-z,A-Z
  CHAR_NUMBER   = 0x08,  // 0-9
  CHAR_UNDER    = 0x10,  // _
  CHAR_PERIOD   = 0x20   // .
};

static void InitCharacterInfo() {
  static bool isInited = false;
  if (isInited) return;
  isInited = true;
  
  // Intiialize the CharInfo table.
  // TODO: statically initialize this.
  CharInfo[(int)' '] = CharInfo[(int)'\t'] = 
  CharInfo[(int)'\f'] = CharInfo[(int)'\v'] = CHAR_HORZ_WS;
  CharInfo[(int)'\n'] = CharInfo[(int)'\r'] = CHAR_VERT_WS;
  
  CharInfo[(int)'_'] = CHAR_UNDER;
  for (unsigned i = 'a'; i <= 'z'; ++i)
    CharInfo[i] = CharInfo[i+'A'-'a'] = CHAR_LETTER;
  for (unsigned i = '0'; i <= '9'; ++i)
    CharInfo[i] = CHAR_NUMBER;
}

/// isIdentifierBody - Return true if this is the body character of an
/// identifier, which is [a-zA-Z0-9_].
static inline bool isIdentifierBody(unsigned char c) {
  return CharInfo[c] & (CHAR_LETTER|CHAR_NUMBER|CHAR_UNDER);
}

/// isHorizontalWhitespace - Return true if this character is horizontal
/// whitespace: ' ', '\t', '\f', '\v'.  Note that this returns false for '\0'.
static inline bool isHorizontalWhitespace(unsigned char c) {
  return CharInfo[c] & CHAR_HORZ_WS;
}

/// isWhitespace - Return true if this character is horizontal or vertical
/// whitespace: ' ', '\t', '\f', '\v', '\n', '\r'.  Note that this returns false
/// for '\0'.
static inline bool isWhitespace(unsigned char c) {
  return CharInfo[c] & (CHAR_HORZ_WS|CHAR_VERT_WS);
}

/// isNumberBody - Return true if this is the body character of an
/// preprocessing number, which is [a-zA-Z0-9_.].
static inline bool isNumberBody(unsigned char c) {
  return CharInfo[c] & (CHAR_LETTER|CHAR_NUMBER|CHAR_UNDER|CHAR_PERIOD);
}


//===----------------------------------------------------------------------===//
// Diagnostics forwarding code.
//===----------------------------------------------------------------------===//

/// getSourceLocation - Return a source location identifier for the specified
/// offset in the current file.
SourceLocation Lexer::getSourceLocation(const char *Loc) const {
  assert(Loc >= InputFile->getBufferStart() && Loc <= BufferEnd &&
         "Location out of range for this buffer!");
  return SourceLocation(CurFileID, Loc-InputFile->getBufferStart());
}


/// Diag - Forwarding function for diagnostics.  This translate a source
/// position in the current buffer into a SourceLocation object for rendering.
void Lexer::Diag(const char *Loc, unsigned DiagID,
                 const std::string &Msg) const {
  if (LexingRawMode && Diagnostic::isNoteWarningOrExtension(DiagID))
    return;
  PP.Diag(getSourceLocation(Loc), DiagID, Msg);
}
void Lexer::Diag(SourceLocation Loc, unsigned DiagID,
                 const std::string &Msg) const {
  if (LexingRawMode && Diagnostic::isNoteWarningOrExtension(DiagID))
    return;
  PP.Diag(Loc, DiagID, Msg);
}


//===----------------------------------------------------------------------===//
// Trigraph and Escaped Newline Handling Code.
//===----------------------------------------------------------------------===//

/// GetTrigraphCharForLetter - Given a character that occurs after a ?? pair,
/// return the decoded trigraph letter it corresponds to, or '\0' if nothing.
static char GetTrigraphCharForLetter(char Letter) {
  switch (Letter) {
  default:   return 0;
  case '=':  return '#';
  case ')':  return ']';
  case '(':  return '[';
  case '!':  return '|';
  case '\'': return '^';
  case '>':  return '}';
  case '/':  return '\\';
  case '<':  return '{';
  case '-':  return '~';
  }
}

/// DecodeTrigraphChar - If the specified character is a legal trigraph when
/// prefixed with ??, emit a trigraph warning.  If trigraphs are enabled,
/// return the result character.  Finally, emit a warning about trigraph use
/// whether trigraphs are enabled or not.
static char DecodeTrigraphChar(const char *CP, Lexer *L) {
  char Res = GetTrigraphCharForLetter(*CP);
  if (Res && L) {
    if (!L->getFeatures().Trigraphs) {
      L->Diag(CP-2, diag::trigraph_ignored);
      return 0;
    } else {
      L->Diag(CP-2, diag::trigraph_converted, std::string()+Res);
    }
  }
  return Res;
}

/// getCharAndSizeSlow - Peek a single 'character' from the specified buffer,
/// get its size, and return it.  This is tricky in several cases:
///   1. If currently at the start of a trigraph, we warn about the trigraph,
///      then either return the trigraph (skipping 3 chars) or the '?',
///      depending on whether trigraphs are enabled or not.
///   2. If this is an escaped newline (potentially with whitespace between
///      the backslash and newline), implicitly skip the newline and return
///      the char after it.
///   3. If this is a UCN, return it.  FIXME: C++ UCN's?
///
/// This handles the slow/uncommon case of the getCharAndSize method.  Here we
/// know that we can accumulate into Size, and that we have already incremented
/// Ptr by Size bytes.
///
/// NOTE: When this method is updated, getCharAndSizeSlowNoWarn (below) should
/// be updated to match.
///
char Lexer::getCharAndSizeSlow(const char *Ptr, unsigned &Size,
                               LexerToken *Tok) {
  // If we have a slash, look for an escaped newline.
  if (Ptr[0] == '\\') {
    ++Size;
    ++Ptr;
Slash:
    // Common case, backslash-char where the char is not whitespace.
    if (!isWhitespace(Ptr[0])) return '\\';
    
    // See if we have optional whitespace characters followed by a newline.
    {
      unsigned SizeTmp = 0;
      do {
        ++SizeTmp;
        if (Ptr[SizeTmp-1] == '\n' || Ptr[SizeTmp-1] == '\r') {
          // Remember that this token needs to be cleaned.
          if (Tok) Tok->SetFlag(LexerToken::NeedsCleaning);

          // Warn if there was whitespace between the backslash and newline.
          if (SizeTmp != 1 && Tok)
            Diag(Ptr, diag::backslash_newline_space);
          
          // If this is a \r\n or \n\r, skip the newlines.
          if ((Ptr[SizeTmp] == '\r' || Ptr[SizeTmp] == '\n') &&
              Ptr[SizeTmp-1] != Ptr[SizeTmp])
            ++SizeTmp;
          
          // Found backslash<whitespace><newline>.  Parse the char after it.
          Size += SizeTmp;
          Ptr  += SizeTmp;
          // Use slow version to accumulate a correct size field.
          return getCharAndSizeSlow(Ptr, Size, Tok);
        }
      } while (isWhitespace(Ptr[SizeTmp]));
    }
      
    // Otherwise, this is not an escaped newline, just return the slash.
    return '\\';
  }
  
  // If this is a trigraph, process it.
  if (Ptr[0] == '?' && Ptr[1] == '?') {
    // If this is actually a legal trigraph (not something like "??x"), emit
    // a trigraph warning.  If so, and if trigraphs are enabled, return it.
    if (char C = DecodeTrigraphChar(Ptr+2, Tok ? this : 0)) {
      // Remember that this token needs to be cleaned.
      if (Tok) Tok->SetFlag(LexerToken::NeedsCleaning);

      Ptr += 3;
      Size += 3;
      if (C == '\\') goto Slash;
      return C;
    }
  }
  
  // If this is neither, return a single character.
  ++Size;
  return *Ptr;
}


/// getCharAndSizeSlowNoWarn - Handle the slow/uncommon case of the
/// getCharAndSizeNoWarn method.  Here we know that we can accumulate into Size,
/// and that we have already incremented Ptr by Size bytes.
///
/// NOTE: When this method is updated, getCharAndSizeSlow (above) should
/// be updated to match.
char Lexer::getCharAndSizeSlowNoWarn(const char *Ptr, unsigned &Size,
                                     const LangOptions &Features) {
  // If we have a slash, look for an escaped newline.
  if (Ptr[0] == '\\') {
    ++Size;
    ++Ptr;
Slash:
    // Common case, backslash-char where the char is not whitespace.
    if (!isWhitespace(Ptr[0])) return '\\';
    
    // See if we have optional whitespace characters followed by a newline.
    {
      unsigned SizeTmp = 0;
      do {
        ++SizeTmp;
        if (Ptr[SizeTmp-1] == '\n' || Ptr[SizeTmp-1] == '\r') {
          
          // If this is a \r\n or \n\r, skip the newlines.
          if ((Ptr[SizeTmp] == '\r' || Ptr[SizeTmp] == '\n') &&
              Ptr[SizeTmp-1] != Ptr[SizeTmp])
            ++SizeTmp;
          
          // Found backslash<whitespace><newline>.  Parse the char after it.
          Size += SizeTmp;
          Ptr  += SizeTmp;
          
          // Use slow version to accumulate a correct size field.
          return getCharAndSizeSlowNoWarn(Ptr, Size, Features);
        }
      } while (isWhitespace(Ptr[SizeTmp]));
    }
    
    // Otherwise, this is not an escaped newline, just return the slash.
    return '\\';
  }
  
  // If this is a trigraph, process it.
  if (Features.Trigraphs && Ptr[0] == '?' && Ptr[1] == '?') {
    // If this is actually a legal trigraph (not something like "??x"), return
    // it.
    if (char C = GetTrigraphCharForLetter(Ptr[2])) {
      Ptr += 3;
      Size += 3;
      if (C == '\\') goto Slash;
      return C;
    }
  }
  
  // If this is neither, return a single character.
  ++Size;
  return *Ptr;
}

//===----------------------------------------------------------------------===//
// Helper methods for lexing.
//===----------------------------------------------------------------------===//

void Lexer::LexIdentifier(LexerToken &Result, const char *CurPtr) {
  // Match [_A-Za-z0-9]*, we have already matched [_A-Za-z$]
  unsigned Size;
  unsigned char C = *CurPtr++;
  while (isIdentifierBody(C)) {
    C = *CurPtr++;
  }
  --CurPtr;   // Back up over the skipped character.

  // Fast path, no $,\,? in identifier found.  '\' might be an escaped newline
  // or UCN, and ? might be a trigraph for '\', an escaped newline or UCN.
  // FIXME: UCNs.
  if (C != '\\' && C != '?' && (C != '$' || !Features.DollarIdents)) {
FinishIdentifier:
    const char *IdStart = BufferPtr;
    FormTokenWithChars(Result, CurPtr);
    Result.SetKind(tok::identifier);
    
    // If we are in raw mode, return this identifier raw.  There is no need to
    // look up identifier information or attempt to macro expand it.
    if (LexingRawMode) return;
    
    // Fill in Result.IdentifierInfo, looking up the identifier in the
    // identifier table.
    PP.LookUpIdentifierInfo(Result, IdStart);
    
    // Finally, now that we know we have an identifier, pass this off to the
    // preprocessor, which may macro expand it or something.
    return PP.HandleIdentifier(Result);
  }
  
  // Otherwise, $,\,? in identifier found.  Enter slower path.
  
  C = getCharAndSize(CurPtr, Size);
  while (1) {
    if (C == '$') {
      // If we hit a $ and they are not supported in identifiers, we are done.
      if (!Features.DollarIdents) goto FinishIdentifier;
      
      // Otherwise, emit a diagnostic and continue.
      Diag(CurPtr, diag::ext_dollar_in_identifier);
      CurPtr = ConsumeChar(CurPtr, Size, Result);
      C = getCharAndSize(CurPtr, Size);
      continue;
    } else if (!isIdentifierBody(C)) { // FIXME: UCNs.
      // Found end of identifier.
      goto FinishIdentifier;
    }

    // Otherwise, this character is good, consume it.
    CurPtr = ConsumeChar(CurPtr, Size, Result);

    C = getCharAndSize(CurPtr, Size);
    while (isIdentifierBody(C)) { // FIXME: UCNs.
      CurPtr = ConsumeChar(CurPtr, Size, Result);
      C = getCharAndSize(CurPtr, Size);
    }
  }
}


/// LexNumericConstant - Lex the remainer of a integer or floating point
/// constant. From[-1] is the first character lexed.  Return the end of the
/// constant.
void Lexer::LexNumericConstant(LexerToken &Result, const char *CurPtr) {
  unsigned Size;
  char C = getCharAndSize(CurPtr, Size);
  char PrevCh = 0;
  while (isNumberBody(C)) { // FIXME: UCNs?
    CurPtr = ConsumeChar(CurPtr, Size, Result);
    PrevCh = C;
    C = getCharAndSize(CurPtr, Size);
  }
  
  // If we fell out, check for a sign, due to 1e+12.  If we have one, continue.
  if ((C == '-' || C == '+') && (PrevCh == 'E' || PrevCh == 'e'))
    return LexNumericConstant(Result, ConsumeChar(CurPtr, Size, Result));

  // If we have a hex FP constant, continue.
  if (Features.HexFloats &&
      (C == '-' || C == '+') && (PrevCh == 'P' || PrevCh == 'p'))
    return LexNumericConstant(Result, ConsumeChar(CurPtr, Size, Result));
  
  Result.SetKind(tok::numeric_constant);

  // Update the location of token as well as BufferPtr.
  FormTokenWithChars(Result, CurPtr);
}

/// LexStringLiteral - Lex the remainder of a string literal, after having lexed
/// either " or L".
void Lexer::LexStringLiteral(LexerToken &Result, const char *CurPtr) {
  const char *NulCharacter = 0; // Does this string contain the \0 character?
  
  char C = getAndAdvanceChar(CurPtr, Result);
  while (C != '"') {
    // Skip escaped characters.
    if (C == '\\') {
      // Skip the escaped character.
      C = getAndAdvanceChar(CurPtr, Result);
    } else if (C == '\n' || C == '\r' ||             // Newline.
               (C == 0 && CurPtr-1 == BufferEnd)) {  // End of file.
      Diag(BufferPtr, diag::err_unterminated_string);
      BufferPtr = CurPtr-1;
      return LexTokenInternal(Result);
    } else if (C == 0) {
      NulCharacter = CurPtr-1;
    }
    C = getAndAdvanceChar(CurPtr, Result);
  }
  
  if (NulCharacter) Diag(NulCharacter, diag::null_in_string);

  Result.SetKind(tok::string_literal);

  // Update the location of the token as well as the BufferPtr instance var.
  FormTokenWithChars(Result, CurPtr);
}

/// LexAngledStringLiteral - Lex the remainder of an angled string literal,
/// after having lexed the '<' character.  This is used for #include filenames.
void Lexer::LexAngledStringLiteral(LexerToken &Result, const char *CurPtr) {
  const char *NulCharacter = 0; // Does this string contain the \0 character?
  
  char C = getAndAdvanceChar(CurPtr, Result);
  while (C != '>') {
    // Skip escaped characters.
    if (C == '\\') {
      // Skip the escaped character.
      C = getAndAdvanceChar(CurPtr, Result);
    } else if (C == '\n' || C == '\r' ||             // Newline.
               (C == 0 && CurPtr-1 == BufferEnd)) {  // End of file.
      Diag(BufferPtr, diag::err_unterminated_string);
      BufferPtr = CurPtr-1;
      return LexTokenInternal(Result);
    } else if (C == 0) {
      NulCharacter = CurPtr-1;
    }
    C = getAndAdvanceChar(CurPtr, Result);
  }
  
  if (NulCharacter) Diag(NulCharacter, diag::null_in_string);
  
  Result.SetKind(tok::angle_string_literal);
  
  // Update the location of token as well as BufferPtr.
  FormTokenWithChars(Result, CurPtr);
}


/// LexCharConstant - Lex the remainder of a character constant, after having
/// lexed either ' or L'.
void Lexer::LexCharConstant(LexerToken &Result, const char *CurPtr) {
  const char *NulCharacter = 0; // Does this character contain the \0 character?

  // Handle the common case of 'x' and '\y' efficiently.
  char C = getAndAdvanceChar(CurPtr, Result);
  if (C == '\'') {
    Diag(BufferPtr, diag::err_empty_character);
    BufferPtr = CurPtr;
    return LexTokenInternal(Result);
  } else if (C == '\\') {
    // Skip the escaped character.
    // FIXME: UCN's.
    C = getAndAdvanceChar(CurPtr, Result);
  }
  
  if (C && C != '\n' && C != '\r' && CurPtr[0] == '\'') {
    ++CurPtr;
  } else {
    // Fall back on generic code for embedded nulls, newlines, wide chars.
    do {
      // Skip escaped characters.
      if (C == '\\') {
        // Skip the escaped character.
        C = getAndAdvanceChar(CurPtr, Result);
      } else if (C == '\n' || C == '\r' ||               // Newline.
                 (C == 0 && CurPtr-1 == BufferEnd)) {    // End of file.
        Diag(BufferPtr, diag::err_unterminated_char);
        BufferPtr = CurPtr-1;
        return LexTokenInternal(Result);
      } else if (C == 0) {
        NulCharacter = CurPtr-1;
      }
      C = getAndAdvanceChar(CurPtr, Result);
    } while (C != '\'');
  }
  
  if (NulCharacter) Diag(NulCharacter, diag::null_in_char);

  Result.SetKind(tok::char_constant);
  
  // Update the location of token as well as BufferPtr.
  FormTokenWithChars(Result, CurPtr);
}

/// SkipWhitespace - Efficiently skip over a series of whitespace characters.
/// Update BufferPtr to point to the next non-whitespace character and return.
void Lexer::SkipWhitespace(LexerToken &Result, const char *CurPtr) {
  // Whitespace - Skip it, then return the token after the whitespace.
  unsigned char Char = *CurPtr;  // Skip consequtive spaces efficiently.
  while (1) {
    // Skip horizontal whitespace very aggressively.
    while (isHorizontalWhitespace(Char))
      Char = *++CurPtr;
    
    // Otherwise if we something other than whitespace, we're done.
    if (Char != '\n' && Char != '\r')
      break;
    
    if (ParsingPreprocessorDirective) {
      // End of preprocessor directive line, let LexTokenInternal handle this.
      BufferPtr = CurPtr;
      return;
    }
    
    // ok, but handle newline.
    // The returned token is at the start of the line.
    Result.SetFlag(LexerToken::StartOfLine);
    // No leading whitespace seen so far.
    Result.ClearFlag(LexerToken::LeadingSpace);
    Char = *++CurPtr;
  }

  // If this isn't immediately after a newline, there is leading space.
  char PrevChar = CurPtr[-1];
  if (PrevChar != '\n' && PrevChar != '\r')
    Result.SetFlag(LexerToken::LeadingSpace);

  // If the next token is obviously a // or /* */ comment, skip it efficiently
  // too (without going through the big switch stmt).
  if (Char == '/' && CurPtr[1] == '/') {
    BufferPtr = CurPtr;
    return SkipBCPLComment(Result, CurPtr+1);
  }
  if (Char == '/' && CurPtr[1] == '*') {
    BufferPtr = CurPtr;
    return SkipBlockComment(Result, CurPtr+2);
  }
  BufferPtr = CurPtr;
}

// SkipBCPLComment - We have just read the // characters from input.  Skip until
// we find the newline character thats terminate the comment.  Then update
/// BufferPtr and return.
void Lexer::SkipBCPLComment(LexerToken &Result, const char *CurPtr) {
  // If BCPL comments aren't explicitly enabled for this language, emit an
  // extension warning.
  if (!Features.BCPLComment) {
    Diag(BufferPtr, diag::ext_bcpl_comment);
    
    // Mark them enabled so we only emit one warning for this translation
    // unit.
    Features.BCPLComment = true;
  }
  
  // Scan over the body of the comment.  The common case, when scanning, is that
  // the comment contains normal ascii characters with nothing interesting in
  // them.  As such, optimize for this case with the inner loop.
  char C;
  do {
    C = *CurPtr;
    // FIXME: Speedup BCPL comment lexing.  Just scan for a \n or \r character.
    // If we find a \n character, scan backwards, checking to see if it's an
    // escaped newline, like we do for block comments.
    
    // Skip over characters in the fast loop.
    while (C != 0 &&                // Potentially EOF.
           C != '\\' &&             // Potentially escaped newline.
           C != '?' &&              // Potentially trigraph.
           C != '\n' && C != '\r')  // Newline or DOS-style newline.
      C = *++CurPtr;

    // If this is a newline, we're done.
    if (C == '\n' || C == '\r')
      break;  // Found the newline? Break out!
    
    // Otherwise, this is a hard case.  Fall back on getAndAdvanceChar to
    // properly decode the character.
    const char *OldPtr = CurPtr;
    C = getAndAdvanceChar(CurPtr, Result);
    
    // If we read multiple characters, and one of those characters was a \r or
    // \n, then we had an escaped newline within the comment.  Emit diagnostic.
    if (CurPtr != OldPtr+1) {
      for (; OldPtr != CurPtr; ++OldPtr)
        if (OldPtr[0] == '\n' || OldPtr[0] == '\r') {
          Diag(OldPtr-1, diag::ext_multi_line_bcpl_comment);
          break;
        }
    }
    
    if (CurPtr == BufferEnd+1) goto FoundEOF;
  } while (C != '\n' && C != '\r');

  // Found and did not consume a newline.

  // If we are inside a preprocessor directive and we see the end of line,
  // return immediately, so that the lexer can return this as an EOM token.
  if (ParsingPreprocessorDirective) {
    BufferPtr = CurPtr;
    return;
  }
  
  // Otherwise, eat the \n character.  We don't care if this is a \n\r or
  // \r\n sequence.
  ++CurPtr;
    
  // The next returned token is at the start of the line.
  Result.SetFlag(LexerToken::StartOfLine);
  // No leading whitespace seen so far.
  Result.ClearFlag(LexerToken::LeadingSpace);
    
  // It is common for the tokens immediately after a // comment to be
  // whitespace (indentation for the next line).  Instead of going through the
  // big switch, handle it efficiently now.
  if (isWhitespace(*CurPtr)) {
    Result.SetFlag(LexerToken::LeadingSpace);
    return SkipWhitespace(Result, CurPtr+1);
  }

  BufferPtr = CurPtr;
  return;

FoundEOF:   // If we ran off the end of the buffer, return EOF.
  BufferPtr = CurPtr-1;
  return;
}

/// isBlockCommentEndOfEscapedNewLine - Return true if the specified newline
/// character (either \n or \r) is part of an escaped newline sequence.  Issue a
/// diagnostic if so.  We know that the is inside of a block comment.
static bool isEndOfBlockCommentWithEscapedNewLine(const char *CurPtr, 
                                                  Lexer *L) {
  assert(CurPtr[0] == '\n' || CurPtr[0] == '\r');
  
  // Back up off the newline.
  --CurPtr;
  
  // If this is a two-character newline sequence, skip the other character.
  if (CurPtr[0] == '\n' || CurPtr[0] == '\r') {
    // \n\n or \r\r -> not escaped newline.
    if (CurPtr[0] == CurPtr[1])
      return false;
    // \n\r or \r\n -> skip the newline.
    --CurPtr;
  }
  
  // If we have horizontal whitespace, skip over it.  We allow whitespace
  // between the slash and newline.
  bool HasSpace = false;
  while (isHorizontalWhitespace(*CurPtr) || *CurPtr == 0) {
    --CurPtr;
    HasSpace = true;
  }
  
  // If we have a slash, we know this is an escaped newline.
  if (*CurPtr == '\\') {
    if (CurPtr[-1] != '*') return false;
  } else {
    // It isn't a slash, is it the ?? / trigraph?
    if (CurPtr[0] != '/' || CurPtr[-1] != '?' || CurPtr[-2] != '?' ||
        CurPtr[-3] != '*')
      return false;
    
    // This is the trigraph ending the comment.  Emit a stern warning!
    CurPtr -= 2;

    // If no trigraphs are enabled, warn that we ignored this trigraph and
    // ignore this * character.
    if (!L->getFeatures().Trigraphs) {
      L->Diag(CurPtr, diag::trigraph_ignored_block_comment);
      return false;
    }
    L->Diag(CurPtr, diag::trigraph_ends_block_comment);
  }
  
  // Warn about having an escaped newline between the */ characters.
  L->Diag(CurPtr, diag::escaped_newline_block_comment_end);
  
  // If there was space between the backslash and newline, warn about it.
  if (HasSpace) L->Diag(CurPtr, diag::backslash_newline_space);
  
  return true;
}

/// SkipBlockComment - We have just read the /* characters from input.  Read
/// until we find the */ characters that terminate the comment.  Note that we
/// don't bother decoding trigraphs or escaped newlines in block comments,
/// because they cannot cause the comment to end.  The only thing that can
/// happen is the comment could end with an escaped newline between the */ end
/// of comment.
void Lexer::SkipBlockComment(LexerToken &Result, const char *CurPtr) {
  // Scan one character past where we should, looking for a '/' character.  Once
  // we find it, check to see if it was preceeded by a *.  This common
  // optimization helps people who like to put a lot of * characters in their
  // comments.
  unsigned char C = *CurPtr++;
  if (C == 0 && CurPtr == BufferEnd+1) {
    Diag(BufferPtr, diag::err_unterminated_block_comment);
    BufferPtr = CurPtr-1;
    return;
  }
  
  while (1) {
    // Skip over all non-interesting characters.
    // TODO: Vectorize this.  Note: memchr on Darwin is slower than this loop.
    while (C != '/' && C != '\0')
      C = *CurPtr++;
    
    if (C == '/') {
      if (CurPtr[-2] == '*')  // We found the final */.  We're done!
        break;
      
      if ((CurPtr[-2] == '\n' || CurPtr[-2] == '\r')) {
        if (isEndOfBlockCommentWithEscapedNewLine(CurPtr-2, this)) {
          // We found the final */, though it had an escaped newline between the
          // * and /.  We're done!
          break;
        }
      }
      if (CurPtr[0] == '*' && CurPtr[1] != '/') {
        // If this is a /* inside of the comment, emit a warning.  Don't do this
        // if this is a /*/, which will end the comment.  This misses cases with
        // embedded escaped newlines, but oh well.
        Diag(CurPtr-1, diag::nested_block_comment);
      }
    } else if (C == 0 && CurPtr == BufferEnd+1) {
      Diag(BufferPtr, diag::err_unterminated_block_comment);
      // Note: the user probably forgot a */.  We could continue immediately
      // after the /*, but this would involve lexing a lot of what really is the
      // comment, which surely would confuse the parser.
      BufferPtr = CurPtr-1;
      return;
    }
    C = *CurPtr++;
  }

  // It is common for the tokens immediately after a /**/ comment to be
  // whitespace.  Instead of going through the big switch, handle it
  // efficiently now.
  if (isHorizontalWhitespace(*CurPtr)) {
    Result.SetFlag(LexerToken::LeadingSpace);
    return SkipWhitespace(Result, CurPtr+1);
  }

  // Otherwise, just return so that the next character will be lexed as a token.
  BufferPtr = CurPtr;
  Result.SetFlag(LexerToken::LeadingSpace);
}

//===----------------------------------------------------------------------===//
// Primary Lexing Entry Points
//===----------------------------------------------------------------------===//

/// LexIncludeFilename - After the preprocessor has parsed a #include, lex and
/// (potentially) macro expand the filename.
std::string Lexer::LexIncludeFilename(LexerToken &FilenameTok) {
  assert(ParsingPreprocessorDirective &&
         ParsingFilename == false &&
         "Must be in a preprocessing directive!");

  // We are now parsing a filename!
  ParsingFilename = true;
  
  // Lex the filename.
  Lex(FilenameTok);

  // We should have gotten the filename now.
  ParsingFilename = false;

  // No filename?
  if (FilenameTok.getKind() == tok::eom) {
    Diag(FilenameTok.getLocation(), diag::err_pp_expects_filename);
    return "";
  }
  
  // Get the text form of the filename.
  std::string Filename = PP.getSpelling(FilenameTok);
  assert(!Filename.empty() && "Can't have tokens with empty spellings!");
  
  // Make sure the filename is <x> or "x".
  if (Filename[0] == '<') {
    if (Filename[Filename.size()-1] != '>') {
      Diag(FilenameTok.getLocation(), diag::err_pp_expects_filename);
      FilenameTok.SetKind(tok::eom);
      return "";
    }
  } else if (Filename[0] == '"') {
    if (Filename[Filename.size()-1] != '"') {
      Diag(FilenameTok.getLocation(), diag::err_pp_expects_filename);
      FilenameTok.SetKind(tok::eom);
      return "";
    }
  } else {
    Diag(FilenameTok.getLocation(), diag::err_pp_expects_filename);
    FilenameTok.SetKind(tok::eom);
    return "";
  }
  
  // Diagnose #include "" as invalid.
  if (Filename.size() == 2) {
    Diag(FilenameTok.getLocation(), diag::err_pp_empty_filename);
    FilenameTok.SetKind(tok::eom);
    return "";
  }
        
  return Filename;
}

/// ReadToEndOfLine - Read the rest of the current preprocessor line as an
/// uninterpreted string.  This switches the lexer out of directive mode.
std::string Lexer::ReadToEndOfLine() {
  assert(ParsingPreprocessorDirective && ParsingFilename == false &&
         "Must be in a preprocessing directive!");
  std::string Result;
  LexerToken Tmp;

  // CurPtr - Cache BufferPtr in an automatic variable.
  const char *CurPtr = BufferPtr;
  while (1) {
    char Char = getAndAdvanceChar(CurPtr, Tmp);
    switch (Char) {
    default:
      Result += Char;
      break;
    case 0:  // Null.
      // Found end of file?
      if (CurPtr-1 != BufferEnd) {
        // Nope, normal character, continue.
        Result += Char;
        break;
      }
      // FALL THROUGH.
    case '\r':
    case '\n':
      // Okay, we found the end of the line. First, back up past the \0, \r, \n.
      assert(CurPtr[-1] == Char && "Trigraphs for newline?");
      BufferPtr = CurPtr-1;
      
      // Next, lex the character, which should handle the EOM transition.
      Lex(Tmp);
      assert(Tmp.getKind() == tok::eom && "Unexpected token!");
      
      // Finally, we're done, return the string we found.
      return Result;
    }
  }
}

/// LexEndOfFile - CurPtr points to the end of this file.  Handle this
/// condition, reporting diagnostics and handling other edge cases as required.
/// This returns true if Result contains a token, false if PP.Lex should be
/// called again.
bool Lexer::LexEndOfFile(LexerToken &Result, const char *CurPtr) {
  // If we hit the end of the file while parsing a preprocessor directive,
  // end the preprocessor directive first.  The next token returned will
  // then be the end of file.
  if (ParsingPreprocessorDirective) {
    // Done parsing the "line".
    ParsingPreprocessorDirective = false;
    Result.SetKind(tok::eom);
    // Update the location of token as well as BufferPtr.
    FormTokenWithChars(Result, CurPtr);
    return true;  // Have a token.
  }        

  // If we are in raw mode, return this event as an EOF token.  Let the caller
  // that put us in raw mode handle the event.
  if (LexingRawMode) {
    Result.StartToken();
    BufferPtr = BufferEnd;
    FormTokenWithChars(Result, BufferEnd);
    Result.SetKind(tok::eof);
    return true;
  }
  
  // Otherwise, issue diagnostics for unterminated #if and missing newline.

  // If we are in a #if directive, emit an error.
  while (!ConditionalStack.empty()) {
    Diag(ConditionalStack.back().IfLoc, diag::err_pp_unterminated_conditional);
    ConditionalStack.pop_back();
  }
  
  // If the file was empty or didn't end in a newline, issue a pedwarn.
  if (CurPtr[-1] != '\n' && CurPtr[-1] != '\r')
    Diag(BufferEnd, diag::ext_no_newline_eof);
  
  BufferPtr = CurPtr;

  // Finally, let the preprocessor handle this.
  return PP.HandleEndOfFile(Result);
}

/// isNextPPTokenLParen - Return 1 if the next unexpanded token lexed from
/// the specified lexer will return a tok::l_paren token, 0 if it is something
/// else and 2 if there are no more tokens in the buffer controlled by the
/// lexer.
unsigned Lexer::isNextPPTokenLParen() {
  assert(!LexingRawMode && "How can we expand a macro from a skipping buffer?");
  
  // Switch to 'skipping' mode.  This will ensure that we can lex a token
  // without emitting diagnostics, disables macro expansion, and will cause EOF
  // to return an EOF token instead of popping the include stack.
  LexingRawMode = true;
  
  // Save state that can be changed while lexing so that we can restore it.
  const char *TmpBufferPtr = BufferPtr;
  
  LexerToken Tok;
  Tok.StartToken();
  LexTokenInternal(Tok);
  
  // Restore state that may have changed.
  BufferPtr = TmpBufferPtr;
  
  // Restore the lexer back to non-skipping mode.
  LexingRawMode = false;
  
  if (Tok.getKind() == tok::eof)
    return 2;
  return Tok.getKind() == tok::l_paren;
}


/// LexTokenInternal - This implements a simple C family lexer.  It is an
/// extremely performance critical piece of code.  This assumes that the buffer
/// has a null character at the end of the file.  Return true if an error
/// occurred and compilation should terminate, false if normal.  This returns a
/// preprocessing token, not a normal token, as such, it is an internal
/// interface.  It assumes that the Flags of result have been cleared before
/// calling this.
void Lexer::LexTokenInternal(LexerToken &Result) {
LexNextToken:
  // New token, can't need cleaning yet.
  Result.ClearFlag(LexerToken::NeedsCleaning);
  Result.SetIdentifierInfo(0);
  
  // CurPtr - Cache BufferPtr in an automatic variable.
  const char *CurPtr = BufferPtr;

  // Small amounts of horizontal whitespace is very common between tokens.
  if ((*CurPtr == ' ') || (*CurPtr == '\t')) {
    ++CurPtr;
    while ((*CurPtr == ' ') || (*CurPtr == '\t'))
      ++CurPtr;
    BufferPtr = CurPtr;
    Result.SetFlag(LexerToken::LeadingSpace);
  }
  
  unsigned SizeTmp, SizeTmp2;   // Temporaries for use in cases below.
  
  // Read a character, advancing over it.
  char Char = getAndAdvanceChar(CurPtr, Result);
  switch (Char) {
  case 0:  // Null.
    // Found end of file?
    if (CurPtr-1 == BufferEnd) {
      // Read the PP instance variable into an automatic variable, because
      // LexEndOfFile will often delete 'this'.
      Preprocessor &PPCache = PP;
      if (LexEndOfFile(Result, CurPtr-1))  // Retreat back into the file.
        return;   // Got a token to return.
      return PPCache.Lex(Result);
    }
    
    Diag(CurPtr-1, diag::null_in_file);
    Result.SetFlag(LexerToken::LeadingSpace);
    SkipWhitespace(Result, CurPtr);
    goto LexNextToken;   // GCC isn't tail call eliminating.
  case '\n':
  case '\r':
    // If we are inside a preprocessor directive and we see the end of line,
    // we know we are done with the directive, so return an EOM token.
    if (ParsingPreprocessorDirective) {
      // Done parsing the "line".
      ParsingPreprocessorDirective = false;
      
      // Since we consumed a newline, we are back at the start of a line.
      IsAtStartOfLine = true;
      
      Result.SetKind(tok::eom);
      break;
    }
    // The returned token is at the start of the line.
    Result.SetFlag(LexerToken::StartOfLine);
    // No leading whitespace seen so far.
    Result.ClearFlag(LexerToken::LeadingSpace);
    SkipWhitespace(Result, CurPtr);
    goto LexNextToken;   // GCC isn't tail call eliminating.
  case ' ':
  case '\t':
  case '\f':
  case '\v':
    Result.SetFlag(LexerToken::LeadingSpace);
    SkipWhitespace(Result, CurPtr);
    goto LexNextToken;   // GCC isn't tail call eliminating.

  case 'L':
    // Notify MIOpt that we read a non-whitespace/non-comment token.
    MIOpt.ReadToken();
    Char = getCharAndSize(CurPtr, SizeTmp);

    // Wide string literal.
    if (Char == '"')
      return LexStringLiteral(Result, ConsumeChar(CurPtr, SizeTmp, Result));

    // Wide character constant.
    if (Char == '\'')
      return LexCharConstant(Result, ConsumeChar(CurPtr, SizeTmp, Result));
    // FALL THROUGH, treating L like the start of an identifier.
    
  // C99 6.4.2: Identifiers.
  case 'A': case 'B': case 'C': case 'D': case 'E': case 'F': case 'G':
  case 'H': case 'I': case 'J': case 'K':    /*'L'*/case 'M': case 'N':
  case 'O': case 'P': case 'Q': case 'R': case 'S': case 'T': case 'U':
  case 'V': case 'W': case 'X': case 'Y': case 'Z':
  case 'a': case 'b': case 'c': case 'd': case 'e': case 'f': case 'g':
  case 'h': case 'i': case 'j': case 'k': case 'l': case 'm': case 'n':
  case 'o': case 'p': case 'q': case 'r': case 's': case 't': case 'u':
  case 'v': case 'w': case 'x': case 'y': case 'z':
  case '_':
    // Notify MIOpt that we read a non-whitespace/non-comment token.
    MIOpt.ReadToken();
    return LexIdentifier(Result, CurPtr);
    
  // C99 6.4.4.1: Integer Constants.
  // C99 6.4.4.2: Floating Constants.
  case '0': case '1': case '2': case '3': case '4':
  case '5': case '6': case '7': case '8': case '9':
    // Notify MIOpt that we read a non-whitespace/non-comment token.
    MIOpt.ReadToken();
    return LexNumericConstant(Result, CurPtr);
    
  // C99 6.4.4: Character Constants.
  case '\'':
    // Notify MIOpt that we read a non-whitespace/non-comment token.
    MIOpt.ReadToken();
    return LexCharConstant(Result, CurPtr);

  // C99 6.4.5: String Literals.
  case '"':
    // Notify MIOpt that we read a non-whitespace/non-comment token.
    MIOpt.ReadToken();
    return LexStringLiteral(Result, CurPtr);

  // C99 6.4.6: Punctuators.
  case '?':
    Result.SetKind(tok::question);
    break;
  case '[':
    Result.SetKind(tok::l_square);
    break;
  case ']':
    Result.SetKind(tok::r_square);
    break;
  case '(':
    Result.SetKind(tok::l_paren);
    break;
  case ')':
    Result.SetKind(tok::r_paren);
    break;
  case '{':
    Result.SetKind(tok::l_brace);
    break;
  case '}':
    Result.SetKind(tok::r_brace);
    break;
  case '.':
    Char = getCharAndSize(CurPtr, SizeTmp);
    if (Char >= '0' && Char <= '9') {
      // Notify MIOpt that we read a non-whitespace/non-comment token.
      MIOpt.ReadToken();

      return LexNumericConstant(Result, ConsumeChar(CurPtr, SizeTmp, Result));
    } else if (Features.CPlusPlus && Char == '*') {
      Result.SetKind(tok::periodstar);
      CurPtr += SizeTmp;
    } else if (Char == '.' &&
               getCharAndSize(CurPtr+SizeTmp, SizeTmp2) == '.') {
      Result.SetKind(tok::ellipsis);
      CurPtr = ConsumeChar(ConsumeChar(CurPtr, SizeTmp, Result),
                           SizeTmp2, Result);
    } else {
      Result.SetKind(tok::period);
    }
    break;
  case '&':
    Char = getCharAndSize(CurPtr, SizeTmp);
    if (Char == '&') {
      Result.SetKind(tok::ampamp);
      CurPtr = ConsumeChar(CurPtr, SizeTmp, Result);
    } else if (Char == '=') {
      Result.SetKind(tok::ampequal);
      CurPtr = ConsumeChar(CurPtr, SizeTmp, Result);
    } else {
      Result.SetKind(tok::amp);
    }
    break;
  case '*': 
    if (getCharAndSize(CurPtr, SizeTmp) == '=') {
      Result.SetKind(tok::starequal);
      CurPtr = ConsumeChar(CurPtr, SizeTmp, Result);
    } else {
      Result.SetKind(tok::star);
    }
    break;
  case '+':
    Char = getCharAndSize(CurPtr, SizeTmp);
    if (Char == '+') {
      Result.SetKind(tok::plusplus);
      CurPtr = ConsumeChar(CurPtr, SizeTmp, Result);
    } else if (Char == '=') {
      Result.SetKind(tok::plusequal);
      CurPtr = ConsumeChar(CurPtr, SizeTmp, Result);
    } else {
      Result.SetKind(tok::plus);
    }
    break;
  case '-':
    Char = getCharAndSize(CurPtr, SizeTmp);
    if (Char == '-') {
      Result.SetKind(tok::minusminus);
      CurPtr = ConsumeChar(CurPtr, SizeTmp, Result);
    } else if (Char == '>' && Features.CPlusPlus && 
               getCharAndSize(CurPtr+SizeTmp, SizeTmp2) == '*') {
      Result.SetKind(tok::arrowstar);  // C++ ->*
      CurPtr = ConsumeChar(ConsumeChar(CurPtr, SizeTmp, Result),
                           SizeTmp2, Result);
    } else if (Char == '>') {
      Result.SetKind(tok::arrow);
      CurPtr = ConsumeChar(CurPtr, SizeTmp, Result);
    } else if (Char == '=') {
      Result.SetKind(tok::minusequal);
      CurPtr = ConsumeChar(CurPtr, SizeTmp, Result);
    } else {
      Result.SetKind(tok::minus);
    }
    break;
  case '~':
    Result.SetKind(tok::tilde);
    break;
  case '!':
    if (getCharAndSize(CurPtr, SizeTmp) == '=') {
      Result.SetKind(tok::exclaimequal);
      CurPtr = ConsumeChar(CurPtr, SizeTmp, Result);
    } else {
      Result.SetKind(tok::exclaim);
    }
    break;
  case '/':
    // 6.4.9: Comments
    Char = getCharAndSize(CurPtr, SizeTmp);
    if (Char == '/') {         // BCPL comment.
      Result.SetFlag(LexerToken::LeadingSpace);
      SkipBCPLComment(Result, ConsumeChar(CurPtr, SizeTmp, Result));
      goto LexNextToken;   // GCC isn't tail call eliminating.
    } else if (Char == '*') {  // /**/ comment.
      Result.SetFlag(LexerToken::LeadingSpace);
      SkipBlockComment(Result, ConsumeChar(CurPtr, SizeTmp, Result));
      goto LexNextToken;   // GCC isn't tail call eliminating.
    } else if (Char == '=') {
      Result.SetKind(tok::slashequal);
      CurPtr = ConsumeChar(CurPtr, SizeTmp, Result);
    } else {
      Result.SetKind(tok::slash);
    }
    break;
  case '%':
    Char = getCharAndSize(CurPtr, SizeTmp);
    if (Char == '=') {
      Result.SetKind(tok::percentequal);
      CurPtr = ConsumeChar(CurPtr, SizeTmp, Result);
    } else if (Features.Digraphs && Char == '>') {
      Result.SetKind(tok::r_brace);    // '%>' -> '}'
      CurPtr = ConsumeChar(CurPtr, SizeTmp, Result);
    } else if (Features.Digraphs && Char == ':') {
      CurPtr = ConsumeChar(CurPtr, SizeTmp, Result);
      Char = getCharAndSize(CurPtr, SizeTmp);
      if (Char == '%' && getCharAndSize(CurPtr+SizeTmp, SizeTmp2) == ':') {
        Result.SetKind(tok::hashhash);   // '%:%:' -> '##'
        CurPtr = ConsumeChar(ConsumeChar(CurPtr, SizeTmp, Result),
                             SizeTmp2, Result);
      } else if (Char == '@' && Features.Microsoft) {  // %:@ -> #@ -> Charize
        Result.SetKind(tok::hashat);
        CurPtr = ConsumeChar(CurPtr, SizeTmp, Result);
        Diag(BufferPtr, diag::charize_microsoft_ext);
      } else {
        Result.SetKind(tok::hash);       // '%:' -> '#'
        
        // We parsed a # character.  If this occurs at the start of the line,
        // it's actually the start of a preprocessing directive.  Callback to
        // the preprocessor to handle it.
        // FIXME: -fpreprocessed mode??
        if (Result.isAtStartOfLine() && !LexingRawMode) {
          BufferPtr = CurPtr;
          PP.HandleDirective(Result);
          
          // As an optimization, if the preprocessor didn't switch lexers, tail
          // recurse.
          if (PP.isCurrentLexer(this)) {
            // Start a new token. If this is a #include or something, the PP may
            // want us starting at the beginning of the line again.  If so, set
            // the StartOfLine flag.
            if (IsAtStartOfLine) {
              Result.SetFlag(LexerToken::StartOfLine);
              IsAtStartOfLine = false;
            }
            goto LexNextToken;   // GCC isn't tail call eliminating.
          }
          
          return PP.Lex(Result);
        }
      }
    } else {
      Result.SetKind(tok::percent);
    }
    break;
  case '<':
    Char = getCharAndSize(CurPtr, SizeTmp);
    if (ParsingFilename) {
      return LexAngledStringLiteral(Result, CurPtr+SizeTmp);
    } else if (Char == '<' &&
               getCharAndSize(CurPtr+SizeTmp, SizeTmp2) == '=') {
      Result.SetKind(tok::lesslessequal);
      CurPtr = ConsumeChar(ConsumeChar(CurPtr, SizeTmp, Result),
                           SizeTmp2, Result);
    } else if (Char == '<') {
      Result.SetKind(tok::lessless);
      CurPtr = ConsumeChar(CurPtr, SizeTmp, Result);
    } else if (Char == '=') {
      Result.SetKind(tok::lessequal);
      CurPtr = ConsumeChar(CurPtr, SizeTmp, Result);
    } else if (Features.Digraphs && Char == ':') {
      Result.SetKind(tok::l_square); // '<:' -> '['
      CurPtr = ConsumeChar(CurPtr, SizeTmp, Result);
    } else if (Features.Digraphs && Char == '>') {
      Result.SetKind(tok::l_brace); // '<%' -> '{'
      CurPtr = ConsumeChar(CurPtr, SizeTmp, Result);
    } else if (Features.CPPMinMax && Char == '?') {     // <?
      CurPtr = ConsumeChar(CurPtr, SizeTmp, Result);
      Diag(BufferPtr, diag::min_max_deprecated);

      if (getCharAndSize(CurPtr, SizeTmp) == '=') {     // <?= 
        Result.SetKind(tok::lessquestionequal);
        CurPtr = ConsumeChar(CurPtr, SizeTmp, Result);
      } else {
        Result.SetKind(tok::lessquestion);
      }
    } else {
      Result.SetKind(tok::less);
    }
    break;
  case '>':
    Char = getCharAndSize(CurPtr, SizeTmp);
    if (Char == '=') {
      Result.SetKind(tok::greaterequal);
      CurPtr = ConsumeChar(CurPtr, SizeTmp, Result);
    } else if (Char == '>' && 
               getCharAndSize(CurPtr+SizeTmp, SizeTmp2) == '=') {
      Result.SetKind(tok::greatergreaterequal);
      CurPtr = ConsumeChar(ConsumeChar(CurPtr, SizeTmp, Result),
                           SizeTmp2, Result);
    } else if (Char == '>') {
      Result.SetKind(tok::greatergreater);
      CurPtr = ConsumeChar(CurPtr, SizeTmp, Result);
    } else if (Features.CPPMinMax && Char == '?') {
      CurPtr = ConsumeChar(CurPtr, SizeTmp, Result);
      Diag(BufferPtr, diag::min_max_deprecated);

      if (getCharAndSize(CurPtr, SizeTmp) == '=') {
        Result.SetKind(tok::greaterquestionequal);    // >?=
        CurPtr = ConsumeChar(CurPtr, SizeTmp, Result);
      } else {
        Result.SetKind(tok::greaterquestion);         // >?
      }
    } else {
      Result.SetKind(tok::greater);
    }
    break;
  case '^':
    Char = getCharAndSize(CurPtr, SizeTmp);
    if (Char == '=') {
      Result.SetKind(tok::caretequal);
      CurPtr = ConsumeChar(CurPtr, SizeTmp, Result);
    } else {
      Result.SetKind(tok::caret);
    }
    break;
  case '|':
    Char = getCharAndSize(CurPtr, SizeTmp);
    if (Char == '=') {
      Result.SetKind(tok::pipeequal);
      CurPtr = ConsumeChar(CurPtr, SizeTmp, Result);
    } else if (Char == '|') {
      Result.SetKind(tok::pipepipe);
      CurPtr = ConsumeChar(CurPtr, SizeTmp, Result);
    } else {
      Result.SetKind(tok::pipe);
    }
    break;
  case ':':
    Char = getCharAndSize(CurPtr, SizeTmp);
    if (Features.Digraphs && Char == '>') {
      Result.SetKind(tok::r_square); // ':>' -> ']'
      CurPtr = ConsumeChar(CurPtr, SizeTmp, Result);
    } else if (Features.CPlusPlus && Char == ':') {
      Result.SetKind(tok::coloncolon);
      CurPtr = ConsumeChar(CurPtr, SizeTmp, Result);
    } else {    
      Result.SetKind(tok::colon);
    }
    break;
  case ';':
    Result.SetKind(tok::semi);
    break;
  case '=':
    Char = getCharAndSize(CurPtr, SizeTmp);
    if (Char == '=') {
      Result.SetKind(tok::equalequal);
      CurPtr = ConsumeChar(CurPtr, SizeTmp, Result);
    } else {      
      Result.SetKind(tok::equal);
    }
    break;
  case ',':
    Result.SetKind(tok::comma);
    break;
  case '#':
    Char = getCharAndSize(CurPtr, SizeTmp);
    if (Char == '#') {
      Result.SetKind(tok::hashhash);
      CurPtr = ConsumeChar(CurPtr, SizeTmp, Result);
    } else if (Char == '@' && Features.Microsoft) {  // #@ -> Charize
      Result.SetKind(tok::hashat);
      Diag(BufferPtr, diag::charize_microsoft_ext);
      CurPtr = ConsumeChar(CurPtr, SizeTmp, Result);
    } else {
      Result.SetKind(tok::hash);
      // We parsed a # character.  If this occurs at the start of the line,
      // it's actually the start of a preprocessing directive.  Callback to
      // the preprocessor to handle it.
      // FIXME: -fpreprocessed mode??
      if (Result.isAtStartOfLine() && !LexingRawMode) {
        BufferPtr = CurPtr;
        PP.HandleDirective(Result);
        
        // As an optimization, if the preprocessor didn't switch lexers, tail
        // recurse.
        if (PP.isCurrentLexer(this)) {
          // Start a new token.  If this is a #include or something, the PP may
          // want us starting at the beginning of the line again.  If so, set
          // the StartOfLine flag.
          if (IsAtStartOfLine) {
            Result.SetFlag(LexerToken::StartOfLine);
            IsAtStartOfLine = false;
          }
          goto LexNextToken;   // GCC isn't tail call eliminating.
        }
        return PP.Lex(Result);
      }
    }
    break;

  case '\\':
    // FIXME: UCN's.
    // FALL THROUGH.
  default:
    // Objective C support.
    if (CurPtr[-1] == '@' && Features.ObjC1) {
      Result.SetKind(tok::at);
      break;
    } else if (CurPtr[-1] == '$' && Features.DollarIdents) {// $ in identifiers.
      Diag(CurPtr-1, diag::ext_dollar_in_identifier);
      // Notify MIOpt that we read a non-whitespace/non-comment token.
      MIOpt.ReadToken();
      return LexIdentifier(Result, CurPtr);
    }
    
    Result.SetKind(tok::unknown);
    break;
  }
  
  // Notify MIOpt that we read a non-whitespace/non-comment token.
  MIOpt.ReadToken();

  // Update the location of token as well as BufferPtr.
  FormTokenWithChars(Result, CurPtr);
}
