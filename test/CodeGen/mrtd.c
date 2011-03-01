// RUN: %clang_cc1 -mrtd -triple i386-unknown-freebsd9.0 -emit-llvm -o - %s | FileCheck %s

void baz(int arg);

// CHECK: define x86_stdcallcc void @foo(i32 %arg) nounwind
void foo(int arg) {
// CHECK: %call = call x86_stdcallcc i32 (...)* @bar(i32 %tmp)
  bar(arg);
// CHECK: call x86_stdcallcc void @baz(i32 %tmp1)
  baz(arg);
}

// CHECK: declare x86_stdcallcc i32 @bar(...)

// CHECK: declare x86_stdcallcc void @baz(i32)
