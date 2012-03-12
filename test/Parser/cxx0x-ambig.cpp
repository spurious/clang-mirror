// RUN: %clang_cc1 -fsyntax-only -verify -std=c++11 %s

// New exciting ambiguities in C++11

// final 'context sensitive' mess.
namespace final {
  struct S { int n; };
  struct T { int n; };
  namespace N {
    int n;
    // These declare variables named final..
    extern struct S final;
    extern struct S final [[]];
    extern struct S final, foo;
    struct S final = S();

    // This defines a class, not a variable, even though it would successfully
    // parse as a variable but not as a class. DR1318's wording suggests that
    // this disambiguation is only performed on an ambiguity, but that was not
    // the intent.
    struct S final { // expected-note {{here}}
      int(n) // expected-error {{expected ';'}}
    };
    // This too.
    struct T final : S {}; // expected-error {{base 'S' is marked 'final'}}
  }
}

// enum versus bitfield mess.
namespace bitfield {
  enum E {};

  struct T {
    constexpr T() {}
    constexpr T(int) {}
    constexpr T(T, T, T, T) {}
    constexpr T operator=(T) { return *this; }
    constexpr operator int() { return 4; }
  };
  constexpr T a, b, c, d;

  struct S1 {
    enum E : T ( a = 1, b = 2, c = 3, 4 ); // ok, declares a bitfield
  };
  // This could be a bit-field.
  struct S2 {
    enum E : T { a = 1, b = 2, c = 3, 4 }; // expected-error {{non-integral type}} expected-error {{expected '}'}} expected-note {{to match}}
  };
  struct S3 {
    enum E : int { a = 1, b = 2, c = 3, d }; // ok, defines an enum
  };
  // Ambiguous.
  struct S4 {
    enum E : int { a = 1 }; // ok, defines an enum
  };
  // This could be a bit-field, but would be ill-formed due to the anonymous
  // member being initialized.
  struct S5 {
    enum E : int { a = 1 } { b = 2 }; // expected-error {{expected member name}}
  };
  // This could be a bit-field.
  struct S6 {
    enum E : int { 1 }; // expected-error {{expected '}'}} expected-note {{to match}}
  };

  struct U {
    constexpr operator T() { return T(); } // expected-note 2{{candidate}}
  };
  // This could be a bit-field.
  struct S7 {
    enum E : int { a = U() }; // expected-error {{no viable conversion}}
  };
  // This could be a bit-field, and does not conform to the grammar of an
  // enum definition, because 'id(U())' is not a constant-expression.
  constexpr const U &id(const U &u) { return u; }
  struct S8 {
    enum E : int { a = id(U()) }; // expected-error {{no viable conversion}}
  };
}

namespace trailing_return {
  typedef int n;
  int a;

  struct S {
    S(int);
    S *operator()() const;
    int n;
  };

  namespace N {
    void f() {
      // This parses as a function declaration, but DR1223 makes the presence of
      // 'auto' be used for disambiguation.
      S(a)()->n; // ok, expression; expected-warning{{expression result unused}}
      auto(a)()->n; // ok, function declaration
      using T = decltype(a);
      using T = auto() -> n;
    }
  }
}
