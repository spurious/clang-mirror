// RUN: clang %s -emit-llvm > %t1
// RUN: grep "shl i32 %tmp, 19" %t1 &&
// RUN: grep "ashr i32 %tmp1, 19" %t1 &&
// RUN: grep "shl i16 %tmp4, 1" %t1 &&
// RUN: grep "lshr i16 %tmp5, 9" %t1
// Test bitfield access


struct STestB1 { int a:13; char b; unsigned short c:7;} stb1;

int f() {
  return stb1.a + stb1.b + stb1.c;
}
