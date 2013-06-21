// RUN: %clang_cc1 -fsyntax-only -verify %s 
int f(double); // expected-note{{candidate function}}
int f(int); // expected-note{{candidate function}}

int (*pfd)(double) = f; // selects f(double)
int (*pfd2)(double) = &f; // selects f(double)
int (*pfd3)(double) = ((&((f)))); // selects f(double)
int (*pfi)(int) = &f;    // selects f(int)
// FIXME: This error message is not very good. We need to keep better
// track of what went wrong when the implicit conversion failed to
// give a better error message here.
int (*pfe)(...) = &f;    // expected-error{{address of overloaded function 'f' does not match required type 'int (...)'}}
int (&rfi)(int) = f;     // selects f(int)
int (&rfd)(double) = f;  // selects f(double)

void g(int (*fp)(int));   // expected-note{{candidate function}}
void g(int (*fp)(float));
void g(int (*fp)(double)); // expected-note{{candidate function}}

int g1(int);
int g1(char);

int g2(int);
int g2(double);

template<typename T> T g3(T);
int g3(int);
int g3(char);

void g_test() {
  g(g1);
  g(g2); // expected-error{{call to 'g' is ambiguous}}
  g(g3);
}

template<typename T> T h1(T);
template<typename R, typename A1> R h1(A1);
int h1(char);

void ha(int (*fp)(int));
void hb(int (*fp)(double));

void h_test() {
  ha(h1);
  hb(h1);
}

struct A { };
void f(void (*)(A *));

struct B
{
  void g() { f(d); }
  void d(void *);
  static void d(A *);
};

struct C {
  C &getC() {
    return makeAC; // expected-error{{reference to non-static member function must be called; did you mean to call it with no arguments?}}
  }

  // FIXME: filter by const so we can unambiguously suggest '()' & point to just the one candidate, probably
  C &makeAC(); // expected-note{{possible target for call}}
  const C &makeAC() const; // expected-note{{possible target for call}}

  static void f(); // expected-note{{candidate function}}
  static void f(int); // expected-note{{candidate function}}

  void g() {
    int (&fp)() = f; // expected-error{{address of overloaded function 'f' does not match required type 'int ()'}}
  }

  template<typename T>
  void q1(int); // expected-note{{possible target for call}}
  template<typename T>
  void q2(T t = T()); // expected-note{{possible target for call}}
  template<typename T>
  void q3(); // expected-note{{possible target for call}}
  template<typename T1, typename T2>
  void q4(); // expected-note{{possible target for call}}
  template<typename T1 = int> // expected-warning{{default template arguments for a function template are a C++11 extension}}
  void q5(); // expected-note{{possible target for call}}

  void h() {
    // Do not suggest '()' since an int argument is required
    q1<int>; // expected-error-re{{reference to non-static member function must be called$}}
    // Suggest '()' since there's a default value for the only argument & the
    // type argument is already provided
    q2<int>; // expected-error{{reference to non-static member function must be called; did you mean to call it with no arguments?}}
    // Suggest '()' since no arguments are required & the type argument is
    // already provided
    q3<int>; // expected-error{{reference to non-static member function must be called; did you mean to call it with no arguments?}}
    // Do not suggest '()' since another type argument is required
    q4<int>; // expected-error-re{{reference to non-static member function must be called$}}
    // Suggest '()' since the type parameter has a default value
    q5; // expected-error{{reference to non-static member function must be called; did you mean to call it with no arguments?}}
  }
};

// PR6886
namespace test0 {
  void myFunction(void (*)(void *));

  class Foo {
    void foo();

    static void bar(void*);
    static void bar();
  };

  void Foo::foo() {
    myFunction(bar);
  }
}

namespace PR7971 {
  struct S {
    void g() {
      f(&g);
    }
    void f(bool (*)(int, char));
    static bool g(int, char);
  };
}

namespace PR8033 {
  template <typename T1, typename T2> int f(T1 *, const T2 *); // expected-note {{candidate function [with T1 = const int, T2 = int]}} \
  // expected-note{{candidate function}}
  template <typename T1, typename T2> int f(const T1 *, T2 *); // expected-note {{candidate function [with T1 = int, T2 = const int]}} \
  // expected-note{{candidate function}}
  int (*p)(const int *, const int *) = f; // expected-error{{address of overloaded function 'f' is ambiguous}} \
  // expected-error{{address of overloaded function 'f' is ambiguous}}

}

namespace PR8196 {
  template <typename T> struct mcdata {
    typedef int result_type;
  };
  template <class T> 
    typename mcdata<T>::result_type wrap_mean(mcdata<T> const&);
  void add_property(double(*)(mcdata<double> const &)); // expected-note{{candidate function not viable: no overload of 'wrap_mean' matching}}
  void f() {
    add_property(&wrap_mean); // expected-error{{no matching function for call to 'add_property'}}
  }
}

namespace PR7425 {
  template<typename T>
  void foo()
  {
  }

