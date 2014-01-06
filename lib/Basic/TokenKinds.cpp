//===--- TokenKinds.cpp - Token Kinds Support -----------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file implements the TokenKind enum and support functions.
//
//===----------------------------------------------------------------------===//

#include "clang/Basic/TokenKinds.h"
#include "llvm/Support/ErrorHandling.h"
using namespace clang;

static const char * const TokNames[] = {
#define TOK(X) #X,
#define KEYWORD(X,Y) #X,
#include "clang/Basic/TokenKinds.def"
  0
};

const char *tok::getTokenName(enum TokenKind Kind) {
  if (Kind < tok::NUM_TOKENS)
    return TokNames[Kind];
  llvm_unreachable("unknown TokenKind");
  return 0;
}

const char *tok::getPunctuatorSpelling(enum TokenKind Kind) {
  switch (Kind) {
#define PUNCTUATOR(X,Y) case X: return Y;
#include "clang/Basic/TokenKinds.def"
  default: break;
  }

  return 0;
}
