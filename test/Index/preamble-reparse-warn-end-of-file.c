// RUN: mkdir -p %t
// RUN: touch %t/header.h
// RUN: env CINDEXTEST_EDITING=1 c-index-test -test-load-source-reparse 2 local %s -I %t 2> %t.err.txt > %t.out.txt
// RUN: cat %t.err.txt >> %t.out.txt
// RUN: FileCheck -input-file=%t.out.txt %s
// CHECK: preamble-reparse-warn-end-of-file.c:[[@LINE+6]]:6: FunctionDecl=test:[[@LINE+6]]:6
// CHECK: preamble-reparse-warn-end-of-file.c:[[@LINE+5]]:14: error: expected '}'
// CHECK: preamble-reparse-warn-end-of-file.c:[[@LINE+4]]:14: error: expected '}'

#include "header.h"

void test() {
