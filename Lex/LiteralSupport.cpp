//===--- LiteralSupport.cpp - Code to parse and process literals ----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file was developed by Steve Naroff and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the NumericLiteralParser, CharLiteralParser, and
// StringLiteralParser interfaces.
//
//===----------------------------------------------------------------------===//

#include "clang/Lex/LiteralSupport.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/TargetInfo.h"
#include "llvm/ADT/StringExtras.h"
using namespace clang;

/// HexDigitValue - Return the value of the specified hex digit, or -1 if it's
/// not valid.
static int HexDigitValue(char C) {
  if (C >= '0' && C <= '9') return C-'0';
  if (C >= 'a' && C <= 'f') return C-'a'+10;
  if (C >= 'A' && C <= 'F') return C-'A'+10;
  return -1;
}

/// ProcessCharEscape - Parse a standard C escape sequence, which can occur in
/// either a character or a string literal.
static unsigned ProcessCharEscape(const char *&ThisTokBuf,
                                  const char *ThisTokEnd, bool &HadError,
                                  SourceLocation Loc, bool IsWide,
                                  Preprocessor &PP) {
  // Skip the '\' char.
  ++ThisTokBuf;

  // We know that this character can't be off the end of the buffer, because
  // that would have been \", which would not have been the end of string.
  unsigned ResultChar = *ThisTokBuf++;
  switch (ResultChar) {
  // These map to themselves.
  case '\\': case '\'': case '"': case '?': break;
    
    // These have fixed mappings.
  case 'a':
    // TODO: K&R: the meaning of '\\a' is different in traditional C
    ResultChar = 7;
    break;
  case 'b':
    ResultChar = 8;
    break;
  case 'e':
    PP.Diag(Loc, diag::ext_nonstandard_escape, "e");
    ResultChar = 27;
    break;
  case 'f':
    ResultChar = 12;
    break;
  case 'n':
    ResultChar = 10;
    break;
  case 'r':
    ResultChar = 13;
    break;
  case 't':
    ResultChar = 9;
    break;
  case 'v':
    ResultChar = 11;
    break;
    
    //case 'u': case 'U':  // FIXME: UCNs.
  case 'x': { // Hex escape.
    ResultChar = 0;
    if (ThisTokBuf == ThisTokEnd || !isxdigit(*ThisTokBuf)) {
      PP.Diag(Loc, diag::err_hex_escape_no_digits);
      HadError = 1;
      break;
    }
    
    // Hex escapes are a maximal series of hex digits.
    bool Overflow = false;
    for (; ThisTokBuf != ThisTokEnd; ++ThisTokBuf) {
      int CharVal = HexDigitValue(ThisTokBuf[0]);
      if (CharVal == -1) break;
      Overflow |= (ResultChar & 0xF0000000) ? true : false;  // About to shift out a digit?
      ResultChar <<= 4;
      ResultChar |= CharVal;
    }

    // See if any bits will be truncated when evaluated as a character.
    unsigned CharWidth = IsWide ? PP.getTargetInfo().getWCharWidth(Loc)
                                : PP.getTargetInfo().getCharWidth(Loc);
    if (CharWidth != 32 && (ResultChar >> CharWidth) != 0) {
      Overflow = true;
      ResultChar &= ~0U >> (32-CharWidth);
    }
    
    // Check for overflow.
    if (Overflow)   // Too many digits to fit in
      PP.Diag(Loc, diag::warn_hex_escape_too_large);
    break;
  }
  case '0': case '1': case '2': case '3':
  case '4': case '5': case '6': case '7': {
    // Octal escapes.
    --ThisTokBuf;
    ResultChar = 0;

    // Octal escapes are a series of octal digits with maximum length 3.
    // "\0123" is a two digit sequence equal to "\012" "3".
    unsigned NumDigits = 0;
    do {
      ResultChar <<= 3;
      ResultChar |= *ThisTokBuf++ - '0';
      ++NumDigits;
    } while (ThisTokBuf != ThisTokEnd && NumDigits < 3 &&
             ThisTokBuf[0] >= '0' && ThisTokBuf[0] <= '7');
    
    // Check for overflow.  Reject '\777', but not L'\777'.
    unsigned CharWidth = IsWide ? PP.getTargetInfo().getWCharWidth(Loc)
                                : PP.getTargetInfo().getCharWidth(Loc);
    if (CharWidth != 32 && (ResultChar >> CharWidth) != 0) {
      PP.Diag(Loc, diag::warn_octal_escape_too_large);
      ResultChar &= ~0U >> (32-CharWidth);
    }
    break;
  }
    
    // Otherwise, these are not valid escapes.
  case '(': case '{': case '[': case '%':
    // GCC accepts these as extensions.  We warn about them as such though.
    if (!PP.getLangOptions().NoExtensions) {
      PP.Diag(Loc, diag::ext_nonstandard_escape,
              std::string()+(char)ResultChar);
      break;
    }
    // FALL THROUGH.
  default:
    if (isgraph(ThisTokBuf[0])) {
      PP.Diag(Loc, diag::ext_unknown_escape, std::string()+(char)ResultChar);
    } else {
      PP.Diag(Loc, diag::ext_unknown_escape, "x"+llvm::utohexstr(ResultChar));
    }
    break;
  }
  
  return ResultChar;
}




