// RUN: clang-cc %s -emit-llvm -o - -triple=x86_64-apple-darwin9 | FileCheck %s

struct A { int a; };
struct B { int b; };
struct C : B, A { };

void (A::*pa)();
void (A::*volatile vpa)();
void (B::*pb)();
void (C::*pc)();

void f() {
  // CHECK: store i64 0, i64* getelementptr inbounds (%0* @pa, i32 0, i32 0)
  // CHECK: store i64 0, i64* getelementptr inbounds (%0* @pa, i32 0, i32 1)
  pa = 0;

  // CHECK: volatile store i64 0, i64* getelementptr inbounds (%0* @vpa, i32 0, i32 0)
  // CHECK: volatile store i64 0, i64* getelementptr inbounds (%0* @vpa, i32 0, i32 1)
  vpa = 0;

  // CHECK: store i64 %0, i64* getelementptr inbounds (%0* @pc, i32 0, i32 0)
  // CHECK: [[ADJ:%[a-zA-Z0-9]+]] = add i64 %1, 4
  // CHECK: store i64 [[ADJ]], i64* getelementptr inbounds (%0* @pc, i32 0, i32 1)
  pc = pa;  
}
