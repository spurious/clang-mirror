// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-obj -o - -x c %s
// Reproduce the crash in PR19760.
static void foo(void) {}
void bar(void) __attribute__((alias("foo")))
__attribute__((visibility("hidden")));

// CHECK: @bar = hidden alias void ()* @foo
// CHECK: define internal void @foo()
