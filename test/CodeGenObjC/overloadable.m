// rdar://6657613
// RUN: clang-cc -emit-llvm %s -o -

@class C;

// RUN: grep _Z1fP11objc_object %t | count 1 && 
void __attribute__((overloadable)) f(C *c) { }

// RUN: grep _Z1fP1C %t | count 1
void __attribute__((overloadable)) f(id c) { }
