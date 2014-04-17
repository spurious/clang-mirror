// RUN: %clang_cc1 -triple arm64-linux-gnu -target-abi aapcs -ffreestanding -emit-llvm -w -o - %s | FileCheck %s

// AAPCS clause C.8 says: If the argument has an alignment of 16 then the NGRN
// is rounded up to the next even number.

// CHECK: void @test1(i32 %x0, i128 %x2_x3, i128 %x4_x5, i128 %x6_x7, i128 %sp.coerce)
typedef union { __int128 a; } Small;
void test1(int x0, __int128 x2_x3, __int128 x4_x5, __int128 x6_x7, Small sp) {
}


// CHECK: void @test2(i32 %x0, i128 %x2_x3.coerce, i32 %x4, i128 %x6_x7.coerce, i32 %sp, i128 %sp16.coerce)
void test2(int x0, Small x2_x3, int x4, Small x6_x7, int sp, Small sp16) {
}

// We coerce HFAs into a contiguous [N x double] type if they're going on the
// stack in order to avoid holes. Make sure we get all of them, and not just the
// first:

// CHECK: void @test3(float %s0_s3.0, float %s0_s3.1, float %s0_s3.2, float %s0_s3.3, float %s4, [3 x float], [2 x double] %sp.coerce, [2 x double] %sp16.coerce)
typedef struct { float arr[4]; } HFA;
void test3(HFA s0_s3, float s4, HFA sp, HFA sp16) {
}
