//===--- MacroExpander.h - Lex from a macro expansion -----------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file was developed by Chris Lattner and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the MacroExpander and MacroArgs interfaces.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_MACROEXPANDER_H
#define LLVM_CLANG_MACROEXPANDER_H

#include "clang/Basic/SourceLocation.h"
#include <vector>

namespace llvm {
namespace clang {
  class MacroInfo;
  class Preprocessor;
  class LexerToken;

/// MacroArgs - An instance of this class captures information about
/// the formal arguments specified to a function-like macro invocation.
class MacroArgs {
  /// UnexpArgTokens - Raw, unexpanded tokens for the arguments.  This includes
  /// an 'EOF' marker at the end of each argument.
  std::vector<std::vector<LexerToken> > UnexpArgTokens;

  /// ExpArgTokens - Pre-expanded tokens for arguments that need them.  Empty if
  /// not yet computed.  This includes the EOF marker at the end of the stream.
  std::vector<std::vector<LexerToken> > ExpArgTokens;

  /// StringifiedArgs - This contains arguments in 'stringified' form.  If the
  /// stringified form of an argument has not yet been computed, this is empty.
  std::vector<LexerToken> StringifiedArgs;
public:
  MacroArgs(const MacroInfo *MI);
  
  /// addArgument - Add an argument for this invocation.  This method destroys
  /// the vector passed in to avoid extraneous memory copies.  This adds the EOF
  /// token to the end of the argument list as a marker.  'Loc' specifies a
  /// location at the end of the argument, e.g. the ',' token or the ')'.
  void addArgument(std::vector<LexerToken> &ArgToks, SourceLocation Loc);
  
  /// ArgNeedsPreexpansion - If we can prove that the argument won't be affected
  /// by pre-expansion, return false.  Otherwise, conservatively return true.
  bool ArgNeedsPreexpansion(unsigned ArgNo) const;
  
  /// getUnexpArgument - Return the unexpanded tokens for the specified formal.
  ///
  const std::vector<LexerToken> &getUnexpArgument(unsigned Arg) const {
    assert(Arg < UnexpArgTokens.size() && "Invalid ArgNo");
    return UnexpArgTokens[Arg];
  }
  
  /// getStringifiedArgument - Compute, cache, and return the specified argument
  /// that has been 'stringified' as required by the # operator.
  const LexerToken &getStringifiedArgument(unsigned ArgNo, Preprocessor &PP);
  
  /// getNumArguments - Return the number of arguments passed into this macro
  /// invocation.
  unsigned getNumArguments() const { return UnexpArgTokens.size(); }
};

  
/// MacroExpander - This implements a lexer that returns token from a macro body
/// instead of lexing from a character buffer.
///
class MacroExpander {
  /// Macro - The macro we are expanding from.
  ///
  MacroInfo &Macro;

  /// ActualArgs - The actual arguments specified for a function-like macro, or
  /// null.  The MacroExpander owns the pointed-to object.
  MacroArgs *ActualArgs;

  /// PP - The current preprocessor object we are expanding for.
  ///
  Preprocessor &PP;

  /// MacroTokens - This is the pointer to the list of tokens that the macro is
  /// defined to, with arguments expanded for function-like macros.
  const std::vector<LexerToken> *MacroTokens;
  
  /// CurToken - This is the next token that Lex will return.
  ///
  unsigned CurToken;
  
  /// InstantiateLoc - The source location where this macro was instantiated.
  ///
  SourceLocation InstantiateLoc;
  
  /// Lexical information about the expansion point of the macro: the identifier
  /// that the macro expanded from had these properties.
  bool AtStartOfLine, HasLeadingSpace;
  
  MacroExpander(const MacroExpander&);  // DO NOT IMPLEMENT
  void operator=(const MacroExpander&); // DO NOT IMPLEMENT
public:
  /// Create a macro expander of the specified macro with the specified actual
  /// arguments.  Note that this ctor takes ownership of the ActualArgs pointer.
  MacroExpander(LexerToken &Tok, MacroArgs *ActualArgs, Preprocessor &PP);
  ~MacroExpander();
  
  /// isNextTokenLParen - If the next token lexed will pop this macro off the
  /// expansion stack, return 2.  If the next unexpanded token is a '(', return
  /// 1, otherwise return 0.
  unsigned isNextTokenLParen() const;
  
  MacroInfo &getMacro() const { return Macro; }

  /// Lex - Lex and return a token from this macro stream.
  void Lex(LexerToken &Tok);
  
private:
  /// isAtEnd - Return true if the next lex call will pop this macro off the
  /// include stack.
  bool isAtEnd() const {
    return CurToken == MacroTokens->size();
  }
  
  /// Expand the arguments of a function-like macro so that we can quickly
  /// return preexpanded tokens from MacroTokens.
  void ExpandFunctionArguments();
};

}  // end namespace llvm
}  // end namespace clang

#endif
