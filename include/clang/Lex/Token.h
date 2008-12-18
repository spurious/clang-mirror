//===--- Token.h - Token interface ------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the Token interface.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOKEN_H
#define LLVM_CLANG_TOKEN_H

#include "clang/Basic/TokenKinds.h"
#include "clang/Basic/SourceLocation.h"

namespace clang {

class IdentifierInfo;

/// Token - This structure provides full information about a lexed token.
/// It is not intended to be space efficient, it is intended to return as much
/// information as possible about each returned token.  This is expected to be
/// compressed into a smaller form if memory footprint is important.
///
/// The parser can create a special "annotation token" representing a stream of
/// tokens that were parsed and semantically resolved, e.g.: "foo::MyClass<int>"
/// can be represented by a single typename annotation token that carries
/// information about the SourceRange of the tokens and the type object.
class Token {
  /// The location of the token.
  SourceLocation Loc;

  // Conceptually these next two fields could be in a union with
  // access depending on isAnnotationToken(). However, this causes gcc
  // 4.2 to pessimize LexTokenInternal, a very performance critical
  // routine. Keeping as separate members with casts until a more
  // beautiful fix presents itself.

  /// UintData - This holds either the length of the token text, when
  /// a normal token, or the end of the SourceRange when an annotation
  /// token.
  unsigned UintData;

  /// PtrData - For normal tokens, this points to the uniqued
  /// information for the identifier (if an identifier token) or
  /// null. For annotation tokens, this points to information specific
  /// to the annotation token.
  void *PtrData;

  /// Kind - The actual flavor of token this is.
  ///
  unsigned Kind : 8;  // DON'T make Kind a 'tok::TokenKind'; 
                      // MSVC will treat it as a signed char and
                      // TokenKinds > 127 won't be handled correctly.
  
  /// Flags - Bits we track about this token, members of the TokenFlags enum.
  unsigned Flags : 8;
public:
    
  // Various flags set per token:
  enum TokenFlags {
    StartOfLine   = 0x01,  // At start of line or only after whitespace.
    LeadingSpace  = 0x02,  // Whitespace exists before this token.
    DisableExpand = 0x04,  // This identifier may never be macro expanded.
    NeedsCleaning = 0x08   // Contained an escaped newline or trigraph.
  };

  tok::TokenKind getKind() const { return (tok::TokenKind)Kind; }
  void setKind(tok::TokenKind K) { Kind = K; }
  
  /// is/isNot - Predicates to check if this token is a specific kind, as in
  /// "if (Tok.is(tok::l_brace)) {...}".
  bool is(tok::TokenKind K) const { return Kind == (unsigned) K; }
  bool isNot(tok::TokenKind K) const { return Kind != (unsigned) K; }

  bool isAnnotationToken() const { 
    return is(tok::annot_qualtypename) || 
           is(tok::annot_cxxscope) ||
           is(tok::annot_template_id);
  }

  /// getLocation - Return a source location identifier for the specified
  /// offset in the current file.
  SourceLocation getLocation() const { return Loc; }
  unsigned getLength() const {
    assert(!isAnnotationToken() && "Used Length on annotation token");
    return UintData;
  }

  void setLocation(SourceLocation L) { Loc = L; }
  void setLength(unsigned Len) { UintData = Len; }

  SourceLocation getAnnotationEndLoc() const {
    assert(isAnnotationToken() && "Used AnnotEndLocID on non-annotation token");
    return SourceLocation::getFromRawEncoding(UintData);
  }
  void setAnnotationEndLoc(SourceLocation L) {
    assert(isAnnotationToken() && "Used AnnotEndLocID on non-annotation token");
    UintData = L.getRawEncoding();
  }

  /// getAnnotationRange - SourceRange of the group of tokens that this
  /// annotation token represents.
  SourceRange getAnnotationRange() const {
    return SourceRange(getLocation(), getAnnotationEndLoc());
  }
  void setAnnotationRange(SourceRange R) {
    setLocation(R.getBegin());
    setAnnotationEndLoc(R.getEnd());
  }
  
