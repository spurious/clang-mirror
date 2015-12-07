// RUN: %clang -target x86_64-unknown-linux -O2 %s -flto=thin -c -o %t.o
// RUN: llvm-lto -thinlto -o %t %t.o

// Ensure clang -cc1 give expected error for incorrect input type
// RUN: not %clang_cc1 -target x86_64-unknown-linux -O2 -o %t1.o %s -c -fthinlto-index=%t.thinlto.bc 2>&1 | FileCheck %s -check-prefix=CHECK-WARNING
// CHECK-WARNING: error: invalid argument '-fthinlto-index={{.*}}' only allowed with '-x ir'

// Ensure we get expected error for missing index file
// RUN: %clang -target x86_64-unknown-linux -O2 -o %t1.o -x ir %t.o -c -fthinlto-index=bad.thinlto.bc 2>&1 | FileCheck %s -check-prefix=CHECK-ERROR
// CHECK-ERROR: Error loading index file 'bad.thinlto.bc': No such file or directory

// Ensure Function Importing pass added
// RUN: %clang -target x86_64-unknown-linux -O2 -o %t1.o -x ir %t.o -c -fthinlto-index=%t.thinlto.bc -mllvm -debug-pass=Structure 2>&1 | FileCheck %s -check-prefix=CHECK-PASS
// CHECK-PASS: Function Importing
