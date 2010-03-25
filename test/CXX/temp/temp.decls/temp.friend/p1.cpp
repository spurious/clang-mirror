// RUN: %clang_cc1 -faccess-control -verify -emit-llvm-only %s
template <typename T> struct Num {
  T value_;

public:
  Num(T value) : value_(value) {}
  T get() const { return value_; }

  template <typename U> struct Rep {
    U count_;
    Rep(U count) : count_(count) {}

    friend Num operator*(const Num &a, const Rep &n) {
      Num x = 0;
      for (U count = n.count_; count; --count)
        x += a;
      return x;
    } 
  };

  friend Num operator+(const Num &a, const Num &b) {
    return a.value_ + b.value_;
  }

  Num& operator+=(const Num& b) {
    value_ += b.value_;
    return *this;
  }

  class Representation {};
  friend class Representation;
};

class A {
  template <typename T> friend bool iszero(const A &a) throw();
};

template <class T> class B_iterator;
template <class T> class B {
  friend class B_iterator<T>;
};

int calc1() {
  Num<int> left = -1;
  Num<int> right = 1;
  Num<int> result = left + right;
  return result.get();
}

int calc2() {
  Num<int> x = 3;
  Num<int>::Rep<char> n = (char) 10;
  Num<int> result = x * n;
  return result.get();
}

// Reduced from GNU <locale>
namespace test1 {
  class A {
    bool b; // expected-note {{declared private here}}
    template <typename T> friend bool has(const A&);
  };
  template <typename T> bool has(const A &x) {
    return x.b;
  }
  template <typename T> bool hasnot(const A &x) {
    return x.b; // expected-error {{'b' is a private member of 'test1::A'}}
  }
}

namespace test2 {
  class A {
    bool b; // expected-note {{declared private here}}
    template <typename T> friend class HasChecker;
  };
  template <typename T> class HasChecker {
    bool check(A *a) {
      return a->b;
    }
  };
  template <typename T> class HasNotChecker {
    bool check(A *a) {
      return a->b; // expected-error {{'b' is a private member of 'test2::A'}}
    }
  };
}

namespace test3 {
  class Bool;
  template <class T> class User;
  template <class T> T transform(class Bool, T);

  class Bool {
    friend class User<bool>;
    friend bool transform<>(Bool, bool);

    bool value; // expected-note 2 {{declared private here}}
  };

  template <class T> class User {
    static T compute(Bool b) {
      return b.value; // expected-error {{'value' is a private member of 'test3::Bool'}}
    }
  };

  template <class T> T transform(Bool b, T value) {
    if (b.value) // expected-error {{'value' is a private member of 'test3::Bool'}}
      return value;
    return value + 1;
  }

  template bool transform(Bool, bool);
  template int transform(Bool, int); // expected-note {{requested here}}

  template class User<bool>;
  template class User<int>; // expected-note {{requested here}}
}

namespace test4 {
  template <class T> class A {
    template <class T0> friend class B;
    bool foo(const A<T> *) const;
  };

  template <class T> class B {
    bool bar(const A<T> *a, const A<T> *b) {
      return a->foo(b);
    }
  };

  template class B<int>;
}

namespace test5 {
  template <class T, class U=int> class A {};
  template <class T> class B {
    template <class X, class Y> friend class A;
  };
  template class B<int>;
  template class A<int>;
}

namespace Dependent {
  template<typename T, typename Traits> class X;
  template<typename T, typename Traits> 
  X<T, Traits> operator+(const X<T, Traits>&, const T*);

  template<typename T, typename Traits> class X {
    typedef typename Traits::value_type value_type;
    friend X operator+<>(const X&, const value_type*);
  };
}
