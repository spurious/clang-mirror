//===--- APValue.cpp - Union class for APFloat/APSInt/Complex -------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file implements the APValue class.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/APValue.h"
#include "clang/AST/CharUnits.h"
#include "clang/Basic/Diagnostic.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/ErrorHandling.h"
using namespace clang;

namespace {
  struct LVBase {
    const Expr *Base;
    CharUnits Offset;
    unsigned PathLength;
  };
}

struct APValue::LV : LVBase {
  static const unsigned InlinePathSpace =
      (MaxSize - sizeof(LVBase)) / sizeof(LValuePathEntry);

  /// Path - The sequence of base classes, fields and array indices to follow to
  /// walk from Base to the subobject. When performing GCC-style folding, there
  /// may not be such a path.
  union {
    LValuePathEntry Path[InlinePathSpace];
    LValuePathEntry *PathPtr;
  };

  LV() { PathLength = (unsigned)-1; }
  ~LV() { if (hasPathPtr()) delete [] PathPtr; }

  void allocPath() {
    if (hasPathPtr()) PathPtr = new LValuePathEntry[PathLength];
  }

  bool hasPath() const { return PathLength != (unsigned)-1; }
  bool hasPathPtr() const { return hasPath() && PathLength > InlinePathSpace; }

  LValuePathEntry *getPath() { return hasPathPtr() ? PathPtr : Path; }
};

APValue::APValue(const Expr* B) : Kind(Uninitialized) {
  MakeLValue();
  setLValue(B, CharUnits::Zero(), ArrayRef<LValuePathEntry>());
}

const APValue &APValue::operator=(const APValue &RHS) {
  if (Kind != RHS.Kind) {
    MakeUninit();
    if (RHS.isInt())
      MakeInt();
    else if (RHS.isFloat())
      MakeFloat();
    else if (RHS.isVector())
      MakeVector();
    else if (RHS.isComplexInt())
      MakeComplexInt();
    else if (RHS.isComplexFloat())
      MakeComplexFloat();
    else if (RHS.isLValue())
      MakeLValue();
  }
  if (isInt())
    setInt(RHS.getInt());
  else if (isFloat())
    setFloat(RHS.getFloat());
  else if (isVector())
    setVector(((const Vec *)(const char *)RHS.Data)->Elts,
              RHS.getVectorLength());
  else if (isComplexInt())
    setComplexInt(RHS.getComplexIntReal(), RHS.getComplexIntImag());
  else if (isComplexFloat())
    setComplexFloat(RHS.getComplexFloatReal(), RHS.getComplexFloatImag());
  else if (isLValue()) {
    if (RHS.hasLValuePath())
      setLValue(RHS.getLValueBase(), RHS.getLValueOffset(),RHS.getLValuePath());
    else
      setLValue(RHS.getLValueBase(), RHS.getLValueOffset(), NoLValuePath());
  }
  return *this;
}

void APValue::MakeUninit() {
  if (Kind == Int)
    ((APSInt*)(char*)Data)->~APSInt();
  else if (Kind == Float)
    ((APFloat*)(char*)Data)->~APFloat();
  else if (Kind == Vector)
    ((Vec*)(char*)Data)->~Vec();
  else if (Kind == ComplexInt)
    ((ComplexAPSInt*)(char*)Data)->~ComplexAPSInt();
  else if (Kind == ComplexFloat)
    ((ComplexAPFloat*)(char*)Data)->~ComplexAPFloat();
  else if (Kind == LValue) {
    ((LV*)(char*)Data)->~LV();
  }
  Kind = Uninitialized;
}

void APValue::dump() const {
  print(llvm::errs());
  llvm::errs() << '\n';
}

static double GetApproxValue(const llvm::APFloat &F) {
  llvm::APFloat V = F;
  bool ignored;
  V.convert(llvm::APFloat::IEEEdouble, llvm::APFloat::rmNearestTiesToEven,
            &ignored);
  return V.convertToDouble();
}

