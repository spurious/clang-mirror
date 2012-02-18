// RUN: %clang_cc1 -std=c++11 %s -Wunused -verify


struct X {
  X(const X&) = delete; // expected-note{{explicitly marked deleted}}
  X(X&);
};

void test_capture(X x) {
  [x] { }(); // okay: non-const copy ctor

  [x] {
    [x] { // expected-error{{call to deleted constructor of 'const X'}}
    }();
  }();

  int a; 
  [=]{ [&] { int&x = a; }(); }(); // expected-error{{binding of reference to type 'int' to a value of type 'const int' drops qualifiers}}
}
