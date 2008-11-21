//===--- Diagnostic.h - C Language Family Diagnostic Handling ---*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the Diagnostic-related interfaces.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_DIAGNOSTIC_H
#define LLVM_CLANG_DIAGNOSTIC_H

#include "clang/Basic/SourceLocation.h"
#include <string>
#include <cassert>

namespace llvm {
  template <typename T> class SmallVectorImpl;
}

namespace clang {
  class DiagnosticClient;
  class SourceRange;
  class SourceManager;
  class DiagnosticInfo;
  class IdentifierInfo;
  
  // Import the diagnostic enums themselves.
  namespace diag {
    class CustomDiagInfo;
    
    /// diag::kind - All of the diagnostics that can be emitted by the frontend.
    enum kind {
#define DIAG(ENUM,FLAGS,DESC) ENUM,
#include "DiagnosticKinds.def"
      NUM_BUILTIN_DIAGNOSTICS
    };
    
    /// Enum values that allow the client to map NOTEs, WARNINGs, and EXTENSIONs
    /// to either MAP_IGNORE (nothing), MAP_WARNING (emit a warning), MAP_ERROR
    /// (emit as an error), or MAP_DEFAULT (handle the default way).
    enum Mapping {
      MAP_DEFAULT = 0,     //< Do not map this diagnostic.
      MAP_IGNORE  = 1,     //< Map this diagnostic to nothing, ignore it.
      MAP_WARNING = 2,     //< Map this diagnostic to a warning.
      MAP_ERROR   = 3      //< Map this diagnostic to an error.
    };
  }
  
/// Diagnostic - This concrete class is used by the front-end to report
/// problems and issues.  It massages the diagnostics (e.g. handling things like
/// "report warnings as errors" and passes them off to the DiagnosticClient for
/// reporting to the user.
class Diagnostic {
public:
  /// Level - The level of the diagnostic, after it has been through mapping.
  enum Level {
    Ignored, Note, Warning, Error, Fatal
  };
  
private:  
  bool IgnoreAllWarnings;     // Ignore all warnings: -w
  bool WarningsAsErrors;      // Treat warnings like errors: 
  bool WarnOnExtensions;      // Enables warnings for gcc extensions: -pedantic.
  bool ErrorOnExtensions;     // Error on extensions: -pedantic-errors.
  bool SuppressSystemWarnings;// Suppress warnings in system headers.
  DiagnosticClient *Client;

  /// DiagMappings - Mapping information for diagnostics.  Mapping info is
  /// packed into two bits per diagnostic.
  unsigned char DiagMappings[(diag::NUM_BUILTIN_DIAGNOSTICS+3)/4];
  
  /// ErrorOccurred - This is set to true when an error is emitted, and is
  /// sticky.
  bool ErrorOccurred;

  unsigned NumDiagnostics;    // Number of diagnostics reported
  unsigned NumErrors;         // Number of diagnostics that are errors

  /// CustomDiagInfo - Information for uniquing and looking up custom diags.
  diag::CustomDiagInfo *CustomDiagInfo;

public:
  explicit Diagnostic(DiagnosticClient *client = 0);
  ~Diagnostic();
  
  //===--------------------------------------------------------------------===//
  //  Diagnostic characterization methods, used by a client to customize how
  //
  
  DiagnosticClient *getClient() { return Client; };
  const DiagnosticClient *getClient() const { return Client; };
    
  void setClient(DiagnosticClient* client) { Client = client; }

  /// setIgnoreAllWarnings - When set to true, any unmapped warnings are
  /// ignored.  If this and WarningsAsErrors are both set, then this one wins.
  void setIgnoreAllWarnings(bool Val) { IgnoreAllWarnings = Val; }
  bool getIgnoreAllWarnings() const { return IgnoreAllWarnings; }
  
  /// setWarningsAsErrors - When set to true, any warnings reported are issued
  /// as errors.
  void setWarningsAsErrors(bool Val) { WarningsAsErrors = Val; }
  bool getWarningsAsErrors() const { return WarningsAsErrors; }
  
  /// setWarnOnExtensions - When set to true, issue warnings on GCC extensions,
  /// the equivalent of GCC's -pedantic.
  void setWarnOnExtensions(bool Val) { WarnOnExtensions = Val; }
  bool getWarnOnExtensions() const { return WarnOnExtensions; }
  
  /// setErrorOnExtensions - When set to true issue errors for GCC extensions
  /// instead of warnings.  This is the equivalent to GCC's -pedantic-errors.
  void setErrorOnExtensions(bool Val) { ErrorOnExtensions = Val; }
  bool getErrorOnExtensions() const { return ErrorOnExtensions; }

