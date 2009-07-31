// RUN: clang-cc -fsyntax-only -verify %s

void f();

// FIXME: would like to refer to the first function parameter in these test,
// but that won't work (yet).

// Test typeof(expr) canonicalization
template<typename T, T N>
void f0(T x, __typeof__(f(N)) y) { } // expected-note{{previous}}

template<typename T, T N>
void f0(T x, __typeof__((f)(N)) y) { }

template<typename U, U M>
void f0(U u, __typeof__(f(M))) { } // expected-error{{redefinition}}

// Test dependently-sized array canonicalization
template<typename T, int N, int M>
void f1(T (&array)[N + M]) { } // expected-note{{previous}}

template<typename T, int N, int M>
void f1(T (&array)[M + N]) { }

template<typename T, int M, int N>
void f1(T (&array)[M + N]) { } // expected-error{{redefinition}}
