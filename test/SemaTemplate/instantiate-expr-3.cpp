// RUN: clang-cc -fsyntax-only -verify %s

// ---------------------------------------------------------------------
// Imaginary literals
// ---------------------------------------------------------------------
template<typename T>
struct ImaginaryLiteral0 {
  void f(T &x) {
    x = 3.0I; // expected-error{{incompatible type}}
  }
};

template struct ImaginaryLiteral0<_Complex float>;
template struct ImaginaryLiteral0<int*>; // expected-note{{instantiation}}

// ---------------------------------------------------------------------
// Compound assignment operator
// ---------------------------------------------------------------------
namespace N1 {
  struct X { };

  int& operator+=(X&, int); // expected-note{{candidate}}
}

namespace N2 {
  long& operator+=(N1::X&, long); // expected-note{{candidate}}

  template<typename T, typename U, typename Result>
  struct PlusEquals0 {
    void f(T t, U u) {
      Result r = t += u; // expected-error{{ambiguous}}
    }
  };
}

namespace N3 {
  struct Y : public N1::X {
    short& operator+=(long); // expected-note{{candidate}}
  };
}

template struct N2::PlusEquals0<N1::X, int, int&>;
template struct N2::PlusEquals0<N1::X, long, long&>;
template struct N2::PlusEquals0<N3::Y, long, short&>;
template struct N2::PlusEquals0<int, int, int&>;
template struct N2::PlusEquals0<N3::Y, int, short&>; // expected-note{{instantiation}}

// ---------------------------------------------------------------------
// Conditional operator
// ---------------------------------------------------------------------
template<typename T, typename U, typename Result>
struct Conditional0 {
  void f(T t, U u) {
    Result result = t? : u;
  }
};

template struct Conditional0<int, int, int>;

// ---------------------------------------------------------------------
// Statement expressions
// ---------------------------------------------------------------------
template<typename T>
struct StatementExpr0 {
  void f(T t) {
    (void)({ if (t) t = t + 17; }); // expected-error{{invalid}}
  }
};

template struct StatementExpr0<int>;
template struct StatementExpr0<N1::X>; // expected-note{{instantiation}}
