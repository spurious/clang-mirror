// RUN: %clang_cc1 -faccess-control -verify %s

namespace test0 {
  struct A {
    static int x;
  };
  struct B : A {};
  struct C : B {};

  int test() {
    return A::x
         + B::x
         + C::x;
  }
}

namespace test1 {
  struct A {
    private: static int x; // expected-note 5 {{declared private here}}
    static int test() { return x; }
  };
  struct B : public A {
    static int test() { return x; } // expected-error {{private member}}
  };
  struct C : private A {
    static int test() { return x; } // expected-error {{private member}}
  };

  struct D {
    public: static int x;
    static int test() { return x; }
  };
  struct E : private D { // expected-note{{constrained by private inheritance}}
    static int test() { return x; }
  };

  int test() {
    return A::x // expected-error {{private member}}
         + B::x // expected-error {{private member}}
         + C::x // expected-error {{private member}}
         + D::x
         + E::x; // expected-error {{private member}}
  }
}

namespace test2 {
  class A {
  protected: static int x;
  };

  class B : private A {}; // expected-note {{private inheritance}}
  class C : private A {
    int test(B *b) {
      return b->x; // expected-error {{private member}}
    }
  };
}

// TODO: flesh out these cases
