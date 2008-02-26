//===--- Targets.cpp - Implement -arch option and targets -----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements construction of a TargetInfo object from a 
// target triple.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/Builtins.h"
#include "clang/AST/TargetBuiltins.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/TargetInfo.h"
#include "llvm/ADT/STLExtras.h"

using namespace clang;

//===----------------------------------------------------------------------===//
//  Common code shared among targets.
//===----------------------------------------------------------------------===//

static void Define(std::vector<char> &Buf, const char *Macro,
                   const char *Val = "1") {
  const char *Def = "#define ";
  Buf.insert(Buf.end(), Def, Def+strlen(Def));
  Buf.insert(Buf.end(), Macro, Macro+strlen(Macro));
  Buf.push_back(' ');
  Buf.insert(Buf.end(), Val, Val+strlen(Val));
  Buf.push_back('\n');
}


namespace {
class DarwinTargetInfo : public TargetInfoImpl {
public:
  DarwinTargetInfo(const std::string& triple) : TargetInfoImpl(triple) {}
  
  virtual void getTargetDefines(std::vector<char> &Defs) const {
// FIXME: we need a real target configuration system.  For now, only define
// __APPLE__ if the host has it.
#ifdef __APPLE__
    Define(Defs, "__APPLE__");
    Define(Defs, "__MACH__");
#endif
    
    if (1) {// -fobjc-gc controls this.
      Define(Defs, "__weak", "");
      Define(Defs, "__strong", "");
    } else {
      Define(Defs, "__weak", "__attribute__((objc_gc(weak)))");
      Define(Defs, "__strong", "__attribute__((objc_gc(strong)))");
      Define(Defs, "__OBJC_GC__");
    }

    // darwin_constant_cfstrings controls this.
    Define(Defs, "__CONSTANT_CFSTRINGS__");
    
    if (0)  // darwin_pascal_strings
      Define(Defs, "__PASCAL_STRINGS__");
  }

};


class SolarisTargetInfo : public TargetInfoImpl {
public:
  SolarisTargetInfo(const std::string& triple) : TargetInfoImpl(triple) {}
  
  virtual void getTargetDefines(std::vector<char> &Defs) const {
// FIXME: we need a real target configuration system.  For now, only define
// __SUN__ if the host has it.
#ifdef __SUN__
    Define(Defs, "__SUN__");
    Define(Defs, "__SOLARIS__");
#endif
    
    if (1) {// -fobjc-gc controls this.
      Define(Defs, "__weak", "");
      Define(Defs, "__strong", "");
    } else {
      Define(Defs, "__weak", "__attribute__((objc_gc(weak)))");
      Define(Defs, "__strong", "__attribute__((objc_gc(strong)))");
      Define(Defs, "__OBJC_GC__");
    }
  }

};
} // end anonymous namespace.


