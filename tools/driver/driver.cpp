//===-- driver.cpp - Clang GCC-Compatible Driver --------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This is the entry point to the clang driver; it is a thin wrapper
// for functionality in the Driver clang library.
//
//===----------------------------------------------------------------------===//

#include "clang/Driver/Compilation.h"
#include "clang/Driver/Driver.h"
#include "clang/Driver/Option.h"
#include "clang/Driver/Options.h"

#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/OwningPtr.h"
#include "llvm/Config/config.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/System/Host.h"
#include "llvm/System/Path.h"
#include "llvm/System/Signals.h"
using namespace clang;
using namespace clang::driver;

class DriverDiagnosticPrinter : public DiagnosticClient {
  std::string ProgName;
  llvm::raw_ostream &OS;

public:
  DriverDiagnosticPrinter(const std::string _ProgName, 
                          llvm::raw_ostream &_OS)
    : ProgName(_ProgName),
      OS(_OS) {}

  virtual void HandleDiagnostic(Diagnostic::Level DiagLevel,
                                const DiagnosticInfo &Info);
};

void DriverDiagnosticPrinter::HandleDiagnostic(Diagnostic::Level Level,
                                               const DiagnosticInfo &Info) {
  OS << ProgName << ": ";

  switch (Level) {
  case Diagnostic::Ignored: assert(0 && "Invalid diagnostic type");
  case Diagnostic::Note:    OS << "note: "; break;
  case Diagnostic::Warning: OS << "warning: "; break;
  case Diagnostic::Error:   OS << "error: "; break;
  case Diagnostic::Fatal:   OS << "fatal error: "; break;
  }
  
  llvm::SmallString<100> OutStr;
  Info.FormatDiagnostic(OutStr);
  OS.write(OutStr.begin(), OutStr.size());
  OS << '\n';
}

llvm::sys::Path GetExecutablePath(const char *Argv0) {
  // This just needs to be some symbol in the binary; C++ doesn't
  // allow taking the address of ::main however.
  void *P = (void*) (intptr_t) GetExecutablePath;
  return llvm::sys::Path::GetMainExecutable(Argv0, P);
}

int main(int argc, const char **argv) {
  llvm::sys::PrintStackTraceOnErrorSignal();
  llvm::PrettyStackTraceProgram X(argc, argv);

  llvm::sys::Path Path = GetExecutablePath(argv[0]);
  llvm::OwningPtr<DiagnosticClient> 
    DiagClient(new DriverDiagnosticPrinter(Path.getBasename(), llvm::errs()));

  Diagnostic Diags(DiagClient.get());

  llvm::OwningPtr<Driver> 
    TheDriver(new Driver(Path.getBasename().c_str(), Path.getDirname().c_str(),
                         llvm::sys::getHostTriple().c_str(),
                         "a.out", Diags));

  llvm::OwningPtr<Compilation> C;

  // Handle CCC_ADD_ARGS, a comma separated list of extra arguments.
  if (const char *Cur = ::getenv("CCC_ADD_ARGS")) {
    std::set<std::string> SavedStrings;
    std::vector<const char*> StringPointers;

    // FIXME: Driver shouldn't take extra initial argument.
    StringPointers.push_back(argv[0]);

    for (;;) {
      const char *Next = strchr(Cur, ',');
      
      if (Next) {
        const char *P = 
          SavedStrings.insert(std::string(Cur, Next)).first->c_str();
        StringPointers.push_back(P);
        Cur = Next + 1;
      } else {
        if (*Cur != '\0') {
          const char *P = 
            SavedStrings.insert(std::string(Cur)).first->c_str();
          StringPointers.push_back(P);
        }
        break;
      }
    }

    StringPointers.insert(StringPointers.end(), argv + 1, argv + argc);

    C.reset(TheDriver->BuildCompilation(StringPointers.size(), 
                                        &StringPointers[0]));
  } else
    C.reset(TheDriver->BuildCompilation(argc, argv));

  int Res = 0;
  if (C.get())
    Res = C->Execute();

  llvm::llvm_shutdown();

  return Res;
}

