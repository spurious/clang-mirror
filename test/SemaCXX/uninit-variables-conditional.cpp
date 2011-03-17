// RUN: %clang_cc1 -fsyntax-only -Wconditional-uninitialized -fsyntax-only %s -verify

class Foo {
public:
  Foo();
  ~Foo();
  operator bool();
};

int bar();
int baz();
int init(double *);

// This case flags a false positive under -Wconditional-uninitialized because
// the destructor in Foo fouls about the minor bit of path-sensitivity in
// -Wuninitialized.
double test() {
  double x; // expected-note {{variable 'x' is declared here}} expected-note{{add initialization to silence this warning}}
  if (bar() || baz() || Foo() || init(&x)) {
    return x; // expected-warning {{variable 'x' is possibly uninitialized when used here}}
  }
  return 1.0;
}
