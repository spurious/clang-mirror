// Test this without pch.
// RUN: %clang_cc1 -include %S/cxx-required-decls.h %s -emit-llvm -o - | FileCheck %s

// Test with pch.
// RUN: %clang_cc1 -x c++-header -emit-pch -o %t %S/cxx-required-decls.h
// RUN: %clang_cc1 -include-pch %t %s -emit-llvm -o - | FileCheck %s

// CHECK: @_ZL5globS = internal global %struct.S zeroinitializer
