//===--- Driver.h - Clang GCC Compatible Driver -----------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef CLANG_DRIVER_DRIVER_H_
#define CLANG_DRIVER_DRIVER_H_

#include "clang/Basic/Diagnostic.h"

#include "clang/Driver/Phases.h"
#include "clang/Driver/Util.h"

#include "llvm/System/Path.h" // FIXME: Kill when CompilationInfo
                              // lands.
#include <list>
#include <set>
#include <string>

namespace clang {
namespace driver {
  class Action;
  class ArgList;
  class Compilation;
  class HostInfo;
  class OptTable;
  class ToolChain;

/// Driver - Encapsulate logic for constructing compilation processes
/// from a set of gcc-driver-like command line arguments.
class Driver {
  OptTable *Opts;

  Diagnostic &Diags;

  // Diag - Forwarding function for diagnostics.
  DiagnosticBuilder Diag(unsigned DiagID) const {
    return Diags.Report(FullSourceLoc(), DiagID);
  }

  // FIXME: Privatize once interface is stable.
public:
  /// The name the driver was invoked as.
  std::string Name;
  
  /// The path the driver executable was in, as invoked from the
  /// command line.
  std::string Dir;
  
  /// Default host triple.
  std::string DefaultHostTriple;

  /// Host information for the platform the driver is running as. This
  /// will generally be the actual host platform, but not always.
  HostInfo *Host;

  /// The default tool chain for this host.
  // FIXME: This shouldn't be here; this should be in a
  // CompilationInfo structure.
  ToolChain *DefaultToolChain;

  /// Information about the host which can be overriden by the user.
  std::string HostBits, HostMachine, HostSystem, HostRelease;

  /// Whether the driver should follow g++ like behavior.
  bool CCCIsCXX : 1;
  
  /// Echo commands while executing (in -v style).
  bool CCCEcho : 1;

  /// Don't use clang for any tasks.
  bool CCCNoClang : 1;

  /// Don't use clang for handling C++ and Objective-C++ inputs.
  bool CCCNoClangCXX : 1;

  /// Don't use clang as a preprocessor (clang's preprocessor will
  /// still be used where an integrated CPP would).
  bool CCCNoClangCPP : 1;

  /// Only use clang for the given architectures. Only used when
  /// non-empty.
  std::set<std::string> CCCClangArchs;

  /// Certain options suppress the 'no input files' warning.
  bool SuppressMissingInputWarning : 1;
  
  std::list<std::string> TempFiles;
  std::list<std::string> ResultFiles;

public:
  Driver(const char *_Name, const char *_Dir,
         const char *_DefaultHostTriple,
         Diagnostic &_Diags);
  ~Driver();

  /// @name Accessors
  /// @{

  const OptTable &getOpts() const { return *Opts; }

  /// @}
  /// @name Primary Functionality
  /// @{

  /// BuildCompilation - Construct a compilation object for a command
  /// line argument vector.
  ///
  /// \return A compilation, or 0 if none was built for the given
  /// argument vector. A null return value does not necessarily
  /// indicate an error condition, the diagnostics should be queried
  /// to determine if an error occurred.
  Compilation *BuildCompilation(int argc, const char **argv);

  /// @name Driver Steps
  /// @{

  /// ParseArgStrings - Parse the given list of strings into an
  /// ArgList.
  ArgList *ParseArgStrings(const char **ArgBegin, const char **ArgEnd);

  /// BuildActions - Construct the list of actions to perform for the
  /// given arguments, which are only done for a single architecture.
  ///
  /// \param Args - The input arguments.
  /// \param Actions - The list to store the resulting actions onto.
  void BuildActions(ArgList &Args, ActionList &Actions) const;

  /// BuildUniversalActions - Construct the list of actions to perform
  /// for the given arguments, which may require a universal build.
  ///
  /// \param Args - The input arguments.
  /// \param Actions - The list to store the resulting actions onto.
  void BuildUniversalActions(ArgList &Args, ActionList &Actions) const;

  /// BuildJobs - Bind actions to concrete tools and translate
  /// arguments to form the list of jobs to run.
  Compilation *BuildJobs(const ArgList &Args, const ActionList &Actions) const;

  /// @}
  /// @name Helper Methods
  /// @{

  /// PrintOptions - Print the list of arguments.
  void PrintOptions(const ArgList &Args) const;

  /// PrintVersion - Print the driver version.
  void PrintVersion() const;

  /// PrintActions - Print the list of actions.
  void PrintActions(const ArgList &Args, const ActionList &Actions) const;

  /// GetFilePath - Lookup \arg Name in the list of file search paths.
  ///
  /// \arg TC - Use the provided tool chain for additional information
  /// on directories to search, or the DefaultToolChain if not
  /// provided.
  // FIXME: This should be in CompilationInfo.
  llvm::sys::Path GetFilePath(const char *Name, const ToolChain *TC=0) const;

  /// GetProgramPath - Lookup \arg Name in the list of program search
  /// paths.
  ///
  /// \arg TC - Use the provided tool chain for additional information
  /// on directories to search, or the DefaultToolChain if not
  /// provided.
  // FIXME: This should be in CompilationInfo.
  llvm::sys::Path GetProgramPath(const char *Name, const ToolChain *TC=0) const;

  /// HandleImmediateArgs - Handle any arguments which should be
  /// treated before building actions or binding tools.
  ///
  /// \return Whether any compilation should be built for this
  /// invocation.
  bool HandleImmediateArgs(const ArgList &Args);

  /// ConstructAction - Construct the appropriate action to do for
  /// \arg Phase on the \arg Input, taking in to account arguments
  /// like -fsyntax-only or --analyze.
  Action *ConstructPhaseAction(const ArgList &Args, phases::ID Phase,
                               Action *Input) const;

  /// GetHostInfo - Construct a new host info object for the given
  /// host triple.
  static HostInfo *GetHostInfo(const char *HostTriple);

  /// @}
};

} // end namespace driver
} // end namespace clang

#endif
