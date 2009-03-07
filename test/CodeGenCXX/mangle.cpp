// RUN: clang -emit-llvm %s -o %t &&

// FIXME: This test is intentionally trivial, because we can't yet
// CodeGen anything real in C++.
struct X { };
struct Y { };

// RUN: grep _ZplRK1YRA100_P1X %t | count 1 &&
bool operator+(const Y&, X* (&xs)[100]) { return false; }

// RUN: grep _Z1f1s %t | count 1 &&
typedef struct { int a; } s;
void f(s) { }

// RUN: grep _Z1f1e %t| count 1 &&
typedef enum { foo } e;
void f(e) { }

// RUN: grep _Z1f1u %t | count 1
typedef union { int a; } u;
void f(u) { }
