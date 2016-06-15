// Checks that cuda compilation does the right thing when passed -march.
// (Specifically, we want to pass it to host compilation, but not to device
// compilation or ptxas!)
//
// REQUIRES: clang-driver
// REQUIRES: x86-registered-target
// REQUIRES: nvptx-registered-target

// RUN: %clang -### -target x86_64-linux-gnu -c -march=haswell %s 2>&1 | FileCheck %s

// RUN: %clang -### -target x86_64-linux-gnu -c -march=haswell --cuda-gpu-arch=sm_20 %s 2>&1 | \
// RUN: FileCheck %s

// CHECK:clang
// CHECK: "-cc1"
// CHECK-SAME: "-triple" "nvptx
// CHECK-SAME: "-target-cpu" "sm_20"

// CHECK: ptxas
// CHECK-SAME: "--gpu-name" "sm_20"

// CHECK:clang
// CHECK-SAME: "-cc1"
// CHECK-SAME: "-target-cpu" "haswell"
