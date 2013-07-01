// RUN: %clang_cc1 -emit-llvm -fblocks -o - -triple x86_64-apple-darwin10 -fobjc-runtime=macosx-fragile-10.5 %s | FileCheck %s

// CHECK: @_ZGVN3fooUb_5valueE = internal global i64 0
// CHECK: @_ZN26externally_visible_statics1SUb_1jE = linkonce_odr global i32 0
// CHECK: @_ZN26externally_visible_statics10inlinefuncUb_1iE = linkonce_odr global i32 0

int f();

void foo() {
  // CHECK: define internal i32 @___Z3foov_block_invoke
  // CHECK: call i32 @__cxa_guard_acquire(i64* @_ZGVN3fooUb_5valueE
  (void)^(int x) { 
    static int value = f();
    return x + value;
  };
}

// CHECK: define internal i32 @i_block_invoke
int i = ^(int x) { return x;}(i);

@interface A
- (void)method;
@end

@implementation A
- (void)method { 
  // CHECK: define internal signext i8 @"__11-[A method]_block_invoke"
  (void)^(int x) {
    // CHECK: @"_ZN11-[A method]Ub0_4nameE"
    static const char *name = "hello";
    return name[x];
  };
}
@end

void foo(int) {
  (void)^(int x) { 
    static const char *name = "hello";
    return name[x];
  };
}

namespace N {
  // CHECK: define internal signext i8 @___Z3fooi_block_invoke
  void bar() {
    (void)^(int x) { 
      // CHECK: @_ZN1N3barUb2_4nameE
      static const char *name = "hello";
      return name[x];
    };
  }
}

class C {
  C();
};
C::C() {
  (void)^(int x) { 
    // CHECK: @_ZN1CC1Ub3_5namebE
    static const char *nameb = "hello";
    return nameb[x];
  };
}

int f();
namespace externally_visible_statics {
  inline void inlinefunc() {
    ^{
      static int i = f();
    }();
  }
  struct S {
    int x = ^{
      static int j = f();
      return j;
    }();
    void foo(int y = ^{ static int k = f(); return k; }()) {}
  };
  void g() {
    inlinefunc();
    S s;
#if 0
    // FIXME: We know how to mangle k, but crash trying to mangle the
    // block itself.
    s.foo();
#endif
  }
}
