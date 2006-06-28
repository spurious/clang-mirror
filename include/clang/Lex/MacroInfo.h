//===--- MacroInfo.h - Information about #defined identifiers ---*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file was developed by Chris Lattner and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the MacroInfo interface.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_MACROINFO_H
#define LLVM_CLANG_MACROINFO_H

#include "clang/Lex/Lexer.h"
#include <vector>

namespace llvm {
namespace clang {
    
/// MacroInfo - Each identifier that is #define'd has an instance of this class
/// associated with it, used to implement macro expansion.
class MacroInfo {
  /// Location - This is the place the macro is defined.
  SourceLocation Location;

  // TODO: Parameter list
  // TODO: # parameters
  
  /// ReplacementTokens - This is the list of tokens that the macro is defined
  /// to.
  std::vector<LexerToken> ReplacementTokens;
  
  /// isDisabled - True if we have started an expansion of this macro already.
  /// This disbles recursive expansion, which would be quite bad for things like
  /// #define A A.
  bool IsDisabled : 1;
  
  /// isBuiltinMacro - True if this is a builtin macro, such as __LINE__, and if
  /// it has not yet been redefined or undefined.
  bool IsBuiltinMacro : 1;
public:
  MacroInfo(SourceLocation DefLoc) : Location(DefLoc) {
    IsDisabled = false;
    IsBuiltinMacro = false;
  }
  
  /// setIsBuiltinMacro - Set or clear the isBuiltinMacro flag.
  ///
  void setIsBuiltinMacro(bool Val = true) {
    IsBuiltinMacro = Val;
  }
  
  /// isBuiltinMacro - Return true if this macro is a builtin macro, such as
  /// __LINE__, which requires processing before expansion.
  bool isBuiltinMacro() const { return IsBuiltinMacro; }

  /// getNumTokens - Return the number of tokens that this macro expands to.
  ///
  unsigned getNumTokens() const {
    return ReplacementTokens.size();
  }

  const LexerToken &getReplacementToken(unsigned Tok) const {
    assert(Tok < ReplacementTokens.size() && "Invalid token #");
    return ReplacementTokens[Tok];
  }

  /// AddTokenToBody - Add the specified token to the replacement text for the
  /// macro.
  void AddTokenToBody(const LexerToken &Tok) {
    ReplacementTokens.push_back(Tok);
  }
  
  /// isEnabled - Return true if this macro is enabled: in other words, that we
  /// are not currently in an expansion of this macro.
  bool isEnabled() const { return !IsDisabled; }
  
  void EnableMacro() {
    assert(IsDisabled && "Cannot enable an already-enabled macro!");
    IsDisabled = false;
  }

  void DisableMacro() {
    assert(!IsDisabled && "Cannot disable an already-disabled macro!");
    IsDisabled = true;
  }
};
    
}  // end namespace llvm
}  // end namespace clang

#endif
