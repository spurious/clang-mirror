// RUN: %clang -Wuninitialized-experimental -fsyntax-only %s

int test1() {
  int x;
  return x; // expected-warning{{use of uninitialized variable 'x'}}
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
  int x;
  ++x; // expected-warning{{use of uninitialized variable 'x'}}
  return x; 
}

int test5() {
  int x, y;
  x = y; // expected-warning{{use of uninitialized variable 'y'}}
  return x;
}

int test6() {
  int x;
  x += 2; // expected-warning{{use of uninitialized variable 'x'}}
  return x;
}

int test7(int y) {
  int x;
  if (y)
    x = 1;
  return x;  // expected-warning{{use of uninitialized variable 'x'}}
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
  int x;
  for (unsigned i = 0 ; i < n; ++i) {
    if (i == n - 1)
      break;
    x = 1;    
  }
  return x; // expected-warning{{use of uninitialized variable 'x'}}
}

int test10(unsigned n) {
  int x;
  for (unsigned i = 0 ; i < n; ++i) {
    x = 1;
  }
  return x; // expected-warning{{use of uninitialized variable 'x'}}
}

int test11(unsigned n) {
  int x;
  for (unsigned i = 0 ; i <= n; ++i) {
    x = 1;
  }
  return x; // expected-warning{{use of uninitialized variable 'x'}}
}

void test12(unsigned n) {
  for (unsigned i ; n ; ++i) ; // expected-warning{{use of uninitialized variable 'i'}}
}

int test13() {
  static int i;
  return i; // no-warning
}


