// RUN: %clang_cc1 -std=c++1z -verify %s

namespace std {
  using size_t = decltype(sizeof(0));
  template<typename T> struct initializer_list {
    const T *p;
    size_t n;
    initializer_list();
  };
  // FIXME: This should probably not be necessary.
  template<typename T> initializer_list(initializer_list<T>) -> initializer_list<T>;
}

template<typename T> constexpr bool has_type(...) { return false; }
template<typename T> constexpr bool has_type(T) { return true; }

std::initializer_list il = {1, 2, 3, 4, 5};

template<typename T> struct vector {
  template<typename Iter> vector(Iter, Iter);
  vector(std::initializer_list<T>);
};

template<typename T> vector(std::initializer_list<T>) -> vector<T>;
template<typename Iter> explicit vector(Iter, Iter) -> vector<typename Iter::value_type>;
template<typename T> explicit vector(std::size_t, T) -> vector<T>;

vector v1 = {1, 2, 3, 4};
static_assert(has_type<vector<int>>(v1));

struct iter { typedef char value_type; } it, end;
vector v2(it, end);
static_assert(has_type<vector<char>>(v2));

vector v3(5, 5);
static_assert(has_type<vector<int>>(v3));


template<typename ...T> struct tuple { tuple(T...); };
template<typename ...T> explicit tuple(T ...t) -> tuple<T...>;
// FIXME: Remove
template<typename ...T> tuple(tuple<T...>) -> tuple<T...>;

const int n = 4;
tuple ta = tuple{1, 'a', "foo", n};
static_assert(has_type<tuple<int, char, const char*, int>>(ta));

tuple tb{ta};
static_assert(has_type<tuple<int, char, const char*, int>>(ta));

// FIXME: This should be tuple<tuple<...>>;
tuple tc = {ta};
static_assert(has_type<tuple<int, char, const char*, int>>(ta));

tuple td = {1, 2, 3};
static_assert(has_type<tuple<int, char, const char*, int>>(ta));

// FIXME: This is a GCC extension for now; if CWG don't allow this, at least
// add a warning for it.
namespace new_expr {
  tuple<int> *p = new tuple{0};
  tuple<float, float> *q = new tuple(1.0f, 2.0f);
}

namespace ambiguity {
  template<typename T> struct A {};
  A(unsigned short) -> A<int>; // expected-note {{candidate}}
  A(short) -> A<int>; // expected-note {{candidate}}
  A a = 0; // expected-error {{ambiguous deduction for template arguments of 'A'}}

  template<typename T> struct B {};
  template<typename T> B(T(&)(int)) -> B<int>; // expected-note {{candidate function [with T = int]}}
  template<typename T> B(int(&)(T)) -> B<int>; // expected-note {{candidate function [with T = int]}}
  int f(int);
  B b = f; // expected-error {{ambiguous deduction for template arguments of 'B'}}
}

// FIXME: Revisit this once CWG decides if attributes, and [[deprecated]] in
// particular, should be permitted here.
namespace deprecated {
  template<typename T> struct A { A(int); };
  [[deprecated]] A(int) -> A<void>; // expected-note {{marked deprecated here}}
  A a = 0; // expected-warning {{'<deduction guide for A>' is deprecated}}
}