/// getPowerPCDefines - Return a set of the PowerPC-specific #defines that are
/// not tied to a specific subtarget.
static void getPowerPCDefines(std::vector<char> &Defs, bool is64Bit) {
  // Target identification.
  Define(Defs, "__ppc__");
  Define(Defs, "_ARCH_PPC");
  Define(Defs, "__POWERPC__");
  if (is64Bit) {
    Define(Defs, "_ARCH_PPC64");
    Define(Defs, "_LP64");
    Define(Defs, "__LP64__");
    Define(Defs, "__ppc64__");
  } else {
    Define(Defs, "__ppc__");
  }

  // Target properties.
  Define(Defs, "_BIG_ENDIAN");
  Define(Defs, "__BIG_ENDIAN__");

  if (is64Bit) {
    Define(Defs, "__INTMAX_MAX__", "9223372036854775807L");
    Define(Defs, "__INTMAX_TYPE__", "long int");
    Define(Defs, "__LONG_MAX__", "9223372036854775807L");
    Define(Defs, "__PTRDIFF_TYPE__", "long int");
    Define(Defs, "__UINTMAX_TYPE__", "long unsigned int");
  } else {
    Define(Defs, "__INTMAX_MAX__", "9223372036854775807LL");
    Define(Defs, "__INTMAX_TYPE__", "long long int");
    Define(Defs, "__LONG_MAX__", "2147483647L");
    Define(Defs, "__PTRDIFF_TYPE__", "int");
    Define(Defs, "__UINTMAX_TYPE__", "long long unsigned int");
  }
  Define(Defs, "__INT_MAX__", "2147483647");
  Define(Defs, "__LONG_LONG_MAX__", "9223372036854775807LL");
  Define(Defs, "__CHAR_BIT__", "8");
  Define(Defs, "__SCHAR_MAX__", "127");
  Define(Defs, "__SHRT_MAX__", "32767");
  Define(Defs, "__SIZE_TYPE__", "long unsigned int");
  
  // Subtarget options.
  Define(Defs, "__USER_LABEL_PREFIX__", "_");
  Define(Defs, "__NATURAL_ALIGNMENT__");
  Define(Defs, "__REGISTER_PREFIX__", "");

  Define(Defs, "__WCHAR_MAX__", "2147483647");
  Define(Defs, "__WCHAR_TYPE__", "int");
  Define(Defs, "__WINT_TYPE__", "int");
  
  // Float macros.
  Define(Defs, "__FLT_DENORM_MIN__", "1.40129846e-45F");
  Define(Defs, "__FLT_DIG__", "6");
  Define(Defs, "__FLT_EPSILON__", "1.19209290e-7F");
  Define(Defs, "__FLT_EVAL_METHOD__", "0");
  Define(Defs, "__FLT_HAS_INFINITY__");
  Define(Defs, "__FLT_HAS_QUIET_NAN__");
  Define(Defs, "__FLT_MANT_DIG__", "24");
  Define(Defs, "__FLT_MAX_10_EXP__", "38");
  Define(Defs, "__FLT_MAX_EXP__", "128");
  Define(Defs, "__FLT_MAX__", "3.40282347e+38F");
  Define(Defs, "__FLT_MIN_10_EXP__", "(-37)");
  Define(Defs, "__FLT_MIN_EXP__", "(-125)");
  Define(Defs, "__FLT_MIN__", "1.17549435e-38F");
  Define(Defs, "__FLT_RADIX__", "2");
  
  // double macros.
  Define(Defs, "__DBL_DENORM_MIN__", "4.9406564584124654e-324");
  Define(Defs, "__DBL_DIG__", "15");
  Define(Defs, "__DBL_EPSILON__", "2.2204460492503131e-16");
  Define(Defs, "__DBL_HAS_INFINITY__");
  Define(Defs, "__DBL_HAS_QUIET_NAN__");
  Define(Defs, "__DBL_MANT_DIG__", "53");
  Define(Defs, "__DBL_MAX_10_EXP__", "308");
  Define(Defs, "__DBL_MAX_EXP__", "1024");
  Define(Defs, "__DBL_MAX__", "1.7976931348623157e+308");
  Define(Defs, "__DBL_MIN_10_EXP__", "(-307)");
  Define(Defs, "__DBL_MIN_EXP__", "(-1021)");
  Define(Defs, "__DBL_MIN__", "2.2250738585072014e-308");
  Define(Defs, "__DECIMAL_DIG__", "33");
  
  // 128-bit long double macros.
  Define(Defs, "__LDBL_DENORM_MIN__",
         "4.94065645841246544176568792868221e-324L");
  Define(Defs, "__LDBL_DIG__", "31");
  Define(Defs, "__LDBL_EPSILON__",
         "4.94065645841246544176568792868221e-324L");
  Define(Defs, "__LDBL_HAS_INFINITY__");
  Define(Defs, "__LDBL_HAS_QUIET_NAN__");
  Define(Defs, "__LDBL_MANT_DIG__", "106");
  Define(Defs, "__LDBL_MAX_10_EXP__", "308");
  Define(Defs, "__LDBL_MAX_EXP__", "1024");
  Define(Defs, "__LDBL_MAX__",
         "1.79769313486231580793728971405301e+308L");
  Define(Defs, "__LDBL_MIN_10_EXP__", "(-291)");
  Define(Defs, "__LDBL_MIN_EXP__", "(-968)");
  Define(Defs, "__LDBL_MIN__",
         "2.00416836000897277799610805135016e-292L");
  Define(Defs, "__LONG_DOUBLE_128__");
}

