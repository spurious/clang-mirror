//===--- ToolChains.h - ToolChain Implementations ---------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef CLANG_LIB_DRIVER_TOOLCHAINS_H_
#define CLANG_LIB_DRIVER_TOOLCHAINS_H_

#include "clang/Driver/Action.h"
#include "clang/Driver/ToolChain.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/Support/Compiler.h"

#include "Tools.h"

namespace clang {
namespace driver {
namespace toolchains {

  /// Generic_GCC - A tool chain using the 'gcc' command to perform
  /// all subcommands; this relies on gcc translating the majority of
  /// command line options.
class VISIBILITY_HIDDEN Generic_GCC : public ToolChain {
protected:
  mutable llvm::DenseMap<unsigned, Tool*> Tools;

public:
  Generic_GCC(const HostInfo &Host, const llvm::Triple& Triple);
  ~Generic_GCC();

  virtual DerivedArgList *TranslateArgs(InputArgList &Args,
                                        const char *BoundArch) const;

  virtual Tool &SelectTool(const Compilation &C, const JobAction &JA) const;

  virtual bool IsMathErrnoDefault() const;
  virtual bool IsUnwindTablesDefault() const;
  virtual const char *GetDefaultRelocationModel() const;
  virtual const char *GetForcedPicModel() const;
};

/// Darwin - Darwin tool chain.
class VISIBILITY_HIDDEN Darwin : public ToolChain {
  mutable llvm::DenseMap<unsigned, Tool*> Tools;

  /// Darwin version of tool chain.
  unsigned DarwinVersion[3];

  /// GCC version to use.
  unsigned GCCVersion[3];

  /// Whether this is this an iPhone toolchain.
  bool IsIPhone;

  /// The directory suffix for this tool chain.
  std::string ToolChainDir;

  /// The default macosx-version-min of this tool chain; empty until
  /// initialized.
  mutable std::string MacosxVersionMin;

  /// The default iphoneos-version-min of this tool chain.
  std::string IPhoneOSVersionMin;

  const char *getMacosxVersionMin() const;

public:
  Darwin(const HostInfo &Host, const llvm::Triple& Triple,
         const unsigned (&DarwinVersion)[3],
         const unsigned (&GCCVersion)[3],
         bool IsIPhone);
  ~Darwin();

  /// @name Darwin Specific Toolchain API
  /// {

  void getDarwinVersion(unsigned (&Res)[3]) const {
    Res[0] = DarwinVersion[0];
    Res[1] = DarwinVersion[1];
    Res[2] = DarwinVersion[2];
  }

  void getMacosxVersion(unsigned (&Res)[3]) const {
    Res[0] = 10;
    Res[1] = DarwinVersion[0] - 4;
    Res[2] = DarwinVersion[1];
  }

  /// getMacosxVersionMin - Get the effective -mmacosx-version-min, which is
  /// either the -mmacosx-version-min, or the current version if unspecified.
  void getMacosxVersionMin(const ArgList &Args, unsigned (&Res)[3]) const;

  const char *getMacosxVersionStr() const {
    return MacosxVersionMin.c_str();
  }

  const char *getIPhoneOSVersionStr() const {
    return IPhoneOSVersionMin.c_str();
  }

  /// AddLinkSearchPathArgs - Add the linker search paths to \arg CmdArgs.
  ///
  /// \param Args - The input argument list.
  /// \param CmdArgs [out] - The command argument list to append the paths
  /// (prefixed by -L) to.
  void AddLinkSearchPathArgs(const ArgList &Args, ArgStringList &CmdArgs) const;

  bool isIPhone() const { return IsIPhone; }

  /// }
  /// @name ToolChain Implementation
  /// {

  virtual DerivedArgList *TranslateArgs(InputArgList &Args,
                                        const char *BoundArch) const;

  virtual Tool &SelectTool(const Compilation &C, const JobAction &JA) const;

  virtual bool IsMathErrnoDefault() const;
  virtual bool IsUnwindTablesDefault() const;
  virtual const char *GetDefaultRelocationModel() const;
  virtual const char *GetForcedPicModel() const;

  /// }
};

  /// Darwin_GCC - Generic Darwin tool chain using gcc.
class VISIBILITY_HIDDEN Darwin_GCC : public Generic_GCC {
public:
  Darwin_GCC(const HostInfo &Host, const llvm::Triple& Triple)
    : Generic_GCC(Host, Triple) {}

  virtual const char *GetDefaultRelocationModel() const { return "pic"; }
};

class VISIBILITY_HIDDEN AuroraUX : public Generic_GCC {
public:
  AuroraUX(const HostInfo &Host, const llvm::Triple& Triple);

  virtual Tool &SelectTool(const Compilation &C, const JobAction &JA) const;
};

class VISIBILITY_HIDDEN OpenBSD : public Generic_GCC {
public:
  OpenBSD(const HostInfo &Host, const llvm::Triple& Triple);

  virtual Tool &SelectTool(const Compilation &C, const JobAction &JA) const;
};

class VISIBILITY_HIDDEN FreeBSD : public Generic_GCC {
public:
  FreeBSD(const HostInfo &Host, const llvm::Triple& Triple, bool Lib32);

  virtual Tool &SelectTool(const Compilation &C, const JobAction &JA) const;
};

class VISIBILITY_HIDDEN DragonFly : public Generic_GCC {
public:
  DragonFly(const HostInfo &Host, const llvm::Triple& Triple);

  virtual Tool &SelectTool(const Compilation &C, const JobAction &JA) const;
};

class VISIBILITY_HIDDEN Linux : public Generic_GCC {
public:
  Linux(const HostInfo &Host, const llvm::Triple& Triple);
};


} // end namespace toolchains
} // end namespace driver
} // end namespace clang

#endif
