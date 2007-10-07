//===--- IdentifierTable.h - Hash table for identifier lookup ---*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file was developed by Chris Lattner and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the IdentifierInfo and IdentifierTable interfaces.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LEX_IDENTIFIERTABLE_H
#define LLVM_CLANG_LEX_IDENTIFIERTABLE_H

// FIXME: Move this header header/module to the "Basic" library. Unlike Lex,
// this data is long-lived.
#include "clang/Basic/TokenKinds.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/SmallString.h"
#include <string> 
#include <cassert> 

namespace llvm {
  template <typename T> struct DenseMapInfo;
}

namespace clang {
  class MacroInfo;
  struct LangOptions;
  class MultiKeywordSelector; // a private class used by Selector.
  
/// IdentifierInfo - One of these records is kept for each identifier that
/// is lexed.  This contains information about whether the token was #define'd,
/// is a language keyword, or if it is a front-end token of some sort (e.g. a
/// variable or function name).  The preprocessor keeps this information in a
/// set, and all tok::identifier tokens have a pointer to one of these.  
class IdentifierInfo {
  MacroInfo *Macro;                // Set if this identifier is #define'd.
  tok::TokenKind TokenID      : 8; // Front-end token ID or tok::identifier.
  tok::PPKeywordKind PPID     : 5; // ID for preprocessor command like #'ifdef'.
  tok::ObjCKeywordKind ObjCID : 5; // ID for objc @ keyword like @'protocol'.
  unsigned BuiltinID          : 9; // ID if this is a builtin (__builtin_inf).
  bool IsExtension            : 1; // True if identifier is a lang extension.
  bool IsPoisoned             : 1; // True if identifier is poisoned.
  bool IsOtherTargetMacro     : 1; // True if ident is macro on another target.
  bool IsCPPOperatorKeyword   : 1; // True if ident is a C++ operator keyword.
  bool IsNonPortableBuiltin   : 1; // True if builtin varies across targets.
  void *FETokenInfo;               // Managed by the language front-end.
  IdentifierInfo(const IdentifierInfo&);  // NONCOPYABLE.
public:
  IdentifierInfo();
  ~IdentifierInfo();

  /// getName - Return the actual string for this identifier.  The returned 
  /// string is properly null terminated.
  ///
  const char *getName() const {
    // We know that this is embedded into a StringMapEntry, and it knows how to
    // efficiently find the string.
    return llvm::StringMapEntry<IdentifierInfo>::
                  GetStringMapEntryFromValue(*this).getKeyData();
  }
  
  /// getLength - Efficiently return the length of this identifier info.
  ///
  unsigned getLength() const {
    return llvm::StringMapEntry<IdentifierInfo>::
                    GetStringMapEntryFromValue(*this).getKeyLength();
  }
  
  /// getMacroInfo - Return macro information about this identifier, or null if
  /// it is not a macro.
  MacroInfo *getMacroInfo() const { return Macro; }
  void setMacroInfo(MacroInfo *I) { Macro = I; }
  
  /// get/setTokenID - If this is a source-language token (e.g. 'for'), this API
  /// can be used to cause the lexer to map identifiers to source-language
  /// tokens.
  tok::TokenKind getTokenID() const { return TokenID; }
  void setTokenID(tok::TokenKind ID) { TokenID = ID; }
  
  /// getPPKeywordID - Return the preprocessor keyword ID for this identifier.
  /// For example, define will return tok::pp_define.
  tok::PPKeywordKind getPPKeywordID() const { return PPID; }
  void setPPKeywordID(tok::PPKeywordKind ID) { PPID = ID; }
  
  /// getObjCKeywordID - Return the Objective-C keyword ID for the this
  /// identifier.  For example, 'class' will return tok::objc_class if ObjC is
  /// enabled.
  tok::ObjCKeywordKind getObjCKeywordID() const { return ObjCID; }
  void setObjCKeywordID(tok::ObjCKeywordKind ID) { ObjCID = ID; }
  
