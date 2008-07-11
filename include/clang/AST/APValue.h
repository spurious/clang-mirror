//===--- APValue.h - Union class for APFloat/APSInt/Complex -----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the APValue class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_APVALUE_H
#define LLVM_CLANG_AST_APVALUE_H

#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/APFloat.h"

namespace clang {
  class Expr;

/// APValue - This class implements a discriminated union of [uninitialized]
/// [APSInt] [APFloat], [Complex APSInt] [Complex APFloat], [Expr + Offset].
class APValue {
  typedef llvm::APSInt APSInt;
  typedef llvm::APFloat APFloat;
public:
  enum ValueKind {
    Uninitialized,
    SInt,
    Float,
    ComplexSInt,
    ComplexFloat,
    LValue
  };
private:
  ValueKind Kind;
  
  struct ComplexAPSInt { 
    APSInt Real, Imag; 
    ComplexAPSInt() : Real(1), Imag(1) {}
  };
  struct ComplexAPFloat {
    APFloat Real, Imag;
    ComplexAPFloat() : Real(0.0), Imag(0.0) {}
  };
  
  struct LV {
    Expr* Base;
    uint64_t Offset;
  };
  
  enum {
    MaxSize = (sizeof(ComplexAPSInt) > sizeof(ComplexAPFloat) ? 
               sizeof(ComplexAPSInt) : sizeof(ComplexAPFloat))
  };
  
  /// Data - space for the largest member in units of void*.  This is an effort
  /// to ensure that the APSInt/APFloat values have proper alignment.
  void *Data[(MaxSize+sizeof(void*)-1)/sizeof(void*)];
  
public:
  APValue() : Kind(Uninitialized) {}
  explicit APValue(const APSInt &I) : Kind(Uninitialized) {
    MakeSInt(); setSInt(I);
  }
  explicit APValue(const APFloat &F) : Kind(Uninitialized) {
    MakeFloat(); setFloat(F);
  }
  APValue(const APSInt &R, const APSInt &I) : Kind(Uninitialized) {
    MakeComplexSInt(); setComplexSInt(R, I);
  }
  APValue(const APFloat &R, const APFloat &I) : Kind(Uninitialized) {
    MakeComplexFloat(); setComplexFloat(R, I);
  }
  APValue(const APValue &RHS) : Kind(Uninitialized) {
    *this = RHS;
  }
  APValue(Expr* B, uint64_t O) : Kind(Uninitialized) {
    MakeLValue(); setLValue(B, O);
  }
  ~APValue() {
    MakeUninit();
  }
  
  ValueKind getKind() const { return Kind; }
  bool isUninit() const { return Kind == Uninitialized; }
  bool isSInt() const { return Kind == SInt; }
  bool isFloat() const { return Kind == Float; }
  bool isComplexSInt() const { return Kind == ComplexSInt; }
  bool isComplexFloat() const { return Kind == ComplexFloat; }
  bool isLValue() const { return Kind == LValue; }
  
  APSInt &getSInt() {
    assert(isSInt() && "Invalid accessor");
    return *(APSInt*)(void*)Data;
  }
  const APSInt &getSInt() const {
    return const_cast<APValue*>(this)->getSInt();
  }
  
  APFloat &getFloat() {
    assert(isFloat() && "Invalid accessor");
    return *(APFloat*)(void*)Data;
  }
  const APFloat &getFloat() const {
    return const_cast<APValue*>(this)->getFloat();
  }
  
  APSInt &getComplexSIntReal() {
    assert(isComplexSInt() && "Invalid accessor");
    return ((ComplexAPSInt*)(void*)Data)->Real;
  }
  const APSInt &getComplexSIntReal() const {
    return const_cast<APValue*>(this)->getComplexSIntReal();
  }
  
  APSInt &getComplexSIntImag() {
    assert(isComplexSInt() && "Invalid accessor");
    return ((ComplexAPSInt*)(void*)Data)->Imag;
  }
  const APSInt &getComplexSIntImag() const {
    return const_cast<APValue*>(this)->getComplexSIntImag();
  }
  
  APFloat &getComplexFloatReal() {
    assert(isComplexFloat() && "Invalid accessor");
    return ((ComplexAPFloat*)(void*)Data)->Real;
  }
  const APFloat &getComplexFloatReal() const {
    return const_cast<APValue*>(this)->getComplexFloatReal();
  }

