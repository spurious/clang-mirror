// RUN: %clang_cc1 -analyze -analyzer-check-objc-mem -verify %s

int f1(char *dst) {
  char *p = dst + 4;
  char *q = dst + 3;
  return !(q >= p);
}
