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
  for (iterator it = begin(), ie = end(); it != ie; ++it)
    delete *it;
}

void ArgList::append(Arg *A) {
  if (A->getOption().isUnsupported()) {
    assert(0 && "FIXME: unsupported unsupported.");
  }

  Args.push_back(A);
}

Arg *ArgList::getLastArg(options::ID Id, bool Claim) const {
  // FIXME: Make search efficient?

  // FIXME: This needs to not require loading of the option.
  for (const_iterator it = begin(), ie = end(); it != ie; ++it) {
    if ((*it)->getOption().matches(Id)) {
      if (Claim) (*it)->claim();
      return *it;
    }
  }
  
  return 0;
}

Arg *ArgList::getLastArg(options::ID Id0, options::ID Id1, bool Claim) const {
  Arg *Res, *A0 = getLastArg(Id0, false), *A1 = getLastArg(Id1, false);
  
  if (A0 && A1)
    Res = A0->getIndex() > A1->getIndex() ? A0 : A1;
  else
    Res = A0 ? A0 : A1;

  if (Claim && Res)
    Res->claim();

  return Res;
}

bool ArgList::hasFlag(options::ID Pos, options::ID Neg, bool Default) const {
  Arg *PosA = getLastArg(Pos);
  Arg *NegA = getLastArg(Pos);
  if (PosA && NegA)
    return NegA->getIndex() < PosA->getIndex();
  if (PosA) return true;
  if (NegA) return false;
  return Default;
}

unsigned ArgList::MakeIndex(const char *String0) const {
  unsigned Index = ArgStrings.size();

  // Tuck away so we have a reliable const char *.
  SynthesizedStrings.push_back(String0);
  ArgStrings.push_back(SynthesizedStrings.back().c_str());

  return Index;
}

unsigned ArgList::MakeIndex(const char *String0, const char *String1) const {
  unsigned Index0 = MakeIndex(String0);
  unsigned Index1 = MakeIndex(String1);
  assert(Index0 + 1 == Index1 && "Unexpected non-consecutive indices!");
  (void) Index1;
  return Index0;
}

const char *ArgList::MakeArgString(const char *Str) const {
  return getArgString(MakeIndex(Str));
}

Arg *ArgList::MakeFlagArg(const Option *Opt) const {
  return new FlagArg(Opt, MakeIndex(Opt->getName()));
}

Arg *ArgList::MakePositionalArg(const Option *Opt, const char *Value) const {
  return new PositionalArg(Opt, MakeIndex(Value));
}

Arg *ArgList::MakeSeparateArg(const Option *Opt, const char *Value) const {
  return new SeparateArg(Opt, MakeIndex(Opt->getName(), Value), 1);
}

Arg *ArgList::MakeJoinedArg(const Option *Opt, const char *Value) const {
  std::string Joined(Opt->getName());
  Joined += Value;
  return new JoinedArg(Opt, MakeIndex(Joined.c_str()));
}

void ArgList::AddLastArg(ArgStringList &Output, options::ID Id) const {
  if (Arg *A = getLastArg(Id)) {
    A->claim();
    A->render(*this, Output);
  }
}

void ArgList::AddAllArgs(ArgStringList &Output, options::ID Id0) const {
  // FIXME: Make fast.
  for (const_iterator it = begin(), ie = end(); it != ie; ++it) {
    const Arg *A = *it;
    if (A->getOption().matches(Id0)) {
      A->claim();
      A->render(*this, Output);
    }
  }
}

void ArgList::AddAllArgs(ArgStringList &Output, options::ID Id0, 
                         options::ID Id1) const {
  // FIXME: Make fast.
  for (const_iterator it = begin(), ie = end(); it != ie; ++it) {
    const Arg *A = *it;
    if (A->getOption().matches(Id0) || A->getOption().matches(Id1)) {
      A->claim();
      A->render(*this, Output);
    }
  }
}

void ArgList::AddAllArgs(ArgStringList &Output, options::ID Id0, 
                         options::ID Id1, options::ID Id2) const {
  // FIXME: Make fast.
  for (const_iterator it = begin(), ie = end(); it != ie; ++it) {
    const Arg *A = *it;
    if (A->getOption().matches(Id0) || A->getOption().matches(Id1) ||
        A->getOption().matches(Id2)) {
      A->claim();
      A->render(*this, Output);
    }
  }
}

void ArgList::AddAllArgValues(ArgStringList &Output, options::ID Id0) const {
  // FIXME: Make fast.
  for (const_iterator it = begin(), ie = end(); it != ie; ++it) {
    const Arg *A = *it;
    if (A->getOption().matches(Id0)) {
      A->claim();
      for (unsigned i = 0, e = A->getNumValues(); i != e; ++i)
        Output.push_back(A->getValue(*this, i));
    }
  }
}

void ArgList::AddAllArgValues(ArgStringList &Output, options::ID Id0, 
                              options::ID Id1) const {
  // FIXME: Make fast.
  for (const_iterator it = begin(), ie = end(); it != ie; ++it) {
    const Arg *A = *it;
    if (A->getOption().matches(Id0) || A->getOption().matches(Id1)) {
      A->claim();
      for (unsigned i = 0, e = A->getNumValues(); i != e; ++i)
        Output.push_back(A->getValue(*this, i));
    }
  }
}
