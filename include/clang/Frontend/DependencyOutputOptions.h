//===--- DependencyOutputOptions.h ------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_FRONTEND_DEPENDENCYOUTPUTOPTIONS_H
#define LLVM_CLANG_FRONTEND_DEPENDENCYOUTPUTOPTIONS_H

#include <string>
#include <vector>

namespace clang {

/// DependencyOutputOptions - Options for controlling the compiler dependency
/// file generation.
class DependencyOutputOptions {
public:
  unsigned IncludeSystemHeaders : 1; ///< Include system header dependencies.
  unsigned UsePhonyTargets : 1;      ///< Include phony targets for each
                                     /// dependency, which can avoid some 'make'
                                     /// problems.

  /// The file to write dependency output to.
  std::string OutputFile;

  /// A list of names to use as the targets in the dependency file; this list
  /// must contain at least one entry.
  std::vector<std::string> Targets;

public:
  DependencyOutputOptions() {
    IncludeSystemHeaders = 0;
    UsePhonyTargets = 0;
  }
};

}  // end namespace clang

#endif
