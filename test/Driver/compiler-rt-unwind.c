// General tests that the driver handles combinations of --rtlib=XXX and
// --unwindlib=XXX properly.
//
// RUN: %clang -no-canonical-prefixes %s -### -o %t.o 2>&1 \
// RUN:     --target=x86_64-unknown-linux \
// RUN:     --gcc-toolchain="" \
// RUN:   | FileCheck --check-prefix=RTLIB-EMPTY %s
// RTLIB-EMPTY: "{{.*}}lgcc"
// RTLIB-EMPTY: "{{.*}}-lgcc_s"
//
// RUN: %clang -no-canonical-prefixes %s -### -o %t.o 2>&1 \
// RUN:     --target=x86_64-unknown-linux -rtlib=gcc \
// RUN:     --gcc-toolchain="" \
// RUN:   | FileCheck --check-prefix=RTLIB-GCC %s
// RTLIB-GCC: "{{.*}}lgcc"
// RTLIB-GCC: "{{.*}}lgcc_s"
//
// RUN: %clang -no-canonical-prefixes %s -### -o %t.o 2>&1 \
// RUN:     --target=x86_64-unknown-linux -rtlib=gcc --unwindlib=compiler-rt \
// RUN:     --gcc-toolchain="" \
// RUN:   | FileCheck --check-prefix=RTLIB-GCC-UNWINDLIB-COMPILER-RT %s
// RTLIB-GCC-UNWINDLIB-COMPILER-RT: "{{.*}}lgcc"
// RTLIB-GCC-UNWINDLIB-COMPILER-RT: "{{.*}}lunwind"
//
// RUN: %clang -no-canonical-prefixes %s -### -o %t.o 2>&1   \
// RUN:     --target=x86_64-unknown-linux -rtlib=compiler-rt \
// RUN:     --gcc-toolchain="" \
// RUN:   | FileCheck --check-prefix=RTLIB-COMPILER-RT %s
// RTLIB-COMPILER-RT: "{{.*}}libclang_rt.builtins-x86_64.a"
// RTLIB-COMPILER-RT: "{{.*}}-lunwind"
//
// RUN: %clang -no-canonical-prefixes %s -### -o %t.o 2>&1   \
// RUN:     --target=x86_64-unknown-linux -rtlib=compiler-rt --unwindlib=gcc \
// RUN:     --gcc-toolchain="" \
// RUN:   | FileCheck --check-prefix=RTLIB-COMPILER-RT-UNWINDLIB-GCC %s
// RTLIB-COMPILER-RT-UNWINDLIB-GCC: "{{.*}}libclang_rt.builtins-x86_64.a"
// RTLIB-COMPILER-RT-UNWINDLIB-GCC: "{{.*}}lgcc_s"
//
// RUN: %clang -no-canonical-prefixes %s -### -o %t.o 2>&1              \
// RUN:     --target=x86_64-unknown-linux -rtlib=compiler-rt --unwindlib=gcc \
// RUN:     -static --gcc-toolchain="" \
// RUN:   | FileCheck --check-prefix=RTLIB-COMPILER-RT-UNWINDLIB-GCC-STATIC %s
// RTLIB-COMPILER-RT-UNWINDLIB-GCC-STATIC: "{{.*}}libclang_rt.builtins-x86_64.a"
// RTLIB-COMPILER-RT-UNWINDLIB-GCC-STATIC: "{{.*}}lgcc_eh"
//
// RUN: not %clang -no-canonical-prefixes %s -o %t.o 2> %t.err              \
// RUN:     --target=x86_64-unknown-linux -rtlib=libgcc --unwindlib=compiler-rt \
// RUN:     --gcc-toolchain="" \
// RUN: FileCheck --input-file=%t.err --check-prefix=RTLIB-GCC-UNWINDLIB-COMPILER_RT %s
// RTLIB-GCC-UNWINDLIB-COMPILER_RT: "{{[.|\\\n]*}}--rtlib=libgcc requires --unwindlib=libgcc"
