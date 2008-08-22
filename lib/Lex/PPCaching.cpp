//===--- PPCaching.cpp - Handle caching lexed tokens ----------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements pieces of the Preprocessor interface that manage the
// caching of lexed tokens.
//
//===----------------------------------------------------------------------===//

#include "clang/Lex/Preprocessor.h"
using namespace clang;

/// EnableBacktrackAtThisPos - From the point that this method is called, and
/// until DisableBacktrack() or Backtrack() is called, the Preprocessor keeps
/// track of the lexed tokens so that a subsequent Backtrack() call will make
/// the Preprocessor re-lex the same tokens.
///
/// Nested backtracks are allowed, meaning that EnableBacktrackAtThisPos can
/// be called multiple times and DisableBacktrack/Backtrack calls will be
/// combined with the EnableBacktrackAtThisPos calls in reverse order.
void Preprocessor::EnableBacktrackAtThisPos() {
  CacheTokens = true;
  BacktrackPositions.push_back(CachedLexPos);
  EnterCachingLexMode();
}

/// DisableBacktrack - Disable the last EnableBacktrackAtThisPos() call.
void Preprocessor::DisableBacktrack() {
  assert(!BacktrackPositions.empty()
         && "EnableBacktrackAtThisPos was not called!");
  BacktrackPositions.pop_back();
  CacheTokens = !BacktrackPositions.empty();
}

/// Backtrack - Make Preprocessor re-lex the tokens that were lexed since
/// EnableBacktrackAtThisPos() was previously called. 
void Preprocessor::Backtrack() {
  assert(!BacktrackPositions.empty()
         && "EnableBacktrackAtThisPos was not called!");
  CachedLexPos = BacktrackPositions.back();
  BacktrackPositions.pop_back();
  CacheTokens = !BacktrackPositions.empty();
}

void Preprocessor::CachingLex(Token &Result) {
  if (CachedLexPos < CachedTokens.size()) {
    Result = CachedTokens[CachedLexPos++];
    return;
  }

  ExitCachingLexMode();
  Lex(Result);

  if (!CacheTokens) {
    // All cached tokens were consumed.
    CachedTokens.clear();
    CachedLexPos = 0;
    return;
  }

  // We should cache the lexed token.

  EnterCachingLexMode();
  if (Result.isNot(tok::eof)) {
    CachedTokens.push_back(Result);
    ++CachedLexPos;
  }
}

void Preprocessor::EnterCachingLexMode() {
  if (InCachingLexMode())
    return;

  IncludeMacroStack.push_back(IncludeStackInfo(CurLexer, CurDirLookup,
                                               CurTokenLexer));
  CurLexer = 0;
  CurTokenLexer = 0;
}


const Token &Preprocessor::PeekAhead(unsigned N) {
  assert(CachedLexPos + N > CachedTokens.size() && "Confused caching.");
  ExitCachingLexMode();
  for (unsigned C = CachedLexPos + N - CachedTokens.size(); C > 0; --C) {
    CachedTokens.push_back(Token());
    Lex(CachedTokens.back());
  }
  EnterCachingLexMode();
  return CachedTokens.back();
}
