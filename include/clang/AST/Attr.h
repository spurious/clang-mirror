//===--- Attr.h - Classes for representing expressions ----------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the Attr interface and subclasses.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_ATTR_H
#define LLVM_CLANG_AST_ATTR_H

#include "llvm/GlobalValue.h"
#include <cassert>
#include <string>

namespace clang {

/// Attr - This represents one attribute.
class Attr {
public:
  enum Kind {
    Aligned,
    Packed,
    Annotate,
    NoReturn,
    Deprecated,
    Weak,
    DLLImport,
    DLLExport,
    NoThrow,
    Format,
    Visibility,
    FastCall,
    StdCall
  };
    
private:
  Attr *Next;
  Kind AttrKind;
  
protected:
  Attr(Kind AK) : Next(0), AttrKind(AK) {}
public:
  virtual ~Attr() {
    delete Next;
  }

  Kind getKind() const { return AttrKind; }

  Attr *getNext() { return Next; }
  const Attr *getNext() const { return Next; }
  void setNext(Attr *next) { Next = next; }
  
  void addAttr(Attr *attr) {
    assert((attr != 0) && "addAttr(): attr is null");
    
    // FIXME: This doesn't preserve the order in any way.
    attr->Next = Next;
    Next = attr;
  }
  
  // Implement isa/cast/dyncast/etc.
  static bool classof(const Attr *) { return true; }
};

class PackedAttr : public Attr {
public:
  PackedAttr() : Attr(Packed) {}
  
  // Implement isa/cast/dyncast/etc.
  static bool classof(const Attr *A) {
    return A->getKind() == Packed;
  }
  static bool classof(const PackedAttr *A) { return true; }
};
  
class AlignedAttr : public Attr {
  unsigned Alignment;
public:
  AlignedAttr(unsigned alignment) : Attr(Aligned), Alignment(alignment) {}
  
  unsigned getAlignment() const { return Alignment; }
  
  // Implement isa/cast/dyncast/etc.
  static bool classof(const Attr *A) {
    return A->getKind() == Aligned;
  }
  static bool classof(const AlignedAttr *A) { return true; }
};

class AnnotateAttr : public Attr {
  std::string Annotation;
public:
  AnnotateAttr(const std::string &ann) : Attr(Annotate), Annotation(ann) {}
  
  const std::string& getAnnotation() const { return Annotation; }
  
  // Implement isa/cast/dyncast/etc.
  static bool classof(const Attr *A) {
    return A->getKind() == Annotate;
  }
  static bool classof(const AnnotateAttr *A) { return true; }
};
  
class NoReturnAttr : public Attr {
public:
  NoReturnAttr() : Attr(NoReturn) {}
  
  // Implement isa/cast/dyncast/etc.
  
  static bool classof(const Attr *A) { return A->getKind() == NoReturn; }  
  static bool classof(const NoReturnAttr *A) { return true; }
};

class DeprecatedAttr : public Attr {
public:
  DeprecatedAttr() : Attr(Deprecated) {}

  // Implement isa/cast/dyncast/etc.

  static bool classof(const Attr *A) { return A->getKind() == Deprecated; }
  static bool classof(const DeprecatedAttr *A) { return true; }
};

class WeakAttr : public Attr {
public:
  WeakAttr() : Attr(Weak) {}

  // Implement isa/cast/dyncast/etc.

  static bool classof(const Attr *A) { return A->getKind() == Weak; }
  static bool classof(const WeakAttr *A) { return true; }
};

class NoThrowAttr : public Attr {
public:
  NoThrowAttr() : Attr(NoThrow) {}

  // Implement isa/cast/dyncast/etc.

  static bool classof(const Attr *A) { return A->getKind() == NoThrow; }
  static bool classof(const NoThrowAttr *A) { return true; }
};

class FormatAttr : public Attr {
  std::string Type;
  int formatIdx, firstArg;
public:
  FormatAttr(const std::string &type, int idx, int first) : Attr(Format),
             Type(type), formatIdx(idx), firstArg(first) {}

  const std::string& getType() const { return Type; }
  int getFormatIdx() const { return formatIdx; }
  int getFirstArg() const { return firstArg; }

  // Implement isa/cast/dyncast/etc.

  static bool classof(const Attr *A) { return A->getKind() == Format; }
  static bool classof(const FormatAttr *A) { return true; }
};

class VisibilityAttr : public Attr {
  llvm::GlobalValue::VisibilityTypes VisibilityType;
public:
  VisibilityAttr(llvm::GlobalValue::VisibilityTypes v) : Attr(Visibility),
                 VisibilityType(v) {}

  llvm::GlobalValue::VisibilityTypes getVisibility() const { return VisibilityType; }

  // Implement isa/cast/dyncast/etc.

  static bool classof(const Attr *A) { return A->getKind() == Visibility; }
  static bool classof(const VisibilityAttr *A) { return true; }
};

class DLLImportAttr : public Attr {
public:
  DLLImportAttr() : Attr(DLLImport) {}

  // Implement isa/cast/dyncast/etc.

  static bool classof(const Attr *A) { return A->getKind() == DLLImport; }
  static bool classof(const DLLImportAttr *A) { return true; }
};

class DLLExportAttr : public Attr {
public:
  DLLExportAttr() : Attr(DLLExport) {}

  // Implement isa/cast/dyncast/etc.

  static bool classof(const Attr *A) { return A->getKind() == DLLExport; }
  static bool classof(const DLLExportAttr *A) { return true; }
};

class FastCallAttr : public Attr {
public:
  FastCallAttr() : Attr(FastCall) {}

  // Implement isa/cast/dyncast/etc.

  static bool classof(const Attr *A) { return A->getKind() == FastCall; }
  static bool classof(const FastCallAttr *A) { return true; }
};

class StdCallAttr : public Attr {
public:
  StdCallAttr() : Attr(StdCall) {}

  // Implement isa/cast/dyncast/etc.

  static bool classof(const Attr *A) { return A->getKind() == StdCall; }
  static bool classof(const StdCallAttr *A) { return true; }
};

}  // end namespace clang

#endif
