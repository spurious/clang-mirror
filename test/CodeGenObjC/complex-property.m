// RUN: %clang_cc1 -triple x86_64-apple-darwin10  -emit-llvm -o - %s | FileCheck -check-prefix LP64 %s
// rdar: // 7351147

@interface A
@property __complex int COMPLEX_PROP;
@end

void f0(A *a) {  
  _Complex int a1 = 25 + 10i;
  a.COMPLEX_PROP += a1;
}

// CHECK-LP64: internal global [13 x i8] c"COMPLEX_PROP
// CHECK-LP64: internal global [17 x i8] c"setCOMPLEX_PROP