  /// getBuiltinID - Return a value indicating whether this is a builtin
  /// function.  0 is not-built-in.  1 is builtin-for-some-nonprimary-target.
  /// 2+ are specific builtin functions.
  unsigned getBuiltinID() const { return BuiltinID; }
  void setBuiltinID(unsigned ID) {
    BuiltinID = ID;
    assert(BuiltinID == ID && "ID too large for field!");
  }
  
  /// isNonPortableBuiltin - Return true if this identifier corresponds to a
  /// builtin on some other target, but isn't one on this target, or if it is on
  /// the target but not on another, or if it is on both but it differs somehow
  /// in behavior.
  bool isNonPortableBuiltin() const { return IsNonPortableBuiltin; }
  void setNonPortableBuiltin(bool Val) { IsNonPortableBuiltin = Val; }
  
  /// get/setExtension - Initialize information about whether or not this
  /// language token is an extension.  This controls extension warnings, and is
  /// only valid if a custom token ID is set.
  bool isExtensionToken() const { return IsExtension; }
  void setIsExtensionToken(bool Val) { IsExtension = Val; }
  
  /// setIsPoisoned - Mark this identifier as poisoned.  After poisoning, the
  /// Preprocessor will emit an error every time this token is used.
  void setIsPoisoned(bool Value = true) { IsPoisoned = Value; }
  
  /// isPoisoned - Return true if this token has been poisoned.
  bool isPoisoned() const { return IsPoisoned; }
  
  /// setIsOtherTargetMacro/isOtherTargetMacro control whether this identifier
  /// is seen as being a macro on some other target.
  void setIsOtherTargetMacro(bool Val = true) { IsOtherTargetMacro = Val; }
  bool isOtherTargetMacro() const { return IsOtherTargetMacro; }

  /// isCPlusPlusOperatorKeyword/setIsCPlusPlusOperatorKeyword controls whether
  /// this identifier is a C++ alternate representation of an operator.
  void setIsCPlusplusOperatorKeyword(bool Val = true)
    { IsCPPOperatorKeyword = Val; }
  bool isCPlusPlusOperatorKeyword() const { return IsCPPOperatorKeyword; }

  /// getFETokenInfo/setFETokenInfo - The language front-end is allowed to
  /// associate arbitrary metadata with this token.
  template<typename T>
  T *getFETokenInfo() const { return static_cast<T*>(FETokenInfo); }
  void setFETokenInfo(void *T) { FETokenInfo = T; }
};

/// IdentifierTable - This table implements an efficient mapping from strings to
/// IdentifierInfo nodes.  It has no other purpose, but this is an
/// extremely performance-critical piece of the code, as each occurrance of
/// every identifier goes through here when lexed.
class IdentifierTable {
  // Shark shows that using MallocAllocator is *much* slower than using this
  // BumpPtrAllocator!
  typedef llvm::StringMap<IdentifierInfo, llvm::BumpPtrAllocator> HashTableTy;
  HashTableTy HashTable;
public:
  /// IdentifierTable ctor - Create the identifier table, populating it with
  /// info about the language keywords for the language specified by LangOpts.
  IdentifierTable(const LangOptions &LangOpts);
  
  /// get - Return the identifier token info for the specified named identifier.
  ///
  IdentifierInfo &get(const char *NameStart, const char *NameEnd) {
    return HashTable.GetOrCreateValue(NameStart, NameEnd).getValue();
  }
  
  IdentifierInfo &get(const char *Name) {
    return get(Name, Name+strlen(Name));
  }
  IdentifierInfo &get(const std::string &Name) {
    // Don't use c_str() here: no need to be null terminated.
    const char *NameBytes = &Name[0];
    return get(NameBytes, NameBytes+Name.size());
  }
  
  typedef HashTableTy::const_iterator iterator;
  typedef HashTableTy::const_iterator const_iterator;
  
  iterator begin() const { return HashTable.begin(); }
  iterator end() const   { return HashTable.end(); }
  
