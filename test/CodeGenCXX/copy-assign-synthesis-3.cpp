// RUN: %clang_cc1 -emit-llvm-only -verify %s

struct A {
  A& operator=(const A&);
};

struct B {
  A a;
  float b;
  int (A::*c)();
  _Complex float d;
  int e[10];
  A f[2];
};
void a(B& x, B& y) {
  x = y;
}