void APValue::print(raw_ostream &OS) const {
  switch (getKind()) {
  default: llvm_unreachable("Unknown APValue kind!");
  case Uninitialized:
    OS << "Uninitialized";
    return;
  case Int:
    OS << "Int: " << getInt();
    return;
  case Float:
    OS << "Float: " << GetApproxValue(getFloat());
    return;
  case Vector:
    OS << "Vector: " << getVectorElt(0);
    for (unsigned i = 1; i != getVectorLength(); ++i)
      OS << ", " << getVectorElt(i);
    return;
  case ComplexInt:
    OS << "ComplexInt: " << getComplexIntReal() << ", " << getComplexIntImag();
    return;
  case ComplexFloat:
    OS << "ComplexFloat: " << GetApproxValue(getComplexFloatReal())
       << ", " << GetApproxValue(getComplexFloatImag());
  case LValue:
    OS << "LValue: <todo>";
    return;
  }
}

static void WriteShortAPValueToStream(raw_ostream& Out,
                                      const APValue& V) {
  switch (V.getKind()) {
  default: llvm_unreachable("Unknown APValue kind!");
  case APValue::Uninitialized:
    Out << "Uninitialized";
    break;
  case APValue::Int:
    Out << V.getInt();
    break;
  case APValue::Float:
    Out << GetApproxValue(V.getFloat());
    break;
  case APValue::Vector:
    Out << '[';
    WriteShortAPValueToStream(Out, V.getVectorElt(0));
    for (unsigned i = 1; i != V.getVectorLength(); ++i) {
      Out << ", ";
      WriteShortAPValueToStream(Out, V.getVectorElt(i));
    }
    Out << ']';
    break;
  case APValue::ComplexInt:
    Out << V.getComplexIntReal() << "+" << V.getComplexIntImag() << "i";
    break;
  case APValue::ComplexFloat:
    Out << GetApproxValue(V.getComplexFloatReal()) << "+"
        << GetApproxValue(V.getComplexFloatImag()) << "i";
    break;
  case APValue::LValue:
    Out << "LValue: <todo>";
    break;
  }
}

const DiagnosticBuilder &clang::operator<<(const DiagnosticBuilder &DB,
                                           const APValue &V) {
  llvm::SmallString<64> Buffer;
  llvm::raw_svector_ostream Out(Buffer);
  WriteShortAPValueToStream(Out, V);
  return DB << Out.str();
}

const Expr* APValue::getLValueBase() const {
  assert(isLValue() && "Invalid accessor");
  return ((const LV*)(const void*)Data)->Base;
}

CharUnits &APValue::getLValueOffset() {
  assert(isLValue() && "Invalid accessor");
  return ((LV*)(void*)Data)->Offset;
}

bool APValue::hasLValuePath() const {
  assert(isLValue() && "Invalid accessor");
  return ((LV*)(char*)Data)->hasPath();
}

ArrayRef<APValue::LValuePathEntry> APValue::getLValuePath() const {
  assert(isLValue() && hasLValuePath() && "Invalid accessor");
  LV &LVal = *((LV*)(char*)Data);
  return ArrayRef<LValuePathEntry>(LVal.getPath(), LVal.PathLength);
}

void APValue::setLValue(const Expr *B, const CharUnits &O, NoLValuePath) {
  assert(isLValue() && "Invalid accessor");
  LV &LVal = *((LV*)(char*)Data);
  LVal.Base = B;
  LVal.Offset = O;
  LVal.PathLength = (unsigned)-1;
}

void APValue::setLValue(const Expr *B, const CharUnits &O,
                        ArrayRef<LValuePathEntry> Path) {
  assert(isLValue() && "Invalid accessor");
  LV &LVal = *((LV*)(char*)Data);
  LVal.Base = B;
  LVal.Offset = O;
  LVal.PathLength = Path.size();
  memcpy(LVal.getPath(), Path.data(), Path.size() * sizeof(LValuePathEntry));
}

void APValue::MakeLValue() {
  assert(isUninit() && "Bad state change");
  assert(sizeof(LV) <= MaxSize && "LV too big");
  new ((void*)(char*)Data) LV();
  Kind = LValue;
}
