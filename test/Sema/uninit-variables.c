// RUN: %clang_cc1 -fsyntax-only -Wuninitialized-experimental -fsyntax-only %s -verify

int test1() {
  int x; // expected-warning{{use of uninitialized variable 'x'}}
  return x; // expected-note{{variable 'x' is possibly uninitialized when used here}}
}

int test2() {
  int x = 0;
  return x; // no-warning
}

int test3() {
  int x;
  x = 0;
  return x; // no-warning
}

int test4() {
  int x; // expected-warning{{use of uninitialized variable 'x'}}
  ++x; // expected-note{{variable 'x' is possibly uninitialized when used here}}
  return x; 
}

int test5() {
  int x, y; // expected-warning{{use of uninitialized variable 'y'}}
  x = y; // expected-note{{variable 'y' is possibly uninitialized when used here}}
  return x;
}

int test6() {
  int x; // expected-warning{{use of uninitialized variable 'x'}}
  x += 2; // expected-note{{variable 'x' is possibly uninitialized when used here}}
  return x;
}

int test7(int y) {
  int x; // expected-warning{{use of uninitialized variable 'x'}}
  if (y)
    x = 1;
  return x;  // expected-note{{variable 'x' is possibly uninitialized when used here}}
}

int test8(int y) {
  int x;
  if (y)
    x = 1;
  else
    x = 0;
  return x; // no-warning
}

int test9(int n) {
  int x; // expected-warning{{use of uninitialized variable 'x'}}
  for (unsigned i = 0 ; i < n; ++i) {
    if (i == n - 1)
      break;
    x = 1;
  }
  return x; // expected-note{{variable 'x' is possibly uninitialized when used here}}
}

int test10(unsigned n) {
  int x; // expected-warning{{use of uninitialized variable 'x'}}
  for (unsigned i = 0 ; i < n; ++i) {
    x = 1;
  }
  return x; // expected-note{{variable 'x' is possibly uninitialized when used here}}
}

int test11(unsigned n) {
  int x; // expected-warning{{use of uninitialized variable 'x'}}
  for (unsigned i = 0 ; i <= n; ++i) {
    x = 1;
  }
  return x; //expected-note{{variable 'x' is possibly uninitialized when used here}}
}

void test12(unsigned n) {
  for (unsigned i ; n ; ++i) ; // expected-warning{{use of uninitialized variable 'i'}} expected-note{{variable 'i' is possibly uninitialized when used here}}}
}

int test13() {
  static int i;
  return i; // no-warning
}

// Simply don't crash on this test case.
void test14() {
  const char *p = 0;
  for (;;) {}
}

void test15() {
  int x = x; // expected-warning{{use of uninitialized variable 'x'}} expected-note{{variable 'x' is possibly uninitialized when used here}}
}

// Don't warn in the following example; shows dataflow confluence.
char *test16_aux();
void test16() {
  char *p = test16_aux();
  for (unsigned i = 0 ; i < 100 ; i++)
    p[i] = 'a'; // no-warning
}

void test17() {
  // Don't warn multiple times about the same uninitialized variable
  // along the same path.
  int *x; // expected-warning{{use of uninitialized variable 'x'}} expected-note{{add initialization to silence this warning}}
  *x = 1; // expected-note{{variable 'x' is possibly uninitialized when used here}}
  *x = 1; // no-warning
}

int test18(int x, int y) {
  int z;
  if (x && y && (z = 1)) {
    return z; // no-warning
  }
  return 0;
}

int test19_aux1();
int test19_aux2();
int test19_aux3(int *x);
int test19() {
  int z;
  if (test19_aux1() + test19_aux2() && test19_aux1() && test19_aux3(&z))
    return z; // no-warning
  return 0;
}

int test20() {
  int z; // expected-warning{{use of uninitialized variable 'z'}}
  if ((test19_aux1() + test19_aux2() && test19_aux1()) || test19_aux3(&z))
    return z; //  expected-note{{variable 'z' is possibly uninitialized when used here}}
  return 0;
}

int test21(int x, int y) {
  int z; // expected-warning{{use of uninitialized variable 'z'}}  
  if ((x && y) || test19_aux3(&z) || test19_aux2())
    return z; // expected-note{{variable 'z' is possibly uninitialized when used here}}
  return 0;
}

int test22() {
  int z;
  while (test19_aux1() + test19_aux2() && test19_aux1() && test19_aux3(&z))
    return z; // no-warning
  return 0;
}

int test23() {
  int z;
  for ( ; test19_aux1() + test19_aux2() && test19_aux1() && test19_aux3(&z) ; )
    return z; // no-warning
  return 0;
}

// The basic uninitialized value analysis doesn't have enough path-sensitivity
// to catch initializations relying on control-dependencies spanning multiple
// conditionals.  This possibly can be handled by making the CFG itself
// represent such control-dependencies, but it is a niche case.
int test24(int flag) {
  unsigned val; // expected-warning{{use of uninitialized variable 'val'}}
  if (flag)
    val = 1;
  if (!flag)
    val = 1;
  return val; //  expected-note{{variable 'val' is possibly uninitialized when used here}}
}