/// getX86Defines - Return a set of the X86-specific #defines that are
/// not tied to a specific subtarget.
static void getX86Defines(std::vector<char> &Defs, bool is64Bit) {
  // Target identification.
  if (is64Bit) {
    Define(Defs, "_LP64");
    Define(Defs, "__LP64__");
    Define(Defs, "__amd64__");
    Define(Defs, "__amd64");
    Define(Defs, "__x86_64");
    Define(Defs, "__x86_64__");
  } else {
    Define(Defs, "__i386__");
    Define(Defs, "__i386");
    Define(Defs, "i386");
  }

  // Target properties.
  Define(Defs, "__LITTLE_ENDIAN__");
  
  if (is64Bit) {
    Define(Defs, "__INTMAX_MAX__", "9223372036854775807L");
    Define(Defs, "__INTMAX_TYPE__", "long int");
    Define(Defs, "__LONG_MAX__", "9223372036854775807L");
    Define(Defs, "__PTRDIFF_TYPE__", "long int");
    Define(Defs, "__UINTMAX_TYPE__", "long unsigned int");
    Define(Defs, "__SIZE_TYPE__", "long unsigned int");
  } else {
    Define(Defs, "__INTMAX_MAX__", "9223372036854775807LL");
    Define(Defs, "__INTMAX_TYPE__", "long long int");
    Define(Defs, "__LONG_MAX__", "2147483647L");
    Define(Defs, "__PTRDIFF_TYPE__", "int");
    Define(Defs, "__UINTMAX_TYPE__", "long long unsigned int");
    Define(Defs, "__SIZE_TYPE__", "unsigned int");
  }
  Define(Defs, "__CHAR_BIT__", "8");
  Define(Defs, "__INT_MAX__", "2147483647");
  Define(Defs, "__LONG_LONG_MAX__", "9223372036854775807LL");
  Define(Defs, "__SCHAR_MAX__", "127");
  Define(Defs, "__SHRT_MAX__", "32767");
  
  // Subtarget options.
  Define(Defs, "__nocona");
  Define(Defs, "__nocona__");
  Define(Defs, "__tune_nocona__");
  Define(Defs, "__SSE2_MATH__");
  Define(Defs, "__SSE2__");
  Define(Defs, "__SSE_MATH__");
  Define(Defs, "__SSE__");
  Define(Defs, "__MMX__");
  Define(Defs, "__REGISTER_PREFIX__", "");

  Define(Defs, "__WCHAR_MAX__", "2147483647");
  Define(Defs, "__WCHAR_TYPE__", "int");
  Define(Defs, "__WINT_TYPE__", "int");
  
  // Float macros.
  Define(Defs, "__FLT_DENORM_MIN__", "1.40129846e-45F");
  Define(Defs, "__FLT_DIG__", "6");
  Define(Defs, "__FLT_EPSILON__", "1.19209290e-7F");
  Define(Defs, "__FLT_EVAL_METHOD__", "0");
  Define(Defs, "__FLT_HAS_INFINITY__");
  Define(Defs, "__FLT_HAS_QUIET_NAN__");
  Define(Defs, "__FLT_MANT_DIG__", "24");
  Define(Defs, "__FLT_MAX_10_EXP__", "38");
  Define(Defs, "__FLT_MAX_EXP__", "128");
  Define(Defs, "__FLT_MAX__", "3.40282347e+38F");
  Define(Defs, "__FLT_MIN_10_EXP__", "(-37)");
  Define(Defs, "__FLT_MIN_EXP__", "(-125)");
  Define(Defs, "__FLT_MIN__", "1.17549435e-38F");
  Define(Defs, "__FLT_RADIX__", "2");
  
  // Double macros.
  Define(Defs, "__DBL_DENORM_MIN__", "4.9406564584124654e-324");
  Define(Defs, "__DBL_DIG__", "15");
  Define(Defs, "__DBL_EPSILON__", "2.2204460492503131e-16");
  Define(Defs, "__DBL_HAS_INFINITY__");
  Define(Defs, "__DBL_HAS_QUIET_NAN__");
  Define(Defs, "__DBL_MANT_DIG__", "53");
  Define(Defs, "__DBL_MAX_10_EXP__", "308");
  Define(Defs, "__DBL_MAX_EXP__", "1024");
  Define(Defs, "__DBL_MAX__", "1.7976931348623157e+308");
  Define(Defs, "__DBL_MIN_10_EXP__", "(-307)");
  Define(Defs, "__DBL_MIN_EXP__", "(-1021)");
  Define(Defs, "__DBL_MIN__", "2.2250738585072014e-308");
  Define(Defs, "__DECIMAL_DIG__", "21");
  
  // 80-bit Long double macros.
  Define(Defs, "__LDBL_DENORM_MIN__", "3.64519953188247460253e-4951L");
  Define(Defs, "__LDBL_DIG__", "18");
  Define(Defs, "__LDBL_EPSILON__", "1.08420217248550443401e-19L");
  Define(Defs, "__LDBL_HAS_INFINITY__");
  Define(Defs, "__LDBL_HAS_QUIET_NAN__");
  Define(Defs, "__LDBL_MANT_DIG__", "64");
  Define(Defs, "__LDBL_MAX_10_EXP__", "4932");
  Define(Defs, "__LDBL_MAX_EXP__", "16384");
  Define(Defs, "__LDBL_MAX__", "1.18973149535723176502e+4932L");
  Define(Defs, "__LDBL_MIN_10_EXP__", "(-4931)");
  Define(Defs, "__LDBL_MIN_EXP__", "(-16381)");
  Define(Defs, "__LDBL_MIN__", "3.36210314311209350626e-4932L");
}