  struct B
  {
    template<typename T>
    B(const T&)
    {
    }
  };

  void bar(const B& b)
  {
  }

  void bar2(const B& b = foo<int>)
  {
  }

  void test(int argc, char** argv)
  {
    bar(foo<int>);
    bar2();
  }
}

namespace test1 {
  void fun(int x) {}

  void parameter_number() {
    void (*ptr1)(int, int) = &fun; // expected-error {{cannot initialize a variable of type 'void (*)(int, int)' with an rvalue of type 'void (*)(int)': different number of parameters (2 vs 1)}}
    void (*ptr2)(int, int);
    ptr2 = &fun;  // expected-error {{assigning to 'void (*)(int, int)' from incompatible type 'void (*)(int)': different number of parameters (2 vs 1)}}
  }

  void parameter_mismatch() {
    void (*ptr1)(double) = &fun; // expected-error {{cannot initialize a variable of type 'void (*)(double)' with an rvalue of type 'void (*)(int)': type mismatch at 1st parameter ('double' vs 'int')}}
    void (*ptr2)(double);
    ptr2 = &fun; // expected-error {{assigning to 'void (*)(double)' from incompatible type 'void (*)(int)': type mismatch at 1st parameter ('double' vs 'int')}}
  }

  void return_type_test() {
    int (*ptr1)(int) = &fun; // expected-error {{cannot initialize a variable of type 'int (*)(int)' with an rvalue of type 'void (*)(int)': different return type ('int' vs 'void')}}
    int (*ptr2)(int);
    ptr2 = &fun;  // expected-error {{assigning to 'int (*)(int)' from incompatible type 'void (*)(int)': different return type ('int' vs 'void')}}
  }

  int foo(double x, double y) {return 0;} // expected-note {{candidate function has different number of parameters (expected 1 but has 2)}}
  int foo(int x, int y) {return 0;} // expected-note {{candidate function has different number of parameters (expected 1 but has 2)}}
  int foo(double x) {return 0;} // expected-note {{candidate function has type mismatch at 1st parameter (expected 'int' but has 'double')}}
  double foo(float x, float y) {return 0;} // expected-note {{candidate function has different number of parameters (expected 1 but has 2)}}
  double foo(int x, float y) {return 0;} // expected-note {{candidate function has different number of parameters (expected 1 but has 2)}}
  double foo(float x) {return 0;} // expected-note {{candidate function has type mismatch at 1st parameter (expected 'int' but has 'float')}}
  double foo(int x) {return 0;} // expected-note {{candidate function has different return type ('int' expected but has 'double')}}
  
  int (*ptr)(int) = &foo; // expected-error {{address of overloaded function 'foo' does not match required type 'int (int)'}}

  struct Qualifiers {
    void N() {};
    void C() const {};
    void V() volatile {};
    void R() __restrict {};
    void CV() const volatile {};
    void CR() const __restrict {};
    void VR() volatile __restrict {};
    void CVR() const volatile __restrict {};
  };


  void QualifierTest() {
    void (Qualifiers::*X)();
    X = &Qualifiers::C; // expected-error {{assigning to 'void (test1::Qualifiers::*)()' from incompatible type 'void (test1::Qualifiers::*)() const': different qualifiers (none vs const)}}
    X = &Qualifiers::V; // expected-error{{assigning to 'void (test1::Qualifiers::*)()' from incompatible type 'void (test1::Qualifiers::*)() volatile': different qualifiers (none vs volatile)}}
    X = &Qualifiers::R; // expected-error{{assigning to 'void (test1::Qualifiers::*)()' from incompatible type 'void (test1::Qualifiers::*)() restrict': different qualifiers (none vs restrict)}}
    X = &Qualifiers::CV; // expected-error{{assigning to 'void (test1::Qualifiers::*)()' from incompatible type 'void (test1::Qualifiers::*)() const volatile': different qualifiers (none vs const and volatile)}}
    X = &Qualifiers::CR; // expected-error{{assigning to 'void (test1::Qualifiers::*)()' from incompatible type 'void (test1::Qualifiers::*)() const restrict': different qualifiers (none vs const and restrict)}}
    X = &Qualifiers::VR; // expected-error{{assigning to 'void (test1::Qualifiers::*)()' from incompatible type 'void (test1::Qualifiers::*)() volatile restrict': different qualifiers (none vs volatile and restrict)}}
    X = &Qualifiers::CVR; // expected-error{{assigning to 'void (test1::Qualifiers::*)()' from incompatible type 'void (test1::Qualifiers::*)() const volatile restrict': different qualifiers (none vs const, volatile, and restrict)}}
  }

  struct Dummy {
    void N() {};
  };

  void (Qualifiers::*X)() = &Dummy::N; // expected-error{{cannot initialize a variable of type 'void (test1::Qualifiers::*)()' with an rvalue of type 'void (test1::Dummy::*)()': different classes ('test1::Qualifiers' vs 'test1::Dummy')}}
}
