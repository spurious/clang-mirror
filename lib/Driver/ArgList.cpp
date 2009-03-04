//===--- ArgList.cpp - Argument List Management -------------------------*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "clang/Driver/ArgList.h"
#include "clang/Driver/Arg.h"
#include "clang/Driver/Option.h"

using namespace clang::driver;

ArgList::ArgList(const char **ArgBegin, const char **ArgEnd) {
  ArgStrings.append(ArgBegin, ArgEnd);
}

ArgList::~ArgList() {
  for (iterator it = begin(), ie = end(); it != ie; ++ie)
    delete *it;
}

void ArgList::append(Arg *A) {
  if (A->getOption().isUnsupported()) {
    assert(0 && "FIXME: unsupported unsupported.");
  }

  Args.push_back(A);
}