static const char* getI386VAListDeclaration() {
  return "typedef char* __builtin_va_list;";
}

static const char* getX86_64VAListDeclaration() {
  return 
    "typedef struct __va_list_tag {"
    "  unsigned gp_offset;"
    "  unsigned fp_offset;"
    "  void* overflow_arg_area;"
    "  void* reg_save_area;"
    "} __builtin_va_list[1];";
}

static const char* getPPCVAListDeclaration() {
  return 
    "typedef struct __va_list_tag {"
    "  unsigned char gpr;"
    "  unsigned char fpr;"
    "  unsigned short reserved;"
    "  void* overflow_arg_area;"
    "  void* reg_save_area;"
    "} __builtin_va_list[1];";
}


/// PPC builtin info.
namespace clang {
namespace PPC {
  
  static const Builtin::Info BuiltinInfo[] = {
#define BUILTIN(ID, TYPE, ATTRS) { #ID, TYPE, ATTRS },
#include "clang/AST/PPCBuiltins.def"
  };
  
  static void getBuiltins(const Builtin::Info *&Records, unsigned &NumRecords) {
    Records = BuiltinInfo;
    NumRecords = LastTSBuiltin-Builtin::FirstTSBuiltin;
  }

  static const char * const GCCRegNames[] = {
    "0", "1", "2", "3", "4", "5", "6", "7",
    "8", "9", "10", "11", "12", "13", "14", "15",
    "16", "17", "18", "19", "20", "21", "22", "23",
    "24", "25", "26", "27", "28", "29", "30", "31",
    "0", "1", "2", "3", "4", "5", "6", "7",
    "8", "9", "10", "11", "12", "13", "14", "15",
    "16", "17", "18", "19", "20", "21", "22", "23",
    "24", "25", "26", "27", "28", "29", "30", "31",
    "mq", "lr", "ctr", "ap",
    "0", "1", "2", "3", "4", "5", "6", "7",
    "xer",
    "0", "1", "2", "3", "4", "5", "6", "7",
    "8", "9", "10", "11", "12", "13", "14", "15",
    "16", "17", "18", "19", "20", "21", "22", "23",
    "24", "25", "26", "27", "28", "29", "30", "31",
    "vrsave", "vscr",
    "spe_acc", "spefscr",
    "sfp"
  };

  static void getGCCRegNames(const char * const *&Names, 
                                  unsigned &NumNames) {
    Names = GCCRegNames;
    NumNames = llvm::array_lengthof(GCCRegNames);
  }

