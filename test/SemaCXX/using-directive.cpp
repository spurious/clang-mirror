// RUN: clang -fsyntax-only -verify %s

namespace A {
  short i; // expected-note{{candidate found by name lookup is 'A::i'}}
  namespace B {
    long i; // expected-note{{candidate found by name lookup is 'A::B::i'}}
    void f() {} // expected-note{{candidate function}}
    int k;
    namespace E {} // \
      expected-note{{candidate found by name lookup is 'A::B::E'}}
  }

  namespace E {} // expected-note{{candidate found by name lookup is 'A::E'}}

  namespace C {
    using namespace B;
    namespace E {} // \
      expected-note{{candidate found by name lookup is 'A::C::E'}}
  }

  void f() {} // expected-note{{candidate function}}

  class K1 {
    void foo();
  };

  void local_i() {
    char i;
    using namespace A;
    using namespace B;
    int a[sizeof(i) == sizeof(char)? 1 : -1]; // okay
  }
  namespace B {
    int j;
  }

  void ambig_i() {
    using namespace A;
    using namespace A::B;
    (void) i; // expected-error{{reference to 'i' is ambiguous}}
    f(); // expected-error{{call to 'f' is ambiguous}}
    (void) j; // okay
    using namespace C;
    (void) k; // okay
    using namespace E; // expected-error{{reference to 'E' is ambiguous}}
  }

  struct K2 {}; // expected-note{{candidate found by name lookup is 'A::K2'}}
}

struct K2 {}; // expected-note{{candidate found by name lookup is 'K2'}}

using namespace A;

void K1::foo() {} // okay

// FIXME: Do we want err_ovl_no_viable_function_in_init here?
struct K2 k2; // expected-error{{reference to 'K2' is ambiguous}} \
                 expected-error{{no matching constructor}}

// FIXME: This case is incorrectly diagnosed!
//K2 k3;


class X {
  // FIXME: produce a suitable error message for this
  using namespace A; // expected-error{{expected unqualified-id}}
};

namespace N {
  // FIXME: both of these should work, but they currently cause an ambiguity.
  struct K2;
  struct K2 { };
}
