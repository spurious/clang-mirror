// RUN: clang -fsyntax-only -verify %s 
class X { };

X operator+(X, X);

void f(X x) {
  x = x + x;
}

struct Y;
struct Z;

struct Y {
  Y(const Z&);
};

struct Z {
  Z(const Y&);
};

Y operator+(Y, Y);
bool operator-(Y, Y); // expected-note{{candidate function}}
bool operator-(Z, Z); // expected-note{{candidate function}}

void g(Y y, Z z) {
  y = y + z;
  bool b = y - z; // expected-error{{use of overloaded operator '-' is ambiguous; candidates are:}}
}

struct A {
  bool operator==(Z&); // expected-note{{candidate function}}
};

A make_A();

bool operator==(A&, Z&); // expected-note{{candidate function}}

void h(A a, const A ac, Z z) {
  make_A() == z;
  a == z; // expected-error{{use of overloaded operator '==' is ambiguous; candidates are:}}
  ac == z; // expected-error{{invalid operands to binary expression ('struct A const' and 'struct Z')}}
}

struct B {
  bool operator==(const B&) const;

  void test(Z z) {
    make_A() == z;
  }
};

enum Enum1 { };
enum Enum2 { };

struct E1 {
  E1(Enum1) { }
};

struct E2 {
  E2(Enum2);
};

// C++ [over.match.oper]p3 - enum restriction.
float& operator==(E1, E2); 

void enum_test(Enum1 enum1, Enum2 enum2, E1 e1, E2 e2) {
  float &f1 = (e1 == e2);
  float &f2 = (enum1 == e2); 
  float &f3 = (e1 == enum2); 
  float &f4 = (enum1 == enum2);  // expected-error{{non-const reference to type 'float' cannot be initialized with a temporary of type '_Bool'}}
}
