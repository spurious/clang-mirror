// RUN: %clang_cc1 -verify -triple x86_64-apple-darwin10 -fopenmp -x c++ -emit-llvm %s -o - | FileCheck %s
// RUN: %clang_cc1 -fopenmp -x c++ -triple x86_64-apple-darwin10 -emit-pch -o %t %s
// RUN: %clang_cc1 -fopenmp -x c++ -triple x86_64-apple-darwin10 -include-pch %t -verify %s -emit-llvm -o - | FileCheck %s
// expected-no-diagnostics

#ifndef HEADER
#define HEADER

// CHECK-DAG: [[IDENT_T:%.+]] = type { i32, i32, i32, i32, i8* }
// CHECK-DAG: [[STRUCT_SHAREDS:%.+]] = type { i8*, [2 x [[STRUCT_S:%.+]]]* }
// CHECK-DAG: [[STRUCT_SHAREDS1:%.+]] = type { [2 x [[STRUCT_S:%.+]]]* }
// CHECK-DAG: [[KMP_TASK_T:%.+]] = type { i8*, i32 (i32, i8*)*, i32, i32 (i32, i8*)* }
// CHECK-DAG: [[KMP_DEPEND_INFO:%.+]] = type { i64, i64, i8 }
struct S {
  int a;
  S() : a(0) {}
  S(const S &s) : a(s.a) {}
  ~S() {}
};
int a;
// CHECK-LABEL : @main
int main() {
// CHECK: [[B:%.+]] = alloca i8
// CHECK: [[S:%.+]] = alloca [2 x [[STRUCT_S]]]
  char b;
  S s[2];
// CHECK: [[GTID:%.+]] = call i32 @__kmpc_global_thread_num([[IDENT_T]]* @{{.+}})
// CHECK: [[B_REF:%.+]] = getelementptr inbounds [[STRUCT_SHAREDS]], [[STRUCT_SHAREDS]]* [[CAPTURES:%.+]], i32 0, i32 0
// CHECK: store i8* [[B]], i8** [[B_REF]]
// CHECK: [[S_REF:%.+]] = getelementptr inbounds [[STRUCT_SHAREDS]], [[STRUCT_SHAREDS]]* [[CAPTURES]], i32 0, i32 1
// CHECK: store [2 x [[STRUCT_S]]]* [[S]], [2 x [[STRUCT_S]]]** [[S_REF]]
// CHECK: [[ORIG_TASK_PTR:%.+]] = call i8* @__kmpc_omp_task_alloc([[IDENT_T]]* @{{.+}}, i32 [[GTID]], i32 1, i64 32, i64 16, i32 (i32, i8*)* bitcast (i32 (i32, [[KMP_TASK_T]]{{.*}}*)* [[TASK_ENTRY1:@.+]] to i32 (i32, i8*)*))
// CHECK: [[SHAREDS_REF_PTR:%.+]] = getelementptr inbounds [[KMP_TASK_T]], [[KMP_TASK_T]]* [[TASK_PTR:%.+]], i32 0, i32 0
// CHECK: [[SHAREDS_REF:%.+]] = load i8*, i8** [[SHAREDS_REF_PTR]]
// CHECK: [[BITCAST:%.+]] = bitcast [[STRUCT_SHAREDS]]* [[CAPTURES]] to i8*
// CHECK: call void @llvm.memcpy.p0i8.p0i8.i64(i8* [[SHAREDS_REF]], i8* [[BITCAST]], i64 16, i32 8, i1 false)
// CHECK: [[DESTRUCTORS_REF_PTR:%.+]] = getelementptr inbounds [[KMP_TASK_T]], [[KMP_TASK_T]]* [[TASK_PTR]], i32 0, i32 3
// CHECK: store i32 (i32, i8*)* null, i32 (i32, i8*)** [[DESTRUCTORS_REF_PTR]]
// CHECK: call i32 @__kmpc_omp_task([[IDENT_T]]* @{{.+}}, i32 [[GTID]], i8* [[ORIG_TASK_PTR]])
#pragma omp task shared(a, b, s)
  {
    a = 15;
    b = a;
    s[0].a = 10;
  }
// CHECK: [[S_REF:%.+]] = getelementptr inbounds [[STRUCT_SHAREDS1]], [[STRUCT_SHAREDS1]]* [[CAPTURES:%.+]], i32 0, i32 0
// CHECK: store [2 x [[STRUCT_S]]]* [[S]], [2 x [[STRUCT_S]]]** [[S_REF]]
// CHECK: [[ORIG_TASK_PTR:%.+]] = call i8* @__kmpc_omp_task_alloc([[IDENT_T]]* @{{[^,]+}}, i32 [[GTID]], i32 1, i64 32, i64 8,
// CHECK: [[SHAREDS_REF_PTR:%.+]] = getelementptr inbounds [[KMP_TASK_T]], [[KMP_TASK_T]]* [[TASK_PTR:%.+]], i32 0, i32 0
// CHECK: [[SHAREDS_REF:%.+]] = load i8*, i8** [[SHAREDS_REF_PTR]]
// CHECK: [[BITCAST:%.+]] = bitcast [[STRUCT_SHAREDS1]]* [[CAPTURES]] to i8*
// CHECK: call void @llvm.memcpy.p0i8.p0i8.i64(i8* [[SHAREDS_REF]], i8* [[BITCAST]], i64 8, i32 8, i1 false)
// CHECK: [[DESTRUCTORS_REF_PTR:%.+]] = getelementptr inbounds [[KMP_TASK_T]], [[KMP_TASK_T]]* [[TASK_PTR]], i32 0, i32 3
// CHECK: store i32 (i32, i8*)* null, i32 (i32, i8*)** [[DESTRUCTORS_REF_PTR]]
// CHECK: getelementptr inbounds [3 x [[KMP_DEPEND_INFO]]], [3 x [[KMP_DEPEND_INFO]]]* %{{[^,]+}}, i32 0, i32 0
// CHECK: getelementptr inbounds [[KMP_DEPEND_INFO]], [[KMP_DEPEND_INFO]]* %{{[^,]+}}, i32 0, i32 0
// CHECK: store i64 ptrtoint (i32* @{{.+}} to i64), i64*
// CHECK: getelementptr inbounds [[KMP_DEPEND_INFO]], [[KMP_DEPEND_INFO]]* %{{[^,]+}}, i32 0, i32 1
// CHECK: store i64 4, i64*
// CHECK: getelementptr inbounds [[KMP_DEPEND_INFO]], [[KMP_DEPEND_INFO]]* %{{[^,]+}}, i32 0, i32 2
// CHECK: store i8 1, i8*
// CHECK: getelementptr inbounds [3 x [[KMP_DEPEND_INFO]]], [3 x [[KMP_DEPEND_INFO]]]* %{{[^,]+}}, i32 0, i32 1
// CHECK: getelementptr inbounds [[KMP_DEPEND_INFO]], [[KMP_DEPEND_INFO]]* %{{[^,]+}}, i32 0, i32 0
// CHECK: ptrtoint i8* [[B]] to i64
// CHECK: store i64 %{{[^,]+}}, i64*
// CHECK: getelementptr inbounds [[KMP_DEPEND_INFO]], [[KMP_DEPEND_INFO]]* %{{[^,]+}}, i32 0, i32 1
// CHECK: store i64 1, i64*
// CHECK: getelementptr inbounds [[KMP_DEPEND_INFO]], [[KMP_DEPEND_INFO]]* %{{[^,]+}}, i32 0, i32 2
// CHECK: store i8 1, i8*
// CHECK: getelementptr inbounds [3 x [[KMP_DEPEND_INFO]]], [3 x [[KMP_DEPEND_INFO]]]* %{{[^,]+}}, i32 0, i32 2
// CHECK: getelementptr inbounds [[KMP_DEPEND_INFO]], [[KMP_DEPEND_INFO]]* %{{[^,]+}}, i32 0, i32 0
// CHECK: ptrtoint [2 x [[STRUCT_S]]]* [[S]] to i64
// CHECK: store i64 %{{[^,]+}}, i64*
// CHECK: getelementptr inbounds [[KMP_DEPEND_INFO]], [[KMP_DEPEND_INFO]]* %{{[^,]+}}, i32 0, i32 1
// CHECK: store i64 8, i64*
// CHECK: getelementptr inbounds [[KMP_DEPEND_INFO]], [[KMP_DEPEND_INFO]]* %{{[^,]+}}, i32 0, i32 2
// CHECK: store i8 1, i8*
// CHECK: getelementptr inbounds [3 x [[KMP_DEPEND_INFO]]], [3 x [[KMP_DEPEND_INFO]]]* %{{[^,]+}}, i32 0, i32 0
// CHECK: bitcast [[KMP_DEPEND_INFO]]* %{{.+}} to i8*
// CHECK: call i32 @__kmpc_omp_task_with_deps([[IDENT_T]]* @{{.+}}, i32 [[GTID]], i8* [[ORIG_TASK_PTR]], i32 3, i8* %{{[^,]+}}, i32 0, i8* null)
#pragma omp task shared(a, s) depend(in : a, b, s)
  {
    a = 15;
    s[1].a = 10;
  }
// CHECK: [[ORIG_TASK_PTR:%.+]] = call i8* @__kmpc_omp_task_alloc([[IDENT_T]]* @{{.+}}, i32 [[GTID]], i32 0, i64 32, i64 1, i32 (i32, i8*)* bitcast (i32 (i32, [[KMP_TASK_T]]{{.*}}*)* [[TASK_ENTRY2:@.+]] to i32 (i32, i8*)*))
// CHECK: [[DESTRUCTORS_REF_PTR:%.+]] = getelementptr inbounds [[KMP_TASK_T]]{{.*}}* {{%.+}}, i32 0, i32 3
// CHECK: store i32 (i32, i8*)* null, i32 (i32, i8*)** [[DESTRUCTORS_REF_PTR]]
// CHECK: call i32 @__kmpc_omp_task([[IDENT_T]]* @{{.+}}, i32 [[GTID]], i8* [[ORIG_TASK_PTR]])
#pragma omp task untied
  {
    a = 1;
  }
// CHECK: [[ORIG_TASK_PTR:%.+]] = call i8* @__kmpc_omp_task_alloc([[IDENT_T]]* @{{.+}}, i32 [[GTID]], i32 0, i64 32, i64 1,
// CHECK: [[DESTRUCTORS_REF_PTR:%.+]] = getelementptr inbounds [[KMP_TASK_T]]{{.*}}* {{%.+}}, i32 0, i32 3
// CHECK: store i32 (i32, i8*)* null, i32 (i32, i8*)** [[DESTRUCTORS_REF_PTR]]
// CHECK: getelementptr inbounds [2 x [[STRUCT_S]]], [2 x [[STRUCT_S]]]* [[S]], i32 0, i64 0
// CHECK: getelementptr inbounds [1 x [[KMP_DEPEND_INFO]]], [1 x [[KMP_DEPEND_INFO]]]* %{{[^,]+}}, i32 0, i32 0
// CHECK: getelementptr inbounds [[KMP_DEPEND_INFO]], [[KMP_DEPEND_INFO]]* %{{[^,]+}}, i32 0, i32 0
// CHECK: ptrtoint [[STRUCT_S]]* %{{.+}} to i64
// CHECK: store i64 %{{[^,]+}}, i64*
// CHECK: getelementptr inbounds [[KMP_DEPEND_INFO]], [[KMP_DEPEND_INFO]]* %{{[^,]+}}, i32 0, i32 1
// CHECK: store i64 4, i64*
// CHECK: getelementptr inbounds [[KMP_DEPEND_INFO]], [[KMP_DEPEND_INFO]]* %{{[^,]+}}, i32 0, i32 2
// CHECK: store i8 2, i8*
// CHECK: getelementptr inbounds [1 x [[KMP_DEPEND_INFO]]], [1 x [[KMP_DEPEND_INFO]]]* %{{[^,]+}}, i32 0, i32 0
// CHECK: bitcast [[KMP_DEPEND_INFO]]* %{{.+}} to i8*
// CHECK: call i32 @__kmpc_omp_task_with_deps([[IDENT_T]]* @{{.+}}, i32 [[GTID]], i8* [[ORIG_TASK_PTR]], i32 1, i8* %{{[^,]+}}, i32 0, i8* null)
#pragma omp task untied depend(out : s[0])
  {
    a = 1;
  }
// CHECK: [[ORIG_TASK_PTR:%.+]] = call i8* @__kmpc_omp_task_alloc([[IDENT_T]]* @{{.+}}, i32 [[GTID]], i32 3, i64 32, i64 1,
// CHECK: [[DESTRUCTORS_REF_PTR:%.+]] = getelementptr inbounds [[KMP_TASK_T]]{{.*}}* {{%.+}}, i32 0, i32 3
// CHECK: store i32 (i32, i8*)* null, i32 (i32, i8*)** [[DESTRUCTORS_REF_PTR]]
// CHECK: getelementptr inbounds [2 x [[KMP_DEPEND_INFO]]], [2 x [[KMP_DEPEND_INFO]]]* %{{[^,]+}}, i32 0, i32 0
// CHECK: getelementptr inbounds [[KMP_DEPEND_INFO]], [[KMP_DEPEND_INFO]]* %{{[^,]+}}, i32 0, i32 0
// CHECK: store i64 ptrtoint (i32* @{{.+}} to i64), i64*
// CHECK: getelementptr inbounds [[KMP_DEPEND_INFO]], [[KMP_DEPEND_INFO]]* %{{[^,]+}}, i32 0, i32 1
// CHECK: store i64 4, i64*
// CHECK: getelementptr inbounds [[KMP_DEPEND_INFO]], [[KMP_DEPEND_INFO]]* %{{[^,]+}}, i32 0, i32 2
// CHECK: store i8 3, i8*
// CHECK: getelementptr inbounds [2 x [[STRUCT_S]]], [2 x [[STRUCT_S]]]* [[S]], i32 0, i64 1
// CHECK: getelementptr inbounds [2 x [[KMP_DEPEND_INFO]]], [2 x [[KMP_DEPEND_INFO]]]* %{{[^,]+}}, i32 0, i32 1
// CHECK: getelementptr inbounds [[KMP_DEPEND_INFO]], [[KMP_DEPEND_INFO]]* %{{[^,]+}}, i32 0, i32 0
// CHECK: ptrtoint [[STRUCT_S]]* %{{.+}} to i64
// CHECK: store i64 %{{[^,]+}}, i64*
// CHECK: getelementptr inbounds [[KMP_DEPEND_INFO]], [[KMP_DEPEND_INFO]]* %{{[^,]+}}, i32 0, i32 1
// CHECK: store i64 4, i64*
// CHECK: getelementptr inbounds [[KMP_DEPEND_INFO]], [[KMP_DEPEND_INFO]]* %{{[^,]+}}, i32 0, i32 2
// CHECK: store i8 3, i8*
// CHECK: getelementptr inbounds [2 x [[KMP_DEPEND_INFO]]], [2 x [[KMP_DEPEND_INFO]]]* %{{[^,]+}}, i32 0, i32 0
// CHECK: bitcast [[KMP_DEPEND_INFO]]* %{{.+}} to i8*
// CHECK: call i32 @__kmpc_omp_task_with_deps([[IDENT_T]]* @{{.+}}, i32 [[GTID]], i8* [[ORIG_TASK_PTR]], i32 2, i8* %{{[^,]+}}, i32 0, i8* null)
#pragma omp task final(true) depend(inout: a, s[1])
  {
    a = 2;
  }
// CHECK: [[ORIG_TASK_PTR:%.+]] = call i8* @__kmpc_omp_task_alloc([[IDENT_T]]* @{{.+}}, i32 [[GTID]], i32 3, i64 32, i64 1, i32 (i32, i8*)* bitcast (i32 (i32, [[KMP_TASK_T]]{{.*}}*)* [[TASK_ENTRY3:@.+]] to i32 (i32, i8*)*))
// CHECK: [[DESTRUCTORS_REF_PTR:%.+]] = getelementptr inbounds [[KMP_TASK_T]]{{.*}}* {{%.+}}, i32 0, i32 3
// CHECK: store i32 (i32, i8*)* null, i32 (i32, i8*)** [[DESTRUCTORS_REF_PTR]]
// CHECK: call i32 @__kmpc_omp_task([[IDENT_T]]* @{{.+}}, i32 [[GTID]], i8* [[ORIG_TASK_PTR]])
#pragma omp task final(true)
  {
    a = 2;
  }
// CHECK: [[ORIG_TASK_PTR:%.+]] = call i8* @__kmpc_omp_task_alloc([[IDENT_T]]* @{{.+}}, i32 [[GTID]], i32 1, i64 32, i64 1, i32 (i32, i8*)* bitcast (i32 (i32, [[KMP_TASK_T]]{{.*}}*)* [[TASK_ENTRY4:@.+]] to i32 (i32, i8*)*))
// CHECK: [[DESTRUCTORS_REF_PTR:%.+]] = getelementptr inbounds [[KMP_TASK_T]]{{.*}}* {{%.*}}, i32 0, i32 3
// CHECK: store i32 (i32, i8*)* null, i32 (i32, i8*)** [[DESTRUCTORS_REF_PTR]]
// CHECK: call i32 @__kmpc_omp_task([[IDENT_T]]* @{{.+}}, i32 [[GTID]], i8* [[ORIG_TASK_PTR]])
  const bool flag = false;
#pragma omp task final(flag)
  {
    a = 3;
  }
// CHECK: [[B_VAL:%.+]] = load i8, i8* [[B]]
// CHECK: [[CMP:%.+]] = icmp ne i8 [[B_VAL]], 0
// CHECK: [[FINAL:%.+]] = select i1 [[CMP]], i32 2, i32 0
// CHECK: [[FLAGS:%.+]] = or i32 [[FINAL]], 1
// CHECK: [[ORIG_TASK_PTR:%.+]] = call i8* @__kmpc_omp_task_alloc([[IDENT_T]]* @{{.+}}, i32 [[GTID]], i32 [[FLAGS]], i64 32, i64 1, i32 (i32, i8*)* bitcast (i32 (i32, [[KMP_TASK_T]]{{.*}}*)* [[TASK_ENTRY5:@.+]] to i32 (i32, i8*)*))
// CHECK: [[DESTRUCTORS_REF_PTR:%.+]] = getelementptr inbounds [[KMP_TASK_T]]{{.*}}* {{%.+}}, i32 0, i32 3
// CHECK: store i32 (i32, i8*)* null, i32 (i32, i8*)** [[DESTRUCTORS_REF_PTR]]
// CHECK: call i32 @__kmpc_omp_task([[IDENT_T]]* @{{.+}}, i32 [[GTID]], i8* [[ORIG_TASK_PTR]])
#pragma omp task final(b)
  {
    a = 4;
  }
  return a;
}
// CHECK: define internal i32 [[TASK_ENTRY1]](i32, [[KMP_TASK_T]]{{.*}}*)
// CHECK: store i32 15, i32* [[A_PTR:@.+]]
// CHECK: [[A_VAL:%.+]] = load i32, i32* [[A_PTR]]
// CHECK: [[A_VAL_I8:%.+]] = trunc i32 [[A_VAL]] to i8
// CHECK: store i8 [[A_VAL_I8]], i8* %{{.+}}
// CHECK: store i32 10, i32* %{{.+}}

// CHECK: define internal i32 [[TASK_ENTRY2]](i32, [[KMP_TASK_T]]{{.*}}*)
// CHECK: store i32 1, i32* [[A_PTR:@.+]]

// CHECK: define internal i32 [[TASK_ENTRY3]](i32, [[KMP_TASK_T]]{{.*}}*)
// CHECK: store i32 2, i32* [[A_PTR:@.+]]

// CHECK: define internal i32 [[TASK_ENTRY4]](i32, [[KMP_TASK_T]]{{.*}}*)
// CHECK: store i32 3, i32* [[A_PTR:@.+]]

// CHECK: define internal i32 [[TASK_ENTRY5]](i32, [[KMP_TASK_T]]{{.*}}*)
// CHECK: store i32 4, i32* [[A_PTR:@.+]]
#endif