  /// setSuppressSystemWarnings - When set to true mask warnings that
  /// come from system headers.
  void setSuppressSystemWarnings(bool Val) { SuppressSystemWarnings = Val; }
  bool getSuppressSystemWarnings() const { return SuppressSystemWarnings; }

  /// setDiagnosticMapping - This allows the client to specify that certain
  /// warnings are ignored.  Only NOTEs, WARNINGs, and EXTENSIONs can be mapped.
  void setDiagnosticMapping(diag::kind Diag, diag::Mapping Map) {
    assert(Diag < diag::NUM_BUILTIN_DIAGNOSTICS &&
           "Can only map builtin diagnostics");
    assert(isBuiltinNoteWarningOrExtension(Diag) && "Cannot map errors!");
    unsigned char &Slot = DiagMappings[Diag/4];
    unsigned Bits = (Diag & 3)*2;
    Slot &= ~(3 << Bits);
    Slot |= Map << Bits;
  }

  /// getDiagnosticMapping - Return the mapping currently set for the specified
  /// diagnostic.
  diag::Mapping getDiagnosticMapping(diag::kind Diag) const {
    return (diag::Mapping)((DiagMappings[Diag/4] >> (Diag & 3)*2) & 3);
  }
  
  bool hasErrorOccurred() const { return ErrorOccurred; }

  unsigned getNumErrors() const { return NumErrors; }
  unsigned getNumDiagnostics() const { return NumDiagnostics; }
  
  /// getCustomDiagID - Return an ID for a diagnostic with the specified message
  /// and level.  If this is the first request for this diagnosic, it is
  /// registered and created, otherwise the existing ID is returned.
  unsigned getCustomDiagID(Level L, const char *Message);
  
  //===--------------------------------------------------------------------===//
  // Diagnostic classification and reporting interfaces.
  //

  /// getDescription - Given a diagnostic ID, return a description of the
  /// issue.
  const char *getDescription(unsigned DiagID) const;
  
  /// isBuiltinNoteWarningOrExtension - Return true if the unmapped diagnostic
  /// level of the specified diagnostic ID is a Note, Warning, or Extension.
  /// Note that this only works on builtin diagnostics, not custom ones.
  static bool isBuiltinNoteWarningOrExtension(unsigned DiagID);

  /// getDiagnosticLevel - Based on the way the client configured the Diagnostic
  /// object, classify the specified diagnostic ID into a Level, consumable by
  /// the DiagnosticClient.
  Level getDiagnosticLevel(unsigned DiagID) const;
  
  
  /// Report - Issue the message to the client.  DiagID is a member of the
  /// diag::kind enum.  This actually returns a new instance of DiagnosticInfo
  /// which emits the diagnostics (through ProcessDiag) when it is destroyed.
  inline DiagnosticInfo Report(FullSourceLoc Pos, unsigned DiagID);
  
private:
  // This is private state used by DiagnosticInfo.  We put it here instead of
  // in DiagnosticInfo in order to keep DiagnosticInfo a small light-weight
  // object.  This implementation choice means that we can only have one
  // diagnostic "in flight" at a time, but this seems to be a reasonable
  // tradeoff to keep these objects small.  Assertions verify that only one
  // diagnostic is in flight at a time.
  friend class DiagnosticInfo;

  /// CurDiagLoc - This is the location of the current diagnostic that is in
  /// flight.
  FullSourceLoc CurDiagLoc;
  
  /// CurDiagID - This is the ID of the current diagnostic that is in flight.
  unsigned CurDiagID;

  enum {
    /// MaxArguments - The maximum number of arguments we can hold. We currently
    /// only support up to 10 arguments (%0-%9).  A single diagnostic with more
    /// than that almost certainly has to be simplified anyway.
    MaxArguments = 10
  };
  
  /// NumDiagArgs - This is set to -1 when no diag is in flight.  Otherwise it
  /// is the number of entries in Arguments.
  signed char NumDiagArgs;
  /// NumRanges - This is the number of ranges in the DiagRanges array.
  unsigned char NumDiagRanges;
  
  /// DiagArgumentsKind - This is an array of ArgumentKind::ArgumentKind enum
  /// values, with one for each argument.  This specifies whether the argument
  /// is in DiagArgumentsStr or in DiagArguments.
  unsigned char DiagArgumentsKind[MaxArguments];
  
  /// DiagArgumentsStr - This holds the values of each string argument for the
  /// current diagnostic.  This value is only used when the corresponding
  /// ArgumentKind is ak_std_string.
  std::string DiagArgumentsStr[MaxArguments];

