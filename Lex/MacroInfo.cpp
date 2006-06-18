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
#include <iostream>
using namespace llvm;
using namespace clang;

/// dump - Print the macro to stderr, used for debugging.
///
void MacroInfo::dump() const {
  std::cerr << "MACRO: ";
  for (unsigned i = 0, e = ReplacementTokens.size(); i != e; ++i) {
    ReplacementTokens[i].dump();
    std::cerr << "  ";
  }
  std::cerr << "\n";
}