  /// PrintStats - Print some statistics to stderr that indicate how well the
  /// hashing is doing.
  void PrintStats() const;
private:
  void AddKeywords(const LangOptions &LangOpts);
};

/// Selector - This smart pointer class efficiently represents Objective-C
/// method names. This class will either point to an IdentifierInfo or a
/// MultiKeywordSelector (which is private). This enables us to optimize
/// selectors that no arguments and selectors that take 1 argument, which 
/// accounts for 78% of all selectors in Cocoa.h.
class Selector {
  enum IdentifierInfoFlag {
    // MultiKeywordSelector = 0.
    ZeroArg  = 0x1,
    OneArg   = 0x2,
    ArgFlags = ZeroArg|OneArg
  };
  uintptr_t InfoPtr; // a pointer to the MultiKeywordSelector or IdentifierInfo.
  
  Selector(IdentifierInfo *II, unsigned nArgs) {
    InfoPtr = reinterpret_cast<uintptr_t>(II);
    assert((InfoPtr & ArgFlags) == 0 &&"Insufficiently aligned IdentifierInfo");
    assert(nArgs < 2 && "nArgs not equal to 0/1");
    InfoPtr |= nArgs+1;
  }
  Selector(MultiKeywordSelector *SI) {
    InfoPtr = reinterpret_cast<uintptr_t>(SI);
    assert((InfoPtr & ArgFlags) == 0 &&"Insufficiently aligned IdentifierInfo");
  }
  Selector(intptr_t V) : InfoPtr(V) {}
public:
  friend class SelectorTable; // only the SelectorTable can create these.
  
  IdentifierInfo *getAsIdentifierInfo() const {
    if (getIdentifierInfoFlag())
      return reinterpret_cast<IdentifierInfo *>(InfoPtr & ~ArgFlags);
    return 0;
  }
  unsigned getIdentifierInfoFlag() const {
    return InfoPtr & ArgFlags;
  }
  /// operator==/!= - Indicate whether the specified selectors are identical.
  bool operator==(const Selector &RHS) const {
    return InfoPtr == RHS.InfoPtr;
  }
  bool operator!=(const Selector &RHS) const {
    return InfoPtr != RHS.InfoPtr;
  }
  void *getAsOpaquePtr() const {
    return reinterpret_cast<void*>(InfoPtr);
  }
  // Predicates to identify the selector type.
  bool isKeywordSelector() const { 
    return getIdentifierInfoFlag() != ZeroArg; 
  }
  bool isUnarySelector() const { 
    return getIdentifierInfoFlag() == ZeroArg;
  }
  unsigned getNumArgs() const;
  IdentifierInfo *getIdentifierInfoForSlot(unsigned argIndex) const;
  
  /// getName - Derive the full selector name (e.g. "foo:bar:") and return it.
  ///
  std::string getName() const;
  
  static Selector getEmptyMarker() {
    return Selector(uintptr_t(-1));
  }
  static Selector getTombstoneMarker() {
    return Selector(uintptr_t(-2));
  }
};

/// SelectorTable - This table allows us to fully hide how we implement
/// multi-keyword caching.
class SelectorTable {
  void *Impl;  // Actually a FoldingSet<MultiKeywordSelector>*
  SelectorTable(const SelectorTable&); // DISABLED: DO NOT IMPLEMENT
  void operator=(const SelectorTable&); // DISABLED: DO NOT IMPLEMENT
public:
  SelectorTable();
  ~SelectorTable();

  /// getSelector - This can create any sort of selector.  NumArgs indicates
  /// whether this is a no argument selector "foo", a single argument selector
  /// "foo:" or multi-argument "foo:bar:".
  Selector getSelector(unsigned NumArgs, IdentifierInfo **IIV);
  
  Selector getUnarySelector(IdentifierInfo *ID) {
    return Selector(ID, 1);
  }
  Selector getNullarySelector(IdentifierInfo *ID) {
    return Selector(ID, 0);
  }
};

}  // end namespace clang

namespace llvm {
template <>
struct DenseMapInfo<clang::Selector> {
  static inline clang::Selector getEmptyKey() {
    return clang::Selector::getEmptyMarker();
  }
  static inline clang::Selector getTombstoneKey() {
    return clang::Selector::getTombstoneMarker(); 
  }
  
  static unsigned getHashValue(clang::Selector S);
  
  static bool isEqual(clang::Selector LHS, clang::Selector RHS) {
    return LHS == RHS;
  }
  
  static bool isPod() { return true; }
};

}  // end namespace llvm

#endif
