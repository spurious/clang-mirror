// RUN: clang-cc -E -C %s | FileCheck check-prefix=CHECK-C -strict-whitespace %s &&
// CHECK-C: boo bork bar // zot

// RUN: clang-cc -E -CC %s | FileCheck check-prefix=CHECK-CC -strict-whitespace %s &&
// CHECK-CC: boo bork /* blah*/ bar // zot

// RUN: clang-cc -E %s | FileCheck check-prefix=CHECK -strict-whitespace %s
// CHECK: boo bork bar


#define FOO bork // blah
boo FOO bar // zot

