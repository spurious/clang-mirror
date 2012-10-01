// RUN: %clang --analyze %s -o /dev/null -Xclang -analyzer-checker=debug.ConfigDumper > %t 2>&1
// RUN: FileCheck --input-file=%t %s

void bar() {}
void foo() { bar(); }

// CHECK: [config]
// CHECK-NEXT: cfg-temporary-dtors = false
// CHECK-NEXT: faux-bodies = true
// CHECK-NEXT: [stats]
// CHECK-NEXT: num-entries = 2
