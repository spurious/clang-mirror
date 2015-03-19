// RUN: %clang_cc1 -std=c++1y %s -emit-llvm -triple x86_64-linux-gnu -o - | FileCheck %s --check-prefix=CHECK-UNSIZED
// RUN: %clang_cc1 -std=c++1y %s -emit-llvm -triple x86_64-linux-gnu -o - -DINLIB | FileCheck %s --check-prefix=CHECK --check-prefix=CHECKUND
// RUN: %clang_cc1 -std=c++1y %s -emit-llvm -triple x86_64-linux-gnu -fdefine-sized-deallocation -o - | FileCheck %s --check-prefix=CHECK --check-prefix=CHECKDEF
// RUN: %clang_cc1 -std=c++11 -fsized-deallocation %s -emit-llvm -triple x86_64-linux-gnu -o - | FileCheck %s --check-prefix=CHECK-UNSIZED
// RUN: %clang_cc1 -std=c++11 -fsized-deallocation %s -emit-llvm -triple x86_64-linux-gnu -o - -DINLIB | FileCheck %s --check-prefix=CHECK --check-prefix=CHECKUND
// RUN: %clang_cc1 -std=c++11 -fsized-deallocation -fdefine-sized-deallocation %s -emit-llvm -triple x86_64-linux-gnu -o - | FileCheck %s --check-prefix=CHECK --check-prefix=CHECKDEF
// RUN: %clang_cc1 -std=c++11 %s -emit-llvm -triple x86_64-linux-gnu -o - | FileCheck %s --check-prefix=CHECK-UNSIZED
// RUN: %clang_cc1 -std=c++1y %s -emit-llvm -triple x86_64-linux-gnu -fno-sized-deallocation -o - | FileCheck %s --check-prefix=CHECK-UNSIZED

// CHECK-UNSIZED-NOT: _ZdlPvm
// CHECK-UNSIZED-NOT: _ZdaPvm

typedef decltype(sizeof(0)) size_t;

#ifdef INLIB
void operator delete(void *, size_t) noexcept;
void operator delete[](void *, size_t) noexcept;
#endif

typedef int A;
struct B { int n; };
struct C { ~C() {} };
struct D { D(); virtual ~D() {} };
struct E {
  void *operator new(size_t);
  void *operator new[](size_t);
  void operator delete(void *) noexcept;
  void operator delete[](void *) noexcept;
};
struct F {
  void *operator new(size_t);
  void *operator new[](size_t);
  void operator delete(void *, size_t) noexcept;
  void operator delete[](void *, size_t) noexcept;
};

template<typename T> T get();

template<typename T>
void del() {
  ::delete get<T*>();
  ::delete[] get<T*>();
  delete get<T*>();
  delete[] get<T*>();
}

template void del<A>();
template void del<B>();
template void del<C>();
template void del<D>();
template void del<E>();
template void del<F>();

D::D() {}

// CHECK-LABEL: define weak_odr void @_Z3delIiEvv()
// CHECK: call void @_ZdlPvm(i8* %{{[^ ]*}}, i64 4)
// CHECK: call void @_ZdaPv(i8* %{{[^ ]*}})
//
// CHECK: call void @_ZdlPvm(i8* %{{[^ ]*}}, i64 4)
// CHECK: call void @_ZdaPv(i8* %{{[^ ]*}})

// CHECKDEF-LABEL: define linkonce void @_ZdlPvm(i8*, i64) #{{[0-9]+}} comdat
// CHECKDEF: call void @_ZdlPv(i8* %0)
// CHECKUND-LABEL: declare void @_ZdlPvm(i8*

// CHECK-LABEL: define weak_odr void @_Z3delI1BEvv()
// CHECK: call void @_ZdlPvm(i8* %{{[^ ]*}}, i64 4)
// CHECK: call void @_ZdaPv(i8* %{{[^ ]*}})
//
// CHECK: call void @_ZdlPvm(i8* %{{[^ ]*}}, i64 4)
// CHECK: call void @_ZdaPv(i8* %{{[^ ]*}})

// CHECK-LABEL: define weak_odr void @_Z3delI1CEvv()
// CHECK: call void @_ZdlPvm(i8* %{{[^ ]*}}, i64 1)
// CHECK: mul i64 1, %{{[^ ]*}}
// CHECK: add i64 %{{[^ ]*}}, 8
// CHECK: call void @_ZdaPvm(i8* %{{[^ ]*}}, i64 %{{[^ ]*}})
//
// CHECK: call void @_ZdlPvm(i8* %{{[^ ]*}}, i64 1)
// CHECK: mul i64 1, %{{[^ ]*}}
// CHECK: add i64 %{{[^ ]*}}, 8
// CHECK: call void @_ZdaPvm(i8* %{{[^ ]*}}, i64 %{{[^ ]*}})

// CHECKDEF-LABEL: define linkonce void @_ZdaPvm(i8*, i64) #{{[0-9]+}} comdat
// CHECKDEF: call void @_ZdaPv(i8* %0)
// CHECKUND-LABEL: declare void @_ZdaPvm(i8*

// CHECK-LABEL: define weak_odr void @_Z3delI1DEvv()
// CHECK: call void @_ZdlPvm(i8* %{{[^ ]*}}, i64 8)
// CHECK: mul i64 8, %{{[^ ]*}}
// CHECK: add i64 %{{[^ ]*}}, 8
// CHECK: call void @_ZdaPvm(i8* %{{[^ ]*}}, i64 %{{[^ ]*}})
//
// CHECK-NOT: Zdl
// CHECK: call void %{{.*}}
// CHECK-NOT: Zdl
// CHECK: mul i64 8, %{{[^ ]*}}
// CHECK: add i64 %{{[^ ]*}}, 8
// CHECK: call void @_ZdaPvm(i8* %{{[^ ]*}}, i64 %{{[^ ]*}})

// CHECK-LABEL: define weak_odr void @_Z3delI1EEvv()
// CHECK: call void @_ZdlPvm(i8* %{{[^ ]*}}, i64 1)
// CHECK: call void @_ZdaPv(i8* %{{[^ ]*}})
//
// CHECK: call void @_ZN1EdlEPv(i8* %{{[^ ]*}})
// CHECK: call void @_ZN1EdaEPv(i8* %{{[^ ]*}})

// CHECK-LABEL: define weak_odr void @_Z3delI1FEvv()
// CHECK: call void @_ZdlPvm(i8* %{{[^ ]*}}, i64 1)
// CHECK: mul i64 1, %{{[^ ]*}}
// CHECK: add i64 %{{[^ ]*}}, 8
// CHECK: call void @_ZdaPvm(i8* %{{[^ ]*}}, i64 %{{[^ ]*}})
//
// CHECK: call void @_ZN1FdlEPvm(i8* %{{[^ ]*}}, i64 1)
// CHECK: mul i64 1, %{{[^ ]*}}
// CHECK: add i64 %{{[^ ]*}}, 8
// CHECK: call void @_ZN1FdaEPvm(i8* %{{[^ ]*}}, i64 %{{[^ ]*}})


// CHECK-LABEL: define linkonce_odr void @_ZN1DD0Ev(%{{[^ ]*}}* %this)
// CHECK: call void @_ZdlPvm(i8* %{{[^ ]*}}, i64 8)