  static const TargetInfoImpl::GCCRegAlias GCCRegAliases[] = {
    // While some of these aliases do map to different registers
    // they still share the same register name.
    { { "cc", "cr0", "fr0", "r0", "v0"}, "0" }, 
    { { "cr1", "fr1", "r1", "sp", "v1"}, "1" }, 
    { { "cr2", "fr2", "r2", "toc", "v2"}, "2" }, 
    { { "cr3", "fr3", "r3", "v3"}, "3" }, 
    { { "cr4", "fr4", "r4", "v4"}, "4" }, 
    { { "cr5", "fr5", "r5", "v5"}, "5" }, 
    { { "cr6", "fr6", "r6", "v6"}, "6" }, 
    { { "cr7", "fr7", "r7", "v7"}, "7" }, 
    { { "fr8", "r8", "v8"}, "8" }, 
    { { "fr9", "r9", "v9"}, "9" }, 
    { { "fr10", "r10", "v10"}, "10" }, 
    { { "fr11", "r11", "v11"}, "11" }, 
    { { "fr12", "r12", "v12"}, "12" }, 
    { { "fr13", "r13", "v13"}, "13" }, 
    { { "fr14", "r14", "v14"}, "14" }, 
    { { "fr15", "r15", "v15"}, "15" }, 
    { { "fr16", "r16", "v16"}, "16" }, 
    { { "fr17", "r17", "v17"}, "17" }, 
    { { "fr18", "r18", "v18"}, "18" }, 
    { { "fr19", "r19", "v19"}, "19" }, 
    { { "fr20", "r20", "v20"}, "20" }, 
    { { "fr21", "r21", "v21"}, "21" }, 
    { { "fr22", "r22", "v22"}, "22" }, 
    { { "fr23", "r23", "v23"}, "23" }, 
    { { "fr24", "r24", "v24"}, "24" }, 
    { { "fr25", "r25", "v25"}, "25" }, 
    { { "fr26", "r26", "v26"}, "26" }, 
    { { "fr27", "r27", "v27"}, "27" }, 
    { { "fr28", "r28", "v28"}, "28" }, 
    { { "fr29", "r29", "v29"}, "29" }, 
    { { "fr30", "r30", "v30"}, "30" }, 
    { { "fr31", "r31", "v31"}, "31" }, 
  };
  
  static void getGCCRegAliases(const TargetInfoImpl::GCCRegAlias *&Aliases, 
                               unsigned &NumAliases) {
    Aliases = GCCRegAliases;
    NumAliases = llvm::array_lengthof(GCCRegAliases);
  }
  
  static bool validateAsmConstraint(char c, 
                                    TargetInfo::ConstraintInfo &info) {
    switch (c) {
    default: return false;
    case 'O': // Zero
      return true;
    case 'b': // Base register
    case 'f': // Floating point register
      info = (TargetInfo::ConstraintInfo)(info|TargetInfo::CI_AllowsRegister);
      return true;
    }
  }
  
  const char *getClobbers() {
    return 0;
  }

  const char *getTargetPrefix() {
    return "ppc";
  }
  
} // End namespace PPC

/// X86 builtin info.
namespace X86 {
  static const Builtin::Info BuiltinInfo[] = {
#define BUILTIN(ID, TYPE, ATTRS) { #ID, TYPE, ATTRS },
#include "clang/AST/X86Builtins.def"
  };

  static void getBuiltins(const Builtin::Info *&Records, unsigned &NumRecords) {
    Records = BuiltinInfo;
    NumRecords = LastTSBuiltin-Builtin::FirstTSBuiltin;
  }
    
  static const char *GCCRegNames[] = {
    "ax", "dx", "cx", "bx", "si", "di", "bp", "sp",
    "st", "st(1)", "st(2)", "st(3)", "st(4)", "st(5)", "st(6)", "st(7)",
    "argp", "flags", "fspr", "dirflag", "frame",
    "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7",
    "mm0", "mm1", "mm2", "mm3", "mm4", "mm5", "mm6", "mm7",
    "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15",
    "xmm8", "xmm9", "xmm10", "xmm11", "xmm12", "xmm13", "xmm14", "xmm15"
  };
  
  static void getGCCRegNames(const char * const *&Names, 
                                  unsigned &NumNames) {
    Names = GCCRegNames;
    NumNames = llvm::array_lengthof(GCCRegNames);
  }
  
  static const TargetInfoImpl::GCCRegAlias GCCRegAliases[] = {
    { { "al", "ah", "eax", "rax" }, "ax" },
    { { "bl", "bh", "ebx", "rbx" }, "bx" },
    { { "cl", "ch", "ecx", "rcx" }, "cx" },
    { { "dl", "dh", "edx", "rdx" }, "dx" },
    { { "esi", "rsi" }, "si" },
    { { "esp", "rsp" }, "sp" },
    { { "ebp", "rbp" }, "bp" },
  };

  static void getGCCRegAliases(const TargetInfoImpl::GCCRegAlias *&Aliases, 
                               unsigned &NumAliases) {
    Aliases = GCCRegAliases;
    NumAliases = llvm::array_lengthof(GCCRegAliases);
  }  
  
