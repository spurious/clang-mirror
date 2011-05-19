// RUN: %clang_cc1 -fcxx-exceptions -fexceptions -triple x86_64-apple-darwin -std=c++0x -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s

struct test1_D {
  double d;
} d1;

void test1() {
  throw d1;
}

// CHECK:     define void @_Z5test1v()
// CHECK:       [[EXNOBJ:%.*]] = call i8* @__cxa_allocate_exception(i64 8)
// CHECK-NEXT:  [[EXN:%.*]] = bitcast i8* [[EXNOBJ]] to [[DSTAR:%[^*]*\*]]
// CHECK-NEXT:  [[EXN2:%.*]] = bitcast [[DSTAR]] [[EXN]] to i8*
// CHECK-NEXT:  call void @llvm.memcpy.p0i8.p0i8.i64(i8* [[EXN2]], i8* bitcast ([[DSTAR]] @d1 to i8*), i64 8, i32 8, i1 false)
// CHECK-NEXT:  call void @__cxa_throw(i8* [[EXNOBJ]], i8* bitcast (%0* @_ZTI7test1_D to i8*), i8* null) noreturn
// CHECK-NEXT:  unreachable


struct test2_D {
  test2_D(const test2_D&o);
  test2_D();
  virtual void bar() { }
  int i; int j;
} d2;

void test2() {
  throw d2;
}

// CHECK:     define void @_Z5test2v()
// CHECK:       [[EXNSLOTVAR:%.*]] = alloca i8*
// CHECK-NEXT:  [[CLEANUPDESTVAR:%.*]] = alloca i32
// CHECK-NEXT:  [[EXNOBJ:%.*]] = call i8* @__cxa_allocate_exception(i64 16)
// CHECK-NEXT:  [[EXN:%.*]] = bitcast i8* [[EXNOBJ]] to [[DSTAR:%[^*]*\*]]
// CHECK-NEXT:  invoke void @_ZN7test2_DC1ERKS_([[DSTAR]] [[EXN]], [[DSTAR]] @d2)
// CHECK-NEXT:     to label %[[CONT:.*]] unwind label %{{.*}}
//      :     [[CONT]]:   (can't check this in Release-Asserts builds)
// CHECK:       call void @__cxa_throw(i8* [[EXNOBJ]], i8* bitcast (%{{.*}}* @_ZTI7test2_D to i8*), i8* null) noreturn
// CHECK-NEXT:  unreachable


struct test3_D {
  test3_D() { }
  test3_D(volatile test3_D&o);
  virtual void bar();
};

void test3() {
  throw (volatile test3_D *)0;
}

// CHECK:     define void @_Z5test3v()
// CHECK:       [[EXNOBJ:%.*]] = call i8* @__cxa_allocate_exception(i64 8)
// CHECK-NEXT:  [[EXN:%.*]] = bitcast i8* [[EXNOBJ]] to [[D:%[^*]+]]**
// CHECK-NEXT:  store [[D]]* null, [[D]]** [[EXN]]
// CHECK-NEXT:  call void @__cxa_throw(i8* [[EXNOBJ]], i8* bitcast (%1* @_ZTIPV7test3_D to i8*), i8* null) noreturn
// CHECK-NEXT:  unreachable


void test4() {
  throw;
}

// CHECK:     define void @_Z5test4v()
// CHECK:        call void @__cxa_rethrow() noreturn
// CHECK-NEXT:   unreachable


// rdar://problem/7696549
namespace test5 {
  struct A {
    A();
    A(const A&);
    ~A();
  };

  void test() {
    try { throw A(); } catch (A &x) {}
  }
// CHECK:      define void @_ZN5test54testEv()
// CHECK:      [[EXNOBJ:%.*]] = call i8* @__cxa_allocate_exception(i64 1)
// CHECK:      [[EXNCAST:%.*]] = bitcast i8* [[EXNOBJ]] to [[A:%[^*]*]]*
// CHECK-NEXT: invoke void @_ZN5test51AC1Ev([[A]]* [[EXNCAST]])
// CHECK:      invoke void @__cxa_throw(i8* [[EXNOBJ]], i8* bitcast ({{%.*}}* @_ZTIN5test51AE to i8*), i8* bitcast (void ([[A]]*)* @_ZN5test51AD1Ev to i8*)) noreturn
// CHECK-NEXT:   to label {{%.*}} unwind label %[[HANDLER:[^ ]*]]
//      :    [[HANDLER]]:  (can't check this in Release-Asserts builds)
// CHECK:      {{%.*}} = call i32 @llvm.eh.typeid.for(i8* bitcast ({{%.*}}* @_ZTIN5test51AE to i8*))
}

