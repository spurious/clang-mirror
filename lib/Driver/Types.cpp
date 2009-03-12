//===--- Types.cpp - Driver input & temporary type information ----------*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "clang/Driver/Types.h"

#include <string.h>
#include <cassert>

using namespace clang::driver;
using namespace clang::driver::types;

struct Info {
  const char *Name;
  const char *Flags;
  const char *TempSuffix;
  ID PreprocessedType;
};

static Info TypeInfos[] = {
#define TYPE(NAME, ID, PP_TYPE, TEMP_SUFFIX, FLAGS) \
  { NAME, FLAGS, TEMP_SUFFIX, TY_##PP_TYPE, },
#include "clang/Driver/Types.def"
#undef TYPE
};
static const unsigned numTypes = sizeof(TypeInfos) / sizeof(TypeInfos[0]);

static Info &getInfo(unsigned id) {
  assert(id > 0 && id - 1 < numTypes && "Invalid Type ID.");
  return TypeInfos[id - 1];
}

const char *types::getTypeName(ID Id) { 
  return getInfo(Id).Name; 
}

types::ID types::getPreprocessedType(ID Id) { 
  return getInfo(Id).PreprocessedType; 
}

const char *types::getTypeTempSuffix(ID Id) { 
  return getInfo(Id).TempSuffix; 
}

bool types::onlyAssembleType(ID Id) { 
  return strchr(getInfo(Id).Flags, 'a'); 
}

bool types::onlyPrecompileType(ID Id) { 
  return strchr(getInfo(Id).Flags, 'p'); 
}

bool types::canTypeBeUserSpecified(ID Id) { 
  return strchr(getInfo(Id).Flags, 'u'); 
}

bool types::appendSuffixForType(ID Id) { 
  return strchr(getInfo(Id).Flags, 'A'); 
}

bool types::canLipoType(ID Id) { 
  return (Id == TY_Nothing ||
          Id == TY_Image ||
          Id == TY_Object); 
}

types::ID types::lookupTypeForExtension(const char *Ext) {
  unsigned N = strlen(Ext);

  switch (N) {
  case 1:
    if (memcmp(Ext, "c", 1) == 0) return TY_C;
    if (memcmp(Ext, "i", 1) == 0) return TY_PP_C;
    if (memcmp(Ext, "m", 1) == 0) return TY_ObjC;
    if (memcmp(Ext, "M", 1) == 0) return TY_ObjCXX;
    if (memcmp(Ext, "h", 1) == 0) return TY_CHeader;
    if (memcmp(Ext, "C", 1) == 0) return TY_CXX;
    if (memcmp(Ext, "H", 1) == 0) return TY_CXXHeader;
    if (memcmp(Ext, "f", 1) == 0) return TY_PP_Fortran;
    if (memcmp(Ext, "F", 1) == 0) return TY_Fortran;
    if (memcmp(Ext, "s", 1) == 0) return TY_PP_Asm;
    if (memcmp(Ext, "S", 1) == 0) return TY_Asm;
  case 2:
    if (memcmp(Ext, "ii", 2) == 0) return TY_PP_CXX;
    if (memcmp(Ext, "mi", 2) == 0) return TY_PP_ObjC;
    if (memcmp(Ext, "mm", 2) == 0) return TY_ObjCXX;
    if (memcmp(Ext, "cc", 2) == 0) return TY_CXX;
    if (memcmp(Ext, "cc", 2) == 0) return TY_CXX;
    if (memcmp(Ext, "cp", 2) == 0) return TY_CXX;
    if (memcmp(Ext, "hh", 2) == 0) return TY_CXXHeader;
    break;
  case 3:
    if (memcmp(Ext, "ads", 3) == 0) return TY_Ada;
    if (memcmp(Ext, "adb", 3) == 0) return TY_Ada;
    if (memcmp(Ext, "cxx", 3) == 0) return TY_CXX;
    if (memcmp(Ext, "cpp", 3) == 0) return TY_CXX;
    if (memcmp(Ext, "CPP", 3) == 0) return TY_CXX;
    if (memcmp(Ext, "cXX", 3) == 0) return TY_CXX;
    if (memcmp(Ext, "for", 3) == 0) return TY_PP_Fortran;
    if (memcmp(Ext, "FOR", 3) == 0) return TY_PP_Fortran;
    if (memcmp(Ext, "fpp", 3) == 0) return TY_Fortran;
    if (memcmp(Ext, "FPP", 3) == 0) return TY_Fortran;
    if (memcmp(Ext, "f90", 3) == 0) return TY_PP_Fortran;
    if (memcmp(Ext, "f95", 3) == 0) return TY_PP_Fortran;
    if (memcmp(Ext, "F90", 3) == 0) return TY_Fortran;
    if (memcmp(Ext, "F95", 3) == 0) return TY_Fortran;
    if (memcmp(Ext, "mii", 3) == 0) return TY_PP_ObjCXX;
    break;
  }

  return TY_INVALID;
}

types::ID types::lookupTypeForTypeSpecifier(const char *Name) {
  unsigned N = strlen(Name);

  for (unsigned i=0; i<numTypes; ++i) {
    types::ID Id = (types::ID) (i + 1);
    if (canTypeBeUserSpecified(Id) && 
        memcmp(Name, getInfo(Id).Name, N) == 0)
      return Id;
  }

  return TY_INVALID;
}
