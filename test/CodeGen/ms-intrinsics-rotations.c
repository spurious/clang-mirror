// RUN: %clang_cc1 -ffreestanding -fms-extensions -fms-compatibility -fms-compatibility-version=17.00 \
// RUN:         -triple i686--windows -emit-llvm %s -o - \
// RUN:         | FileCheck %s --check-prefixes CHECK,CHECK-32BIT-LONG
// RUN: %clang_cc1 -ffreestanding -fms-extensions -fms-compatibility -fms-compatibility-version=17.00 \
// RUN:         -triple thumbv7--windows -emit-llvm %s -o - \
// RUN:         | FileCheck %s --check-prefixes CHECK,CHECK-32BIT-LONG
// RUN: %clang_cc1 -ffreestanding -fms-extensions -fms-compatibility -fms-compatibility-version=17.00 \
// RUN:         -triple x86_64--windows -emit-llvm %s -o - \
// RUN:         | FileCheck %s --check-prefixes CHECK,CHECK-32BIT-LONG
// RUN: %clang_cc1 -ffreestanding -fms-extensions -fms-compatibility -fms-compatibility-version=17.00 \
// RUN:         -triple i686--linux -emit-llvm %s -o - \
// RUN:         | FileCheck %s --check-prefixes CHECK,CHECK-32BIT-LONG
// RUN: %clang_cc1 -ffreestanding -fms-extensions -fms-compatibility -fms-compatibility-version=17.00 \
// RUN:         -triple x86_64--linux -emit-llvm %s -o - \
// RUN:         | FileCheck %s --check-prefixes CHECK,CHECK-32BIT-LONG
// RUN: %clang_cc1 -ffreestanding -fms-extensions \
// RUN:         -triple x86_64--darwin -emit-llvm %s -o - \
// RUN:         | FileCheck %s --check-prefixes CHECK,CHECK-32BIT-LONG

// LP64 targets use 'long' as 'int' for MS intrinsics (-fms-extensions)
#ifdef __LP64__
#define LONG int
#else
#define LONG long
#endif

// rotate left

unsigned char test_rotl8(unsigned char value, unsigned char shift) {
  return _rotl8(value, shift);
}
// CHECK: i8 @test_rotl8
// CHECK:   [[LSHIFT:%[0-9]+]] = and i8 [[SHIFT:%[0-9]+]], 7
// CHECK:   [[HIGH:%[0-9]+]] = shl i8 [[VALUE:%[0-9]+]], [[LSHIFT]]
// CHECK:   [[NEGATE:%[0-9]+]] = sub i8 0, [[SHIFT]]
// CHECK:   [[RSHIFT:%[0-9]+]] = and i8 [[NEGATE]], 7
// CHECK:   [[LOW:%[0-9]+]] = lshr i8 [[VALUE]], [[RSHIFT]]
// CHECK:   [[RESULT:%[0-9]+]] = or i8 [[HIGH]], [[LOW]]
// CHECK:   ret i8 [[RESULT]]
// CHECK  }

unsigned short test_rotl16(unsigned short value, unsigned char shift) {
  return _rotl16(value, shift);
}
// CHECK: i16 @test_rotl16
// CHECK:   [[LSHIFT:%[0-9]+]] = and i16 [[SHIFT:%[0-9]+]], 15
// CHECK:   [[HIGH:%[0-9]+]] = shl i16 [[VALUE:%[0-9]+]], [[LSHIFT]]
// CHECK:   [[NEGATE:%[0-9]+]] = sub i16 0, [[SHIFT]]
// CHECK:   [[RSHIFT:%[0-9]+]] = and i16 [[NEGATE]], 15
// CHECK:   [[LOW:%[0-9]+]] = lshr i16 [[VALUE]], [[RSHIFT]]
// CHECK:   [[RESULT:%[0-9]+]] = or i16 [[HIGH]], [[LOW]]
// CHECK:   ret i16 [[RESULT]]
// CHECK  }

