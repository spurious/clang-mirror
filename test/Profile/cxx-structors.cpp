// Tests for instrumentation of C++ constructors and destructors.
//
// RUN: %clang_cc1 -triple x86_64-apple-macosx10.11.0 -x c++ %s -o %t -emit-llvm -fprofile-instrument=clang
// RUN: FileCheck %s -input-file=%t -check-prefix=INSTR
// RUN: FileCheck %s -input-file=%t -check-prefix=NOINSTR

struct Foo {
  Foo() {}
  Foo(int) {}
  ~Foo() {}
};

struct Bar : public Foo {
  Bar() {}
  Bar(int x) : Foo(x) {}
  ~Bar();
};

Foo foo;
Foo foo2(1);
Bar bar;

// Profile data for complete constructors and destructors must absent.

// INSTR: @__profc_main =
// INSTR: @__profc__ZN3FooC2Ev =
// INSTR: @__profc__ZN3FooD2Ev =
// INSTR: @__profc__ZN3FooC2Ei =
// INSTR: @__profc__ZN3BarC2Ev =

// NOINSTR-NOT: @__profc__ZN3FooC1Ev
// NOINSTR-NOT: @__profc__ZN3FooC1Ei
// NOINSTR-NOT: @__profc__ZN3FooD1Ev
// NOINSTR-NOT: @__profc__ZN3BarC1Ev
// NOINSTR-NOT: @__profc__ZN3BarD1Ev
// NOINSTR-NOT: @__profc__ZN3FooD1Ev

int main() {
}
