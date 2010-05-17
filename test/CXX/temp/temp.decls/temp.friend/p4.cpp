// RUN: %clang_cc1 -fsyntax-only -verify %s

template<typename T>
struct X {
  friend void f(int x) { T* y = x; } // expected-error{{cannot initialize a variable of type 'int *' with an lvalue of type 'int'}}
};

X<int> xi; // expected-note{{in instantiation of member function 'f' requested here}}

void f0(double) { }
void f0(int) { } // expected-note{{previous definition}}
void f1(int) { } // expected-note{{previous definition}}
void f2(int);
void f3(int);

template<typename T>
struct X1 {
  friend void f0(T) { } // expected-error{{redefinition of}}
  friend void f1(T) { } // expected-error{{redefinition of}}
  friend void f2(T) { } // expected-error{{redefinition of}}
  friend void f3(T) { } // expected-error{{redefinition of}}
  friend void f4(T) { } // expected-error{{redefinition of}}
  friend void f5(T) { } // expected-error{{redefinition of}}

  // FIXME: should have a redefinition error for f6(int)
  friend void f6(int) { }
};

void f2(int) { } // expected-note{{previous definition}}
void f4(int) { } // expected-note{{previous definition}}

X1<int> x1a; // expected-note 6{{in instantiation of}}

void f3(int) { } // expected-note{{previous definition}}
void f5(int) { } // expected-note{{previous definition}}

X1<float> x1b;


X1<double> *X0d() { return 0;}
