// RUN: %clang_cc1 -fsyntax-only -verify -std=c++0x %s

struct S; // expected-note 4{{forward declaration of 'S'}}

struct T0 {
  S s; // expected-error{{field has incomplete type 'S'}}
  T0() = default;
};

struct T1 {
  S s; // expected-error{{field has incomplete type 'S'}}
  T1(T1&) = default;
};

struct T2 {
  S s; // expected-error{{field has incomplete type 'S'}}
  T2& operator=(T2&) = default;
};

struct T3 {
  S s; // expected-error{{field has incomplete type 'S'}}
  ~T3() = default;
};