unsigned int test_rotl(unsigned int value, int shift) {
  return _rotl(value, shift);
}
// CHECK: i32 @test_rotl
// CHECK:   [[LSHIFT:%[0-9]+]] = and i32 [[SHIFT:%[0-9]+]], 31
// CHECK:   [[HIGH:%[0-9]+]] = shl i32 [[VALUE:%[0-9]+]], [[LSHIFT]]
// CHECK:   [[NEGATE:%[0-9]+]] = sub i32 0, [[SHIFT]]
// CHECK:   [[RSHIFT:%[0-9]+]] = and i32 [[NEGATE]], 31
// CHECK:   [[LOW:%[0-9]+]] = lshr i32 [[VALUE]], [[RSHIFT]]
// CHECK:   [[RESULT:%[0-9]+]] = or i32 [[HIGH]], [[LOW]]
// CHECK:   ret i32 [[RESULT]]
// CHECK  }

unsigned LONG test_lrotl(unsigned LONG value, int shift) {
  return _lrotl(value, shift);
}
// CHECK-32BIT-LONG: i32 @test_lrotl
// CHECK-32BIT-LONG:   [[LSHIFT:%[0-9]+]] = and i32 [[SHIFT:%[0-9]+]], 31
// CHECK-32BIT-LONG:   [[HIGH:%[0-9]+]] = shl i32 [[VALUE:%[0-9]+]], [[LSHIFT]]
// CHECK-32BIT-LONG:   [[NEGATE:%[0-9]+]] = sub i32 0, [[SHIFT]]
// CHECK-32BIT-LONG:   [[RSHIFT:%[0-9]+]] = and i32 [[NEGATE]], 31
// CHECK-32BIT-LONG:   [[LOW:%[0-9]+]] = lshr i32 [[VALUE]], [[RSHIFT]]
// CHECK-32BIT-LONG:   [[RESULT:%[0-9]+]] = or i32 [[HIGH]], [[LOW]]
// CHECK-32BIT-LONG:   ret i32 [[RESULT]]
// CHECK-32BIT-LONG  }

unsigned __int64 test_rotl64(unsigned __int64 value, int shift) {
  return _rotl64(value, shift);
}
// CHECK: i64 @test_rotl64
// CHECK:   [[LSHIFT:%[0-9]+]] = and i64 [[SHIFT:%[0-9]+]], 63
// CHECK:   [[HIGH:%[0-9]+]] = shl i64 [[VALUE:%[0-9]+]], [[LSHIFT]]
// CHECK:   [[NEGATE:%[0-9]+]] = sub i64 0, [[SHIFT]]
// CHECK:   [[RSHIFT:%[0-9]+]] = and i64 [[NEGATE]], 63
// CHECK:   [[LOW:%[0-9]+]] = lshr i64 [[VALUE]], [[RSHIFT]]
// CHECK:   [[RESULT:%[0-9]+]] = or i64 [[HIGH]], [[LOW]]
// CHECK:   ret i64 [[RESULT]]
// CHECK  }

// rotate right

unsigned char test_rotr8(unsigned char value, unsigned char shift) {
  return _rotr8(value, shift);
}
// CHECK: i8 @test_rotr8
// CHECK:   [[RSHIFT:%[0-9]+]] = and i8 [[SHIFT:%[0-9]+]], 7
// CHECK:   [[LOW:%[0-9]+]] = lshr i8 [[VALUE:%[0-9]+]], [[RSHIFT]]
// CHECK:   [[NEGATE:%[0-9]+]] = sub i8 0, [[SHIFT]]
// CHECK:   [[LSHIFT:%[0-9]+]] = and i8 [[NEGATE]], 7
// CHECK:   [[HIGH:%[0-9]+]] = shl i8 [[VALUE]], [[LSHIFT]]
// CHECK:   [[RESULT:%[0-9]+]] = or i8 [[HIGH]], [[LOW]]
// CHECK  }

