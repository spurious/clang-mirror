// RUN: %clang_cc1 -emit-llvm < %s

int main(void)
{
  double _Complex a = 5;
  double _Complex b = 42;

  return a * b != b * a;
}

_Complex double bar(int);
void test(_Complex double*);
void takecomplex(_Complex double);

void test2(int c) {
  _Complex double X;
  X = bar(1);
  test(&X);
  takecomplex(X);
}

_Complex double g1, g2;
_Complex float cf;
double D;

void test3() {
  g1 = g1 + g2;
  g1 = g1 - g2;
  g1 = g1 * g2;
  g1 = +-~g1;

  double Gr = __real g1;

  cf += D;
  // FIXME: Currently unsupported!
  //D += cf;
  cf /= g1;
  g1 = g1 + D;
  g1 = D + g1;
}

void t1() {
  (__real__ cf) = 4.0;
}

void t2() {
  (__imag__ cf) = 4.0;
}

// PR1960
void t3() {
  __complex__ long long v = 2;
}

// PR3131
float _Complex t4();

void t5() {
  float _Complex x = t4();
}

