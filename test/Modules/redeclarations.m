@import redeclarations_left;
@import redeclarations_right;

@interface MyObject : NSObject
@end

// RUN: rm -rf %t
// RUN: %clang_cc1 -x objective-c -fmodule-cache-path %t -emit-module -fmodule-name=redeclarations_left %S/Inputs/module.map
// RUN: %clang_cc1 -x objective-c -fmodule-cache-path %t -emit-module -fmodule-name=redeclarations_right %S/Inputs/module.map
// RUN: %clang_cc1 -fmodule-cache-path %t %s -verify