  static bool validateAsmConstraint(char c, 
                                    TargetInfo::ConstraintInfo &info) {
    switch (c) {
    default: return false;
    case 'a': // eax.
    case 'b': // ebx.
    case 'c': // ecx.
    case 'd': // edx.
    case 'S': // esi.
    case 'D': // edi.
    case 'A': // edx:eax.
    case 't': // top of floating point stack.
    case 'u': // second from top of floating point stack.
    case 'q': // a, b, c, d registers or any integer register in 64-bit.
    case 'Z': // 32-bit integer constant for used with zero-extending x86_64
              // instructions.
      info = (TargetInfo::ConstraintInfo)(info|TargetInfo::CI_AllowsRegister);
      return true;
    }
  }

  static std::string convertConstraint(const char Constraint) {
    switch (Constraint) {
    case 'a': return std::string("{ax}");
    case 'b': return std::string("{bx}");
    case 'c': return std::string("{cx}");
    case 'd': return std::string("{dx}");
    case 'S': return std::string("{si}");
    case 'D': return std::string("{di}");
    case 't': // top of floating point stack.
      return std::string("{st}");
    case 'u': // second from top of floating point stack.
      return std::string("{st(1)}"); // second from top of floating point stack.
    case 'A': // edx:eax.
    case 'q': // a, b, c, d registers or any integer register in 64-bit.
    case 'Z': // 32-bit integer constant for used with zero-extending x86_64
              // instructions.
      assert(false && "Unimplemented inline asm constraint");
    default:
      return std::string(1, Constraint);
    }
  }

  const char *getClobbers() {
    return "~{dirflag},~{fpsr},~{flags}";
  }
  
  const char *getTargetPrefix() {
    return "x86";
  }
  
} // End namespace X86
} // end namespace clang.

//===----------------------------------------------------------------------===//
// Specific target implementations.
//===----------------------------------------------------------------------===//


namespace {
class DarwinPPCTargetInfo : public DarwinTargetInfo {
public:
  DarwinPPCTargetInfo(const std::string& triple) : DarwinTargetInfo(triple) {}
  
  virtual void getTargetDefines(std::vector<char> &Defines) const {
    DarwinTargetInfo::getTargetDefines(Defines);
    getPowerPCDefines(Defines, false);
  }
  virtual void getTargetBuiltins(const Builtin::Info *&Records,
                                 unsigned &NumRecords) const {
    PPC::getBuiltins(Records, NumRecords);
  }
  virtual const char *getVAListDeclaration() const {
    return getPPCVAListDeclaration();
  }
  virtual const char *getTargetPrefix() const {
    return PPC::getTargetPrefix();
  }
  virtual void getGCCRegNames(const char * const *&Names, 
                              unsigned &NumNames) const {
    PPC::getGCCRegNames(Names, NumNames);
  }
  virtual void getGCCRegAliases(const GCCRegAlias *&Aliases, 
                                unsigned &NumAliases) const {
    PPC::getGCCRegAliases(Aliases, NumAliases);
  }
  virtual bool validateAsmConstraint(char c,
                                     TargetInfo::ConstraintInfo &info) const {
    return PPC::validateAsmConstraint(c, info);
  }
  virtual const char *getClobbers() const {
    return PPC::getClobbers();
  }
};
} // end anonymous namespace.

namespace {
class DarwinPPC64TargetInfo : public DarwinTargetInfo {
public:
  DarwinPPC64TargetInfo(const std::string& triple) : DarwinTargetInfo(triple) {}
  
  virtual void getTargetDefines(std::vector<char> &Defines) const {
    DarwinTargetInfo::getTargetDefines(Defines);
    getPowerPCDefines(Defines, true);
  }
  virtual void getTargetBuiltins(const Builtin::Info *&Records,
                                 unsigned &NumRecords) const {
    PPC::getBuiltins(Records, NumRecords);
  }
  virtual const char *getVAListDeclaration() const {
    return getPPCVAListDeclaration();
  }  
  virtual const char *getTargetPrefix() const {
    return PPC::getTargetPrefix();
  }  
  virtual void getGCCRegNames(const char * const *&Names, 
                                   unsigned &NumNames) const {
    PPC::getGCCRegNames(Names, NumNames);
  }    
  virtual void getGCCRegAliases(const GCCRegAlias *&Aliases, 
                                unsigned &NumAliases) const {
    PPC::getGCCRegAliases(Aliases, NumAliases);
  }
  virtual bool validateAsmConstraint(char c,
                                     TargetInfo::ConstraintInfo &info) const {
    return PPC::validateAsmConstraint(c, info);
  }
  virtual const char *getClobbers() const {
    return PPC::getClobbers();
  }  
};
} // end anonymous namespace.

