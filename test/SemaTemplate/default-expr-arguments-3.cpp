// FIXME: Remove the next line after a bit; this test used to
// write a .ll file and that confuses the bots. The next line
// cleans that up.
// RUN: rm -f %S/default-expr-arguments-3.ll
// RUN: %clang_cc1 -std=c++14 -verify %s
// expected-no-diagnostics

namespace PR28795 {
  template<typename T>
  void func() {
    enum class foo { a, b };
    auto bar = [](foo f = foo::a) { return f; };
    bar();
  }

  void foo() {
    func<int>();
  }
}

// Template struct case:
template <class T> struct class2 {
  void bar() {
    enum class foo { a, b };
    [](foo f = foo::a) { return f; }();
  }
};

template struct class2<int>;

template<typename T>
void f1() {
  enum class foo { a, b };
  struct S {
    int g1(foo n = foo::a);
  };
}

template void f1<int>();
