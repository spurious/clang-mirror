//===--- LiteralSupport.h ---------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file was developed by Steve Naroff and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the NumericLiteralParser, CharLiteralParser, and
// StringLiteralParser interfaces.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LITERALSUPPORT_H
#define LLVM_CLANG_LITERALSUPPORT_H

#include <string>
#include "llvm/ADT/SmallString.h"

namespace llvm {
  class APInt;
namespace clang {

class Diagnostic;
class Preprocessor;
class LexerToken;
class SourceLocation;
class TargetInfo;
    
/// NumericLiteralParser - This performs strict semantic analysis of the content
/// of a ppnumber, classifying it as either integer, floating, or erroneous,
/// determines the radix of the value and can convert it to a useful value.
class NumericLiteralParser {
  Preprocessor &PP; // needed for diagnostics
  
  const char *const ThisTokBegin;
  const char *const ThisTokEnd;
  const char *DigitsBegin, *SuffixBegin; // markers
  const char *s; // cursor
  
  unsigned int radix;
  
  bool saw_exponent, saw_period;
  bool saw_float_suffix;
  
public:
  NumericLiteralParser(const char *begin, const char *end,
                       SourceLocation Loc, Preprocessor &PP);
  bool hadError;
  bool isUnsigned;
  bool isLong;
  bool isLongLong;
  
  bool isIntegerLiteral() const { 
    return !saw_period && !saw_exponent ? true : false;
  }
  bool isFloatingLiteral() const {
    return saw_period || saw_exponent ? true : false;
  }
  bool hasSuffix() const {
    return SuffixBegin != ThisTokEnd;
  }
  /// getIntegerValue - Convert the string into a number. At this point, we 
  /// know the digit characters are valid (0...9, a...f, A...F). We don't know
  /// how many bits are needed to store the number. Sizing of the integer
  /// type (int, unsigned, long, unsigned long, long long, unsigned long long) 
  /// will be done elsewhere - the size computation is target dependent. We  
  /// return true if the value fit into "val", false otherwise. 
  /// NOTE: The api of these returns an inverted value for 'overflow' than the
  /// version below does.
  bool GetIntegerValue(uintmax_t &val);
  bool GetIntegerValue(int &val);
  
  /// GetIntegerValue - Convert this numeric literal value to an APInt that
  /// matches Val's input width.  If there is an overflow, set Val to the low
  /// bits of the result and return true.  Otherwise, return false.
  bool GetIntegerValue(APInt &Val);

private:  
  void Diag(SourceLocation Loc, unsigned DiagID, 
            const std::string &M = std::string());
  
  /// SkipHexDigits - Read and skip over any hex digits, up to End.
  /// Return a pointer to the first non-hex digit or End.
  const char *SkipHexDigits(const char *ptr) {
    while (ptr != ThisTokEnd && isxdigit(*ptr))
      ptr++;
    return ptr;
  }
  
  /// SkipOctalDigits - Read and skip over any octal digits, up to End.
  /// Return a pointer to the first non-hex digit or End.
  const char *SkipOctalDigits(const char *ptr) {
    while (ptr != ThisTokEnd && ((*ptr >= '0') && (*ptr <= '7')))
      ptr++;
    return ptr;
  }
  
  /// SkipDigits - Read and skip over any digits, up to End.
  /// Return a pointer to the first non-hex digit or End.
  const char *SkipDigits(const char *ptr) {
    while (ptr != ThisTokEnd && isdigit(*ptr))
      ptr++;
    return ptr;
  }
};

/// CharLiteralParser - Perform interpretation and semantic analysis of a
/// character literal.
class CharLiteralParser {
  unsigned Value;
  bool IsWide;
  bool HadError;
public:
  CharLiteralParser(const char *begin, const char *end,
                    SourceLocation Loc, Preprocessor &PP);

  bool hadError() const { return HadError; }
  bool isWide() const { return IsWide; }
  unsigned getValue() const { return Value; }
};

/// StringLiteralParser - This decodes string escape characters and performs
/// wide string analysis and Translation Phase #6 (concatenation of string
/// literals) (C99 5.1.1.2p1).
class StringLiteralParser {
  Preprocessor &PP;
  TargetInfo &Target;
  
  unsigned MaxTokenLength;
  unsigned SizeBound;
  unsigned wchar_tByteWidth;
  SmallString<512> ResultBuf;
  char *ResultPtr; // cursor
public:
  StringLiteralParser(const LexerToken *StringToks, unsigned NumStringToks,
                      Preprocessor &PP, TargetInfo &T);
  bool hadError;
  bool AnyWide;
  
  const char *GetString() { return &ResultBuf[0]; }
  unsigned GetStringLength() { return ResultPtr-&ResultBuf[0]; }
};
  
}  // end namespace clang
}  // end namespace llvm

#endif