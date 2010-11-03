// Test is line- and column-sensitive. Run lines are below.

struct X {
  X();
  X(int);
  X(int, int);
  X(const X&);
};

X getX(int value) { 
  switch (value) {
  case 1: return X(value);
  case 2: return X(value, value);
  case 3: return (X)value;
  default: break;
  }
  return X();
}

// RUN: c-index-test -cursor-at=%s:12:20 %s | FileCheck -check-prefix=CHECK-VALUE-REF %s
// RUN: c-index-test -cursor-at=%s:13:21 %s | FileCheck -check-prefix=CHECK-VALUE-REF %s
// RUN: c-index-test -cursor-at=%s:13:28 %s | FileCheck -check-prefix=CHECK-VALUE-REF %s
// RUN: c-index-test -cursor-at=%s:14:23 %s | FileCheck -check-prefix=CHECK-VALUE-REF %s
// CHECK-VALUE-REF: DeclRefExpr=value:10:12

// FIXME: c-index-test -cursor-at=%s:12:18 %s | FileCheck -check-prefix=CHECK-TYPE-REF %s
// RUN: c-index-test -cursor-at=%s:13:18 %s | FileCheck -check-prefix=CHECK-TYPE-REF %s
// FIXME: c-index-test -cursor-at=%s:14:19 %s | FileCheck -check-prefix=CHECK-TYPE-REF %s
// RUN: c-index-test -cursor-at=%s:17:10 %s | FileCheck -check-prefix=CHECK-TYPE-REF %s
// CHECK-TYPE-REF: TypeRef=struct X:3:8
