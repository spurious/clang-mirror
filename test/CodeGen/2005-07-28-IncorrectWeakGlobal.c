// RUN: %clang_cc1 %s -emit-llvm -o - | grep TheGlobal | not grep weak

extern int TheGlobal;
int foo() { return TheGlobal; }
int TheGlobal = 1;
