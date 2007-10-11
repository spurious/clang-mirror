// RUN: clang -fsyntax-only -verify %s

int foo(float x, float y) {
  return x == y; // expected-warning {{comparing floating point with ==}}
} 

int bar(float x, float y) {
  return x != y; // expected-warning {{comparing floating point with ==}}
}
