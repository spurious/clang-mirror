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
#include "clang/Basic/IdentifierTable.h"
#include "clang/Basic/SourceLocation.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include <vector>
#include <map>
#include <cstring>
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
  EXTWARN    = 0x04,
  ERROR      = 0x05,
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
      
      unsigned getOrCreateDiagID(Diagnostic::Level L, const char *Message,
                                 Diagnostic &Diags) {
        DiagDesc D(L, Message);
        // Check to see if it already exists.
        std::map<DiagDesc, unsigned>::iterator I = DiagIDs.lower_bound(D);
        if (I != DiagIDs.end() && I->first == D)
          return I->second;
        
        // If not, assign a new ID.
        unsigned ID = DiagInfo.size()+diag::NUM_BUILTIN_DIAGNOSTICS;
        DiagIDs.insert(std::make_pair(D, ID));
        DiagInfo.push_back(D);

        // If this is a warning, and all warnings are supposed to map to errors,
        // insert the mapping now.
        if (L == Diagnostic::Warning && Diags.getWarningsAsErrors())
          Diags.setDiagnosticMapping((diag::kind)ID, diag::MAP_ERROR);
        return ID;
      }
    };
    
  } // end diag namespace 
} // end clang namespace 


//===----------------------------------------------------------------------===//
// Common Diagnostic implementation
//===----------------------------------------------------------------------===//

Diagnostic::Diagnostic(DiagnosticClient *client) : Client(client) {
  IgnoreAllWarnings = false;
  WarningsAsErrors = false;
  WarnOnExtensions = false;
  ErrorOnExtensions = false;
  SuppressSystemWarnings = false;
  // Clear all mappings, setting them to MAP_DEFAULT.
  memset(DiagMappings, 0, sizeof(DiagMappings));
  
  ErrorOccurred = false;
  NumDiagnostics = 0;
  NumErrors = 0;
  CustomDiagInfo = 0;
  NumDiagArgs = -1;
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
  return CustomDiagInfo->getOrCreateDiagID(L, Message, *this);
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
const char *Diagnostic::getDescription(unsigned DiagID) const {
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
    case diag::MAP_IGNORE:  return Diagnostic::Ignored;
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
  } else if (DiagClass == EXTWARN) {
    DiagClass = ErrorOnExtensions ? ERROR : WARNING;
  }
  
  // If warnings are globally mapped to ignore or error, do it.
  if (DiagClass == WARNING) {
    if (IgnoreAllWarnings)
      return Diagnostic::Ignored;
    if (WarningsAsErrors)
      DiagClass = ERROR;
  }
  
  switch (DiagClass) {
  default: assert(0 && "Unknown diagnostic class!");
  case NOTE:        return Diagnostic::Note;
  case WARNING:     return Diagnostic::Warning;
  case ERROR:       return Diagnostic::Error;
  }
}

/// ProcessDiag - This is the method used to report a diagnostic that is
/// finally fully formed.
void Diagnostic::ProcessDiag(const DiagnosticInfo &Info) {
  // Figure out the diagnostic level of this message.
  Diagnostic::Level DiagLevel = getDiagnosticLevel(Info.getID());
  
  // If the client doesn't care about this message, don't issue it.
  if (DiagLevel == Diagnostic::Ignored)
    return;

  // If this is not an error and we are in a system header, ignore it.  We
  // have to check on the original Diag ID here, because we also want to
  // ignore extensions and warnings in -Werror and -pedantic-errors modes,
  // which *map* warnings/extensions to errors.
  if (SuppressSystemWarnings &&
      Info.getID() < diag::NUM_BUILTIN_DIAGNOSTICS &&
      getBuiltinDiagClass(Info.getID()) != ERROR &&
      Info.getLocation().isValid() &&
      Info.getLocation().getPhysicalLoc().isInSystemHeader())
    return;
  
  if (DiagLevel >= Diagnostic::Error) {
    ErrorOccurred = true;

    ++NumErrors;
  }

  // Finally, report it.
  Client->HandleDiagnostic(DiagLevel, Info);
  ++NumDiagnostics;
}


DiagnosticClient::~DiagnosticClient() {}


/// FormatDiagnostic - Format this diagnostic into a string, substituting the
/// formal arguments into the %0 slots.  The result is appended onto the Str
/// array.
void DiagnosticInfo::
FormatDiagnostic(llvm::SmallVectorImpl<char> &OutStr) const {
  const char *DiagStr = getDiags()->getDescription(getID());
  const char *DiagEnd = DiagStr+strlen(DiagStr);
  
  while (DiagStr != DiagEnd) {
    if (DiagStr[0] != '%') {
      // Append non-%0 substrings to Str if we have one.
      const char *StrEnd = std::find(DiagStr, DiagEnd, '%');
      OutStr.append(DiagStr, StrEnd);
      DiagStr = StrEnd;
    } else if (DiagStr[1] == '%') {
      OutStr.push_back('%');  // %% -> %.
      DiagStr += 2;
    } else {
      assert(isdigit(DiagStr[1]) && "Must escape % with %%");
      unsigned StrNo = DiagStr[1] - '0';

      switch (getArgKind(StrNo)) {
      case DiagnosticInfo::ak_std_string: {
        const std::string &S = getArgStdStr(StrNo);
        OutStr.append(S.begin(), S.end());
        break;
      }
      case DiagnosticInfo::ak_c_string: {
        const char *S = getArgCStr(StrNo);
        OutStr.append(S, S + strlen(S));
        break;
      }
      case DiagnosticInfo::ak_sint: {
        // FIXME: Optimize
        std::string S = llvm::itostr(getArgSInt(StrNo));
        OutStr.append(S.begin(), S.end());
        break;
      }
      case DiagnosticInfo::ak_uint: {
        // FIXME: Optimize
        std::string S = llvm::utostr_32(getArgUInt(StrNo));
        OutStr.append(S.begin(), S.end());
        break;
      }
      case DiagnosticInfo::ak_identifierinfo: {
        const IdentifierInfo *II = getArgIdentifier(StrNo);
        OutStr.append(II->getName(), II->getName() + II->getLength());
        break;
      }
      }
      DiagStr += 2;
    }
  }
}
