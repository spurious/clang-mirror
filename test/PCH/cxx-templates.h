// Header for PCH test cxx-templates.cpp

template <typename T1, typename T2>
struct S {
  static void templ();
};

template <typename T>
struct S<int, T> {
    static void partial();
};

template <>
struct S<int, float> {
    static void explicit_special();
};

template <typename T>
T templ_f(T x) {
  return x;
}

template <typename T>
struct Dep {
  typedef typename T::type Ty;
  void f() {
    Ty x = Ty();
    T::my_f();
    int y = T::template my_templf<int>(0);
  }
};

template<typename T, typename A1>
inline T make_a(const A1& a1) {
  return T(a1);
}