///       integer-constant: [C99 6.4.4.1]
///         decimal-constant integer-suffix
///         octal-constant integer-suffix
///         hexadecimal-constant integer-suffix
///       decimal-constant: 
///         nonzero-digit
///         decimal-constant digit
///       octal-constant: 
///         0
///         octal-constant octal-digit
///       hexadecimal-constant: 
///         hexadecimal-prefix hexadecimal-digit
///         hexadecimal-constant hexadecimal-digit
///       hexadecimal-prefix: one of
///         0x 0X
///       integer-suffix:
///         unsigned-suffix [long-suffix]
///         unsigned-suffix [long-long-suffix]
///         long-suffix [unsigned-suffix]
///         long-long-suffix [unsigned-sufix]
///       nonzero-digit:
///         1 2 3 4 5 6 7 8 9
///       octal-digit:
///         0 1 2 3 4 5 6 7
///       hexadecimal-digit:
///         0 1 2 3 4 5 6 7 8 9
///         a b c d e f
///         A B C D E F
///       unsigned-suffix: one of
///         u U
///       long-suffix: one of
///         l L
///       long-long-suffix: one of 
///         ll LL
///
///       floating-constant: [C99 6.4.4.2]
///         TODO: add rules...
///

NumericLiteralParser::
NumericLiteralParser(const char *begin, const char *end,
                     SourceLocation TokLoc, Preprocessor &pp)
  : PP(pp), ThisTokBegin(begin), ThisTokEnd(end) {
  s = DigitsBegin = begin;
  saw_exponent = false;
  saw_period = false;
  isLong = false;
  isUnsigned = false;
  isLongLong = false;
  isFloat = false;
  isImaginary = false;
  hadError = false;
  
  if (*s == '0') { // parse radix
    s++;
    if ((*s == 'x' || *s == 'X') && (isxdigit(s[1]) || s[1] == '.')) {
      s++;
      radix = 16;
      DigitsBegin = s;
      s = SkipHexDigits(s);
      if (s == ThisTokEnd) {
        // Done.
      } else if (*s == '.') {
        s++;
        saw_period = true;
        s = SkipHexDigits(s);
      }
      // A binary exponent can appear with or with a '.'. If dotted, the
      // binary exponent is required. 
      if ((*s == 'p' || *s == 'P') && PP.getLangOptions().HexFloats) { 
        s++;
        saw_exponent = true;
        if (*s == '+' || *s == '-')  s++; // sign
        const char *first_non_digit = SkipDigits(s);
        if (first_non_digit == s) {
          Diag(TokLoc, diag::err_exponent_has_no_digits);
          return;
        } else {
          s = first_non_digit;
        }
      } else if (saw_period) {
        Diag(TokLoc, diag::err_hexconstant_requires_exponent);
        return;
      }
    } else if (*s == 'b' || *s == 'B') {
      // 0b101010 is a GCC extension.
      ++s;
      radix = 2;
      DigitsBegin = s;
      s = SkipBinaryDigits(s);
      if (s == ThisTokEnd) {
        // Done.
      } else if (isxdigit(*s)) {
        Diag(TokLoc, diag::err_invalid_binary_digit, std::string(s, s+1));
        return;
      }
      PP.Diag(TokLoc, diag::ext_binary_literal);
    } else {
      // For now, the radix is set to 8. If we discover that we have a
      // floating point constant, the radix will change to 10. Octal floating
      // point constants are not permitted (only decimal and hexadecimal). 
      radix = 8;
      DigitsBegin = s;
      s = SkipOctalDigits(s);
      if (s == ThisTokEnd) {
        // Done.
      } else if (isxdigit(*s)) {
        TokLoc = PP.AdvanceToTokenCharacter(TokLoc, s-begin);
        Diag(TokLoc, diag::err_invalid_octal_digit, std::string(s, s+1));
        return;
      } else if (*s == '.') {
        s++;
        radix = 10;
        saw_period = true;
        s = SkipDigits(s);
      }
      if (*s == 'e' || *s == 'E') { // exponent
        s++;
        radix = 10;
        saw_exponent = true;
        if (*s == '+' || *s == '-')  s++; // sign
        const char *first_non_digit = SkipDigits(s);
        if (first_non_digit == s) {
          Diag(TokLoc, diag::err_exponent_has_no_digits);
          return;
        } else {
          s = first_non_digit;
        }
      }
    }
  } else { // the first digit is non-zero
    radix = 10;
    s = SkipDigits(s);
    if (s == ThisTokEnd) {
      // Done.
    } else if (isxdigit(*s)) {
      Diag(TokLoc, diag::err_invalid_decimal_digit, std::string(s, s+1));
      return;
    } else if (*s == '.') {
      s++;
      saw_period = true;
      s = SkipDigits(s);
    } 
    if (*s == 'e' || *s == 'E') { // exponent
      s++;
      saw_exponent = true;
      if (*s == '+' || *s == '-')  s++; // sign
      const char *first_non_digit = SkipDigits(s);
      if (first_non_digit == s) {
        Diag(TokLoc, diag::err_exponent_has_no_digits);
        return;
      } else {
        s = first_non_digit;
      }
    }
  }

  SuffixBegin = s;
  
  // Parse the suffix.  At this point we can classify whether we have an FP or
  // integer constant.
  bool isFPConstant = isFloatingLiteral();
  
  // Loop over all of the characters of the suffix.  If we see something bad,
  // we break out of the loop.
  for (; s != ThisTokEnd; ++s) {
    switch (*s) {
    case 'f':      // FP Suffix for "float"
    case 'F':
      if (!isFPConstant) break;  // Error for integer constant.
      if (isFloat || isLong) break; // FF, LF invalid.
      isFloat = true;
      continue;  // Success.
    case 'u':
    case 'U':
      if (isFPConstant) break;  // Error for floating constant.
      if (isUnsigned) break;    // Cannot be repeated.
      isUnsigned = true;
      continue;  // Success.
    case 'l':
    case 'L':
      if (isLong || isLongLong) break;  // Cannot be repeated.
      if (isFloat) break;               // LF invalid.
      
      // Check for long long.  The L's need to be adjacent and the same case.
      if (s+1 != ThisTokEnd && s[1] == s[0]) {
        if (isFPConstant) break;        // long long invalid for floats.
        isLongLong = true;
        ++s;  // Eat both of them.
      } else {
        isLong = true;
      }
      continue;  // Success.
    case 'i':
    case 'I':
    case 'j':
    case 'J':
      if (isImaginary) break;   // Cannot be repeated.
      PP.Diag(PP.AdvanceToTokenCharacter(TokLoc, s-begin),
              diag::ext_imaginary_constant);
      isImaginary = true;
      continue;  // Success.
    }
    // If we reached here, there was an error.
    break;
  }
  
  // Report an error if there are any.
  if (s != ThisTokEnd) {
    TokLoc = PP.AdvanceToTokenCharacter(TokLoc, s-begin);
    Diag(TokLoc, isFPConstant ? diag::err_invalid_suffix_float_constant :
                                diag::err_invalid_suffix_integer_constant, 
         std::string(SuffixBegin, ThisTokEnd));
    return;
  }
}

