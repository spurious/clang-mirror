// RUN: clang-cc -fsyntax-only %s

// FIXME: We need to test anonymous structs/unions in templates for real.

template <typename T> class A { struct { }; };

A<int> a0;

template <typename T> struct B {
  union {
    int a;
    void* b;
  };
    
  void f() {
    a = 10;
    b = 0;
  }
};

B<int> b0;
