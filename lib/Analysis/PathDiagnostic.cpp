//===--- PathDiagnostic.cpp - Path-Specific Diagnostic Handling -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the PathDiagnostic-related interfaces.
//
//===----------------------------------------------------------------------===//

#include "clang/Analysis/PathDiagnostic.h"
#include "llvm/ADT/SmallString.h"
#include <sstream>
using namespace clang;

static size_t GetNumCharsToLastNonPeriod(const char *s) {
  const char *start = s;
  const char *lastNonPeriod = 0;  

  for ( ; *s != '\0' ; ++s)
    if (*s != '.') lastNonPeriod = s;
  
  if (!lastNonPeriod)
    return 0;
  
  return (lastNonPeriod - start) + 1;
}

static inline size_t GetNumCharsToLastNonPeriod(const std::string &s) {
  return s.empty () ? 0 : GetNumCharsToLastNonPeriod(&s[0]);
}

PathDiagnosticPiece::PathDiagnosticPiece(FullSourceLoc pos,
                                         const std::string& s,
                                         Kind k, DisplayHint hint)
  : Pos(pos), str(s, 0, GetNumCharsToLastNonPeriod(s)), kind(k), Hint(hint) {}

PathDiagnosticPiece::PathDiagnosticPiece(FullSourceLoc pos,
                                         const char* s, Kind k,
                                         DisplayHint hint)
  : Pos(pos), str(s, GetNumCharsToLastNonPeriod(s)), kind(k), Hint(hint) {}

PathDiagnostic::~PathDiagnostic() {
  for (iterator I = begin(), E = end(); I != E; ++I) delete &*I;
}

void PathDiagnosticClient::HandleDiagnostic(Diagnostic::Level DiagLevel,
                                            const DiagnosticInfo &Info) {
  
  // Create a PathDiagnostic with a single piece.
  
  PathDiagnostic* D = new PathDiagnostic();
  
  const char *LevelStr;
  switch (DiagLevel) {
  default:
  case Diagnostic::Ignored: assert(0 && "Invalid diagnostic type");
  case Diagnostic::Note:    LevelStr = "note: "; break;
  case Diagnostic::Warning: LevelStr = "warning: "; break;
  case Diagnostic::Error:   LevelStr = "error: "; break;
  case Diagnostic::Fatal:   LevelStr = "fatal error: "; break;
  }

  llvm::SmallString<100> StrC;
  StrC += LevelStr;
  Info.FormatDiagnostic(StrC);
  
  PathDiagnosticPiece *P =
    new PathDiagnosticPiece(Info.getLocation(),
                            std::string(StrC.begin(), StrC.end()));
  
  for (unsigned i = 0, e = Info.getNumRanges(); i != e; ++i)
    P->addRange(Info.getRange(i));
  for (unsigned i = 0, e = Info.getNumCodeModificationHints(); i != e; ++i)
    P->addCodeModificationHint(Info.getCodeModificationHint(i));
  D->push_front(P);

  HandlePathDiagnostic(D);  
}
