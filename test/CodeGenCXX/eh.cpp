// RUN: %clang_cc1 -fexceptions -triple x86_64-apple-darwin -std=c++0x -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s

struct test1_D {
  double d;
} d1;

void test1() {
  throw d1;
}

// CHECK:     define void @_Z5test1v()
// CHECK:       [[FREEVAR:%.*]] = alloca i1
// CHECK-NEXT:  [[EXNOBJVAR:%.*]] = alloca i8*
// CHECK-NEXT:  store i1 false, i1* [[FREEVAR]]
// CHECK-NEXT:  [[EXNOBJ:%.*]] = call i8* @__cxa_allocate_exception(i64 8)
// CHECK-NEXT:  store i8* [[EXNOBJ]], i8** [[EXNOBJVAR]]
// CHECK-NEXT:  store i1 true, i1* [[FREEVAR]]
// CHECK-NEXT:  [[EXN:%.*]] = bitcast i8* [[EXNOBJ]] to [[DSTAR:%[^*]*\*]]
// CHECK-NEXT:  [[EXN2:%.*]] = bitcast [[DSTAR]] [[EXN]] to i8*
// CHECK-NEXT:  call void @llvm.memcpy.p0i8.p0i8.i64(i8* [[EXN2]], i8* bitcast ([[DSTAR]] @d1 to i8*), i64 8, i32 8, i1 false)
// CHECK-NEXT:  store i1 false, i1* [[FREEVAR]]
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
// CHECK:       [[FREEVAR:%.*]] = alloca i1
// CHECK-NEXT:  [[EXNOBJVAR:%.*]] = alloca i8*
// CHECK-NEXT:  [[EXNSLOTVAR:%.*]] = alloca i8*
// CHECK-NEXT:  store i1 false, i1* [[FREEVAR]]
// CHECK-NEXT:  [[EXNOBJ:%.*]] = call i8* @__cxa_allocate_exception(i64 16)
// CHECK-NEXT:  store i8* [[EXNOBJ]], i8** [[EXNOBJVAR]]
// CHECK-NEXT:  store i1 true, i1* [[FREEVAR]]
// CHECK-NEXT:  [[EXN:%.*]] = bitcast i8* [[EXNOBJ]] to [[DSTAR:%[^*]*\*]]
// CHECK-NEXT:  invoke void @_ZN7test2_DC1ERKS_([[DSTAR]] [[EXN]], [[DSTAR]] @d2)
// CHECK-NEXT:     to label %[[CONT:.*]] unwind label %{{.*}}
//      :     [[CONT]]:   (can't check this in Release-Asserts builds)
// CHECK:       store i1 false, i1* [[FREEVAR]]
// CHECK-NEXT:  call void @__cxa_throw(i8* [[EXNOBJ]], i8* bitcast (%{{.*}}* @_ZTI7test2_D to i8*), i8* null) noreturn
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
// CHECK:       [[FREEVAR:%.*]] = alloca i1
// CHECK-NEXT:  [[EXNOBJVAR:%.*]] = alloca i8*
// CHECK-NEXT:  store i1 false, i1* [[FREEVAR]]
// CHECK-NEXT:  [[EXNOBJ:%.*]] = call i8* @__cxa_allocate_exception(i64 8)
// CHECK-NEXT:  store i8* [[EXNOBJ]], i8** [[EXNOBJVAR]]
// CHECK-NEXT:  store i1 true, i1* [[FREEVAR]]
// CHECK-NEXT:  [[EXN:%.*]] = bitcast i8* [[EXNOBJ]] to [[DSS:%[^*]*\*]]*
// CHECK-NEXT:  store [[DSS]] null, [[DSS]]* [[EXN]]
// CHECK-NEXT:  store i1 false, i1* [[FREEVAR]]
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
// CHECK:      [[FREEEXNOBJ:%.*]] = alloca i1
// CHECK-NEXT: [[EXNALLOCVAR:%.*]] = alloca i8*
// CHECK-NEXT: [[CAUGHTEXNVAR:%.*]] = alloca i8*
// CHECK-NEXT: [[INTCATCHVAR:%.*]] = alloca i32
// CHECK-NEXT: store i1 false, i1* [[FREEEXNOBJ]]
    try {
      try {
// CHECK-NEXT: [[EXNALLOC:%.*]] = call i8* @__cxa_allocate_exception
// CHECK-NEXT: store i8* [[EXNALLOC]], i8** [[EXNALLOCVAR]]
// CHECK-NEXT: store i1 true, i1* [[FREEEXNOBJ]]
// CHECK-NEXT: bitcast i8* [[EXNALLOC]] to i32*
// CHECK-NEXT: store i32 1, i32*
// CHECK-NEXT: store i1 false, i1* [[FREEEXNOBJ]]
// CHECK-NEXT: invoke void @__cxa_throw(i8* [[EXNALLOC]], i8* bitcast (i8** @_ZTIi to i8*), i8* null
        throw 1;
      }
// This cleanup ends up here for no good reason.  It's actually unused.
// CHECK:      load i8** [[EXNALLOCVAR]]
// CHECK-NEXT: call void @__cxa_free_exception(

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
      // CHECK-NEXT: invoke void @_ZN5test81AD1Ev(

      // CHECK:      call void @__cxa_end_catch()
      // CHECK-NEXT: load
      // CHECK-NEXT: switch

      // CHECK:      ret void
    }
  }
}
