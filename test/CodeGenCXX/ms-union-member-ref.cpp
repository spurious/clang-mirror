// RUN: %clang_cc1 -fms-extensions %s -emit-llvm -o- | FileCheck %s

union A {
  int *&ref;
  int **ptr;
};

int *f1(A *a) {
  return a->ref;
}
// CHECK-LABEL: define {{.*}}i32* @_Z2f1P1A(%union.A* %a)
// CHECK:       [[REF:%[^[:space:]]+]] = bitcast %union.A* %{{.*}} to i32***
// CHECK:       [[IPP:%[^[:space:]]+]] = load i32**, i32*** [[REF]]
// CHECK:       [[IP:%[^[:space:]]+]]  = load i32*, i32** [[IPP]]
// CHECK:       ret i32* [[IP]]

void f2(A *a) {
  *a->ref = 1;
}
// CHECK-LABEL: define {{.*}}void @_Z2f2P1A(%union.A* %a)
// CHECK:       [[REF:%[^[:space:]]+]] = bitcast %union.A* %{{.*}} to i32***
// CHECK:       [[IPP:%[^[:space:]]+]] = load i32**, i32*** [[REF]]
// CHECK:       [[IP:%[^[:space:]]+]]  = load i32*, i32** [[IPP]]
// CHECK:       store i32 1, i32* [[IP]]

bool f3(A *a, int *b) {
  return a->ref != b;
}
// CHECK-LABEL: define {{.*}}i1 @_Z2f3P1APi(%union.A* %a, i32* %b)
// CHECK:       [[REF:%[^[:space:]]+]] = bitcast %union.A* %{{.*}} to i32***
// CHECK:       [[IPP:%[^[:space:]]+]] = load i32**, i32*** [[REF]]
// CHECK:       [[IP:%[^[:space:]]+]]  = load i32*, i32** [[IPP]]
// CHECK:       [[IP2:%[^[:space:]]+]]  = load i32*, i32** %b.addr
// CHECK:       icmp ne i32* [[IP]], [[IP2]]
