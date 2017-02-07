// RUN: %clang_cc1 -std=c++1z %s -verify

// The same restrictions apply to the parameter-declaration-clause of a
// deduction guide as in a function declaration.
template<typename T> struct A {};
A(void) -> A<int>; // ok
A(void, int) -> A<int>; // expected-error {{'void' must be the first and only parameter if specified}}

// We interpret this as also extending to the validity of redeclarations. It's
// a bit of a stretch (OK, a lot of a stretch) but it gives desirable answers.
A() -> A<int>; // ok, redeclaration

A() -> A<int>; // expected-note {{previous}}
A() -> A<float>; // FIXME: "functions" is a poor term. expected-error {{functions that differ only in their return type cannot be overloaded}}

template<typename T> A(T) -> A<typename T::foo>;
template<typename T> A(T) -> A<typename T::bar>; // ok, can overload on return type (SFINAE applies)

A(long) -> A<int>;
template<typename T = int> A(long) -> A<char>; // ok, non-template beats template as usual

// (Pending DR) The template-name shall name a class template.
template<typename T> using B = A<T>; // expected-note {{template}}
B() -> B<int>; // expected-error {{cannot specify deduction guide for alias template 'B'}}
// FIXME: expected-error@-1 {{declarator requires an identifier}}
template<typename T> int C;
C() -> int; // expected-error {{requires a type specifier}}
template<typename T> void D();
D() -> int; // expected-error {{requires a type specifier}}
template<template<typename> typename TT> struct E { // expected-note 2{{template}}
  // FIXME: Should only diagnose this once!
  TT(int) -> TT<int>; // expected-error 2{{cannot specify deduction guide for template template parameter 'TT'}} expected-error {{requires an identifier}}
};

// FIXME: Even if the DR is applied as we hope, we should still warn if the
// trailing-return-type can obviously never produce a specialization of the
// named template.
A(int) -> int;
template<typename T> A(T) -> T*;

// A deduction-guide shall be declared in the same scope as the corresponding
// class template.
namespace WrongScope {
  namespace {
    template<typename T> struct AnonNS1 {};
    AnonNS1(float) -> AnonNS1<float>; // ok
  }
  AnonNS1(int) -> AnonNS1<int>; // FIXME
  template<typename T> struct AnonNS2 {};
  namespace {
    AnonNS1(char) -> AnonNS1<char>; // ok
    AnonNS2(int) -> AnonNS2<int>; // FIXME
  }
  namespace N {
    template<typename T> struct NamedNS1 {};
    template<typename T> struct NamedNS2 {};
  }
  using N::NamedNS1;
  NamedNS1(int) -> NamedNS1<int>; // FIXME
  using namespace N;
  NamedNS2(int) -> NamedNS2<int>; // FIXME
  struct ClassMemberA {
    template<typename T> struct X {};
  };
  struct ClassMemberB : ClassMemberA {
    X(int) -> X<int>; // FIXME
  };
  template<typename T> struct Local {};
  void f() {
    Local(int) -> Local<int>; // expected-error 2{{expected}} expected-note {{to match}}
    using WrongScope::Local;
    Local(int) -> Local<int>; // expected-error 2{{expected}} expected-note {{to match}}
  }
}
