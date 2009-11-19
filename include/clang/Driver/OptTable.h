//===--- OptTable.h - Option Table ------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef CLANG_DRIVER_OPTTABLE_H
#define CLANG_DRIVER_OPTTABLE_H

#include <cassert>

namespace clang {
namespace driver {
namespace options {
  enum DriverFlag {
    DriverOption     = (1 << 0),
    LinkerInput      = (1 << 1),
    NoArgumentUnused = (1 << 2),
    RenderAsInput    = (1 << 3),
    RenderJoined     = (1 << 4),
    RenderSeparate   = (1 << 5),
    Unsupported      = (1 << 6)
  };
}

  class Arg;
  class InputArgList;
  class Option;

  /// OptTable - Provide access to the Option info table.
  ///
  /// The OptTable class provides a layer of indirection which allows Option
  /// instance to be created lazily. In the common case, only a few options will
  /// be needed at runtime; the OptTable class maintains enough information to
  /// parse command lines without instantiating Options, while letting other
  /// parts of the driver still use Option instances where convenient.
  //
  // FIXME: Introduce an OptionSpecifier class to wrap the option ID
  // variant?
  class OptTable {
  public:
    /// Info - Entry for a single option instance in the option data table.
    struct Info {
      const char *Name;
      const char *HelpText;
      const char *MetaVar;
      unsigned char Kind;
      unsigned char Flags;
      unsigned char Param;
      unsigned short GroupID;
      unsigned short AliasID;
    };

  private:
    /// The static option information table.
    const Info *OptionInfos;
    unsigned NumOptionInfos;

    /// The lazily constructed options table, indexed by option::ID - 1.
    mutable Option **Options;

    /// Prebound input option instance.
    const Option *TheInputOption;

    /// Prebound unknown option instance.
    const Option *TheUnknownOption;

    /// The index of the first option which can be parsed (i.e., is not a
    /// special option like 'input' or 'unknown', and is not an option group).
    unsigned FirstSearchableIndex;

  private:
    const Info &getInfo(unsigned id) const {
      assert(id > 0 && id - 1 < getNumOptions() && "Invalid Option ID.");
      return OptionInfos[id - 1];
    }

    Option *CreateOption(unsigned id) const;

  protected:
    OptTable(const Info *_OptionInfos, unsigned _NumOptionInfos);
  public:
    ~OptTable();

    /// getNumOptions - Return the total number of option classes.
    unsigned getNumOptions() const { return NumOptionInfos; }

    /// getOption - Get the given \arg id's Option instance, lazily creating it
    /// if necessary.
    ///
    /// \return The option, or null for the INVALID option id.
    const Option *getOption(unsigned id) const {
      if (id == 0)
        return 0;

      assert((unsigned) (id - 1) < getNumOptions() && "Invalid ID.");
      Option *&Entry = Options[id - 1];
      if (!Entry)
        Entry = CreateOption(id);
      return Entry;
    }

    /// getOptionName - Lookup the name of the given option.
    const char *getOptionName(unsigned id) const {
      return getInfo(id).Name;
    }

    /// getOptionKind - Get the kind of the given option.
    unsigned getOptionKind(unsigned id) const {
      return getInfo(id).Kind;
    }

    /// getOptionHelpText - Get the help text to use to describe this option.
    const char *getOptionHelpText(unsigned id) const {
      return getInfo(id).HelpText;
    }

    /// getOptionMetaVar - Get the meta-variable name to use when describing
    /// this options values in the help text.
    const char *getOptionMetaVar(unsigned id) const {
      return getInfo(id).MetaVar;
    }

    /// parseOneArg - Parse a single argument; returning the new argument and
    /// updating Index.
    ///
    /// \param [in] [out] Index - The current parsing position in the argument
    /// string list; on return this will be the index of the next argument
    /// string to parse.
    ///
    /// \return - The parsed argument, or 0 if the argument is missing values
    /// (in which case Index still points at the conceptual next argument string
    /// to parse).
    Arg *ParseOneArg(const InputArgList &Args, unsigned &Index) const;
  };
}
}

#endif
