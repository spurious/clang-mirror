// General tests that ld invocations on Linux targets sane. Note that we use
// sysroot to make these tests independent of the host system.
//
// RUN: %clang -no-canonical-prefixes %s -### -o %t.o 2>&1 \
// RUN:     -ccc-host-triple i386-unknown-linux \
// RUN:     --sysroot=%S/Inputs/basic_linux_tree \
// RUN:   | FileCheck --check-prefix=CHECK-LD-32 %s
// CHECK-LD-32: "{{.*}}ld{{(.exe)?}}" "--sysroot=[[SYSROOT:[^"]+]]"
// CHECK-LD-32: "{{.*}}/usr/lib/gcc/i386-unknown-linux/4.6.0/crtbegin.o"
// CHECK-LD-32: "-L[[SYSROOT]]/usr/lib/gcc/i386-unknown-linux/4.6.0"
// CHECK-LD-32: "-L[[SYSROOT]]/usr/lib/gcc/i386-unknown-linux/4.6.0/../../../../i386-unknown-linux/lib"
// CHECK-LD-32: "-L[[SYSROOT]]/usr/lib/gcc/i386-unknown-linux/4.6.0/../../.."
// CHECK-LD-32: "-L[[SYSROOT]]/lib"
// CHECK-LD-32: "-L[[SYSROOT]]/usr/lib"
//
// RUN: %clang -no-canonical-prefixes %s -### -o %t.o 2>&1 \
// RUN:     -ccc-host-triple x86_64-unknown-linux \
// RUN:     --sysroot=%S/Inputs/basic_linux_tree \
// RUN:   | FileCheck --check-prefix=CHECK-LD-64 %s
// CHECK-LD-64: "{{.*}}ld{{(.exe)?}}" "--sysroot=[[SYSROOT:[^"]+]]"
// CHECK-LD-64: "{{.*}}/usr/lib/gcc/x86_64-unknown-linux/4.6.0/crtbegin.o"
// CHECK-LD-64: "-L[[SYSROOT]]/usr/lib/gcc/x86_64-unknown-linux/4.6.0"
// CHECK-LD-64: "-L[[SYSROOT]]/usr/lib/gcc/x86_64-unknown-linux/4.6.0/../../../../x86_64-unknown-linux/lib"
// CHECK-LD-64: "-L[[SYSROOT]]/usr/lib/gcc/x86_64-unknown-linux/4.6.0/../../.."
// CHECK-LD-64: "-L[[SYSROOT]]/lib"
// CHECK-LD-64: "-L[[SYSROOT]]/usr/lib"
//
// RUN: %clang -no-canonical-prefixes %s -### -o %t.o 2>&1 \
// RUN:     -ccc-host-triple i386-unknown-linux -m32 \
// RUN:     --sysroot=%S/Inputs/multilib_32bit_linux_tree \
// RUN:   | FileCheck --check-prefix=CHECK-32-TO-32 %s
// CHECK-32-TO-32: "{{.*}}ld{{(.exe)?}}" "--sysroot=[[SYSROOT:[^"]+]]"
// CHECK-32-TO-32: "{{.*}}/usr/lib/gcc/i386-unknown-linux/4.6.0/crtbegin.o"
// CHECK-32-TO-32: "-L[[SYSROOT]]/usr/lib/gcc/i386-unknown-linux/4.6.0"
// CHECK-32-TO-32: "-L[[SYSROOT]]/usr/lib/gcc/i386-unknown-linux/4.6.0/../../../../i386-unknown-linux/lib/../lib32"
// CHECK-32-TO-32: "-L[[SYSROOT]]/usr/lib/gcc/i386-unknown-linux/4.6.0/../../../../lib32"
// CHECK-32-TO-32: "-L[[SYSROOT]]/lib/../lib32"
// CHECK-32-TO-32: "-L[[SYSROOT]]/usr/lib/../lib32"
// CHECK-32-TO-32: "-L[[SYSROOT]]/usr/lib/gcc/i386-unknown-linux/4.6.0/../../../../i386-unknown-linux/lib"
// CHECK-32-TO-32: "-L[[SYSROOT]]/usr/lib/gcc/i386-unknown-linux/4.6.0/../../.."
// CHECK-32-TO-32: "-L[[SYSROOT]]/lib"
// CHECK-32-TO-32: "-L[[SYSROOT]]/usr/lib"
//
// RUN: %clang -no-canonical-prefixes %s -### -o %t.o 2>&1 \
// RUN:     -ccc-host-triple i386-unknown-linux -m64 \
// RUN:     --sysroot=%S/Inputs/multilib_32bit_linux_tree \
// RUN:   | FileCheck --check-prefix=CHECK-32-TO-64 %s
// CHECK-32-TO-64: "{{.*}}ld{{(.exe)?}}" "--sysroot=[[SYSROOT:[^"]+]]"
// CHECK-32-TO-64: "{{.*}}/usr/lib/gcc/i386-unknown-linux/4.6.0/64/crtbegin.o"
// CHECK-32-TO-64: "-L[[SYSROOT]]/usr/lib/gcc/i386-unknown-linux/4.6.0/64"
// CHECK-32-TO-64: "-L[[SYSROOT]]/usr/lib/gcc/i386-unknown-linux/4.6.0/../../../../i386-unknown-linux/lib/../lib64"
// CHECK-32-TO-64: "-L[[SYSROOT]]/usr/lib/gcc/i386-unknown-linux/4.6.0/../../../../lib64"
// CHECK-32-TO-64: "-L[[SYSROOT]]/lib/../lib64"
// CHECK-32-TO-64: "-L[[SYSROOT]]/usr/lib/../lib64"
// CHECK-32-TO-64: "-L[[SYSROOT]]/usr/lib/gcc/i386-unknown-linux/4.6.0"
// CHECK-32-TO-64: "-L[[SYSROOT]]/usr/lib/gcc/i386-unknown-linux/4.6.0/../../../../i386-unknown-linux/lib"
// CHECK-32-TO-64: "-L[[SYSROOT]]/usr/lib/gcc/i386-unknown-linux/4.6.0/../../.."
// CHECK-32-TO-64: "-L[[SYSROOT]]/lib"
// CHECK-32-TO-64: "-L[[SYSROOT]]/usr/lib"
