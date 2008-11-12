//===--- PreprocessorLexer.h - C Language Family Lexer ----------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the PreprocessorLexer interface.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_PreprocessorLexer_H
#define LLVM_CLANG_PreprocessorLexer_H

#include "clang/Lex/MultipleIncludeOpt.h"
#include "clang/Lex/Token.h"
#include <vector>

namespace clang {

class Preprocessor;

class PreprocessorLexer {
protected:  
  // Context-specific lexing flags set by the preprocessor.
  
  /// ParsingPreprocessorDirective - This is true when parsing #XXX.  This turns
  /// '\n' into a tok::eom token.
  bool ParsingPreprocessorDirective;
  
  /// ParsingFilename - True after #include: this turns <xx> into a
  /// tok::angle_string_literal token.
  bool ParsingFilename;
  
  /// LexingRawMode - True if in raw mode:  This flag disables interpretation of
  /// tokens and is a far faster mode to lex in than non-raw-mode.  This flag:
  ///  1. If EOF of the current lexer is found, the include stack isn't popped.
  ///  2. Identifier information is not looked up for identifier tokens.  As an
  ///     effect of this, implicit macro expansion is naturally disabled.
  ///  3. "#" tokens at the start of a line are treated as normal tokens, not
  ///     implicitly transformed by the lexer.
  ///  4. All diagnostic messages are disabled.
  ///  5. No callbacks are made into the preprocessor.
  ///
  /// Note that in raw mode that the PP pointer may be null.
  bool LexingRawMode;
  
  /// MIOpt - This is a state machine that detects the #ifndef-wrapping a file 
  /// idiom for the multiple-include optimization.
  MultipleIncludeOpt MIOpt;
  
  /// ConditionalStack - Information about the set of #if/#ifdef/#ifndef blocks
  /// we are currently in.
  std::vector<PPConditionalInfo> ConditionalStack;
  
  PreprocessorLexer(const PreprocessorLexer&);          // DO NOT IMPLEMENT
  void operator=(const PreprocessorLexer&); // DO NOT IMPLEMENT
  friend class Preprocessor;
  
  PreprocessorLexer() {}
  
protected:
  
  //===--------------------------------------------------------------------===//
  // #if directive handling.
  
  /// pushConditionalLevel - When we enter a #if directive, this keeps track of
  /// what we are currently in for diagnostic emission (e.g. #if with missing
  /// #endif).
  void pushConditionalLevel(SourceLocation DirectiveStart, bool WasSkipping,
                            bool FoundNonSkip, bool FoundElse) {
    PPConditionalInfo CI;
    CI.IfLoc = DirectiveStart;
    CI.WasSkipping = WasSkipping;
    CI.FoundNonSkip = FoundNonSkip;
    CI.FoundElse = FoundElse;
    ConditionalStack.push_back(CI);
  }
  void pushConditionalLevel(const PPConditionalInfo &CI) {
    ConditionalStack.push_back(CI);
  }    
  
  /// popConditionalLevel - Remove an entry off the top of the conditional
  /// stack, returning information about it.  If the conditional stack is empty,
  /// this returns true and does not fill in the arguments.
  bool popConditionalLevel(PPConditionalInfo &CI) {
    if (ConditionalStack.empty()) return true;
    CI = ConditionalStack.back();
    ConditionalStack.pop_back();
    return false;
  }
  
  /// peekConditionalLevel - Return the top of the conditional stack.  This
  /// requires that there be a conditional active.
  PPConditionalInfo &peekConditionalLevel() {
    assert(!ConditionalStack.empty() && "No conditionals active!");
    return ConditionalStack.back();
  }
  
  unsigned getConditionalStackDepth() const { return ConditionalStack.size(); }    
};

}  // end namespace clang

#endif
