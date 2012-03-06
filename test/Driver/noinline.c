// Make sure the driver is correctly passing -fno-inline
// rdar://10972766

// RUN: %clang -target x86_64-apple-darwin10 \
// RUN:   -fno-inline -### -fsyntax-only %s 2> %t
// RUN: FileCheck --check-prefix=CHECK < %t %s

// CHECK: clang
// CHECK: "-fno-inline"