namespace test6 {
  template <class T> struct allocator {
    ~allocator() throw() { }
  };

  void foo() {
    allocator<int> a;
  }
}

// PR7127
namespace test7 {
// CHECK:      define i32 @_ZN5test73fooEv() 
  int foo() {
// CHECK:      [[CAUGHTEXNVAR:%.*]] = alloca i8*
// CHECK-NEXT: [[INTCATCHVAR:%.*]] = alloca i32
// CHECK-NEXT: [[EHCLEANUPDESTVAR:%.*]] = alloca i32
    try {
      try {
// CHECK-NEXT: [[EXNALLOC:%.*]] = call i8* @__cxa_allocate_exception
// CHECK-NEXT: bitcast i8* [[EXNALLOC]] to i32*
// CHECK-NEXT: store i32 1, i32*
// CHECK-NEXT: invoke void @__cxa_throw(i8* [[EXNALLOC]], i8* bitcast (i8** @_ZTIi to i8*), i8* null
        throw 1;
      }

// CHECK:      [[CAUGHTEXN:%.*]] = call i8* @llvm.eh.exception()
// CHECK-NEXT: store i8* [[CAUGHTEXN]], i8** [[CAUGHTEXNVAR]]
// CHECK-NEXT: call i32 (i8*, i8*, ...)* @llvm.eh.selector(i8* [[CAUGHTEXN]], i8* bitcast (i32 (...)* @__gxx_personality_v0 to i8*), i8* bitcast (i8** @_ZTIi to i8*), i8* null)
// CHECK-NEXT: call i32 @llvm.eh.typeid.for(i8* bitcast (i8** @_ZTIi to i8*))
// CHECK-NEXT: icmp eq
// CHECK-NEXT: br i1
// CHECK:      load i8** [[CAUGHTEXNVAR]]
// CHECK-NEXT: call i8* @__cxa_begin_catch
// CHECK:      invoke void @__cxa_rethrow
      catch (int) {
        throw;
      }
    }
// CHECK:      [[CAUGHTEXN:%.*]] = call i8* @llvm.eh.exception()
// CHECK-NEXT: store i8* [[CAUGHTEXN]], i8** [[CAUGHTEXNVAR]]
// CHECK-NEXT: call i32 (i8*, i8*, ...)* @llvm.eh.selector(i8* [[CAUGHTEXN]], i8* bitcast (i32 (...)* @__gxx_personality_v0 to i8*), i8* null)
// CHECK-NEXT: store i32 1, i32* [[EHCLEANUPDESTVAR]]
// CHECK-NEXT: call void @__cxa_end_catch()
// CHECK-NEXT: br label
// CHECK:      load i8** [[CAUGHTEXNVAR]]
// CHECK-NEXT: call i8* @__cxa_begin_catch
// CHECK-NEXT: call void @__cxa_end_catch
    catch (...) {
    }
// CHECK:      ret i32 0
    return 0;
  }
}

// Ordering of destructors in a catch handler.
namespace test8 {
  struct A { A(const A&); ~A(); };
  void bar();

  // CHECK: define void @_ZN5test83fooEv()
  void foo() {
    try {
      // CHECK:      invoke void @_ZN5test83barEv()
      bar();
    } catch (A a) {
      // CHECK:      call i8* @__cxa_get_exception_ptr
      // CHECK-NEXT: bitcast
      // CHECK-NEXT: invoke void @_ZN5test81AC1ERKS0_(
      // CHECK:      call i8* @__cxa_begin_catch
      // CHECK-NEXT: call void @_ZN5test81AD1Ev(
      // CHECK:      call void @__cxa_end_catch()
      // CHECK:      ret void
    }
  }
}

// Constructor function-try-block must rethrow on fallthrough.
// rdar://problem/7696603
namespace test9 {
  void opaque();

  struct A { A(); };

  // CHECK:      define void @_ZN5test91AC1Ev(%"struct.test10::A"* %this) unnamed_addr
  // CHECK:      call void @_ZN5test91AC2Ev
  // CHECK-NEXT: ret void

  // CHECK: define void @_ZN5test91AC2Ev(%"struct.test10::A"* %this) unnamed_addr
  A::A() try {
  // CHECK:      invoke void @_ZN5test96opaqueEv()
    opaque();
  } catch (int x) {
  // CHECK:      call i8* @__cxa_begin_catch
  // CHECK:      invoke void @_ZN5test96opaqueEv()
  // CHECK:      invoke void @__cxa_rethrow()
    opaque();
  }