  const char *getName() const {
    return tok::getTokenName( (tok::TokenKind) Kind);
  }
  
  /// startToken - Reset all flags to cleared.
  ///
  void startToken() {
    Flags = 0;
    PtrData = 0;
    Loc = SourceLocation();
  }
  
  IdentifierInfo *getIdentifierInfo() const {
    assert(!isAnnotationToken() && "Used IdentInfo on annotation token");
    return (IdentifierInfo*) PtrData;
  }
  void setIdentifierInfo(IdentifierInfo *II) {
    PtrData = (void*) II;
  }

  void *getAnnotationValue() const {
    assert(isAnnotationToken() && "Used AnnotVal on non-annotation token");
    return PtrData;
  }
  void setAnnotationValue(void *val) {
    assert(isAnnotationToken() && "Used AnnotVal on non-annotation token");
    PtrData = val;
  }
  
  /// setFlag - Set the specified flag.
  void setFlag(TokenFlags Flag) {
    Flags |= Flag;
  }
  
  /// clearFlag - Unset the specified flag.
  void clearFlag(TokenFlags Flag) {
    Flags &= ~Flag;
  }
  
  /// getFlags - Return the internal represtation of the flags.
  ///  Only intended for low-level operations such as writing tokens to
  //   disk.
  unsigned getFlags() const {
    return Flags;
  }

  /// setFlagValue - Set a flag to either true or false.
  void setFlagValue(TokenFlags Flag, bool Val) {
    if (Val) 
      setFlag(Flag);
    else
      clearFlag(Flag);
  }
  
  /// isAtStartOfLine - Return true if this token is at the start of a line.
  ///
  bool isAtStartOfLine() const { return (Flags & StartOfLine) ? true : false; }
  
  /// hasLeadingSpace - Return true if this token has whitespace before it.
  ///
  bool hasLeadingSpace() const { return (Flags & LeadingSpace) ? true : false; }
  
  /// isExpandDisabled - Return true if this identifier token should never
  /// be expanded in the future, due to C99 6.10.3.4p2.
  bool isExpandDisabled() const {
    return (Flags & DisableExpand) ? true : false;
  }
  
  /// isObjCAtKeyword - Return true if we have an ObjC keyword identifier. 
  bool isObjCAtKeyword(tok::ObjCKeywordKind objcKey) const;
  
  /// getObjCKeywordID - Return the ObjC keyword kind.
  tok::ObjCKeywordKind getObjCKeywordID() const;
  
  /// needsCleaning - Return true if this token has trigraphs or escaped
  /// newlines in it.
  ///
  bool needsCleaning() const { return (Flags & NeedsCleaning) ? true : false; }
};

/// PPConditionalInfo - Information about the conditional stack (#if directives)
/// currently active.
struct PPConditionalInfo {
  /// IfLoc - Location where the conditional started.
  ///
  SourceLocation IfLoc;
  
  /// WasSkipping - True if this was contained in a skipping directive, e.g.
  /// in a "#if 0" block.
  bool WasSkipping;
  
  /// FoundNonSkip - True if we have emitted tokens already, and now we're in
  /// an #else block or something.  Only useful in Skipping blocks.
  bool FoundNonSkip;
  
  /// FoundElse - True if we've seen a #else in this block.  If so,
  /// #elif/#else directives are not allowed.
  bool FoundElse;
};

/// TemplateIdAnnotation - Information about a template-id annotation
/// token, which contains the template declaration, template
/// arguments, and the source locations for important tokens.
struct TemplateIdAnnotation {
  /// TemplateNameLoc - The location of the template name within the
  /// source.
  SourceLocation TemplateNameLoc;

  /// Template - The declaration of the template corresponding to the
  /// template-name. This is an Action::DeclTy*.
  void *Template; 

  /// LAngleLoc - The location of the '<' before the template argument
  /// list. 
  SourceLocation LAngleLoc;

  /// NumArgs - The number of template arguments. The arguments
  /// themselves are Action::TemplateArgTy pointers allocated directly
  /// following the TemplateIdAnnotation structure.
  unsigned NumArgs; 
};

}  // end namespace clang

#endif
