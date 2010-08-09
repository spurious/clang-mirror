// Test this without pch.
// RUN: %clang_cc1 -include %S/objcxx-ivar-class.h -verify %s -emit-llvm -o - | FileCheck %s

// Test with pch.
// RUN: %clang_cc1 -x objective-c++-header -emit-pch -o %t %S/objcxx-ivar-class.h
// RUN: %clang_cc1 -include-pch %t -verify %s -emit-llvm -o - | FileCheck %s

// CHECK: [C .cxx_destruct]
// CHECK: [C .cxx_construct]
