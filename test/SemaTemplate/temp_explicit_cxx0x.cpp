// RUN: clang-cc -fsyntax-only -std=c++0x -verify %s
namespace N1 {

  template<typename T> struct X0 { }; // expected-note{{here}}

  namespace Inner {
    template<typename T> struct X1 { };
  }

  template struct X0<int>;
  template struct Inner::X1<int>;
}

template<typename T> struct X2 { }; // expected-note{{here}}

template struct ::N1::Inner::X1<float>;

namespace N2 {
  using namespace N1;

  template struct X0<double>; // expected-error{{not in a namespace enclosing}}

  template struct X2<float>; // expected-error{{at global scope}}
}