  /// DiagArgumentsVal - The values for the various substitution positions. This
  /// is used when the argument is not an std::string.  The specific value is
  /// mangled into an intptr_t and the intepretation depends on exactly what
  /// sort of argument kind it is.
  intptr_t DiagArgumentsVal[MaxArguments];
  
  /// DiagRanges - The list of ranges added to this diagnostic.  It currently
  /// only support 10 ranges, could easily be extended if needed.
  const SourceRange *DiagRanges[10];
  
  /// ProcessDiag - This is the method used to report a diagnostic that is
  /// finally fully formed.
  void ProcessDiag(const DiagnosticInfo &Info);
};
  
/// DiagnosticInfo - This is a little helper class used to produce diagnostics.
/// This is constructed with an ID and location, and then has some number of
/// arguments (for %0 substitution) and SourceRanges added to it with the
/// overloaded operator<<.  Once it is destroyed, it emits the diagnostic with
/// the accumulated information.
///
/// Note that many of these will be created as temporary objects (many call
/// sites), so we want them to be small to reduce stack space usage etc.  For
/// this reason, we stick state in the Diagnostic class, see the comment there
/// for more info.
class DiagnosticInfo {
  mutable Diagnostic *DiagObj;
  void operator=(const DiagnosticInfo&); // DO NOT IMPLEMENT
public:
  enum ArgumentKind {
    ak_std_string,     // std::string
    ak_c_string,       // const char *
    ak_sint,           // int
    ak_uint,           // unsigned
    ak_identifierinfo  // IdentifierInfo
  };
  
  
  DiagnosticInfo(Diagnostic *diagObj, FullSourceLoc Loc, unsigned DiagID) :
    DiagObj(diagObj) {
    if (DiagObj == 0) return;
    assert(DiagObj->NumDiagArgs == -1 &&
           "Multiple diagnostics in flight at once!");
    DiagObj->NumDiagArgs = DiagObj->NumDiagRanges = 0;
    DiagObj->CurDiagLoc = Loc;
    DiagObj->CurDiagID = DiagID;
  }
  
  /// Copy constructor.  When copied, this "takes" the diagnostic info from the
  /// input and neuters it.
  DiagnosticInfo(const DiagnosticInfo &D) {
    DiagObj = D.DiagObj;
    D.DiagObj = 0;
  }
  
  /// Destructor - The dtor emits the diagnostic.
  ~DiagnosticInfo() {
    // If DiagObj is null, then its soul was stolen by the copy ctor.
    if (!DiagObj) return;
    
    DiagObj->ProcessDiag(*this);

    // This diagnostic is no longer in flight.
    DiagObj->NumDiagArgs = -1;
  }
  
  const Diagnostic *getDiags() const { return DiagObj; }
  unsigned getID() const { return DiagObj->CurDiagID; }
  const FullSourceLoc &getLocation() const { return DiagObj->CurDiagLoc; }
  
  /// Operator bool: conversion of DiagnosticInfo to bool always returns true.
  /// This allows is to be used in boolean error contexts like:
  /// return Diag(...);
  operator bool() const { return true; }

  unsigned getNumArgs() const { return DiagObj->NumDiagArgs; }
  
  
  /// getArgKind - Return the kind of the specified index.  Based on the kind
  /// of argument, the accessors below can be used to get the value.
  ArgumentKind getArgKind(unsigned Idx) const {
    assert((signed char)Idx < DiagObj->NumDiagArgs &&
           "Argument index out of range!");
    return (ArgumentKind)DiagObj->DiagArgumentsKind[Idx];
  }
  
  /// getArgStdStr - Return the provided argument string specified by Idx.
  const std::string &getArgStdStr(unsigned Idx) const {
    assert(getArgKind(Idx) == ak_std_string && "invalid argument accessor!");
    return DiagObj->DiagArgumentsStr[Idx];
  }

  /// getArgCStr - Return the specified C string argument.
  const char *getArgCStr(unsigned Idx) const {
    assert(getArgKind(Idx) == ak_c_string && "invalid argument accessor!");
    return reinterpret_cast<const char*>(DiagObj->DiagArgumentsVal[Idx]);
  }
  
  /// getArgSInt - Return the specified signed integer argument.
  int getArgSInt(unsigned Idx) const {
    assert(getArgKind(Idx) == ak_sint && "invalid argument accessor!");
    return (int)DiagObj->DiagArgumentsVal[Idx];
  }

