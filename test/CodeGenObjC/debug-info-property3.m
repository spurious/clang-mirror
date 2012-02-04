// RUN: %clang -S -emit-llvm -g %s -o - | FileCheck %s

// CHECK: metadata !"p1", metadata !"p1", metadata !"setP1:", i32 2316} ; [ DW_TAG_APPLE_property ]
@interface I1
@property int p1;
@end

@implementation I1
@synthesize p1;
@end

void foo(I1 *iptr) {}
