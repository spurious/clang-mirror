// RUN: %clang_cc1 -triple i386-unknown-unknown -emit-llvm %s -o - | FileCheck %s

void f1() {
  // Scalars in braces.
  int a = { 1 };
}

void f2() {
  int a[2][2] = { { 1, 2 }, { 3, 4 } };
  int b[3][3] = { { 1, 2 }, { 3, 4 } };
  int *c[2] = { &a[1][1], &b[2][2] };
  int *d[2][2] = { {&a[1][1], &b[2][2]}, {&a[0][0], &b[1][1]} };
  int *e[3][3] = { {&a[1][1], &b[2][2]}, {&a[0][0], &b[1][1]} };
  char ext[3][3] = {".Y",".U",".V"};
}

typedef void (* F)(void);
extern void foo(void);
struct S { F f; };
void f3() {
  struct S a[1] = { { foo } };
}

// Constants
// CHECK: @g3 = constant i32 10
// CHECK: @f4.g4 = internal constant i32 12
const int g3 = 10;
int f4() {
  static const int g4 = 12;
  return g4;
}

// PR6537
typedef union vec3 {
  struct { double x, y, z; };
  double component[3];
} vec3;
vec3 f5(vec3 value) {
  return (vec3) {{
    .x = value.x
  }};
}

// rdar://problem/8154689
void f6() {
  int x;
  long ids[] = { (long) &x };  
}




// CHECK: @test7 = global{{.*}}{ i32 0, [4 x i8] c"bar\00" }
// PR8217
struct a7 {
  int  b;
  char v[];
};

struct a7 test7 = { .b = 0, .v = "bar" };


// PR279 comment #3
char test8(int X) {
  char str[100000] = "abc"; // tail should be memset.
  return str[X];
// CHECK: @test8(
// CHECK: call void @llvm.memset
// CHECK: store i8 97
// CHECK: store i8 98
// CHECK: store i8 99
}
