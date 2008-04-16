//==- HTMLRewrite.h - Translate source code into prettified HTML ---*- C++ -*-//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines a set of functions used for translating source code
//  into beautified HTML.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_HTMLREWRITER_H
#define LLVM_CLANG_HTMLREWRITER_H

#include "clang/Basic/SourceLocation.h"
#include <string>

namespace clang {
  
class Rewriter;
class Preprocessor;
  
namespace html {

  /// HighlightRange - Highlight a range in the source code with the specified
  /// start/end tags.  B/E must be in the same file.  This ensures that
  /// start/end tags are placed at the start/end of each line if the range is
  /// multiline.
  void HighlightRange(Rewriter &R, SourceLocation B, SourceLocation E,
                      const char *StartTag, const char *EndTag);
  
  /// HighlightRange - This is the same as the above method, but takes
  /// decomposed file locations.
  void HighlightRange(RewriteBuffer &RB, unsigned B, unsigned E,
                      const char *BufferStart,
                      const char *StartTag, const char *EndTag);
  
  /// EscapeText - HTMLize a specified file so that special characters are
  /// are translated so that they are not interpreted as HTML tags.  In this
  /// version tabs are not replaced with spaces by default, as this can
  /// introduce a serious performance overhead as the amount of replaced
  /// text can be very large.
  void EscapeText(Rewriter& R, unsigned FileID,
                  bool EscapeSpaces = false, bool ReplacesTabs = false);

  /// EscapeText - HTMLized the provided string so that special characters
  ///  in 's' are not interpreted as HTML tags.  Unlike the version of
  ///  EscapeText that rewrites a file, this version by default replaces tabs
  ///  with spaces.
  std::string EscapeText(const std::string& s,
                         bool EscapeSpaces = false, bool ReplaceTabs = true);

  void AddLineNumbers(Rewriter& R, unsigned FileID);  
  
  void AddHeaderFooterInternalBuiltinCSS(Rewriter& R, unsigned FileID);

  /// SyntaxHighlight - Relex the specified FileID and annotate the HTML with
  /// information about keywords, comments, etc.
  void SyntaxHighlight(Rewriter &R, unsigned FileID, Preprocessor &PP);

  /// HighlightMacros - This uses the macro table state from the end of the
  /// file, to reexpand macros and insert (into the HTML) information about the
  /// macro expansions.  This won't be perfectly perfect, but it will be
  /// reasonably close.
  void HighlightMacros(Rewriter &R, unsigned FileID, Preprocessor &PP);
  

  
} // end html namespace
} // end clang namespace

#endif
