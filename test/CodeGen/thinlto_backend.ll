; REQUIRES: x86-registered-target

; RUN: opt -module-summary -o %t1.o %s
; RUN: opt -module-summary -o %t2.o %S/Inputs/thinlto_backend.ll
; RUN: llvm-lto -thinlto -o %t %t1.o %t2.o

; Ensure clang -cc1 give expected error for incorrect input type
; RUN: not %clang_cc1 -O2 -o %t1.o -x c %s -c -fthinlto-index=%t.thinlto.bc 2>&1 | FileCheck %s -check-prefix=CHECK-WARNING
; CHECK-WARNING: error: invalid argument '-fthinlto-index={{.*}}' only allowed with '-x ir'

; Ensure sample profile pass are passed to backend
; RUN: %clang_cc1 -emit-obj -O2 -o %t5.o -x ir %t1.o -fthinlto-index=%t.thinlto.bc -fprofile-sample-use=%S/Inputs/pgo-sample.prof -mllvm -debug-pass=Structure 2>&1 | FileCheck %s -check-prefix=CHECK-SAMPLEPGO
; CHECK-SAMPLEPGO: Sample profile pass

; Ensure we get expected error for missing index file
; RUN: %clang -O2 -o %t4.o -x ir %t1.o -c -fthinlto-index=bad.thinlto.bc 2>&1 | FileCheck %s -check-prefix=CHECK-ERROR1
; CHECK-ERROR1: Error loading index file 'bad.thinlto.bc'

; Ensure we ignore empty index file under -ignore-empty-index-file, and run
; non-ThinLTO compilation which would not import f2
; RUN: touch %t4.thinlto.bc
; RUN: %clang -target x86_64-unknown-linux-gnu -O2 -o %t4.o -x ir %t1.o -c -fthinlto-index=%t4.thinlto.bc -mllvm -ignore-empty-index-file
; RUN: llvm-nm %t4.o | FileCheck --check-prefix=CHECK-OBJ-IGNORE-EMPTY %s
; CHECK-OBJ-IGNORE-EMPTY: T f1
; CHECK-OBJ-IGNORE-EMPTY: U f2

; Ensure f2 was imported
; RUN: %clang -target x86_64-unknown-linux-gnu -O2 -o %t3.o -x ir %t1.o -c -fthinlto-index=%t.thinlto.bc
; RUN: llvm-nm %t3.o | FileCheck --check-prefix=CHECK-OBJ %s
; CHECK-OBJ: T f1
; CHECK-OBJ-NOT: U f2

; Ensure we get expected error for input files without summaries
; RUN: opt -o %t2.o %s
; RUN: %clang -target x86_64-unknown-linux-gnu -O2 -o %t3.o -x ir %t1.o -c -fthinlto-index=%t.thinlto.bc 2>&1 | FileCheck %s -check-prefix=CHECK-ERROR2
; CHECK-ERROR2: Error loading imported file '{{.*}}': Could not find module summary

target datalayout = "e-m:e-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

declare void @f2()

define void @f1() {
  call void @f2()
  ret void
}