namespace {
class DarwinI386TargetInfo : public DarwinTargetInfo {
public:
  DarwinI386TargetInfo(const std::string& triple) : DarwinTargetInfo(triple) {}
  
  virtual void getTargetDefines(std::vector<char> &Defines) const {
    DarwinTargetInfo::getTargetDefines(Defines);
    getX86Defines(Defines, false);
  }
  virtual void getTargetBuiltins(const Builtin::Info *&Records,
                                 unsigned &NumRecords) const {
    X86::getBuiltins(Records, NumRecords);
  }
  virtual const char *getVAListDeclaration() const {
    return getI386VAListDeclaration();
  }  
  virtual const char *getTargetPrefix() const {
    return X86::getTargetPrefix();
  }  
  virtual void getGCCRegNames(const char * const *&Names, 
                                   unsigned &NumNames) const {
    X86::getGCCRegNames(Names, NumNames);
  }
  virtual void getGCCRegAliases(const GCCRegAlias *&Aliases, 
                                unsigned &NumAliases) const {
    X86::getGCCRegAliases(Aliases, NumAliases);
  }
  virtual bool validateAsmConstraint(char c,
                                     TargetInfo::ConstraintInfo &info) const {
    return X86::validateAsmConstraint(c, info);
  }

  virtual std::string convertConstraint(const char Constraint) const {
    return X86::convertConstraint(Constraint);
  }

  virtual const char *getClobbers() const {
    return X86::getClobbers();
  }  
};
} // end anonymous namespace.

namespace {
class DarwinX86_64TargetInfo : public DarwinTargetInfo {
public:
  DarwinX86_64TargetInfo(const std::string& triple) :DarwinTargetInfo(triple) {}
  
  virtual void getTargetDefines(std::vector<char> &Defines) const {
    DarwinTargetInfo::getTargetDefines(Defines);
    getX86Defines(Defines, true);
  }
  virtual void getTargetBuiltins(const Builtin::Info *&Records,
                                 unsigned &NumRecords) const {
    X86::getBuiltins(Records, NumRecords);
  }
  virtual const char *getVAListDeclaration() const {
    return getX86_64VAListDeclaration();
  }
  virtual const char *getTargetPrefix() const {
    return X86::getTargetPrefix();
  }
  virtual void getGCCRegNames(const char * const *&Names, 
                                   unsigned &NumNames) const {
    X86::getGCCRegNames(Names, NumNames);
  }  
  virtual void getGCCRegAliases(const GCCRegAlias *&Aliases, 
                                unsigned &NumAliases) const {
    X86::getGCCRegAliases(Aliases, NumAliases);
  }
  virtual bool validateAsmConstraint(char c,
                                     TargetInfo::ConstraintInfo &info) const {
    return X86::validateAsmConstraint(c, info);
  }
  virtual std::string convertConstraint(const char Constraint) const {
    return X86::convertConstraint(Constraint);
  }
  virtual const char *getClobbers() const {
    return X86::getClobbers();
  }    
};
} // end anonymous namespace.

namespace {
class SolarisSparcV8TargetInfo : public SolarisTargetInfo {
public:
  SolarisSparcV8TargetInfo(const std::string& triple) : SolarisTargetInfo(triple) {}
  
  virtual void getTargetDefines(std::vector<char> &Defines) const {
    SolarisTargetInfo::getTargetDefines(Defines);
//    getSparcDefines(Defines, false);
    Define(Defines, "__sparc");
    Define(Defines, "__sparcv8");
  }
  virtual void getTargetBuiltins(const Builtin::Info *&Records,
                                 unsigned &NumRecords) const {
    PPC::getBuiltins(Records, NumRecords);
  }
  virtual const char *getVAListDeclaration() const {
    return getPPCVAListDeclaration();
  }
  virtual const char *getTargetPrefix() const {
    return PPC::getTargetPrefix();
  }
  virtual void getGCCRegNames(const char * const *&Names, 
                              unsigned &NumNames) const {
    PPC::getGCCRegNames(Names, NumNames);
  }
  virtual void getGCCRegAliases(const GCCRegAlias *&Aliases, 
                                unsigned &NumAliases) const {
    PPC::getGCCRegAliases(Aliases, NumAliases);
  }
  virtual bool validateAsmConstraint(char c,
                                     TargetInfo::ConstraintInfo &info) const {
    return PPC::validateAsmConstraint(c, info);
  }
  virtual const char *getClobbers() const {
    return PPC::getClobbers();
  }
};

} // end anonymous namespace.

