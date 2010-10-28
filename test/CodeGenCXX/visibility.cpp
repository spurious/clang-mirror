// RUN: %clang_cc1 %s -triple=x86_64-apple-darwin10 -emit-llvm -o - | FileCheck %s
// RUN: %clang_cc1 %s -triple=x86_64-apple-darwin10 -fvisibility hidden -emit-llvm -o - | FileCheck %s -check-prefix=CHECK-HIDDEN

#define HIDDEN __attribute__((visibility("hidden")))
#define PROTECTED __attribute__((visibility("protected")))
#define DEFAULT __attribute__((visibility("default")))

// CHECK: @_ZN5Test425VariableInHiddenNamespaceE = hidden global i32 10
// CHECK: @_ZN5Test71aE = hidden global
// CHECK: @_ZN5Test71bE = global
// CHECK: @test9_var = global
// CHECK-HIDDEN: @test9_var = global
// CHECK: @_ZTVN5Test63fooE = weak_odr hidden constant 

namespace Test1 {
  // CHECK: define hidden void @_ZN5Test11fEv
  void HIDDEN f() { }
  
}

namespace Test2 {
  struct HIDDEN A {
    void f();
  };

  // A::f is a member function of a hidden class.
  // CHECK: define hidden void @_ZN5Test21A1fEv
  void A::f() { }
}
 
namespace Test3 {
  struct HIDDEN A {
    struct B {
      void f();
    };
  };

  // B is a nested class where its parent class is hidden.
  // CHECK: define hidden void @_ZN5Test31A1B1fEv
  void A::B::f() { }  
}

namespace Test4 HIDDEN {
  int VariableInHiddenNamespace = 10;

  // Test4::g is in a hidden namespace.
  // CHECK: define hidden void @_ZN5Test41gEv
  void g() { } 
  
  struct DEFAULT A {
    void f();
  };
  
  // A has default visibility.
  // CHECK: define void @_ZN5Test41A1fEv
  void A::f() { } 
}

namespace Test5 {

  namespace NS HIDDEN {
    // f is in NS which is hidden.
    // CHECK: define hidden void @_ZN5Test52NS1fEv()
    void f() { }
  }
  
  namespace NS {
    // g is in NS, but this NS decl is not hidden.
    // CHECK: define void @_ZN5Test52NS1gEv
    void g() { }
  }
}

// <rdar://problem/8091955>
namespace Test6 {
  struct HIDDEN foo {
    foo() { }
    void bonk();
    virtual void bar() = 0;

    virtual void zonk() {}
  };

  struct barc : public foo {
    barc();
    virtual void bar();
  };

  barc::barc() {}
}

namespace Test7 {
  class HIDDEN A {};
  A a; // top of file

  template <A&> struct Aref {
    static void foo() {}
  };

  class B : public A {};
  B b; // top of file

  // CHECK: define linkonce_odr hidden void @_ZN5Test74ArefILZNS_1aEEE3fooEv()
  void test() {
    Aref<a>::foo();
  }
}

namespace Test8 {
  void foo();
  void bar() {}
  // CHECK-HIDDEN: define hidden void @_ZN5Test83barEv()
  // CHECK-HIDDEN: declare void @_ZN5Test83fooEv()

  void test() {
    foo();
    bar();
  }
}

// PR8457
namespace Test9 {
  extern "C" {
    struct A { int field; };
    void DEFAULT test9_fun(struct A *a) { }
    struct A DEFAULT test9_var; // above
  }
  // CHECK: define void @test9_fun(
  // CHECK-HIDDEN: define void @test9_fun(

  void test() {
    A a = test9_var;
    test9_fun(&a);
  }
}

// PR8478
namespace Test10 {
  struct A;

  DEFAULT class B {
    void foo(A*);
  };

  // CHECK: define void @_ZN6Test101B3fooEPNS_1AE(
  // CHECK-HIDDEN: define void @_ZN6Test101B3fooEPNS_1AE(
  void B::foo(A*) {}
}
