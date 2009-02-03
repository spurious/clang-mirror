// RUN: clang -triple i386-unknown-unknown %s -emit-llvm-bc -o - | opt -std-compile-opts | llvm-dis > %t &&
// RUN: grep "ret i32" %t | count 4 &&
// RUN: grep "ret i32 1" %t | count 4

static int f0(int n) {
  struct s0 {
    int a : 30;
    int b : 2;
    long long c : 31;
  } x = { 0xdeadbeef, 0xdeadbeef, 0xdeadbeef };
  
  x.a += n;
  x.b += n;
  x.c += n;

  return x.a + x.b + x.c;
}

int g0(void) {
  return f0(-1) + 44335655;
}

static int f1(void) {
  struct s1 { 
    int a:13; 
    char b; 
    unsigned short c:7;
  } x;
  
  x.a = -40;
  x.b = 10;
  x.c = 15;

  return x.a + x.b + x.c;
}

int g1(void) {
  return f1() + 16;
}

static int f2(void) {
  struct s2 {
    short a[3];
    int b : 15;
  } x;
  
  x.a[0] = x.a[1] = x.a[2] = -40;
  x.b = 10;

  return x.b;
}

int g2(void) {
  return f2() - 9;
}

static int f3(int n) {
  struct s3 {
    unsigned a:16;
    unsigned b:28 __attribute__ ((packed));
  } x = { 0xdeadbeef, 0xdeadbeef };
  struct s4 {
    signed a:16;
    signed b:28 __attribute__ ((packed));
  } y;
  y.a = -0x56789abcL;
  y.b = -0x56789abcL;
  return ((y.a += x.a += n) + 
          (y.b += x.b += n));
}

int g3(void) {
  return f3(20) + 130725747;
}
