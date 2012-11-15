// RUN: %clang_cc1 -fblocks -triple x86_64-apple-darwin -encode-extended-block-signature -emit-llvm %s -o - | FileCheck %s
// RUN: %clang_cc1 -fblocks -triple x86_64-apple-darwin -emit-llvm %s -o - | FileCheck %s -check-prefix=EXPANDED
// rdar://12109031

@class NSString, NSArray;

typedef NSString*(^BBB)(NSArray*);

int main ()
{
  BBB b1;
  ^(BBB arg1, double arg2){ return b1; }(0, 3.14);
}
// CHECK: @{{.*}} = private unnamed_addr constant [64 x i8] c"@?<@\22NSString\22@?@\22NSArray\22>24@?0@?<@\22NSString\22@?@\22NSArray\22>8d16\00"
// CHECK-EXPANDED: @{{.*}} = private unnamed_addr constant [64 x i8] c"@?<@\22NSString\22@?@\22NSArray\22>24@?0@?<@\22NSString\22@?@\22NSArray\22>8d16\00"
