//===--- Compilation.h - Compilation Task Data Structure --------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef CLANG_DRIVER_COMPILATION_H_
#define CLANG_DRIVER_COMPILATION_H_

#include "clang/Driver/Job.h"
#include "clang/Driver/Util.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"

namespace llvm {
  class raw_ostream;
}

namespace clang {
namespace driver {
  class ArgList;
  class Driver;
  class JobList;
  class ToolChain;

/// Compilation - A set of tasks to perform for a single driver
/// invocation.
class Compilation {
  /// The driver we were created by.
  Driver &TheDriver;

  /// The default tool chain.
  ToolChain &DefaultToolChain;

  /// The original (untranslated) input argument list.
  ArgList *Args;

  /// The list of actions.
  ActionList Actions;

  /// The root list of jobs.
  JobList Jobs;

  /// Cache of translated arguments for a particular tool chain.
  llvm::DenseMap<const ToolChain*, ArgList*> TCArgs;

  /// Temporary files which should be removed on exit.
  ArgStringList TempFiles;

  /// Result files which should be removed on failure.
  ArgStringList ResultFiles;

public:
  Compilation(Driver &D, ToolChain &DefaultToolChain, ArgList *Args);
  ~Compilation();

  const Driver &getDriver() const { return TheDriver; }

  const ToolChain &getDefaultToolChain() const { return DefaultToolChain; }

  const ArgList &getArgs() const { return *Args; }

  ActionList &getActions() { return Actions; }
  const ActionList &getActions() const { return Actions; }

  JobList &getJobs() { return Jobs; }

  /// getArgsForToolChain - Return the argument list, possibly
  /// translated by the tool chain \arg TC (or by the default tool
  /// chain, if TC is not specified).
  const ArgList &getArgsForToolChain(const ToolChain *TC = 0);

  /// addTempFile - Add a file to remove on exit, and returns its
  /// argument.
  const char *addTempFile(const char *Name) { 
    TempFiles.push_back(Name); 
    return Name;
  }

  /// addResultFile - Add a file to remove on failure, and returns its
  /// argument.
  const char *addResultFile(const char *Name) {
    ResultFiles.push_back(Name);
    return Name;
  }

  /// Execute - Execute the compilation jobs and return an
  /// appropriate exit code.
  int Execute() const;

private:
  /// CleanupFileList - Remove the files in the given list.
  ///
  /// \param IssueErrors - Report failures as errors.
  /// \return Whether all files were removed successfully.
  bool CleanupFileList(const ArgStringList &Files, 
                       bool IssueErrors=false) const;

  /// PrintJob - Print one job in -### format.
  ///
  /// OS - The stream to print on.
  /// J - The job to print.
  /// Terminator - A string to print at the end of the line.
  void PrintJob(llvm::raw_ostream &OS, const Job *J, 
                const char *Terminator) const;
};

} // end namespace driver
} // end namespace clang

#endif
