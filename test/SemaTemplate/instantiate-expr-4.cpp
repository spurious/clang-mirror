// RUN: clang-cc -fsyntax-only -verify %s

// ---------------------------------------------------------------------
// C++ Functional Casts
// ---------------------------------------------------------------------
template<int N>
struct ValueInit0 {
  int f() {
    return int();
  }
};

template struct ValueInit0<5>;

template<int N>
struct FunctionalCast0 {
  int f() {
    return int(N);
  }
};

template struct FunctionalCast0<5>;

struct X { // expected-note 2 {{candidate function}}
  X(int, int); // expected-note 2 {{candidate function}}
};

template<int N, int M>
struct BuildTemporary0 {
  X f() {
    return X(N, M);
  }
};

template struct BuildTemporary0<5, 7>;

template<int N, int M>
struct Temporaries0 {
  void f() {
    (void)X(N, M);
  }
};

template struct Temporaries0<5, 7>;

// ---------------------------------------------------------------------
// new expressions
// ---------------------------------------------------------------------
struct Y { };

template<typename T>
struct New0 {
  T* f(bool x) {
    if (x)
      return new T; // expected-error{{no matching}}
    else
      return new T();
  }
};

template struct New0<int>;
template struct New0<Y>;
template struct New0<X>; // expected-note{{instantiation}}

template<typename T, typename Arg1>
struct New1 {
  T* f(bool x, Arg1 a1) {
    return new T(a1); // expected-error{{no matching}}
  }
};

template struct New1<int, float>;
template struct New1<Y, Y>;
template struct New1<X, Y>; // expected-note{{instantiation}}

template<typename T, typename Arg1, typename Arg2>
struct New2 {
  T* f(bool x, Arg1 a1, Arg2 a2) {
    return new T(a1, a2); // expected-error{{no matching}}
  }
};

template struct New2<X, int, float>;
template struct New2<X, int, int*>; // expected-note{{instantiation}}
// FIXME: template struct New2<int, int, float>;