/// GetIntegerValue - Convert this numeric literal value to an APInt that
/// matches Val's input width.  If there is an overflow, set Val to the low bits
/// of the result and return true.  Otherwise, return false.
bool NumericLiteralParser::GetIntegerValue(llvm::APInt &Val) {
  Val = 0;
  s = DigitsBegin;

  llvm::APInt RadixVal(Val.getBitWidth(), radix);
  llvm::APInt CharVal(Val.getBitWidth(), 0);
  llvm::APInt OldVal = Val;
  
  bool OverflowOccurred = false;
  while (s < SuffixBegin) {
    unsigned C = HexDigitValue(*s++);
    
    // If this letter is out of bound for this radix, reject it.
    assert(C < radix && "NumericLiteralParser ctor should have rejected this");
    
    CharVal = C;
    
    // Add the digit to the value in the appropriate radix.  If adding in digits
    // made the value smaller, then this overflowed.
    OldVal = Val;

    // Multiply by radix, did overflow occur on the multiply?
    Val *= RadixVal;
    OverflowOccurred |= Val.udiv(RadixVal) != OldVal;

    OldVal = Val;
    // Add value, did overflow occur on the value?
    Val += CharVal;
    OverflowOccurred |= Val.ult(OldVal);
    OverflowOccurred |= Val.ult(CharVal);
  }
  return OverflowOccurred;
}

