// RUN: %clang_cc1 -emit-llvm %s -o - | FileCheck %s

__attribute((aligned(16))) float a[128];
union {int a[4]; __attribute((aligned(16))) float b[4];} b;

// CHECK: @a = {{.*}}zeroinitializer, align 16
// CHECK: @b = {{.*}}zeroinitializer, align 16



// PR5279 - Reduced alignment on typedef.
typedef int myint __attribute__((aligned(1)));

void test1(myint *p) {
  *p = 0;
}
// CHECK: @test1(
// CHECK: store i32 0, i32* {{.*}}, align 1
// CHECK: ret void


// PR5279 - Reduced alignment on typedef.
typedef float __attribute__((vector_size(16), aligned(4))) packedfloat4;

void test2(packedfloat4 *p) {
  *p = (packedfloat4) { 3.2f, 2.3f, 0.1f, 0.0f };
}
// CHECK: @test2(
// CHECK: store <4 x float> {{.*}}, align 4
// CHECK: ret void


// PR5279 - Reduced alignment on typedef.
typedef float __attribute__((ext_vector_type(3), aligned(4))) packedfloat3;
void test3(packedfloat3 *p) {
  *p = (packedfloat3) { 3.2f, 2.3f, 0.1f };
}
// CHECK: @test3(
// CHECK: store <3 x float> {{.*}}, align 4
// CHECK: ret void

