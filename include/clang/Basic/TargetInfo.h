//===--- TargetInfo.h - Expose information about the target -----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the TargetInfo and TargetInfoImpl interfaces.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_BASIC_TARGETINFO_H
#define LLVM_CLANG_BASIC_TARGETINFO_H

#include "llvm/Support/DataTypes.h"
#include <vector>
#include <string>

namespace llvm { struct fltSemantics; }

namespace clang {

class TargetInfoImpl;
class Diagnostic;
class SourceManager;
  
namespace Builtin { struct Info; }
  
/// TargetInfo - This class exposes information about the current target.
///
class TargetInfo {
  /// Primary - This tracks the primary target in the target set.
  ///
  const TargetInfoImpl *Target;
  
  /// These are all caches for target values.
  unsigned WCharWidth, WCharAlign;

  //==----------------------------------------------------------------==/
  //                  TargetInfo Construction.
  //==----------------------------------------------------------------==/  
  
  TargetInfo(const TargetInfoImpl *TII);
  
public:  
  /// CreateTargetInfo - Return the target info object for the specified target
  /// triple.
  static TargetInfo* CreateTargetInfo(const std::string &Triple);

  ~TargetInfo();

  ///===---- Target property query methods --------------------------------===//

  /// getTargetDefines - Appends the target-specific #define values for this
  /// target set to the specified buffer.
  void getTargetDefines(std::vector<char> &DefineBuffer);
  
  /// isCharSigned - Return true if 'char' is 'signed char' or false if it is
  /// treated as 'unsigned char'.  This is implementation defined according to
  /// C99 6.2.5p15.  In our implementation, this is target-specific.
  bool isCharSigned() {
    // FIXME: implement correctly.
    return true;
  }
  
  /// getPointerWidth - Return the width of pointers on this target, we
  /// currently assume one pointer type.
  void getPointerInfo(uint64_t &Size, unsigned &Align) {
    Size = 32;  // FIXME: implement correctly.
    Align = 32;
  }
  
  /// getBoolInfo - Return the size of '_Bool' and C++ 'bool' for this target,
  /// in bits.  
  void getBoolInfo(uint64_t &Size, unsigned &Align) {
    Size = Align = 8;    // FIXME: implement correctly: wrong for ppc32.
  }
  
  /// getCharInfo - Return the size of 'char', 'signed char' and
  /// 'unsigned char' for this target, in bits.  
  void getCharInfo(uint64_t &Size, unsigned &Align) {
    Size = Align = 8; // FIXME: implement correctly.
  }
  
  /// getShortInfo - Return the size of 'signed short' and 'unsigned short' for
  /// this target, in bits.  
  void getShortInfo(uint64_t &Size, unsigned &Align) {
    Size = Align = 16; // FIXME: implement correctly.
  }
  
  /// getIntInfo - Return the size of 'signed int' and 'unsigned int' for this
  /// target, in bits.  
  void getIntInfo(uint64_t &Size, unsigned &Align) {
    Size = Align = 32; // FIXME: implement correctly.
  }
  
  /// getLongInfo - Return the size of 'signed long' and 'unsigned long' for
  /// this target, in bits.  
  void getLongInfo(uint64_t &Size, unsigned &Align) {
    Size = Align = 32;  // FIXME: implement correctly: wrong for ppc64/x86-64
  }

  /// getLongLongInfo - Return the size of 'signed long long' and
  /// 'unsigned long long' for this target, in bits.  
  void getLongLongInfo(uint64_t &Size, unsigned &Align) {
    Size = Align = 64; // FIXME: implement correctly.
  }
  
  /// getFloatInfo - Characterize 'float' for this target.  
  void getFloatInfo(uint64_t &Size, unsigned &Align,
                    const llvm::fltSemantics *&Format);

  /// getDoubleInfo - Characterize 'double' for this target.
  void getDoubleInfo(uint64_t &Size, unsigned &Align,
                     const llvm::fltSemantics *&Format);

  /// getLongDoubleInfo - Characterize 'long double' for this target.
  void getLongDoubleInfo(uint64_t &Size, unsigned &Align,
                         const llvm::fltSemantics *&Format);
  
  /// getWCharInfo - Return the size of wchar_t in bits.
  ///
  void getWCharInfo(uint64_t &Size, unsigned &Align) {
    Size = WCharWidth;
    Align = WCharAlign;
  }
  
  /// getIntMaxTWidth - Return the size of intmax_t and uintmax_t for this
  /// target, in bits.  
  unsigned getIntMaxTWidth() {
    // FIXME: implement correctly.
    return 64;
  }
  
  /// getTargetBuiltins - Return information about target-specific builtins for
  /// the current primary target, and info about which builtins are non-portable
  /// across the current set of primary and secondary targets.
  void getTargetBuiltins(const Builtin::Info *&Records, 
                         unsigned &NumRecords) const;

  /// getVAListDeclaration - Return the declaration to use for
  /// __builtin_va_list, which is target-specific.
  const char *getVAListDeclaration() const;

  /// isValidGCCRegisterName - Returns whether the passed in string
  /// is a valid register name according to GCC. This is used by Sema for
  /// inline asm statements.
  bool isValidGCCRegisterName(const char *Name) const;