llvm::APFloat NumericLiteralParser::
GetFloatValue(const llvm::fltSemantics &Format, bool* isExact) {
  using llvm::APFloat;
  
  llvm::SmallVector<char,256> floatChars;
  for (unsigned i = 0, n = ThisTokEnd-ThisTokBegin; i != n; ++i)
    floatChars.push_back(ThisTokBegin[i]);
  
  floatChars.push_back('\0');
  
  APFloat V (Format, APFloat::fcZero, false);
  APFloat::opStatus status;
  
  status = V.convertFromString(&floatChars[0],APFloat::rmNearestTiesToEven);
  
  if (isExact)
    *isExact = status == APFloat::opOK;
  
  return V;
}

void NumericLiteralParser::Diag(SourceLocation Loc, unsigned DiagID, 
          const std::string &M) {
  PP.Diag(Loc, DiagID, M);
  hadError = true;
}


CharLiteralParser::CharLiteralParser(const char *begin, const char *end,
                                     SourceLocation Loc, Preprocessor &PP) {
  // At this point we know that the character matches the regex "L?'.*'".
  HadError = false;
  Value = 0;
  
  // Determine if this is a wide character.
  IsWide = begin[0] == 'L';
  if (IsWide) ++begin;
  
  // Skip over the entry quote.
  assert(begin[0] == '\'' && "Invalid token lexed");
  ++begin;

  // FIXME: This assumes that 'int' is 32-bits in overflow calculation, and the
  // size of "value".
  assert(PP.getTargetInfo().getIntWidth(Loc) == 32 &&
         "Assumes sizeof(int) == 4 for now");
  // FIXME: This assumes that wchar_t is 32-bits for now.
  assert(PP.getTargetInfo().getWCharWidth(Loc) == 32 && 
         "Assumes sizeof(wchar_t) == 4 for now");
  // FIXME: This extensively assumes that 'char' is 8-bits.
  assert(PP.getTargetInfo().getCharWidth(Loc) == 8 &&
         "Assumes char is 8 bits");
  
  bool isFirstChar = true;
  bool isMultiChar = false;
  while (begin[0] != '\'') {
    unsigned ResultChar;
    if (begin[0] != '\\')     // If this is a normal character, consume it.
      ResultChar = *begin++;
    else                      // Otherwise, this is an escape character.
      ResultChar = ProcessCharEscape(begin, end, HadError, Loc, IsWide, PP);

    // If this is a multi-character constant (e.g. 'abc'), handle it.  These are
    // implementation defined (C99 6.4.4.4p10).
    if (!isFirstChar) {
      // If this is the second character being processed, do special handling.
      if (!isMultiChar) {
        isMultiChar = true;
      
        // Warn about discarding the top bits for multi-char wide-character
        // constants (L'abcd').
        if (IsWide)
          PP.Diag(Loc, diag::warn_extraneous_wide_char_constant);
      }

      if (IsWide) {
        // Emulate GCC's (unintentional?) behavior: L'ab' -> L'b'.
        Value = 0;
      } else {
        // Narrow character literals act as though their value is concatenated
        // in this implementation.
        if (((Value << 8) >> 8) != Value)
          PP.Diag(Loc, diag::warn_char_constant_too_large);
        Value <<= 8;
      }
    }
    
    Value += ResultChar;
    isFirstChar = false;
  }
  
  // If this is a single narrow character, sign extend it (e.g. '\xFF' is "-1")
  // if 'char' is signed for this target (C99 6.4.4.4p10).  Note that multiple
  // character constants are not sign extended in the this implementation:
  // '\xFF\xFF' = 65536 and '\x0\xFF' = 255, which matches GCC.
  if (!IsWide && !isMultiChar && (Value & 128) &&
      PP.getTargetInfo().isCharSigned(Loc))
    Value = (signed char)Value;
}


