//===--- Module.h - Describe a module ---------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the Module class, which describes a module in the source
// code.
//
//===----------------------------------------------------------------------===//
#include "clang/Basic/Module.h"
#include "clang/Basic/FileManager.h"
#include "llvm/Support/raw_ostream.h"
using namespace clang;

Module::~Module() {
  for (llvm::StringMap<Module *>::iterator I = SubModules.begin(), 
                                        IEnd = SubModules.end();
       I != IEnd; ++I) {
    delete I->getValue();
  }
  
}

std::string Module::getFullModuleName() const {
  llvm::SmallVector<StringRef, 2> Names;
  
  // Build up the set of module names (from innermost to outermost).
  for (const Module *M = this; M; M = M->Parent)
    Names.push_back(M->Name);
  
  std::string Result;
  for (llvm::SmallVector<StringRef, 2>::reverse_iterator I = Names.rbegin(),
                                                      IEnd = Names.rend(); 
       I != IEnd; ++I) {
    if (!Result.empty())
      Result += '.';
    
    Result += *I;
  }
  
  return Result;
}

StringRef Module::getTopLevelModuleName() const {
  const Module *Top = this;
  while (Top->Parent)
    Top = Top->Parent;
  
  return Top->Name;
}

void Module::print(llvm::raw_ostream &OS, unsigned Indent) const {
  OS.indent(Indent);
  if (IsFramework)
    OS << "framework ";
  if (IsExplicit)
    OS << "explicit ";
  OS << "module " << Name << " {\n";
  
  if (UmbrellaHeader) {
    OS.indent(Indent + 2);
    OS << "umbrella \"";
    OS.write_escaped(UmbrellaHeader->getName());
    OS << "\"\n";
  }
  
  for (unsigned I = 0, N = Headers.size(); I != N; ++I) {
    OS.indent(Indent + 2);
    OS << "header \"";
    OS.write_escaped(Headers[I]->getName());
    OS << "\"\n";
  }
  
  for (llvm::StringMap<Module *>::const_iterator MI = SubModules.begin(), 
       MIEnd = SubModules.end();
       MI != MIEnd; ++MI)
    MI->getValue()->print(OS, Indent + 2);
  
  OS.indent(Indent);
  OS << "}\n";
}

void Module::dump() const {
  print(llvm::errs());
}


