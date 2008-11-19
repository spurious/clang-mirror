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


struct PostInc {
  PostInc operator++(int);
  PostInc& operator++();
};

struct PostDec {
  PostDec operator--(int);
  PostDec& operator--();
};

void incdec_test(PostInc pi, PostDec pd) {
  const PostInc& pi1 = pi++;
  const PostDec& pd1 = pd--;
  PostInc &pi2 = ++pi;
  PostDec &pd2 = --pd;
}

struct SmartPtr {
  int& operator*();
  // FIXME: spurious error:  long& operator*() const;
};

void test_smartptr(SmartPtr ptr, const SmartPtr cptr) {
  int &ir = *ptr;
  // FIXME: reinstate long &lr = *cptr;
}


struct ArrayLike {
  int& operator[](int);
};

void test_arraylike(ArrayLike a) {
  int& ir = a[17];
}

struct SmartRef {
  int* operator&();
};

void test_smartref(SmartRef r) {
  int* ip = &r;
}

bool& operator,(X, Y);

void test_comma(X x, Y y) {
  bool& b1 = (x, y);
  X& xr = (x, x);
}


struct Callable {
  int& operator()(int, double = 2.71828); // expected-note{{candidate function}}
  float& operator()(int, double, long, ...); // expected-note{{candidate function}}
};

void test_callable(Callable c) {
  int &ir = c(1);
  float &fr = c(1, 3.14159, 17, 42);

  c(); // expected-error{{no matching function for call to object of type 'struct Callable'; candidates are:}}
}

typedef int& Func1(float, double);
typedef float& Func2(int, double);

struct ConvertToFunc {
  operator Func1*(); // expected-note{{conversion candidate of type 'int &(*)(float, double)'}}
  operator Func2&(); // expected-note{{conversion candidate of type 'float &(&)(int, double)'}}
};

void test_funcptr_call(ConvertToFunc ctf) {
  int &i1 = ctf(1.0f, 2.0);
  float &f2 = ctf((short int)1, 1.0f);
  ctf((long int)17, 2.0); // expected-error{{error: call to object of type 'struct ConvertToFunc' is ambiguous; candidates are:}}
}
