//===- CIndexUSR.cpp - Clang-C Source Indexing Library --------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the generation and use of USRs from CXEntities.
//
//===----------------------------------------------------------------------===//

#include "CIndexer.h"
#include "CXCursor.h"
#include "CXString.h"
#include "clang/Index/USRGeneration.h"
#include "clang/Lex/PreprocessingRecord.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/raw_ostream.h"

using namespace clang;

//===----------------------------------------------------------------------===//
// API hooks.
//===----------------------------------------------------------------------===//

static inline StringRef extractUSRSuffix(StringRef s) {
  return s.startswith("c:") ? s.substr(2) : "";
}

bool cxcursor::getDeclCursorUSR(const Decl *D, SmallVectorImpl<char> &Buf) {
  return index::generateUSRForDecl(D, Buf);
}

extern "C" {

CXString clang_getCursorUSR(CXCursor C) {
  const CXCursorKind &K = clang_getCursorKind(C);

  if (clang_isDeclaration(K)) {
    const Decl *D = cxcursor::getCursorDecl(C);
    if (!D)
      return cxstring::createEmpty();

    CXTranslationUnit TU = cxcursor::getCursorTU(C);
    if (!TU)
      return cxstring::createEmpty();

    cxstring::CXStringBuf *buf = cxstring::getCXStringBuf(TU);
    if (!buf)
      return cxstring::createEmpty();

    bool Ignore = cxcursor::getDeclCursorUSR(D, buf->Data);
    if (Ignore) {
      buf->dispose();
      return cxstring::createEmpty();
    }

    // Return the C-string, but don't make a copy since it is already in
    // the string buffer.
    buf->Data.push_back('\0');
    return createCXString(buf);
  }

  if (K == CXCursor_MacroDefinition) {
    CXTranslationUnit TU = cxcursor::getCursorTU(C);
    if (!TU)
      return cxstring::createEmpty();

    cxstring::CXStringBuf *buf = cxstring::getCXStringBuf(TU);
    if (!buf)
      return cxstring::createEmpty();

    buf->Data += index::getUSRSpacePrefix();
    buf->Data += "macro@";
    buf->Data +=
        cxcursor::getCursorMacroDefinition(C)->getName()->getNameStart();
    buf->Data.push_back('\0');
    return createCXString(buf);
  }

  return cxstring::createEmpty();
}

CXString clang_constructUSR_ObjCIvar(const char *name, CXString classUSR) {
  SmallString<128> Buf(index::getUSRSpacePrefix());
  llvm::raw_svector_ostream OS(Buf);
  OS << extractUSRSuffix(clang_getCString(classUSR));
  index::generateUSRForObjCIvar(name, OS);
  return cxstring::createDup(OS.str());
}

CXString clang_constructUSR_ObjCMethod(const char *name,
                                       unsigned isInstanceMethod,
                                       CXString classUSR) {
  SmallString<128> Buf(index::getUSRSpacePrefix());
  llvm::raw_svector_ostream OS(Buf);
  OS << extractUSRSuffix(clang_getCString(classUSR));
  index::generateUSRForObjCMethod(name, isInstanceMethod, OS);
  return cxstring::createDup(OS.str());
}

CXString clang_constructUSR_ObjCClass(const char *name) {
  SmallString<128> Buf(index::getUSRSpacePrefix());
  llvm::raw_svector_ostream OS(Buf);
  index::generateUSRForObjCClass(name, OS);
  return cxstring::createDup(OS.str());
}

CXString clang_constructUSR_ObjCProtocol(const char *name) {
  SmallString<128> Buf(index::getUSRSpacePrefix());
  llvm::raw_svector_ostream OS(Buf);
  index::generateUSRForObjCProtocol(name, OS);
  return cxstring::createDup(OS.str());
}

CXString clang_constructUSR_ObjCCategory(const char *class_name,
                                         const char *category_name) {
  SmallString<128> Buf(index::getUSRSpacePrefix());
  llvm::raw_svector_ostream OS(Buf);
  index::generateUSRForObjCCategory(class_name, category_name, OS);
  return cxstring::createDup(OS.str());
}

CXString clang_constructUSR_ObjCProperty(const char *property,
                                         CXString classUSR) {
  SmallString<128> Buf(index::getUSRSpacePrefix());
  llvm::raw_svector_ostream OS(Buf);
  OS << extractUSRSuffix(clang_getCString(classUSR));
  index::generateUSRForObjCProperty(property, OS);
  return cxstring::createDup(OS.str());
}

} // end extern "C"
