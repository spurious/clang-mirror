// RUN: cp %s %t
// RUN: %clang_cc1 -fixit -x c %t
// RUN: FileCheck -input-file=%t %t

// Note that this file is not valid UTF-8.

int test1 = '�';
// CHECK: int test1 = '\x88';

int test2 = 'ab�c';
// CHECK: int test2 = 'ab\x88c';