unsigned short test_rotr16(unsigned short value, unsigned char shift) {
  return _rotr16(value, shift);
}
// CHECK: i16 @test_rotr16
// CHECK:   [[RSHIFT:%[0-9]+]] = and i16 [[SHIFT:%[0-9]+]], 15
// CHECK:   [[LOW:%[0-9]+]] = lshr i16 [[VALUE:%[0-9]+]], [[RSHIFT]]
// CHECK:   [[NEGATE:%[0-9]+]] = sub i16 0, [[SHIFT]]
// CHECK:   [[LSHIFT:%[0-9]+]] = and i16 [[NEGATE]], 15
// CHECK:   [[HIGH:%[0-9]+]] = shl i16 [[VALUE]], [[LSHIFT]]
// CHECK:   [[RESULT:%[0-9]+]] = or i16 [[HIGH]], [[LOW]]
// CHECK  }

unsigned int test_rotr(unsigned int value, int shift) {
  return _rotr(value, shift);
}
// CHECK: i32 @test_rotr
// CHECK:   [[RSHIFT:%[0-9]+]] = and i32 [[SHIFT:%[0-9]+]], 31
// CHECK:   [[LOW:%[0-9]+]] = lshr i32 [[VALUE:%[0-9]+]], [[RSHIFT]]
// CHECK:   [[NEGATE:%[0-9]+]] = sub i32 0, [[SHIFT]]
// CHECK:   [[LSHIFT:%[0-9]+]] = and i32 [[NEGATE]], 31
// CHECK:   [[HIGH:%[0-9]+]] = shl i32 [[VALUE]], [[LSHIFT]]
// CHECK:   [[RESULT:%[0-9]+]] = or i32 [[HIGH]], [[LOW]]
// CHECK:   ret i32 [[RESULT]]
// CHECK  }

unsigned LONG test_lrotr(unsigned LONG value, int shift) {
  return _lrotr(value, shift);
}
// CHECK-32BIT-LONG: i32 @test_lrotr
// CHECK-32BIT-LONG:   [[RSHIFT:%[0-9]+]] = and i32 [[SHIFT:%[0-9]+]], 31
// CHECK-32BIT-LONG:   [[LOW:%[0-9]+]] = lshr i32 [[VALUE:%[0-9]+]], [[RSHIFT]]
// CHECK-32BIT-LONG:   [[NEGATE:%[0-9]+]] = sub i32 0, [[SHIFT]]
// CHECK-32BIT-LONG:   [[LSHIFT:%[0-9]+]] = and i32 [[NEGATE]], 31
// CHECK-32BIT-LONG:   [[HIGH:%[0-9]+]] = shl i32 [[VALUE]], [[LSHIFT]]
// CHECK-32BIT-LONG:   [[RESULT:%[0-9]+]] = or i32 [[HIGH]], [[LOW]]
// CHECK-32BIT-LONG:   ret i32 [[RESULT]]
// CHECK-32BIT-LONG  }

unsigned __int64 test_rotr64(unsigned __int64 value, int shift) {
  return _rotr64(value, shift);
}
// CHECK: i64 @test_rotr64
// CHECK:   [[RSHIFT:%[0-9]+]] = and i64 [[SHIFT:%[0-9]+]], 63
// CHECK:   [[LOW:%[0-9]+]] = lshr i64 [[VALUE:%[0-9]+]], [[RSHIFT]]
// CHECK:   [[NEGATE:%[0-9]+]] = sub i64 0, [[SHIFT]]
// CHECK:   [[LSHIFT:%[0-9]+]] = and i64 [[NEGATE]], 63
// CHECK:   [[HIGH:%[0-9]+]] = shl i64 [[VALUE]], [[LSHIFT]]
// CHECK:   [[RESULT:%[0-9]+]] = or i64 [[HIGH]], [[LOW]]
// CHECK:   ret i64 [[RESULT]]
// CHECK  }