namespace {
class LinuxTargetInfo : public DarwinTargetInfo {
public:
  LinuxTargetInfo(const std::string& triple) : DarwinTargetInfo(triple) {
    // Note: I have no idea if this is right, just for testing.
    WCharWidth = 16;
    WCharAlign = 16;
  }
  
  virtual void getTargetDefines(std::vector<char> &Defines) const {
    // TODO: linux-specific stuff.
    getX86Defines(Defines, false);
  }
  virtual void getTargetBuiltins(const Builtin::Info *&Records,
                                 unsigned &NumRecords) const {
    X86::getBuiltins(Records, NumRecords);
  }
  virtual const char *getVAListDeclaration() const {
    return getI386VAListDeclaration();
  }
  virtual const char *getTargetPrefix() const {
    return X86::getTargetPrefix();
  }  
  virtual void getGCCRegNames(const char * const *&Names, 
                                   unsigned &NumNames) const {
    X86::getGCCRegNames(Names, NumNames);
  }  
  virtual void getGCCRegAliases(const GCCRegAlias *&Aliases, 
                                unsigned &NumAliases) const {
    X86::getGCCRegAliases(Aliases, NumAliases);
  }
  virtual bool validateAsmConstraint(char c,
                                     TargetInfo::ConstraintInfo &info) const {
    return X86::validateAsmConstraint(c, info);
  }
  virtual std::string convertConstraint(const char Constraint) const {
    return X86::convertConstraint(Constraint);
  }
  virtual const char *getClobbers() const {
    return X86::getClobbers();
  }  
};
} // end anonymous namespace.


//===----------------------------------------------------------------------===//
// Driver code
//===----------------------------------------------------------------------===//

static inline bool IsX86(const std::string& TT) {
  return (TT.size() >= 5 && TT[0] == 'i' && TT[2] == '8' && TT[3] == '6' &&
          TT[4] == '-' && TT[1] - '3' < 6);
}

/// CreateTarget - Create the TargetInfoImpl object for the specified target
/// enum value.
static TargetInfoImpl *CreateTarget(const std::string& T) {
  if (T.find("ppc-") == 0 || T.find("powerpc-") == 0)
    return new DarwinPPCTargetInfo(T);
  else if (T.find("ppc64-") == 0 || T.find("powerpc64-") == 0)
    return new DarwinPPC64TargetInfo(T);
  else if (T.find("sparc-") == 0)
      return new SolarisSparcV8TargetInfo(T); // ugly hack
  else if (T.find("x86_64-") == 0)
    return new DarwinX86_64TargetInfo(T);
  else if (IsX86(T))
    return new DarwinI386TargetInfo(T);
  else if (T.find("bogusW16W16-") == 0) // For testing portability.
    return new LinuxTargetInfo(T);
  else
    return NULL;
}

/// CreateTargetInfo - Return the set of target info objects as specified by
/// the -arch command line option.
TargetInfo* TargetInfo::CreateTargetInfo(const std::string* TriplesStart,
                                         const std::string* TriplesEnd,
                                         Diagnostic *Diags) {

  // Create the primary target and target info.
  TargetInfoImpl* PrimaryTarget = CreateTarget(*TriplesStart);

  if (!PrimaryTarget)
    return NULL;
  
  TargetInfo *TI = new TargetInfo(PrimaryTarget, Diags);
  
  // Add all secondary targets.
  for (const std::string* I=TriplesStart+1; I != TriplesEnd; ++I) {
    TargetInfoImpl* SecondaryTarget = CreateTarget(*I);

    if (!SecondaryTarget) {
      fprintf (stderr,
               "Warning: secondary target '%s' unrecognized.\n", 
               I->c_str());

      continue;
    }

    TI->AddSecondaryTarget(SecondaryTarget);
  }
  
  return TI;
}