  // getNormalizedGCCRegisterName - Returns the "normalized" GCC register name.
  // For example, on x86 it will return "ax" when "eax" is passed in.
  const char *getNormalizedGCCRegisterName(const char *Name) const;
  
  enum ConstraintInfo {
    CI_None = 0x00,
    CI_AllowsMemory = 0x01,
    CI_AllowsRegister = 0x02,
    CI_ReadWrite = 0x04
  };

  // validateOutputConstraint, validateInputConstraint - Checks that
  // a constraint is valid and provides information about it.
  // FIXME: These should return a real error instead of just true/false.
  bool validateOutputConstraint(const char *Name, ConstraintInfo &Info) const;
  bool validateInputConstraint (const char *Name, unsigned NumOutputs,
                                ConstraintInfo &info) const;

  std::string convertConstraint(const char Constraint) const;
  
  // Returns a string of target-specific clobbers, in LLVM format.
  const char *getClobbers() const;
  
  ///===---- Some helper methods ------------------------------------------===//

  unsigned getBoolWidth() {
    uint64_t Size; unsigned Align;
    getBoolInfo(Size, Align);
    return static_cast<unsigned>(Size);
  }
  
  unsigned getCharWidth(bool isWide = false) {
    uint64_t Size; unsigned Align;
    if (isWide)
      getWCharInfo(Size, Align);
    else
      getCharInfo(Size, Align);
    return static_cast<unsigned>(Size);
  }
  
  unsigned getWCharWidth() {
    uint64_t Size; unsigned Align;
    getWCharInfo(Size, Align);
    return static_cast<unsigned>(Size);
  }
  
  unsigned getIntWidth() {
    uint64_t Size; unsigned Align;
    getIntInfo(Size, Align);
    return static_cast<unsigned>(Size);
  }
  
  unsigned getLongWidth() {
    uint64_t Size; unsigned Align;
    getLongInfo(Size, Align);
    return static_cast<unsigned>(Size);
  }

  unsigned getLongLongWidth() {
    uint64_t Size; unsigned Align;
    getLongLongInfo(Size, Align);
    return static_cast<unsigned>(Size);
  }

  /// getTargetPrefix - Return the target prefix used for identifying
  /// llvm intrinsics.
  const char *getTargetPrefix() const;
    
  /// getTargetTriple - Return the target triple of the primary target.
  const char *getTargetTriple() const;
  
  const char *getTargetDescription() const {
    // FIXME !
    // Hard code darwin-x86 for now.
    return "e-p:32:32:32-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:32:64-f32:32:\
32-f64:32:64-v64:64:64-v128:128:128-a0:0:64-f80:128:128";
  }
};




/// TargetInfoImpl - This class is implemented for specific targets and is used
/// by the TargetInfo class.  Target implementations should initialize instance
/// variables and implement various virtual methods if the default values are
/// not appropriate for the target.
class TargetInfoImpl {
protected:
  unsigned WCharWidth;    /// sizeof(wchar_t) in bits.  Default value is 32.
  unsigned WCharAlign;    /// alignof(wchar_t) in bits.  Default value is 32.
  std::string Triple;
public:
  TargetInfoImpl(const std::string& triple) 
    : WCharWidth(32), WCharAlign(32), Triple(triple) {}
  
  virtual ~TargetInfoImpl() {}
  
  /// getTargetTriple - Return the string representing the target triple this
  ///  TargetInfoImpl object was created from.
  const char* getTargetTriple() const { return Triple.c_str(); }
  
  virtual const char *getTargetPrefix() const = 0;

  /// getTargetDefines - Return a list of the target-specific #define values set
  /// when compiling to this target.  Each string should be of the form
  /// "#define X Y\n".
  virtual void getTargetDefines(std::vector<char> &Defines) const = 0;

  /// getVAListDeclaration - Return the declaration to use for
  /// __builtin_va_list, which is target-specific.
  virtual const char *getVAListDeclaration() const = 0;
    
  /// getWCharWidth - Return the size of wchar_t in bits.
  ///
  void getWCharInfo(unsigned &Size, unsigned &Align) const {
    Size = WCharWidth;
    Align = WCharAlign;
  }
    
  /// getTargetBuiltins - Return information about target-specific builtins for
  /// the target.
  virtual void getTargetBuiltins(const Builtin::Info *&Records,
                                 unsigned &NumRecords) const {
    Records = 0;
    NumRecords = 0;
  }
  
  virtual void getGCCRegNames(const char * const *&Names, 
                              unsigned &NumNames) const = 0;

  struct GCCRegAlias {
    const char * const Aliases[5];
    const char * const Register;
  };
  virtual void getGCCRegAliases(const GCCRegAlias *&Aliases, 
                                unsigned &NumAliases) const = 0;
  
  virtual bool validateAsmConstraint(char c, 
                                     TargetInfo::ConstraintInfo &info) const= 0;

  virtual std::string convertConstraint(const char Constraint) const {
    return std::string(1, Constraint);
  }
  
  virtual const char *getClobbers() const = 0;
private:
  virtual void ANCHOR(); // out-of-line virtual method for class.
};

}  // end namespace clang

#endif
