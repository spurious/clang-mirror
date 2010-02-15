// RUN: %clang_cc1 -verify %s
template<typename T>
void f0() {
  struct X;
  typedef struct Y {
    T (X::* f1())(int) { return 0; }
  } Y2;

  Y2 y = Y();
}

template void f0<int>();

// PR5764
namespace PR5764 {
  class X {
    template <typename T>
    void Bar() {
      typedef T ValueType;
      class Y {
        Y() { V = ValueType(); }

        ValueType V;
      };

      Y y;
    }
  };

  void test(X x) {
    x.Bar<int>();
  }
}

// Instantiation of local classes with virtual functions.
namespace local_class_with_virtual_functions {
  template <typename T> struct X { };
  template <typename T> struct Y { };

  template <typename T>
  void f() {
    struct Z : public X<Y<T>*> {
      virtual void g(Y<T>* y) { }
      void g2(int x) {(void)x;}
    };
    Z z;
    (void)z;
  }

  struct S { };
  void test() { f<S>(); }
}
