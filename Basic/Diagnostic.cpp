//===--- Diagnostic.cpp - C Language Family Diagnostic Handling -----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file implements the Diagnostic-related interfaces.
//
//===----------------------------------------------------------------------===//

#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/SourceLocation.h"
#include <cassert>
#include <vector>
#include <map>
using namespace clang;

//===----------------------------------------------------------------------===//
// Builtin Diagnostic information
//===----------------------------------------------------------------------===//

/// Flag values for diagnostics.
enum {
  // Diagnostic classes.
  NOTE       = 0x01,
  WARNING    = 0x02,
  EXTENSION  = 0x03,
  ERROR      = 0x04,
  class_mask = 0x07
};

/// DiagnosticFlags - A set of flags, or'd together, that describe the
/// diagnostic.
static unsigned char DiagnosticFlags[] = {
#define DIAG(ENUM,FLAGS,DESC) FLAGS,
#include "clang/Basic/DiagnosticKinds.def"
  0
};

/// getDiagClass - Return the class field of the diagnostic.
///
static unsigned getBuiltinDiagClass(unsigned DiagID) {
  assert(DiagID < diag::NUM_BUILTIN_DIAGNOSTICS &&
         "Diagnostic ID out of range!");
  return DiagnosticFlags[DiagID] & class_mask;
}

/// DiagnosticText - An english message to print for the diagnostic.  These
/// should be localized.
static const char * const DiagnosticText[] = {
#define DIAG(ENUM,FLAGS,DESC) DESC,
#include "clang/Basic/DiagnosticKinds.def"
  0
};

//===----------------------------------------------------------------------===//
// Custom Diagnostic information
//===----------------------------------------------------------------------===//

namespace clang {
  namespace diag {
    class CustomDiagInfo {
      typedef std::pair<Diagnostic::Level, std::string> DiagDesc;
      std::vector<DiagDesc> DiagInfo;
      std::map<DiagDesc, unsigned> DiagIDs;
    public:
      
      /// getDescription - Return the description of the specified custom
      /// diagnostic.
      const char *getDescription(unsigned DiagID) const {
        assert(this && DiagID-diag::NUM_BUILTIN_DIAGNOSTICS < DiagInfo.size() &&
               "Invalid diagnosic ID");
        return DiagInfo[DiagID-diag::NUM_BUILTIN_DIAGNOSTICS].second.c_str();
      }
      
      /// getLevel - Return the level of the specified custom diagnostic.
      Diagnostic::Level getLevel(unsigned DiagID) const {
        assert(this && DiagID-diag::NUM_BUILTIN_DIAGNOSTICS < DiagInfo.size() &&
               "Invalid diagnosic ID");
        return DiagInfo[DiagID-diag::NUM_BUILTIN_DIAGNOSTICS].first;
      }
      
      unsigned getOrCreateDiagID(Diagnostic::Level L, const char *Message) {
        DiagDesc D(L, Message);
        // Check to see if it already exists.
        std::map<DiagDesc, unsigned>::iterator I = DiagIDs.lower_bound(D);
        if (I != DiagIDs.end() && I->first == D)
          return I->second;
        
        // If not, assign a new ID.
        unsigned ID = DiagInfo.size()+diag::NUM_BUILTIN_DIAGNOSTICS;
        DiagIDs.insert(std::make_pair(D, ID));
        DiagInfo.push_back(D);
        return ID;
      }
    };
    
  } // end diag namespace 
} // end clang namespace 


//===----------------------------------------------------------------------===//
// Common Diagnostic implementation
//===----------------------------------------------------------------------===//

Diagnostic::Diagnostic(DiagnosticClient &client) : Client(client) {
  WarningsAsErrors = false;
  WarnOnExtensions = false;
  ErrorOnExtensions = false;
  // Clear all mappings, setting them to MAP_DEFAULT.
  memset(DiagMappings, 0, sizeof(DiagMappings));
  
  ErrorOccurred = false;
  NumDiagnostics = 0;
  NumErrors = 0;
  CustomDiagInfo = 0;
}

Diagnostic::~Diagnostic() {
  delete CustomDiagInfo;
}

