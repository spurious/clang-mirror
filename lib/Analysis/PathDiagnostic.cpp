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
#include <sstream>

using namespace clang;
  
PathDiagnostic::~PathDiagnostic() {
  for (iterator I = begin(), E = end(); I != E; ++I) delete &*I;
}

void PathDiagnosticClient::HandleDiagnostic(Diagnostic &Diags, 
                                            Diagnostic::Level DiagLevel,
                                            FullSourceLoc Pos,
                                            diag::kind ID,
                                            const std::string *Strs,
                                            unsigned NumStrs,
                                            const SourceRange *Ranges, 
                                            unsigned NumRanges) {
  
  // Create a PathDiagnostic with a single piece.
  
  PathDiagnostic* D = new PathDiagnostic();
  
  const char *LevelStr;
  switch (DiagLevel) {
  default: assert(0 && "Unknown diagnostic type!");
  case Diagnostic::Note:    LevelStr = "note: "; break;
  case Diagnostic::Warning: LevelStr = "warning: "; break;
  case Diagnostic::Error:   LevelStr = "error: "; break;
  case Diagnostic::Fatal:   LevelStr = "fatal error: "; break;
  }

  std::string Msg = FormatDiagnostic(Diags, DiagLevel, ID, Strs, NumStrs);
  
  PathDiagnosticPiece* P = new PathDiagnosticPiece(Pos, LevelStr+Msg);
  
  while (NumRanges) {
    P->addRange(*Ranges);
    --NumRanges;
    ++Ranges;
  }
  
  D->push_front(P);

  HandlePathDiagnostic(D);  
}