  /// getArgUInt - Return the specified unsigned integer argument.
  unsigned getArgUInt(unsigned Idx) const {
    assert(getArgKind(Idx) == ak_uint && "invalid argument accessor!");
    return (unsigned)DiagObj->DiagArgumentsVal[Idx];
  }
  
  /// getArgIdentifier - Return the specified IdentifierInfo argument.
  const IdentifierInfo *getArgIdentifier(unsigned Idx) const {
    assert(getArgKind(Idx) == ak_identifierinfo &&"invalid argument accessor!");
    return reinterpret_cast<const IdentifierInfo*>(
                                                DiagObj->DiagArgumentsVal[Idx]);
  }
  
  /// getNumRanges - Return the number of source ranges associated with this
  /// diagnostic.
  unsigned getNumRanges() const {
    return DiagObj->NumDiagRanges;
  }
  
  const SourceRange &getRange(unsigned Idx) const {
    assert(Idx < DiagObj->NumDiagRanges && "Invalid diagnostic range index!");
    return *DiagObj->DiagRanges[Idx];
  }
  
  void AddString(const std::string &S) const {
    assert((unsigned)DiagObj->NumDiagArgs < Diagnostic::MaxArguments &&
           "Too many arguments to diagnostic!");
    DiagObj->DiagArgumentsKind[DiagObj->NumDiagArgs] = ak_std_string;
    DiagObj->DiagArgumentsStr[DiagObj->NumDiagArgs++] = S;
  }
  
  void AddTaggedVal(intptr_t V, ArgumentKind Kind) const {
    assert((unsigned)DiagObj->NumDiagArgs < Diagnostic::MaxArguments &&
           "Too many arguments to diagnostic!");
    DiagObj->DiagArgumentsKind[DiagObj->NumDiagArgs] = Kind;
    DiagObj->DiagArgumentsVal[DiagObj->NumDiagArgs++] = V;
  }
  
  void AddSourceRange(const SourceRange &R) const {
    assert((unsigned)DiagObj->NumDiagArgs < 
           sizeof(DiagObj->DiagRanges)/sizeof(DiagObj->DiagRanges[0]) &&
           "Too many arguments to diagnostic!");
    DiagObj->DiagRanges[DiagObj->NumDiagRanges++] = &R;
  }    
  
  /// FormatDiagnostic - Format this diagnostic into a string, substituting the
  /// formal arguments into the %0 slots.  The result is appended onto the Str
  /// array.
  void FormatDiagnostic(llvm::SmallVectorImpl<char> &OutStr) const;
};

inline const DiagnosticInfo &operator<<(const DiagnosticInfo &DI,
                                        const std::string &S) {
  DI.AddString(S);
  return DI;
}

inline const DiagnosticInfo &operator<<(const DiagnosticInfo &DI,
                                        const char *Str) {
  DI.AddTaggedVal(reinterpret_cast<intptr_t>(Str), DiagnosticInfo::ak_c_string);
  return DI;
}

inline const DiagnosticInfo &operator<<(const DiagnosticInfo &DI, int I) {
  DI.AddTaggedVal(I, DiagnosticInfo::ak_sint);
  return DI;
}

inline const DiagnosticInfo &operator<<(const DiagnosticInfo &DI, unsigned I) {
  DI.AddTaggedVal(I, DiagnosticInfo::ak_uint);
  return DI;
}

inline const DiagnosticInfo &operator<<(const DiagnosticInfo &DI,
                                        const IdentifierInfo *II){
  DI.AddTaggedVal(reinterpret_cast<intptr_t>(II),
                  DiagnosticInfo::ak_identifierinfo);
  return DI;
}
  
inline const DiagnosticInfo &operator<<(const DiagnosticInfo &DI,
                                        const SourceRange &R) {
  DI.AddSourceRange(R);
  return DI;
}
  

/// Report - Issue the message to the client.  DiagID is a member of the
/// diag::kind enum.  This actually returns a new instance of DiagnosticInfo
/// which emits the diagnostics (through ProcessDiag) when it is destroyed.
inline DiagnosticInfo Diagnostic::Report(FullSourceLoc Pos, unsigned DiagID) {
  return DiagnosticInfo(this, Pos, DiagID);
}
  

/// DiagnosticClient - This is an abstract interface implemented by clients of
/// the front-end, which formats and prints fully processed diagnostics.
class DiagnosticClient {
public:
  virtual ~DiagnosticClient();

  /// HandleDiagnostic - Handle this diagnostic, reporting it to the user or
  /// capturing it to a log as needed.
  virtual void HandleDiagnostic(Diagnostic::Level DiagLevel,
                                const DiagnosticInfo &Info) = 0;
};

}  // end namespace clang

#endif
