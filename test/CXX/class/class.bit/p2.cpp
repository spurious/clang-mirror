// RUN: %clang_cc1 -fsyntax-only -verify -std=c++0x %s

struct A {
private: 
  int : 0;
};

A a = { };
A a2 = { 1 }; // expected-error{{excess elements in struct initializer}}

struct B {
  const int : 0;
};

B b;

void testB() {
  B b2(b);
  B b3(static_cast<B&&>(b2));
  b = b;
  b = static_cast<B&&>(b);
}