  // landing pad from first call to invoke
  // CHECK:      call i8* @llvm.eh.exception
  // CHECK:      call i32 (i8*, i8*, ...)* @llvm.eh.selector(i8* {{.*}}, i8* bitcast (i32 (...)* @__gxx_personality_v0 to i8*), i8* bitcast (i8** @_ZTIi to i8*), i8* null)
}

// __cxa_end_catch can throw for some kinds of caught exceptions.
namespace test10 {
  void opaque();

  struct A { ~A(); };
  struct B { int x; };

  // CHECK: define void @_ZN6test103fooEv()
  void foo() {
    A a; // force a cleanup context

    try {
    // CHECK:      invoke void @_ZN6test106opaqueEv()
      opaque();
    } catch (int i) {
    // CHECK:      call i8* @__cxa_begin_catch
    // CHECK-NEXT: bitcast
    // CHECK-NEXT: load i32*
    // CHECK-NEXT: store i32
    // CHECK-NEXT: call void @__cxa_end_catch() nounwind
    } catch (B a) {
    // CHECK:      call i8* @__cxa_begin_catch
    // CHECK-NEXT: bitcast
    // CHECK-NEXT: bitcast
    // CHECK-NEXT: bitcast
    // CHECK-NEXT: call void @llvm.memcpy
    // CHECK-NEXT: invoke void @__cxa_end_catch()
    } catch (...) {
    // CHECK:      call i8* @__cxa_begin_catch
    // CHECK-NEXT: invoke void @__cxa_end_catch()
    }

    // CHECK: call void @_ZN6test101AD1Ev(
  }
}

// __cxa_begin_catch returns pointers by value, even when catching by reference
// <rdar://problem/8212123>
namespace test11 {
  void opaque();

  // CHECK: define void @_ZN6test113fooEv()
  void foo() {
    try {
      // CHECK:      invoke void @_ZN6test116opaqueEv()
      opaque();
    } catch (int**&p) {
      // CHECK:      [[EXN:%.*]] = load i8**
      // CHECK-NEXT: call i8* @__cxa_begin_catch(i8* [[EXN]]) nounwind
      // CHECK-NEXT: [[ADJ1:%.*]] = getelementptr i8* [[EXN]], i32 32
      // CHECK-NEXT: [[ADJ2:%.*]] = bitcast i8* [[ADJ1]] to i32***
      // CHECK-NEXT: store i32*** [[ADJ2]], i32**** [[P:%.*]]
      // CHECK-NEXT: call void @__cxa_end_catch() nounwind
    }
  }

  struct A {};

  // CHECK: define void @_ZN6test113barEv()
  void bar() {
    try {
      // CHECK:      [[EXNSLOT:%.*]] = alloca i8*
      // CHECK-NEXT: [[P:%.*]] = alloca [[A:%.*]]**,
      // CHECK-NEXT: [[TMP:%.*]] = alloca [[A]]*
      // CHECK-NEXT: invoke void @_ZN6test116opaqueEv()
      opaque();
    } catch (A*&p) {
      // CHECK:      [[EXN:%.*]] = load i8** [[EXNSLOT]]
      // CHECK-NEXT: [[ADJ1:%.*]] = call i8* @__cxa_begin_catch(i8* [[EXN]]) nounwind
      // CHECK-NEXT: [[ADJ2:%.*]] = bitcast i8* [[ADJ1]] to [[A]]*
      // CHECK-NEXT: store [[A]]* [[ADJ2]], [[A]]** [[TMP]]
      // CHECK-NEXT: store [[A]]** [[TMP]], [[A]]*** [[P]]
      // CHECK-NEXT: call void @__cxa_end_catch() nounwind
    }
  }
}

// PR7686
namespace test12 {
  struct A { ~A() noexcept(false); };
  bool opaque(const A&);

  // CHECK: define void @_ZN6test124testEv()
  void test() {
    // CHECK: [[X:%.*]] = alloca [[A:%.*]],
    // CHECK: [[EHCLEANUPDEST:%.*]] = alloca i32
    // CHECK: [[Y:%.*]] = alloca [[A]]
    // CHECK: [[Z:%.*]] = alloca [[A]]
    // CHECK: [[CLEANUPDEST:%.*]] = alloca i32

    A x;
    // CHECK: invoke zeroext i1 @_ZN6test126opaqueERKNS_1AE(
    if (opaque(x)) {
      A y;
      A z;

      // CHECK: invoke void @_ZN6test121AD1Ev([[A]]* [[Z]])
      // CHECK: invoke void @_ZN6test121AD1Ev([[A]]* [[Y]])

      // It'd be great if something eliminated this switch.
      // CHECK:      load i32* [[CLEANUPDEST]]
      // CHECK-NEXT: switch i32
      goto success;
    }

  success:
    bool _ = true;

    // CHECK: call void @_ZN6test121AD1Ev([[A]]* [[X]])
    // CHECK-NEXT: ret void
  }
}