  APFloat &getComplexFloatImag() {
    assert(isComplexFloat() && "Invalid accessor");
    return ((ComplexAPFloat*)(void*)Data)->Imag;
  }
  const APFloat &getComplexFloatImag() const {
    return const_cast<APValue*>(this)->getComplexFloatImag();
  }

  Expr* getLValueBase() const {
    assert(isLValue() && "Invalid accessor");
    return ((const LV*)(const void*)Data)->Base;
  }
  uint64_t getLValueOffset() const {
    assert(isLValue() && "Invalid accessor");
    return ((const LV*)(const void*)Data)->Offset;
  }
  
  void setSInt(const APSInt &I) {
    assert(isSInt() && "Invalid accessor");
    *(APSInt*)(void*)Data = I;
  }
  void setFloat(const APFloat &F) {
    assert(isFloat() && "Invalid accessor");
    *(APFloat*)(void*)Data = F;
  }
  void setComplexSInt(const APSInt &R, const APSInt &I) {
    assert(isComplexSInt() && "Invalid accessor");
    ((ComplexAPSInt*)(void*)Data)->Real = R;
    ((ComplexAPSInt*)(void*)Data)->Imag = I;
  }
  void setComplexFloat(const APFloat &R, const APFloat &I) {
    assert(isComplexFloat() && "Invalid accessor");
    ((ComplexAPFloat*)(void*)Data)->Real = R;
    ((ComplexAPFloat*)(void*)Data)->Imag = I;
  }
  void setLValue(Expr *B, uint64_t O) {
    assert(isLValue() && "Invalid accessor");
    ((LV*)(void*)Data)->Base = B;
    ((LV*)(void*)Data)->Offset = O;
  }
  
  const APValue &operator=(const APValue &RHS) {
    if (Kind != RHS.Kind) {
      MakeUninit();
      if (RHS.isSInt())
        MakeSInt();
      else if (RHS.isFloat())
        MakeFloat();
      else if (RHS.isComplexSInt())
        MakeComplexSInt();
      else if (RHS.isComplexFloat())
        MakeComplexFloat();
      else if (RHS.isLValue())
        MakeLValue();
    }
    if (isSInt())
      setSInt(RHS.getSInt());
    else if (isFloat())
      setFloat(RHS.getFloat());
    else if (isComplexSInt())
      setComplexSInt(RHS.getComplexSIntReal(), RHS.getComplexSIntImag());
    else if (isComplexFloat())
      setComplexFloat(RHS.getComplexFloatReal(), RHS.getComplexFloatImag());
    else if (isLValue())
      setLValue(RHS.getLValueBase(), RHS.getLValueOffset());
    return *this;
  }
  
private:
  void MakeUninit() {
    if (Kind == SInt)
      ((APSInt*)(void*)Data)->~APSInt();
    else if (Kind == Float)
      ((APFloat*)(void*)Data)->~APFloat();
    else if (Kind == ComplexSInt)
      ((ComplexAPSInt*)(void*)Data)->~ComplexAPSInt();
    else if (Kind == ComplexFloat)
      ((ComplexAPFloat*)(void*)Data)->~ComplexAPFloat();
    else if (Kind == LValue) {
      ((LV*)(void*)Data)->~LV();
    }
  }
  void MakeSInt() {
    assert(isUninit() && "Bad state change");
    new ((void*)Data) APSInt(1);
    Kind = SInt;
  }
  void MakeFloat() {
    assert(isUninit() && "Bad state change");
    new ((APFloat*)(void*)Data) APFloat(0.0);
    Kind = Float;
  }
  void MakeComplexSInt() {
    assert(isUninit() && "Bad state change");
    new ((ComplexAPSInt*)(void*)Data) ComplexAPSInt();
    Kind = ComplexSInt;
  }
  void MakeComplexFloat() {
    assert(isUninit() && "Bad state change");
    new ((ComplexAPFloat*)(void*)Data) ComplexAPFloat();
    Kind = ComplexFloat;
  }
  void MakeLValue() {
    assert(isUninit() && "Bad state change");
    new ((LV*)(void*)Data) LV();
    Kind = LValue;
  }
};

}

#endif
