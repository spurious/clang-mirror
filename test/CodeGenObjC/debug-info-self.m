// RUN: %clang_cc1 -emit-llvm -triple %itanium_abi_triple -g %s -o - | FileCheck %s
// self and _cmd are marked as DW_AT_artificial. 
// myarg is not marked as DW_AT_artificial.

@interface MyClass {
}
- (id)init:(int) myarg;
@end

@implementation MyClass
- (id) init:(int) myarg
{
    return self;
}
@end

// CHECK: metadata !{metadata !"0x101\00self\0016777216\001088", metadata ![[CTOR:.*]], null, metadata !{{.*}}} ; [ DW_TAG_arg_variable ] [self] [line 0]
// CHECK: metadata !{metadata !"0x101\00_cmd\0033554432\0064", metadata ![[CTOR]], null, metadata !{{.*}}} ; [ DW_TAG_arg_variable ] [_cmd] [line 0]
// CHECK: metadata !{metadata !"0x101\00myarg\0050331659\000", metadata ![[CTOR]], metadata !{{.*}}, metadata !{{.*}}} ; [ DW_TAG_arg_variable ] [myarg] [line 11]