// Reduced from some TableGen code that was causing a self-host crash.
namespace test13 {
  struct A { ~A(); };

  void test0(int x) {
    try {
      switch (x) {
      case 0:
        break;
      case 1:{
        A a;
        break;
      }
      default:
        return;
      }
      return;
    } catch (int x) {
    }
    return;
  }

  void test1(int x) {
    A y;
    try {
      switch (x) {
      default: break;
      }
    } catch (int x) {}
  }
}

// rdar://problem/8231514
namespace test14 {
  struct A { ~A(); };
  struct B { ~B(); };

  B b();
  void opaque();

  void foo() {
    A a;
    try {
      B str = b();
      opaque();
    } catch (int x) {
    }
  }
}

// rdar://problem/8231514
// JumpDests shouldn't get confused by scopes that aren't normal cleanups.
namespace test15 {
  struct A { ~A(); };

  bool opaque(int);

  // CHECK: define void @_ZN6test153fooEv()
  void foo() {
    A a;

    try {
      // CHECK:      [[X:%.*]] = alloca i32
      // CHECK:      store i32 10, i32* [[X]]
      // CHECK-NEXT: br label
      //   -> while.cond
      int x = 10;

      while (true) {
        // CHECK:      load i32* [[X]]
        // CHECK-NEXT: [[COND:%.*]] = invoke zeroext i1 @_ZN6test156opaqueEi
        // CHECK:      br i1 [[COND]]
        if (opaque(x))
        // CHECK:      br label
          break;

        // CHECK:      br label
      }
      // CHECK:      br label
    } catch (int x) { }

    // CHECK: call void @_ZN6test151AD1Ev
  }
}

namespace test16 {
  struct A { A(); ~A() noexcept(false); };
  struct B { int x; B(const A &); ~B() noexcept(false); };
  void foo();
  bool cond();

  // CHECK: define void @_ZN6test163barEv()
  void bar() {
    // CHECK:      [[EXN_SAVE:%.*]] = alloca i8*
    // CHECK-NEXT: [[EXN_ACTIVE:%.*]] = alloca i1
    // CHECK-NEXT: [[TEMP:%.*]] = alloca [[A:%.*]],
    // CHECK-NEXT: [[EXNSLOT:%.*]] = alloca i8*
    // CHECK-NEXT: [[EHDEST:%.*]] = alloca i32
    // CHECK-NEXT: [[TEMP_ACTIVE:%.*]] = alloca i1

    cond() ? throw B(A()) : foo();

    // CHECK-NEXT: [[COND:%.*]] = call zeroext i1 @_ZN6test164condEv()
    // CHECK-NEXT: store i1 false, i1* [[EXN_ACTIVE]]
    // CHECK-NEXT: store i1 false, i1* [[TEMP_ACTIVE]]
    // CHECK-NEXT: br i1 [[COND]],

    // CHECK:      [[EXN:%.*]] = call i8* @__cxa_allocate_exception(i64 4)
    // CHECK-NEXT: store i8* [[EXN]], i8** [[EXN_SAVE]]
    // CHECK-NEXT: store i1 true, i1* [[EXN_ACTIVE]]
    // CHECK-NEXT: [[T0:%.*]] = bitcast i8* [[EXN]] to [[B:%.*]]*
    // CHECK-NEXT: invoke void @_ZN6test161AC1Ev([[A]]* [[TEMP]])
    // CHECK:      store i1 true, i1* [[TEMP_ACTIVE]]
    // CHECK-NEXT: invoke void @_ZN6test161BC1ERKNS_1AE([[B]]* [[T0]], [[A]]* [[TEMP]])
    // CHECK:      store i1 false, i1* [[EXN_ACTIVE]]
    // CHECK-NEXT: invoke void @__cxa_throw(i8* [[EXN]],

    // CHECK:      invoke void @_ZN6test163fooEv()
    // CHECK:      br label

    // CHECK:      invoke void @_ZN6test161AD1Ev([[A]]* [[TEMP]])
    // CHECK:      ret void

    // CHECK:      [[T0:%.*]] = load i1* [[EXN_ACTIVE]]
    // CHECK-NEXT: br i1 [[T0]]
    // CHECK:      [[T1:%.*]] = load i8** [[EXN_SAVE]]
    // CHECK-NEXT: call void @__cxa_free_exception(i8* [[T1]])
    // CHECK-NEXT: br label
  }
}
