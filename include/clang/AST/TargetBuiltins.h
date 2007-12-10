//===--- TargetBuiltins.h - Target specific builtin IDs -------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file was developed by Anders Carlsson and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_TARGET_BUILTINS_H
#define LLVM_CLANG_AST_TARGET_BUILTINS_H

#include "clang/AST/Builtins.h"

namespace clang {

/// X86 builtins
namespace X86 {
    enum {
        LastTIBuiltin = clang::Builtin::FirstTSBuiltin-1,
#define BUILTIN(ID, TYPE, ATTRS) BI##ID,
#include "X86Builtins.def"
        LastTSBuiltin
    };
}

/// PPC builtins
namespace PPC {
    enum {
        LastTIBuiltin = clang::Builtin::FirstTSBuiltin-1,
#define BUILTIN(ID, TYPE, ATTRS) BI##ID,
#include "PPCBuiltins.def"
        LastTSBuiltin
    };
}

}

#endif
