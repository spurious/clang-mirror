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
  CurDiagID = ~0U;
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
void Diagnostic::ProcessDiag() {
  DiagnosticInfo Info(this);
  
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


/// ModifierIs - Return true if the specified modifier matches specified string.
template <std::size_t StrLen>
static bool ModifierIs(const char *Modifier, unsigned ModifierLen,
                       const char (&Str)[StrLen]) {
  return StrLen-1 == ModifierLen && !memcmp(Modifier, Str, StrLen-1);
}

/// HandleSelectModifier - Handle the integer 'select' modifier.  This is used
/// like this:  %select{foo|bar|baz}2.  This means that the integer argument
/// "%2" has a value from 0-2.  If the value is 0, the diagnostic prints 'foo'.
/// If the value is 1, it prints 'bar'.  If it has the value 2, it prints 'baz'.
/// This is very useful for certain classes of variant diagnostics.
static void HandleSelectModifier(unsigned ValNo,
                                 const char *Argument, unsigned ArgumentLen,
                                 llvm::SmallVectorImpl<char> &OutStr) {
  const char *ArgumentEnd = Argument+ArgumentLen;
  
  // Skip over 'ValNo' |'s.
  while (ValNo) {
    const char *NextVal = std::find(Argument, ArgumentEnd, '|');
    assert(NextVal != ArgumentEnd && "Value for integer select modifier was"
           " larger than the number of options in the diagnostic string!");
    Argument = NextVal+1;  // Skip this string.
    --ValNo;
  }
  
  // Get the end of the value.  This is either the } or the |.
  const char *EndPtr = std::find(Argument, ArgumentEnd, '|');
  // Add the value to the output string.
  OutStr.append(Argument, EndPtr);
}

/// HandleIntegerSModifier - Handle the integer 's' modifier.  This adds the
/// letter 's' to the string if the value is not 1.  This is used in cases like
/// this:  "you idiot, you have %4 parameter%s4!".
static void HandleIntegerSModifier(unsigned ValNo,
                                   llvm::SmallVectorImpl<char> &OutStr) {
  if (ValNo != 1)
    OutStr.push_back('s');
}


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
      continue;
    } else if (DiagStr[1] == '%') {
      OutStr.push_back('%');  // %% -> %.
      DiagStr += 2;
      continue;
    }
    
    // Skip the %.
    ++DiagStr;
    
    // This must be a placeholder for a diagnostic argument.  The format for a
    // placeholder is one of "%0", "%modifier0", or "%modifier{arguments}0".
    // The digit is a number from 0-9 indicating which argument this comes from.
    // The modifier is a string of digits from the set [-a-z]+, arguments is a
    // brace enclosed string.
    const char *Modifier = 0, *Argument = 0;
    unsigned ModifierLen = 0, ArgumentLen = 0;
    
    // Check to see if we have a modifier.  If so eat it.
    if (!isdigit(DiagStr[0])) {
      Modifier = DiagStr;
      while (DiagStr[0] == '-' ||
             (DiagStr[0] >= 'a' && DiagStr[0] <= 'z'))
        ++DiagStr;
      ModifierLen = DiagStr-Modifier;

      // If we have an argument, get it next.
      if (DiagStr[0] == '{') {
        ++DiagStr; // Skip {.
        Argument = DiagStr;
        
        for (; DiagStr[0] != '}'; ++DiagStr)
          assert(DiagStr[0] && "Mismatched {}'s in diagnostic string!");
        ArgumentLen = DiagStr-Argument;
        ++DiagStr;  // Skip }.
      }
    }
      
    assert(isdigit(*DiagStr) && "Invalid format for argument in diagnostic");
    unsigned StrNo = *DiagStr++ - '0';

    switch (getArgKind(StrNo)) {
    case Diagnostic::ak_std_string: {
      const std::string &S = getArgStdStr(StrNo);
      assert(ModifierLen == 0 && "No modifiers for strings yet");
      OutStr.append(S.begin(), S.end());
      break;
    }
    case Diagnostic::ak_c_string: {
      const char *S = getArgCStr(StrNo);
      assert(ModifierLen == 0 && "No modifiers for strings yet");
      OutStr.append(S, S + strlen(S));
      break;
    }
    case Diagnostic::ak_identifierinfo: {
      const IdentifierInfo *II = getArgIdentifier(StrNo);
      assert(ModifierLen == 0 && "No modifiers for strings yet");
      OutStr.append(II->getName(), II->getName() + II->getLength());
      break;
    }
    case Diagnostic::ak_sint: {
      int Val = getArgSInt(StrNo);
      
      if (ModifierIs(Modifier, ModifierLen, "select")) {
        HandleSelectModifier((unsigned)Val, Argument, ArgumentLen, OutStr);
      } else if (ModifierIs(Modifier, ModifierLen, "s")) {
        HandleIntegerSModifier(Val, OutStr);
      } else {
        assert(ModifierLen == 0 && "Unknown integer modifier");
        // FIXME: Optimize
        std::string S = llvm::itostr(Val);
        OutStr.append(S.begin(), S.end());
      }
      break;
    }
    case Diagnostic::ak_uint: {
      unsigned Val = getArgUInt(StrNo);
      
      if (ModifierIs(Modifier, ModifierLen, "select")) {
        HandleSelectModifier(Val, Argument, ArgumentLen, OutStr);
      } else if (ModifierIs(Modifier, ModifierLen, "s")) {
        HandleIntegerSModifier(Val, OutStr);
      } else {
        assert(ModifierLen == 0 && "Unknown integer modifier");
        
        // FIXME: Optimize
        std::string S = llvm::utostr_32(Val);
        OutStr.append(S.begin(), S.end());
        break;
      }
    }
    }
  }
}
