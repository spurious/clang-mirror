//===--- Integral.h - Wrapper for numeric types for the VM ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Defines the VM types and helpers operating on types.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_INTERP_NUMBER_H
#define LLVM_CLANG_AST_INTERP_NUMBER_H

#include "clang/AST/ComparisonCategories.h"
#include "clang/AST/APValue.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include <cstddef>
#include <cstdint>

namespace clang {
namespace interp {

using APInt = llvm::APInt;
using APSInt = llvm::APSInt;

/// Helper to compare two comparable types.
template <typename T>
ComparisonCategoryResult Compare(const T &X, const T &Y) {
  if (X < Y)
    return ComparisonCategoryResult::Less;
  if (X > Y)
    return ComparisonCategoryResult::Greater;
  return ComparisonCategoryResult::Equal;
}


// Helper structure to select the representation.
template <unsigned Bits, bool Signed> struct Repr;

template <> struct Repr<1, false> { using Type = bool; };
template <> struct Repr<8, false> { using Type = uint8_t; };
template <> struct Repr<16, false> { using Type = uint16_t; };
template <> struct Repr<32, false> { using Type = uint32_t; };
template <> struct Repr<64, false> { using Type = uint64_t; };
template <> struct Repr<8, true> { using Type = int8_t; };
template <> struct Repr<16, true> { using Type = int16_t; };
template <> struct Repr<32, true> { using Type = int32_t; };
template <> struct Repr<64, true> { using Type = int64_t; };


/// Helper structure to select the min and max value.
template <unsigned Bits, bool Signed> struct IntegralLimits {
  using Type = typename Repr<Bits, Signed>::Type;
  static const auto Min = std::numeric_limits<Type>::min();
  static const auto Max = std::numeric_limits<Type>::max();
};

template <> struct IntegralLimits<1, false> {
  static const bool Min = false;
  static const bool Max = true;
};

/// Wrapper around numeric types.
///
/// These wrappers are required to shared an interface between APSint and
/// builtin primitive numeral types, while optimising for storage and
/// allowing methods operating on primitive type to compile to fast code.
template <unsigned Bits, bool Signed> class Integral {
private:
  template <unsigned OtherBits, bool OtherSigned> friend class Integral;


  // The primitive representing the integral.
  using T = typename Repr<Bits, Signed>::Type;
  T V;

  /// Primitive representing limits.
  using Limit = IntegralLimits<Bits, Signed>;

  /// Construct an integral from anything that is convertible to storage.
  template <typename T> explicit Integral(T V) : V(V) {}

public:
  /// Zero-initializes an integral.
  Integral() : V(0) {}

  /// Constructs an integral from another integral.
  template <unsigned SrcBits, bool SrcSign>
  explicit Integral(Integral<SrcBits, SrcSign> V) : V(V.V) {}

  /// Construct an integral from a value based on signedness.
  explicit Integral(const APSInt &V)
      : V(V.isSigned() ? V.getSExtValue() : V.getZExtValue()) {}

  bool operator<(Integral RHS) const { return V < RHS.V; }
  bool operator>(Integral RHS) const { return V > RHS.V; }
  bool operator<=(Integral RHS) const { return V <= RHS.V; }
  bool operator>=(Integral RHS) const { return V >= RHS.V; }
  bool operator==(Integral RHS) const { return V == RHS.V; }
  bool operator!=(Integral RHS) const { return V != RHS.V; }

  bool operator>(unsigned RHS) const {
    return V >= 0 && static_cast<unsigned>(V) > RHS;
  }

  Integral operator+(Integral RHS) const { return Integral(V + RHS.V); }
  Integral operator-(Integral RHS) const { return Integral(V - RHS.V); }
  Integral operator*(Integral RHS) const { return Integral(V * RHS.V); }
  Integral operator/(Integral RHS) const { return Integral(V / RHS.V); }
  Integral operator%(Integral RHS) const { return Integral(V % RHS.V); }
  Integral operator&(Integral RHS) const { return Integral(V & RHS.V); }
  Integral operator|(Integral RHS) const { return Integral(V | RHS.V); }
  Integral operator^(Integral RHS) const { return Integral(V ^ RHS.V); }

  Integral operator-() const { return Integral(-V); }
  Integral operator~() const { return Integral(~V); }

  Integral operator>>(unsigned RHS) const { return Integral(V >> RHS); }
  Integral operator<<(unsigned RHS) const { return Integral(V << RHS); }

  template <unsigned DstBits, bool DstSign>
  explicit operator Integral<DstBits, DstSign>() const {
    return Integral<DstBits, DstSign>(V);
  }

  explicit operator unsigned() const { return V; }
  explicit operator int64_t() const { return V; }
  explicit operator uint64_t() const { return V; }

  APSInt toAPSInt() const {
    return APSInt(APInt(Bits, static_cast<uint64_t>(V), Signed), !Signed);
  }
  APSInt toAPSInt(unsigned NumBits) const {
    if (Signed)
      return APSInt(toAPSInt().sextOrTrunc(NumBits), !Signed);
    else
      return APSInt(toAPSInt().zextOrTrunc(NumBits), !Signed);
  }
  APValue toAPValue() const { return APValue(toAPSInt()); }

  Integral<Bits, false> toUnsigned() const {
    return Integral<Bits, false>(*this);
  }

  constexpr static unsigned bitWidth() { return Bits; }

  bool isZero() const { return !V; }

  bool isMin() const { return *this == min(bitWidth()); }

  bool isMinusOne() const { return Signed && V == T(-1); }

  constexpr static bool isSigned() { return Signed; }

  bool isNegative() const { return V < T(0); }
  bool isPositive() const { return !isNegative(); }

  ComparisonCategoryResult compare(const Integral &RHS) const {
    return Compare(V, RHS.V);
  }

  unsigned countLeadingZeros() const {
    unsigned LeadingZeros = __builtin_clzll(V);
    constexpr auto FullBits = std::numeric_limits<unsigned long long>::digits;
    return LeadingZeros - (FullBits - bitWidth());
  }

  Integral truncate(unsigned TruncBits) const {
    if (TruncBits >= Bits)
      return *this;
    const T BitMask = (T(1) << T(TruncBits)) - 1;
    const T SignBit = T(1) << (TruncBits - 1);
    const T ExtMask = ~BitMask;
    return Integral((V & BitMask) | (Signed && (V & SignBit) ? ExtMask : 0));
  }

  void print(llvm::raw_ostream &OS) const { OS << V; }

  static Integral min(unsigned NumBits) {
    return Integral(Limit::Min);
  }
  static Integral max(unsigned NumBits) {
    return Integral(Limit::Max);
  }

  template <typename T>
  static typename std::enable_if<std::is_integral<T>::value, Integral>::type
  from(T Value) {
    return Integral(Value);
  }

  template <unsigned SrcBits, bool SrcSign>
  static typename std::enable_if<SrcBits != 0, Integral>::type
  from(Integral<SrcBits, SrcSign> Value) {
    return Integral(Value.V);
  }

  template <bool SrcSign> static Integral from(Integral<0, SrcSign> Value) {
    if (SrcSign)
      return Integral(Value.V.getSExtValue());
    else
      return Integral(Value.V.getZExtValue());
  }

  static Integral zero() { return from(0); }

  template <typename T> static Integral from(T Value, unsigned NumBits) {
    return Integral(Value);
  }

  static bool inRange(int64_t Value, unsigned NumBits) {
    return CheckRange<T, Limit::Min, Limit::Max>(Value);
  }

  static bool increment(Integral A, Integral *R) {
    return add(A, Integral(T(1)), A.bitWidth(), R);
  }

  static bool decrement(Integral A, Integral *R) {
    return sub(A, Integral(T(1)), A.bitWidth(), R);
  }

  static bool add(Integral A, Integral B, unsigned OpBits, Integral *R) {
    return CheckAddUB(A.V, B.V, R->V);
  }

  static bool sub(Integral A, Integral B, unsigned OpBits, Integral *R) {
    return CheckSubUB(A.V, B.V, R->V);
  }

  static bool mul(Integral A, Integral B, unsigned OpBits, Integral *R) {
    return CheckMulUB(A.V, B.V, R->V);
  }

private:
  template <typename T>
  static typename std::enable_if<std::is_signed<T>::value, bool>::type
  CheckAddUB(T A, T B, T &R) {
    return llvm::AddOverflow<T>(A, B, R);
  }

  template <typename T>
  static typename std::enable_if<std::is_unsigned<T>::value, bool>::type
  CheckAddUB(T A, T B, T &R) {
    R = A + B;
    return false;
  }

  template <typename T>
  static typename std::enable_if<std::is_signed<T>::value, bool>::type
  CheckSubUB(T A, T B, T &R) {
    return llvm::SubOverflow<T>(A, B, R);
  }

  template <typename T>
  static typename std::enable_if<std::is_unsigned<T>::value, bool>::type
  CheckSubUB(T A, T B, T &R) {
    R = A - B;
    return false;
  }

  template <typename T>
  static typename std::enable_if<std::is_signed<T>::value, bool>::type
  CheckMulUB(T A, T B, T &R) {
    return llvm::MulOverflow<T>(A, B, R);
  }

  template <typename T>
  static typename std::enable_if<std::is_unsigned<T>::value, bool>::type
  CheckMulUB(T A, T B, T &R) {
    R = A * B;
    return false;
  }

  template <typename T, T Min, T Max>
  static typename std::enable_if<std::is_signed<T>::value, bool>::type
  CheckRange(int64_t V) {
    return Min <= V && V <= Max;
  }

  template <typename T, T Min, T Max>
  static typename std::enable_if<std::is_unsigned<T>::value, bool>::type
  CheckRange(int64_t V) {
    return V >= 0 && static_cast<uint64_t>(V) <= Max;
  }
};

template <unsigned Bits, bool Signed>
llvm::raw_ostream &operator<<(llvm::raw_ostream &OS, Integral<Bits, Signed> I) {
  I.print(OS);
  return OS;
}

template <>
inline llvm::raw_ostream &operator<<(llvm::raw_ostream &OS,
                                     Integral<1, false> B) {
  OS << (B.isZero() ? "false" : "true");
  return OS;
}

} // namespace interp
} // namespace clang

#endif