///       string-literal: [C99 6.4.5]
///          " [s-char-sequence] "
///         L" [s-char-sequence] "
///       s-char-sequence:
///         s-char
///         s-char-sequence s-char
///       s-char:
///         any source character except the double quote ",
///           backslash \, or newline character
///         escape-character
///         universal-character-name
///       escape-character: [C99 6.4.4.4]
///         \ escape-code
///         universal-character-name
///       escape-code:
///         character-escape-code
///         octal-escape-code
///         hex-escape-code
///       character-escape-code: one of
///         n t b r f v a
///         \ ' " ?
///       octal-escape-code:
///         octal-digit
///         octal-digit octal-digit
///         octal-digit octal-digit octal-digit
///       hex-escape-code:
///         x hex-digit
///         hex-escape-code hex-digit
///       universal-character-name:
///         \u hex-quad
///         \U hex-quad hex-quad
///       hex-quad:
///         hex-digit hex-digit hex-digit hex-digit
///
StringLiteralParser::
StringLiteralParser(const Token *StringToks, unsigned NumStringToks,
                    Preprocessor &pp, TargetInfo &t)
  : PP(pp), Target(t) {
  // Scan all of the string portions, remember the max individual token length,
  // computing a bound on the concatenated string length, and see whether any
  // piece is a wide-string.  If any of the string portions is a wide-string
  // literal, the result is a wide-string literal [C99 6.4.5p4].
  MaxTokenLength = StringToks[0].getLength();
  SizeBound = StringToks[0].getLength()-2;  // -2 for "".
  AnyWide = StringToks[0].is(tok::wide_string_literal);
  
  hadError = false;

  // Implement Translation Phase #6: concatenation of string literals
  /// (C99 5.1.1.2p1).  The common case is only one string fragment.
  for (unsigned i = 1; i != NumStringToks; ++i) {
    // The string could be shorter than this if it needs cleaning, but this is a
    // reasonable bound, which is all we need.
    SizeBound += StringToks[i].getLength()-2;  // -2 for "".
    
    // Remember maximum string piece length.
    if (StringToks[i].getLength() > MaxTokenLength) 
      MaxTokenLength = StringToks[i].getLength();
    
    // Remember if we see any wide strings.
    AnyWide |= StringToks[i].is(tok::wide_string_literal);
  }
  
  
  // Include space for the null terminator.
  ++SizeBound;
  
  // TODO: K&R warning: "traditional C rejects string constant concatenation"
  
  // Get the width in bytes of wchar_t.  If no wchar_t strings are used, do not
  // query the target.  As such, wchar_tByteWidth is only valid if AnyWide=true.
  wchar_tByteWidth = ~0U;
  if (AnyWide) {
    wchar_tByteWidth = Target.getWCharWidth(StringToks[0].getLocation());
    assert((wchar_tByteWidth & 7) == 0 && "Assumes wchar_t is byte multiple!");
    wchar_tByteWidth /= 8;
  }
  
  // The output buffer size needs to be large enough to hold wide characters.
  // This is a worst-case assumption which basically corresponds to L"" "long".
  if (AnyWide)
    SizeBound *= wchar_tByteWidth;
  
  // Size the temporary buffer to hold the result string data.
  ResultBuf.resize(SizeBound);
  
  // Likewise, but for each string piece.
  llvm::SmallString<512> TokenBuf;
  TokenBuf.resize(MaxTokenLength);
  
  // Loop over all the strings, getting their spelling, and expanding them to
  // wide strings as appropriate.
  ResultPtr = &ResultBuf[0];   // Next byte to fill in.
  
  Pascal = false;
  
  for (unsigned i = 0, e = NumStringToks; i != e; ++i) {
    const char *ThisTokBuf = &TokenBuf[0];
    // Get the spelling of the token, which eliminates trigraphs, etc.  We know
    // that ThisTokBuf points to a buffer that is big enough for the whole token
    // and 'spelled' tokens can only shrink.
    unsigned ThisTokLen = PP.getSpelling(StringToks[i], ThisTokBuf);
    const char *ThisTokEnd = ThisTokBuf+ThisTokLen-1;  // Skip end quote.
    
    // TODO: Input character set mapping support.
    
    // Skip L marker for wide strings.
    bool ThisIsWide = false;
    if (ThisTokBuf[0] == 'L') {
      ++ThisTokBuf;
      ThisIsWide = true;
    }
    
    assert(ThisTokBuf[0] == '"' && "Expected quote, lexer broken?");
    ++ThisTokBuf;
    
    // Check if this is a pascal string
    if (pp.getLangOptions().PascalStrings && ThisTokBuf + 1 != ThisTokEnd &&
        ThisTokBuf[0] == '\\' && ThisTokBuf[1] == 'p') {
      
      // If the \p sequence is found in the first token, we have a pascal string
      // Otherwise, if we already have a pascal string, ignore the first \p
      if (i == 0) {
        ++ThisTokBuf;
        Pascal = true;
      } else if (Pascal)
        ThisTokBuf += 2;
    }
      
    while (ThisTokBuf != ThisTokEnd) {
      // Is this a span of non-escape characters?
      if (ThisTokBuf[0] != '\\') {
        const char *InStart = ThisTokBuf;
        do {
          ++ThisTokBuf;
        } while (ThisTokBuf != ThisTokEnd && ThisTokBuf[0] != '\\');
        
        // Copy the character span over.
        unsigned Len = ThisTokBuf-InStart;
        if (!AnyWide) {
          memcpy(ResultPtr, InStart, Len);
          ResultPtr += Len;
        } else {
          // Note: our internal rep of wide char tokens is always little-endian.
          for (; Len; --Len, ++InStart) {
            *ResultPtr++ = InStart[0];
            // Add zeros at the end.
            for (unsigned i = 1, e = wchar_tByteWidth; i != e; ++i)
            *ResultPtr++ = 0;
          }
        }
        continue;
      }
      
      // Otherwise, this is an escape character.  Process it.
      unsigned ResultChar = ProcessCharEscape(ThisTokBuf, ThisTokEnd, hadError,
                                              StringToks[i].getLocation(),
                                              ThisIsWide, PP);
      
      // Note: our internal rep of wide char tokens is always little-endian.
      *ResultPtr++ = ResultChar & 0xFF;
      
      if (AnyWide) {
        for (unsigned i = 1, e = wchar_tByteWidth; i != e; ++i)
          *ResultPtr++ = ResultChar >> i*8;
      }
    }
  }
  
  // Add zero terminator.
  *ResultPtr = 0;
  if (AnyWide) {
    for (unsigned i = 1, e = wchar_tByteWidth; i != e; ++i)
    *ResultPtr++ = 0;
  }
    
  if (Pascal) 
    ResultBuf[0] = ResultPtr-&ResultBuf[0]-1;
}
