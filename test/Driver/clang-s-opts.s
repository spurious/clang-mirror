// RUN: %clang -### -c -Werror -flto -fno-lto %s 2>&1 | FileCheck %s
// RUN: %clang -### -c -Werror -flto -fno-lto -fno-integrated-as %s 2>&1 | FileCheck %s

// CHECK-NOT: argument unused during compilation
