// RUN: %clang_cc1 -triple s390x-linux-gnu -emit-llvm %s -o - | FileCheck %s

// SystemZ prefers to align all global variables to two bytes.

struct test {
   signed char a;
};

char c;
// CHECK-DAG: @c = common global i8 0, align 2

struct test s;
// CHECK-DAG: @s = common global %struct.test zeroinitializer, align 2

