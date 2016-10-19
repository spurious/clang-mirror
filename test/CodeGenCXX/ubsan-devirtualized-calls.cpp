// RUN: %clang_cc1 -std=c++11 -triple %itanium_abi_triple -emit-llvm -fsanitize=vptr %s -o - | FileCheck %s

struct Base1 {
  virtual void f1() {}
};

struct Base2 {
  virtual void f1() {}
};

struct Derived1 final : Base1 {
  void f1() override {}
};

struct Derived2 final : Base1, Base2 {
  void f1() override {}
};

// PR13127 documents some missed devirtualization opportunities, including
// devirt for methods marked 'final'. We can enable the checks marked 'PR13127'
// if we implement this in the frontend.
struct Derived3 : Base1 {
  void f1() override /* nofinal */ {}
};

struct Derived4 final : Base1 {
  void f1() override final {}
};

// CHECK: [[UBSAN_TI_DERIVED1_1:@[0-9]+]] = private unnamed_addr global {{.*}} i8* bitcast {{.*}} @_ZTI8Derived1 to i8*
// CHECK: [[UBSAN_TI_DERIVED2_1:@[0-9]+]] = private unnamed_addr global {{.*}} i8* bitcast {{.*}} @_ZTI8Derived2 to i8*
// CHECK: [[UBSAN_TI_DERIVED2_2:@[0-9]+]] = private unnamed_addr global {{.*}} i8* bitcast {{.*}} @_ZTI8Derived2 to i8*
// PR13127: [[UBSAN_TI_DERIVED3:@[0-9]+]] = private unnamed_addr global {{.*}} i8* bitcast {{.*}} @_ZTI8Derived3 to i8*
// PR13127: [[UBSAN_TI_BASE1:@[0-9]+]] = private unnamed_addr global {{.*}} i8* bitcast {{.*}} @_ZTI5Base1 to i8*
// PR13127: [[UBSAN_TI_DERIVED4_1:@[0-9]+]] = private unnamed_addr global {{.*}} i8* bitcast {{.*}} @_ZTI8Derived4 to i8*
// PR13127: [[UBSAN_TI_DERIVED4_2:@[0-9]+]] = private unnamed_addr global {{.*}} i8* bitcast {{.*}} @_ZTI8Derived4 to i8*

// CHECK-LABEL: define void @_Z2t1v
void t1() {
  Derived1 d1;
  static_cast<Base1 *>(&d1)->f1(); //< Devirt Base1::f1 to Derived1::f1.
  // CHECK: handler.dynamic_type_cache_miss:
  // CHECK-NEXT: %[[D1:[0-9]+]] = ptrtoint %struct.Derived1* %d1 to i64, !nosanitize
  // CHECK-NEXT: call void @{{[_a-z]+}}({{.*}} [[UBSAN_TI_DERIVED1_1]] {{.*}}, i64 %[[D1]]
}

// CHECK-LABEL: define void @_Z2t2v
void t2() {
  Derived2 d2;
  static_cast<Base1 *>(&d2)->f1(); //< Devirt Base1::f1 to Derived2::f1.
  // CHECK: handler.dynamic_type_cache_miss:
  // CHECK-NEXT: %[[D2_1:[0-9]+]] = ptrtoint %struct.Derived2* %d2 to i64, !nosanitize
  // CHECK-NEXT: call void @{{[_a-z]+}}({{.*}} [[UBSAN_TI_DERIVED2_1]] {{.*}}, i64 %[[D2_1]]
}

// CHECK-LABEL: define void @_Z2t3v
void t3() {
  Derived2 d2;
  static_cast<Base2 *>(&d2)->f1(); //< Devirt Base2::f1 to Derived2::f1.
  // CHECK: handler.dynamic_type_cache_miss:
  // CHECK-NEXT: %[[D2_2:[0-9]+]] = ptrtoint %struct.Derived2* %d2 to i64, !nosanitize
  // CHECK-NEXT: call void @{{[_a-z]+}}({{.*}} [[UBSAN_TI_DERIVED2_2]] {{.*}}, i64 %[[D2_2]]
}

// PR13127-LABEL: define void @_Z2t4v
void t4() {
  Base1 p;
  Derived3 *badp = static_cast<Derived3 *>(&p); //< Check that &p isa Derived3.
  // PR13127: handler.dynamic_type_cache_miss
  // PR13127: %[[P1:[0-9]+]] = ptrtoint %struct.Derived3* {{%[0-9]+}} to i64, !nosanitize
  // PR13127-NEXT: call void @{{[_a-z]+}}({{.*}} [[UBSAN_TI_DERIVED3]] {{.*}}, i64 %[[P1]]

  static_cast<Base1 *>(badp)->f1(); //< No devirt, test 'badp isa Base1'.
  // PR13127: handler.dynamic_type_cache_miss
  // PR13127: %[[BADP1:[0-9]+]] = ptrtoint %struct.Base1* {{%[0-9]+}} to i64, !nosanitize
  // PR13127-NEXT: call void @{{[_a-z]+}}({{.*}} [[UBSAN_TI_BASE1]] {{.*}}, i64 %[[BADP1]]
}

// PR13127-LABEL: define void @_Z2t5v
void t5() {
  Base1 p;
  Derived4 *badp = static_cast<Derived4 *>(&p); //< Check that &p isa Derived4.
  // PR13127: handler.dynamic_type_cache_miss:
  // PR13127: %[[P1:[0-9]+]] = ptrtoint %struct.Derived4* {{%[0-9]+}} to i64, !nosanitize
  // PR13127-NEXT: call void @{{[_a-z]+}}({{.*}} [[UBSAN_TI_DERIVED4_1]] {{.*}}, i64 %[[P1]]

  static_cast<Base1 *>(badp)->f1(); //< Devirt Base1::f1 to Derived4::f1.
  // PR13127: handler.dynamic_type_cache_miss1:
  // PR13127: %[[BADP1:[0-9]+]] = ptrtoint %struct.Derived4* {{%[0-9]+}} to i64, !nosanitize
  // PR13127-NEXT: call void @{{[_a-z]+}}({{.*}} [[UBSAN_TI_DERIVED4_2]] {{.*}}, i64 %[[BADP1]]
}