/// getCustomDiagID - Return an ID for a diagnostic with the specified message
/// and level.  If this is the first request for this diagnosic, it is
/// registered and created, otherwise the existing ID is returned.
unsigned Diagnostic::getCustomDiagID(Level L, const char *Message) {
  if (CustomDiagInfo == 0) 
    CustomDiagInfo = new diag::CustomDiagInfo();
  return CustomDiagInfo->getOrCreateDiagID(L, Message);
}


/// isBuiltinNoteWarningOrExtension - Return true if the unmapped diagnostic
/// level of the specified diagnostic ID is a Note, Warning, or Extension.
/// Note that this only works on builtin diagnostics, not custom ones.
bool Diagnostic::isBuiltinNoteWarningOrExtension(unsigned DiagID) {
  return DiagID < diag::NUM_BUILTIN_DIAGNOSTICS && 
         getBuiltinDiagClass(DiagID) < ERROR;
}


/// getDescription - Given a diagnostic ID, return a description of the
/// issue.
const char *Diagnostic::getDescription(unsigned DiagID) {
  if (DiagID < diag::NUM_BUILTIN_DIAGNOSTICS)
    return DiagnosticText[DiagID];
  else 
    return CustomDiagInfo->getDescription(DiagID);
}

/// getDiagnosticLevel - Based on the way the client configured the Diagnostic
/// object, classify the specified diagnostic ID into a Level, consumable by
/// the DiagnosticClient.
Diagnostic::Level Diagnostic::getDiagnosticLevel(unsigned DiagID) const {
  // Handle custom diagnostics, which cannot be mapped.
  if (DiagID >= diag::NUM_BUILTIN_DIAGNOSTICS)
    return CustomDiagInfo->getLevel(DiagID);
  
  unsigned DiagClass = getBuiltinDiagClass(DiagID);
  
  // Specific non-error diagnostics may be mapped to various levels from ignored
  // to error.
  if (DiagClass < ERROR) {
    switch (getDiagnosticMapping((diag::kind)DiagID)) {
    case diag::MAP_DEFAULT: break;
    case diag::MAP_IGNORE:  return Ignored;
    case diag::MAP_WARNING: DiagClass = WARNING; break;
    case diag::MAP_ERROR:   DiagClass = ERROR; break;
    }
  }
  
  // Map diagnostic classes based on command line argument settings.
  if (DiagClass == EXTENSION) {
    if (ErrorOnExtensions)
      DiagClass = ERROR;
    else if (WarnOnExtensions)
      DiagClass = WARNING;
    else
      return Ignored;
  }
  
  // If warnings are to be treated as errors, indicate this as such.
  if (DiagClass == WARNING && WarningsAsErrors)
    DiagClass = ERROR;
  
  switch (DiagClass) {
  default: assert(0 && "Unknown diagnostic class!");
  case NOTE:        return Diagnostic::Note;
  case WARNING:     return Diagnostic::Warning;
  case ERROR:       return Diagnostic::Error;
  }
}

/// Report - Issue the message to the client. If the client wants us to stop
/// compilation, return true, otherwise return false.  DiagID is a member of
/// the diag::kind enum.  
void Diagnostic::Report(FullSourceLoc Pos, unsigned DiagID,
                        const std::string *Strs, unsigned NumStrs,
                        const SourceRange *Ranges, unsigned NumRanges) {
  // Figure out the diagnostic level of this message.
  Diagnostic::Level DiagLevel = getDiagnosticLevel(DiagID);
  
  // If the client doesn't care about this message, don't issue it.
  if (DiagLevel == Diagnostic::Ignored)
    return;
  
  if (DiagLevel >= Diagnostic::Error) {
    ErrorOccurred = true;
    ++NumErrors;
  }

  // Are we going to ignore this diagnosic?
  if (Client.IgnoreDiagnostic(DiagLevel, Pos))
    return;

  // Finally, report it.
  Client.HandleDiagnostic(*this, DiagLevel, Pos, (diag::kind)DiagID,
                          Strs, NumStrs, Ranges, NumRanges);
  ++NumDiagnostics;
}

DiagnosticClient::~DiagnosticClient() {}
