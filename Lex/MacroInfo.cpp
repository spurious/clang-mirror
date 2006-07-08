//===--- MacroInfo.cpp - Information about #defined identifiers -----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file was developed by Chris Lattner and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the MacroInfo interface.
//
//===----------------------------------------------------------------------===//

#include "clang/Lex/MacroInfo.h"
#include "clang/Lex/Preprocessor.h"
#include <iostream>
using namespace llvm;
using namespace clang;

MacroInfo::MacroInfo(SourceLocation DefLoc) : Location(DefLoc) {
  IsFunctionLike = false;
  IsC99Varargs = false;
  IsGNUVarargs = false;
  IsBuiltinMacro = false;
  IsDisabled = false;
  IsUsed = true;
}


/// isIdenticalTo - Return true if the specified macro definition is equal to
/// this macro in spelling, arguments, and whitespace.  This is used to emit
/// duplicate definition warnings.  This implements the rules in C99 6.10.3.
bool MacroInfo::isIdenticalTo(const MacroInfo &Other, Preprocessor &PP) const {
  // TODO: Check param count.
  
  // Check # tokens in replacement match.
  if (ReplacementTokens.size() != Other.ReplacementTokens.size() ||
      isFunctionLike() != Other.isFunctionLike() ||
      isC99Varargs() != Other.isC99Varargs() ||
      isGNUVarargs() != Other.isGNUVarargs())
    return false;
  
  // Check all the tokens.
  for (unsigned i = 0, e = ReplacementTokens.size(); i != e; ++i) {
    const LexerToken &A = ReplacementTokens[i];
    const LexerToken &B = Other.ReplacementTokens[i];
    if (A.getKind() != B.getKind() || 
        A.isAtStartOfLine() != B.isAtStartOfLine() ||
        A.hasLeadingSpace() != B.hasLeadingSpace())
      return false;
    
    // If this is an identifier, it is easy.
    if (A.getIdentifierInfo() || B.getIdentifierInfo()) {
      if (A.getIdentifierInfo() != B.getIdentifierInfo())
        return false;
      continue;
    }
    
    // Otherwise, check the spelling.
    if (PP.getSpelling(A) != PP.getSpelling(B))
      return false;
  }
  
  return true;
}
