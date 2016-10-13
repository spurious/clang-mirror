// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -target-cpu core2 -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple arm64-apple-ios9 -target-cpu cyclone -emit-llvm -o - %s | FileCheck %s

// REQUIRES: aarch64-registered-target,x86-registered-target

// The union_het_vecint test case crashes on windows bot but only in stage1 and not in stage2.
// UNSUPPORTED: system-windows

#define SWIFTCALL __attribute__((swiftcall))
#define OUT __attribute__((swift_indirect_result))
#define ERROR __attribute__((swift_error_result))
#define CONTEXT __attribute__((swift_context))

// CHECK: [[STRUCT2_RESULT:@.*]] = private {{.*}} constant [[STRUCT2_TYPE:%.*]] { i32 0, i8 0, i8 undef, i8 0, float 0.000000e+00, float 0.000000e+00 }

/*****************************************************************************/
/****************************** PARAMETER ABIS *******************************/
/*****************************************************************************/

SWIFTCALL void indirect_result_1(OUT int *arg0, OUT float *arg1) {}
// CHECK-LABEL: define {{.*}} void @indirect_result_1(i32* noalias sret align 4 dereferenceable(4){{.*}}, float* noalias align 4 dereferenceable(4){{.*}})

// TODO: maybe this shouldn't suppress sret.
SWIFTCALL int indirect_result_2(OUT int *arg0, OUT float *arg1) {  __builtin_unreachable(); }
// CHECK-LABEL: define {{.*}} i32 @indirect_result_2(i32* noalias align 4 dereferenceable(4){{.*}}, float* noalias align 4 dereferenceable(4){{.*}})

typedef struct { char array[1024]; } struct_reallybig;
SWIFTCALL struct_reallybig indirect_result_3(OUT int *arg0, OUT float *arg1) { __builtin_unreachable(); }
// CHECK-LABEL: define {{.*}} void @indirect_result_3({{.*}}* noalias sret {{.*}}, i32* noalias align 4 dereferenceable(4){{.*}}, float* noalias align 4 dereferenceable(4){{.*}})

SWIFTCALL void context_1(CONTEXT void *self) {}
// CHECK-LABEL: define {{.*}} void @context_1(i8* swiftself

SWIFTCALL void context_2(void *arg0, CONTEXT void *self) {}
// CHECK-LABEL: define {{.*}} void @context_2(i8*{{.*}}, i8* swiftself

SWIFTCALL void context_error_1(CONTEXT int *self, ERROR float **error) {}
// CHECK-LABEL: define {{.*}} void @context_error_1(i32* swiftself{{.*}}, float** swifterror)
// CHECK:       [[TEMP:%.*]] = alloca float*, align 8
// CHECK:       [[T0:%.*]] = load float*, float** [[ERRORARG:%.*]], align 8
// CHECK:       store float* [[T0]], float** [[TEMP]], align 8
// CHECK:       [[T0:%.*]] = load float*, float** [[TEMP]], align 8
// CHECK:       store float* [[T0]], float** [[ERRORARG]], align 8
void test_context_error_1() {
  int x;
  float *error;
  context_error_1(&x, &error);
}
// CHECK-LABEL: define void @test_context_error_1()
// CHECK:       [[X:%.*]] = alloca i32, align 4
// CHECK:       [[ERROR:%.*]] = alloca float*, align 8
// CHECK:       [[TEMP:%.*]] = alloca swifterror float*, align 8
// CHECK:       [[T0:%.*]] = load float*, float** [[ERROR]], align 8
// CHECK:       store float* [[T0]], float** [[TEMP]], align 8
// CHECK:       call [[SWIFTCC:swiftcc]] void @context_error_1(i32* swiftself [[X]], float** swifterror [[TEMP]])
// CHECK:       [[T0:%.*]] = load float*, float** [[TEMP]], align 8
// CHECK:       store float* [[T0]], float** [[ERROR]], align 8

SWIFTCALL void context_error_2(short s, CONTEXT int *self, ERROR float **error) {}
// CHECK-LABEL: define {{.*}} void @context_error_2(i16{{.*}}, i32* swiftself{{.*}}, float** swifterror)

/*****************************************************************************/
/********************************** LOWERING *********************************/
/*****************************************************************************/

typedef float float4 __attribute__((ext_vector_type(4)));
typedef float float8 __attribute__((ext_vector_type(8)));
typedef double double2 __attribute__((ext_vector_type(2)));
typedef double double4 __attribute__((ext_vector_type(4)));
typedef int int3 __attribute__((ext_vector_type(3)));
typedef int int4 __attribute__((ext_vector_type(4)));
typedef int int5 __attribute__((ext_vector_type(5)));
typedef int int8 __attribute__((ext_vector_type(8)));

#define TEST(TYPE)                       \
  SWIFTCALL TYPE return_##TYPE(void) {   \
    TYPE result = {};                    \
    return result;                       \
  }                                      \
  SWIFTCALL void take_##TYPE(TYPE v) {   \
  }                                      \
  void test_##TYPE() {                   \
    take_##TYPE(return_##TYPE());        \
  }

/*****************************************************************************/
/*********************************** STRUCTS *********************************/
/*****************************************************************************/

typedef struct {
} struct_empty;
TEST(struct_empty);
// CHECK-LABEL: define {{.*}} @return_struct_empty()
// CHECK:   ret void
// CHECK-LABEL: define {{.*}} @take_struct_empty()
// CHECK:   ret void

typedef struct {
  int x;
  char c0;
  char c1;
  float f0;
  float f1;
} struct_1;
TEST(struct_1);
// CHECK-LABEL: define swiftcc { i64, i64 } @return_struct_1() {{.*}}{
// CHECK:   [[RET:%.*]] = alloca [[STRUCT1:%.*]], align 4
// CHECK:   [[VAR:%.*]] = alloca [[STRUCT1]], align 4
// CHECK:   call void @llvm.memset
// CHECK:   call void @llvm.memcpy
// CHECK:   [[CAST:%.*]] = bitcast [[STRUCT1]]* %retval to { i64, i64 }*
// CHECK:   [[GEP0:%.*]] = getelementptr inbounds { i64, i64 }, { i64, i64 }* [[CAST]], i32 0, i32 0
// CHECK:   [[T0:%.*]] = load i64, i64* [[GEP0]], align 4
// CHECK:   [[GEP1:%.*]] = getelementptr inbounds { i64, i64 }, { i64, i64 }* [[CAST]], i32 0, i32 1
// CHECK:   [[T1:%.*]] = load i64, i64* [[GEP1]], align 4
// CHECK:   [[R0:%.*]] = insertvalue { i64, i64 } undef, i64 [[T0]], 0
// CHECK:   [[R1:%.*]] = insertvalue { i64, i64 } [[R0]], i64 [[T1]], 1
// CHECK:   ret { i64, i64 } [[R1]]
// CHECK: }
// CHECK-LABEL: define swiftcc void @take_struct_1(i64, i64) {{.*}}{
// CHECK:   [[V:%.*]] = alloca [[STRUCT1:%.*]], align 4
// CHECK:   [[CAST:%.*]] = bitcast [[STRUCT1]]* [[V]] to { i64, i64 }*
// CHECK:   [[GEP0:%.*]] = getelementptr inbounds { i64, i64 }, { i64, i64 }* [[CAST]], i32 0, i32 0
// CHECK:   store i64 %0, i64* [[GEP0]], align 4
// CHECK:   [[GEP1:%.*]] = getelementptr inbounds { i64, i64 }, { i64, i64 }* [[CAST]], i32 0, i32 1
// CHECK:   store i64 %1, i64* [[GEP1]], align 4
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define void @test_struct_1() {{.*}}{
// CHECK:   [[AGG:%.*]] = alloca [[STRUCT1:%.*]], align 4
// CHECK:   [[RET:%.*]] = call swiftcc { i64, i64 } @return_struct_1()
// CHECK:   [[CAST:%.*]] = bitcast [[STRUCT1]]* [[AGG]] to { i64, i64 }*
// CHECK:   [[GEP0:%.*]] = getelementptr inbounds { i64, i64 }, { i64, i64 }* [[CAST]], i32 0, i32 0
// CHECK:   [[E0:%.*]] = extractvalue { i64, i64 } [[RET]], 0
// CHECK:   store i64 [[E0]], i64* [[GEP0]], align 4
// CHECK:   [[GEP1:%.*]] = getelementptr inbounds { i64, i64 }, { i64, i64 }* [[CAST]], i32 0, i32 1
// CHECK:   [[E1:%.*]] = extractvalue { i64, i64 } [[RET]], 1
// CHECK:   store i64 [[E1]], i64* [[GEP1]], align 4
// CHECK:   [[CAST2:%.*]] = bitcast [[STRUCT1]]* [[AGG]] to { i64, i64 }*
// CHECK:   [[GEP2:%.*]] = getelementptr inbounds { i64, i64 }, { i64, i64 }* [[CAST2]], i32 0, i32 0
// CHECK:   [[V0:%.*]] = load i64, i64* [[GEP2]], align 4
// CHECK:   [[GEP3:%.*]] = getelementptr inbounds { i64, i64 }, { i64, i64 }* [[CAST2]], i32 0, i32 1
// CHECK:   [[V1:%.*]] = load i64, i64* [[GEP3]], align 4
// CHECK:   call swiftcc void @take_struct_1(i64 [[V0]], i64 [[V1]])
// CHECK:   ret void
// CHECK: }

typedef struct {
  int x;
  char c0;
  __attribute__((aligned(2))) char c1;
  float f0;
  float f1;
} struct_2;
TEST(struct_2);
// CHECK-LABEL: define swiftcc { i64, i64 } @return_struct_2() {{.*}}{
// CHECK:   [[RET:%.*]] = alloca [[STRUCT2_TYPE]], align 4
// CHECK:   [[VAR:%.*]] = alloca [[STRUCT2_TYPE]], align 4
// CHECK:   [[CASTVAR:%.*]] = bitcast {{.*}} [[VAR]]
// CHECK:   call void @llvm.memcpy{{.*}}({{.*}}[[CASTVAR]], {{.*}}[[STRUCT2_RESULT]]
// CHECK:   [[CASTRET:%.*]] = bitcast {{.*}} [[RET]]
// CHECK:   [[CASTVAR:%.*]] = bitcast {{.*}} [[VAR]]
// CHECK:   call void @llvm.memcpy{{.*}}({{.*}}[[CASTRET]], {{.*}}[[CASTVAR]]
// CHECK:   [[CAST:%.*]] = bitcast [[STRUCT2_TYPE]]* [[RET]] to { i64, i64 }*
// CHECK:   [[GEP0:%.*]] = getelementptr inbounds { i64, i64 }, { i64, i64 }* [[CAST]], i32 0, i32 0
// CHECK:   [[T0:%.*]] = load i64, i64* [[GEP0]], align 4
// CHECK:   [[GEP1:%.*]] = getelementptr inbounds { i64, i64 }, { i64, i64 }* [[CAST]], i32 0, i32 1
// CHECK:   [[T1:%.*]] = load i64, i64* [[GEP1]], align 4
// CHECK:   [[R0:%.*]] = insertvalue { i64, i64 } undef, i64 [[T0]], 0
// CHECK:   [[R1:%.*]] = insertvalue { i64, i64 } [[R0]], i64 [[T1]], 1
// CHECK:   ret { i64, i64 } [[R1]]
// CHECK: }
// CHECK-LABEL: define swiftcc void @take_struct_2(i64, i64) {{.*}}{
// CHECK:   [[V:%.*]] = alloca [[STRUCT:%.*]], align 4
// CHECK:   [[CAST:%.*]] = bitcast [[STRUCT]]* [[V]] to { i64, i64 }*
// CHECK:   [[GEP0:%.*]] = getelementptr inbounds { i64, i64 }, { i64, i64 }* [[CAST]], i32 0, i32 0
// CHECK:   store i64 %0, i64* [[GEP0]], align 4
// CHECK:   [[GEP1:%.*]] = getelementptr inbounds { i64, i64 }, { i64, i64 }* [[CAST]], i32 0, i32 1
// CHECK:   store i64 %1, i64* [[GEP1]], align 4
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define void @test_struct_2() {{.*}} {
// CHECK:   [[TMP:%.*]] = alloca [[STRUCT2_TYPE]], align 4
// CHECK:   [[CALL:%.*]] = call swiftcc { i64, i64 } @return_struct_2()
// CHECK:   [[CAST_TMP:%.*]] = bitcast [[STRUCT2_TYPE]]* [[TMP]] to { i64, i64 }*
// CHECK:   [[GEP:%.*]] = getelementptr inbounds {{.*}} [[CAST_TMP]], i32 0, i32 0
// CHECK:   [[T0:%.*]] = extractvalue { i64, i64 } [[CALL]], 0
// CHECK:   store i64 [[T0]], i64* [[GEP]], align 4
// CHECK:   [[GEP:%.*]] = getelementptr inbounds {{.*}} [[CAST_TMP]], i32 0, i32 1
// CHECK:   [[T0:%.*]] = extractvalue { i64, i64 } [[CALL]], 1
// CHECK:   store i64 [[T0]], i64* [[GEP]], align 4
// CHECK:   [[CAST:%.*]] = bitcast [[STRUCT2_TYPE]]* [[TMP]] to { i64, i64 }*
// CHECK:   [[GEP:%.*]] = getelementptr inbounds { i64, i64 }, { i64, i64 }* [[CAST]], i32 0, i32 0
// CHECK:   [[R0:%.*]] = load i64, i64* [[GEP]], align 4
// CHECK:   [[GEP:%.*]] = getelementptr inbounds { i64, i64 }, { i64, i64 }* [[CAST]], i32 0, i32 1
// CHECK:   [[R1:%.*]] = load i64, i64* [[GEP]], align 4
// CHECK:   call swiftcc void @take_struct_2(i64 [[R0]], i64 [[R1]])
// CHECK:   ret void
// CHECK: }

// There's no way to put a field randomly in the middle of an otherwise
// empty storage unit in C, so that case has to be tested in C++, which
// can use empty structs to introduce arbitrary padding.  (In C, they end up
// with size 0 and so don't affect layout.)

// Misaligned data rule.
typedef struct {
  char c0;
  __attribute__((packed)) float f;
} struct_misaligned_1;
TEST(struct_misaligned_1)
// CHECK-LABEL: define swiftcc i64 @return_struct_misaligned_1()
// CHECK:  [[RET:%.*]] = alloca [[STRUCT:%.*]], align 1
// CHECK:  [[RES:%.*]] = alloca [[STRUCT]], align 1
// CHECK:  [[CAST:%.*]] = bitcast [[STRUCT]]* [[RES]] to i8*
// CHECK:  call void @llvm.memset{{.*}}(i8* [[CAST]], i8 0, i64 5
// CHECK:  [[CASTRET:%.*]] = bitcast [[STRUCT]]* [[RET]] to i8*
// CHECK:  [[CASTRES:%.*]] = bitcast [[STRUCT]]* [[RES]] to i8*
// CHECK:  call void @llvm.memcpy{{.*}}(i8* [[CASTRET]], i8* [[CASTRES]], i64 5
// CHECK:  [[CAST:%.*]] = bitcast [[STRUCT]]* [[RET]] to { i64 }*
// CHECK:  [[GEP:%.*]] = getelementptr inbounds { i64 }, { i64 }* [[CAST]], i32 0, i32 0
// CHECK:  [[R0:%.*]] = load i64, i64* [[GEP]], align 1
// CHECK:  ret i64 [[R0]]
// CHECK:}
// CHECK-LABEL: define swiftcc void @take_struct_misaligned_1(i64) {{.*}}{
// CHECK:   [[V:%.*]] = alloca [[STRUCT:%.*]], align 1
// CHECK:   [[CAST:%.*]] = bitcast [[STRUCT]]* [[V]] to { i64 }*
// CHECK:   [[GEP:%.*]] = getelementptr inbounds { i64 }, { i64 }* [[CAST]], i32 0, i32 0
// CHECK:   store i64 %0, i64* [[GEP]], align 1
// CHECK:   ret void
// CHECK: }
// CHECK: define void @test_struct_misaligned_1() {{.*}}{
// CHECK:   [[AGG:%.*]] = alloca [[STRUCT:%.*]], align 1
// CHECK:   [[CALL:%.*]] = call swiftcc i64 @return_struct_misaligned_1()
// CHECK:   [[T0:%.*]] = bitcast [[STRUCT]]* [[AGG]] to { i64 }*
// CHECK:   [[T1:%.*]] = getelementptr inbounds { i64 }, { i64 }* [[T0]], i32 0, i32 0
// CHECK:   store i64 [[CALL]], i64* [[T1]], align 1
// CHECK:   [[T0:%.*]] = bitcast [[STRUCT]]* [[AGG]] to { i64 }*
// CHECK:   [[T1:%.*]] = getelementptr inbounds { i64 }, { i64 }* [[T0]], i32 0, i32 0
// CHECK:   [[P:%.*]] = load i64, i64* [[T1]], align 1
// CHECK:   call swiftcc void @take_struct_misaligned_1(i64 [[P]])
// CHECK:   ret void
// CHECK: }

// Too many scalars.
typedef struct {
  long long x[5];
} struct_big_1;
TEST(struct_big_1)

// CHECK-LABEL: define {{.*}} void @return_struct_big_1({{.*}} noalias sret

// Should not be byval.
// CHECK-LABEL: define {{.*}} void @take_struct_big_1({{.*}}*{{( %.*)?}})

/*****************************************************************************/
/********************************* TYPE MERGING ******************************/
/*****************************************************************************/

typedef union {
  float f;
  double d;
} union_het_fp;
TEST(union_het_fp)
// CHECK-LABEL: define swiftcc i64 @return_union_het_fp()
// CHECK:  [[RET:%.*]] = alloca [[UNION:%.*]], align 8
// CHECK:  [[RES:%.*]] = alloca [[UNION]], align 8
// CHECK:  [[CAST:%.*]] = bitcast [[UNION]]* [[RES]] to i8*
// CHECK:  call void @llvm.memcpy{{.*}}(i8* [[CAST]]
// CHECK:  [[CASTRET:%.*]] = bitcast [[UNION]]* [[RET]] to i8*
// CHECK:  [[CASTRES:%.*]] = bitcast [[UNION]]* [[RES]] to i8*
// CHECK:  call void @llvm.memcpy{{.*}}(i8* [[CASTRET]], i8* [[CASTRES]]
// CHECK:  [[CAST:%.*]] = bitcast [[UNION]]* [[RET]] to { i64 }*
// CHECK:  [[GEP:%.*]] = getelementptr inbounds { i64 }, { i64 }* [[CAST]], i32 0, i32 0
// CHECK:  [[R0:%.*]] = load i64, i64* [[GEP]], align 8
// CHECK:  ret i64 [[R0]]
// CHECK-LABEL: define swiftcc void @take_union_het_fp(i64) {{.*}}{
// CHECK:   [[V:%.*]] = alloca [[UNION:%.*]], align 8
// CHECK:   [[CAST:%.*]] = bitcast [[UNION]]* [[V]] to { i64 }*
// CHECK:   [[GEP:%.*]] = getelementptr inbounds { i64 }, { i64 }* [[CAST]], i32 0, i32 0
// CHECK:   store i64 %0, i64* [[GEP]], align 8
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define void @test_union_het_fp() {{.*}}{
// CHECK:   [[AGG:%.*]] = alloca [[UNION:%.*]], align 8
// CHECK:   [[CALL:%.*]] = call swiftcc i64 @return_union_het_fp()
// CHECK:   [[T0:%.*]] = bitcast [[UNION]]* [[AGG]] to { i64 }*
// CHECK:   [[T1:%.*]] = getelementptr inbounds { i64 }, { i64 }* [[T0]], i32 0, i32 0
// CHECK:   store i64 [[CALL]], i64* [[T1]], align 8
// CHECK:   [[T0:%.*]] = bitcast [[UNION]]* [[AGG]] to { i64 }*
// CHECK:   [[T1:%.*]] = getelementptr inbounds { i64 }, { i64 }* [[T0]], i32 0, i32 0
// CHECK:   [[V0:%.*]] = load i64, i64* [[T1]], align 8
// CHECK:   call swiftcc void @take_union_het_fp(i64 [[V0]])
// CHECK:   ret void
// CHECK: }


typedef union {
  float f1;
  float f2;
} union_hom_fp;
TEST(union_hom_fp)
// CHECK-LABEL: define void @test_union_hom_fp()
// CHECK:   [[TMP:%.*]] = alloca [[REC:%.*]], align 4
// CHECK:   [[CALL:%.*]] = call [[SWIFTCC]] float @return_union_hom_fp()
// CHECK:   [[CAST_TMP:%.*]] = bitcast [[REC]]* [[TMP]] to [[AGG:{ float }]]*
// CHECK:   [[T0:%.*]] = getelementptr inbounds [[AGG]], [[AGG]]* [[CAST_TMP]], i32 0, i32 0
// CHECK:   store float [[CALL]], float* [[T0]], align 4
// CHECK:   [[CAST_TMP:%.*]] = bitcast [[REC]]* [[TMP]] to [[AGG]]*
// CHECK:   [[T0:%.*]] = getelementptr inbounds [[AGG]], [[AGG]]* [[CAST_TMP]], i32 0, i32 0
// CHECK:   [[FIRST:%.*]] = load float, float* [[T0]], align 4
// CHECK:   call [[SWIFTCC]] void @take_union_hom_fp(float [[FIRST]])
// CHECK:   ret void

typedef union {
  float f1;
  float4 fv2;
} union_hom_fp_partial;
TEST(union_hom_fp_partial)
// CHECK: define void @test_union_hom_fp_partial()
// CHECK:   [[AGG:%.*]] = alloca [[UNION:%.*]], align 16
// CHECK:   [[CALL:%.*]] = call swiftcc { i64, i64 } @return_union_hom_fp_partial()
// CHECK:   [[CAST:%.*]] = bitcast [[UNION]]* [[AGG]] to { i64, i64 }*
// CHECK:   [[T0:%.*]] = getelementptr inbounds { i64, i64 }, { i64, i64 }* [[CAST]], i32 0, i32 0
// CHECK:   [[T1:%.*]] = extractvalue { i64, i64 } [[CALL]], 0
// CHECK:   store i64 [[T1]], i64* [[T0]], align 16
// CHECK:   [[T0:%.*]] = getelementptr inbounds { i64, i64 }, { i64, i64 }* [[CAST]], i32 0, i32 1
// CHECK:   [[T1:%.*]] = extractvalue { i64, i64 } [[CALL]], 1
// CHECK:   store i64 [[T1]], i64* [[T0]], align 8
// CHECK:   [[CAST:%.*]] = bitcast [[UNION]]* [[AGG]] to { i64, i64 }*
// CHECK:   [[T0:%.*]] = getelementptr inbounds { i64, i64 }, { i64, i64 }* [[CAST]], i32 0, i32 0
// CHECK:   [[V0:%.*]] = load i64, i64* [[T0]], align 16
// CHECK:   [[T0:%.*]] = getelementptr inbounds { i64, i64 }, { i64, i64 }* [[CAST]], i32 0, i32 1
// CHECK:   [[V1:%.*]] = load i64, i64* [[T0]], align 8
// CHECK:   call swiftcc void @take_union_hom_fp_partial(i64 [[V0]], i64 [[V1]])
// CHECK:   ret void
// CHECK: }

typedef union {
  struct { int x, y; } f1;
  float4 fv2;
} union_het_fpv_partial;
TEST(union_het_fpv_partial)
// CHECK-LABEL: define void @test_union_het_fpv_partial()
// CHECK:   [[AGG:%.*]] = alloca [[UNION:%.*]], align 16
// CHECK:   [[CALL:%.*]] = call swiftcc { i64, i64 } @return_union_het_fpv_partial()
// CHECK:   [[CAST:%.*]] = bitcast [[UNION]]* [[AGG]] to { i64, i64 }*
// CHECK:   [[T0:%.*]] = getelementptr inbounds { i64, i64 }, { i64, i64 }* [[CAST]], i32 0, i32 0
// CHECK:   [[T1:%.*]] = extractvalue { i64, i64 } [[CALL]], 0
// CHECK:   store i64 [[T1]], i64* [[T0]], align 16
// CHECK:   [[T0:%.*]] = getelementptr inbounds { i64, i64 }, { i64, i64 }* [[CAST]], i32 0, i32 1
// CHECK:   [[T1:%.*]] = extractvalue { i64, i64 } [[CALL]], 1
// CHECK:   store i64 [[T1]], i64* [[T0]], align 8
// CHECK:   [[CAST:%.*]] = bitcast [[UNION]]* [[AGG]] to { i64, i64 }*
// CHECK:   [[T0:%.*]] = getelementptr inbounds { i64, i64 }, { i64, i64 }* [[CAST]], i32 0, i32 0
// CHECK:   [[V0:%.*]] = load i64, i64* [[T0]], align 16
// CHECK:   [[T0:%.*]] = getelementptr inbounds { i64, i64 }, { i64, i64 }* [[CAST]], i32 0, i32 1
// CHECK:   [[V1:%.*]] = load i64, i64* [[T0]], align 8
// CHECK:   call swiftcc void @take_union_het_fpv_partial(i64 [[V0]], i64 [[V1]])
// CHECK:   ret void
// CHECK: }

/*****************************************************************************/
/****************************** VECTOR LEGALIZATION **************************/
/*****************************************************************************/

TEST(int4)
// CHECK-LABEL: define {{.*}} <4 x i32> @return_int4()
// CHECK-LABEL: define {{.*}} @take_int4(<4 x i32>

TEST(int8)
// CHECK-LABEL: define {{.*}} @return_int8()
// CHECK:   [[RET:%.*]] = alloca [[REC:<8 x i32>]], align 16
// CHECK:   [[VAR:%.*]] = alloca [[REC]], align
// CHECK:   store
// CHECK:   load
// CHECK:   store
// CHECK:   [[CAST_TMP:%.*]] = bitcast [[REC]]* [[RET]] to [[AGG:{ <4 x i32>, <4 x i32> }]]*
// CHECK:   [[T0:%.*]] = getelementptr inbounds [[AGG]], [[AGG]]* [[CAST_TMP]], i32 0, i32 0
// CHECK:   [[FIRST:%.*]] = load <4 x i32>, <4 x i32>* [[T0]], align
// CHECK:   [[T0:%.*]] = getelementptr inbounds [[AGG]], [[AGG]]* [[CAST_TMP]], i32 0, i32 1
// CHECK:   [[SECOND:%.*]] = load <4 x i32>, <4 x i32>* [[T0]], align
// CHECK:   [[T0:%.*]] = insertvalue [[UAGG:{ <4 x i32>, <4 x i32> }]] undef, <4 x i32> [[FIRST]], 0
// CHECK:   [[T1:%.*]] = insertvalue [[UAGG]] [[T0]], <4 x i32> [[SECOND]], 1
// CHECK:   ret [[UAGG]] [[T1]]
// CHECK-LABEL: define {{.*}} @take_int8(<4 x i32>, <4 x i32>)
// CHECK:   [[V:%.*]] = alloca [[REC]], align
// CHECK:   [[CAST_TMP:%.*]] = bitcast [[REC]]* [[V]] to [[AGG]]*
// CHECK:   [[T0:%.*]] = getelementptr inbounds [[AGG]], [[AGG]]* [[CAST_TMP]], i32 0, i32 0
// CHECK:   store <4 x i32> %0, <4 x i32>* [[T0]], align
// CHECK:   [[T0:%.*]] = getelementptr inbounds [[AGG]], [[AGG]]* [[CAST_TMP]], i32 0, i32 1
// CHECK:   store <4 x i32> %1, <4 x i32>* [[T0]], align
// CHECK:   ret void
// CHECK-LABEL: define void @test_int8()
// CHECK:   [[TMP1:%.*]] = alloca [[REC]], align
// CHECK:   [[TMP2:%.*]] = alloca [[REC]], align
// CHECK:   [[CALL:%.*]] = call [[SWIFTCC]] [[UAGG]] @return_int8()
// CHECK:   [[CAST_TMP:%.*]] = bitcast [[REC]]* [[TMP1]] to [[AGG]]*
// CHECK:   [[T0:%.*]] = getelementptr inbounds [[AGG]], [[AGG]]* [[CAST_TMP]], i32 0, i32 0
// CHECK:   [[T1:%.*]] = extractvalue [[UAGG]] [[CALL]], 0
// CHECK:   store <4 x i32> [[T1]], <4 x i32>* [[T0]], align
// CHECK:   [[T0:%.*]] = getelementptr inbounds [[AGG]], [[AGG]]* [[CAST_TMP]], i32 0, i32 1
// CHECK:   [[T1:%.*]] = extractvalue [[UAGG]] [[CALL]], 1
// CHECK:   store <4 x i32> [[T1]], <4 x i32>* [[T0]], align
// CHECK:   [[V:%.*]] = load [[REC]], [[REC]]* [[TMP1]], align
// CHECK:   store [[REC]] [[V]], [[REC]]* [[TMP2]], align
// CHECK:   [[CAST_TMP:%.*]] = bitcast [[REC]]* [[TMP2]] to [[AGG]]*
// CHECK:   [[T0:%.*]] = getelementptr inbounds [[AGG]], [[AGG]]* [[CAST_TMP]], i32 0, i32 0
// CHECK:   [[FIRST:%.*]] = load <4 x i32>, <4 x i32>* [[T0]], align
// CHECK:   [[T0:%.*]] = getelementptr inbounds [[AGG]], [[AGG]]* [[CAST_TMP]], i32 0, i32 1
// CHECK:   [[SECOND:%.*]] = load <4 x i32>, <4 x i32>* [[T0]], align
// CHECK:   call [[SWIFTCC]] void @take_int8(<4 x i32> [[FIRST]], <4 x i32> [[SECOND]])
// CHECK:   ret void

TEST(int5)
// CHECK-LABEL: define {{.*}} @return_int5()
// CHECK:   [[RET:%.*]] = alloca [[REC:<5 x i32>]], align 16
// CHECK:   [[VAR:%.*]] = alloca [[REC]], align
// CHECK:   store
// CHECK:   load
// CHECK:   store
// CHECK:   [[CAST_TMP:%.*]] = bitcast [[REC]]* [[RET]] to [[AGG:{ <4 x i32>, i32 }]]*
// CHECK:   [[T0:%.*]] = getelementptr inbounds [[AGG]], [[AGG]]* [[CAST_TMP]], i32 0, i32 0
// CHECK:   [[FIRST:%.*]] = load <4 x i32>, <4 x i32>* [[T0]], align
// CHECK:   [[T0:%.*]] = getelementptr inbounds [[AGG]], [[AGG]]* [[CAST_TMP]], i32 0, i32 1
// CHECK:   [[SECOND:%.*]] = load i32, i32* [[T0]], align
// CHECK:   [[T0:%.*]] = insertvalue [[UAGG:{ <4 x i32>, i32 }]] undef, <4 x i32> [[FIRST]], 0
// CHECK:   [[T1:%.*]] = insertvalue [[UAGG]] [[T0]], i32 [[SECOND]], 1
// CHECK:   ret [[UAGG]] [[T1]]
// CHECK-LABEL: define {{.*}} @take_int5(<4 x i32>, i32)
// CHECK:   [[V:%.*]] = alloca [[REC]], align
// CHECK:   [[CAST_TMP:%.*]] = bitcast [[REC]]* [[V]] to [[AGG]]*
// CHECK:   [[T0:%.*]] = getelementptr inbounds [[AGG]], [[AGG]]* [[CAST_TMP]], i32 0, i32 0
// CHECK:   store <4 x i32> %0, <4 x i32>* [[T0]], align
// CHECK:   [[T0:%.*]] = getelementptr inbounds [[AGG]], [[AGG]]* [[CAST_TMP]], i32 0, i32 1
// CHECK:   store i32 %1, i32* [[T0]], align
// CHECK:   ret void
// CHECK-LABEL: define void @test_int5()
// CHECK:   [[TMP1:%.*]] = alloca [[REC]], align
// CHECK:   [[TMP2:%.*]] = alloca [[REC]], align
// CHECK:   [[CALL:%.*]] = call [[SWIFTCC]] [[UAGG]] @return_int5()
// CHECK:   [[CAST_TMP:%.*]] = bitcast [[REC]]* [[TMP1]] to [[AGG]]*
// CHECK:   [[T0:%.*]] = getelementptr inbounds [[AGG]], [[AGG]]* [[CAST_TMP]], i32 0, i32 0
// CHECK:   [[T1:%.*]] = extractvalue [[UAGG]] [[CALL]], 0
// CHECK:   store <4 x i32> [[T1]], <4 x i32>* [[T0]], align
// CHECK:   [[T0:%.*]] = getelementptr inbounds [[AGG]], [[AGG]]* [[CAST_TMP]], i32 0, i32 1
// CHECK:   [[T1:%.*]] = extractvalue [[UAGG]] [[CALL]], 1
// CHECK:   store i32 [[T1]], i32* [[T0]], align
// CHECK:   [[V:%.*]] = load [[REC]], [[REC]]* [[TMP1]], align
// CHECK:   store [[REC]] [[V]], [[REC]]* [[TMP2]], align
// CHECK:   [[CAST_TMP:%.*]] = bitcast [[REC]]* [[TMP2]] to [[AGG]]*
// CHECK:   [[T0:%.*]] = getelementptr inbounds [[AGG]], [[AGG]]* [[CAST_TMP]], i32 0, i32 0
// CHECK:   [[FIRST:%.*]] = load <4 x i32>, <4 x i32>* [[T0]], align
// CHECK:   [[T0:%.*]] = getelementptr inbounds [[AGG]], [[AGG]]* [[CAST_TMP]], i32 0, i32 1
// CHECK:   [[SECOND:%.*]] = load i32, i32* [[T0]], align
// CHECK:   call [[SWIFTCC]] void @take_int5(<4 x i32> [[FIRST]], i32 [[SECOND]])
// CHECK:   ret void

typedef struct {
  int x;
  int3 v __attribute__((packed));
} misaligned_int3;
TEST(misaligned_int3)
// CHECK-LABEL: define swiftcc void @take_misaligned_int3(i64, i64)

typedef struct {
  float f0;
} struct_f1;
TEST(struct_f1)
// CHECK-LABEL: define swiftcc float @return_struct_f1()
// CHECK-LABEL: define swiftcc void @take_struct_f1(float)

typedef struct {
  float f0;
  float f1;
} struct_f2;
TEST(struct_f2)
// CHECK-LABEL: define swiftcc i64 @return_struct_f2()
// CHECK-LABEL: define swiftcc void @take_struct_f2(i64)

typedef struct {
  float f0;
  float f1;
  float f2;
} struct_f3;
TEST(struct_f3)
// CHECK-LABEL: define swiftcc { i64, float } @return_struct_f3()
// CHECK-LABEL: define swiftcc void @take_struct_f3(i64, float)

typedef struct {
  float f0;
  float f1;
  float f2;
  float f3;
} struct_f4;
TEST(struct_f4)
// CHECK-LABEL: define swiftcc { i64, i64 } @return_struct_f4()
// CHECK-LABEL: define swiftcc void @take_struct_f4(i64, i64)


typedef struct {
  double d0;
} struct_d1;
TEST(struct_d1)
// CHECK-LABEL: define swiftcc double @return_struct_d1()
// CHECK-LABEL: define swiftcc void @take_struct_d1(double)

typedef struct {
  double d0;
  double d1;
} struct_d2;
TEST(struct_d2)
// CHECK-LABEL: define swiftcc { double, double } @return_struct_d2()
// CHECK-LABEL: define swiftcc void @take_struct_d2(double, double)

typedef struct {
  char c0;
} struct_c1;
TEST(struct_c1)
// CHECK-LABEL: define swiftcc i8 @return_struct_c1()
// CHECK-LABEL: define swiftcc void @take_struct_c1(i8)

typedef struct {
  char c0;
  char c1;
} struct_c2;
TEST(struct_c2)
// CHECK-LABEL: define swiftcc i16 @return_struct_c2()
// CHECK-LABEL: define swiftcc void @take_struct_c2(i16)
//

typedef struct {
  char c0;
  char c1;
  char c2;
} struct_c3;
TEST(struct_c3)
// CHECK-LABEL: define swiftcc i32 @return_struct_c3()
// CHECK-LABEL: define swiftcc void @take_struct_c3(i32)

typedef struct {
  char c0;
  char c1;
  char c2;
  char c3;
} struct_c4;
TEST(struct_c4)
// CHECK-LABEL: define swiftcc i32 @return_struct_c4()
// CHECK-LABEL: define swiftcc void @take_struct_c4(i32)

typedef struct {
  char c0;
  char c1;
  char c2;
  char c3;
  char c4;
} struct_c5;
TEST(struct_c5)
// CHECK-LABEL: define swiftcc i64 @return_struct_c5()
// CHECK-LABEL: define swiftcc void @take_struct_c5(i64)
//
typedef struct {
  char c0;
  char c1;
  char c2;
  char c3;
  char c4;
  char c5;
  char c6;
  char c7;
  char c8;
} struct_c9;
TEST(struct_c9)
// CHECK-LABEL: define swiftcc { i64, i8 } @return_struct_c9()
// CHECK-LABEL: define swiftcc void @take_struct_c9(i64, i8)

typedef struct {
  short s0;
} struct_s1;
TEST(struct_s1)
// CHECK-LABEL: define swiftcc i16 @return_struct_s1()
// CHECK-LABEL: define swiftcc void @take_struct_s1(i16)

typedef struct {
  short s0;
  short s1;
} struct_s2;
TEST(struct_s2)
// CHECK-LABEL: define swiftcc i32 @return_struct_s2()
// CHECK-LABEL: define swiftcc void @take_struct_s2(i32)
//

typedef struct {
  short s0;
  short s1;
  short s2;
} struct_s3;
TEST(struct_s3)
// CHECK-LABEL: define swiftcc i64 @return_struct_s3()
// CHECK-LABEL: define swiftcc void @take_struct_s3(i64)

typedef struct {
  short s0;
  short s1;
  short s2;
  short s3;
} struct_s4;
TEST(struct_s4)
// CHECK-LABEL: define swiftcc i64 @return_struct_s4()
// CHECK-LABEL: define swiftcc void @take_struct_s4(i64)

typedef struct {
  short s0;
  short s1;
  short s2;
  short s3;
  short s4;
} struct_s5;
TEST(struct_s5)
// CHECK-LABEL: define swiftcc { i64, i16 } @return_struct_s5()
// CHECK-LABEL: define swiftcc void @take_struct_s5(i64, i16)


typedef struct {
  int i0;
} struct_i1;
TEST(struct_i1)
// CHECK-LABEL: define swiftcc i32 @return_struct_i1()
// CHECK-LABEL: define swiftcc void @take_struct_i1(i32)

typedef struct {
  int i0;
  int i1;
} struct_i2;
TEST(struct_i2)
// CHECK-LABEL: define swiftcc i64 @return_struct_i2()
// CHECK-LABEL: define swiftcc void @take_struct_i2(i64)

typedef struct {
  int i0;
  int i1;
  int i2;
} struct_i3;
TEST(struct_i3)
// CHECK-LABEL: define swiftcc { i64, i32 } @return_struct_i3()
// CHECK-LABEL: define swiftcc void @take_struct_i3(i64, i32)

typedef struct {
  int i0;
  int i1;
  int i2;
  int i3;
} struct_i4;
TEST(struct_i4)
// CHECK-LABEL: define swiftcc { i64, i64 } @return_struct_i4()
// CHECK-LABEL: define swiftcc void @take_struct_i4(i64, i64)

typedef struct {
  long long l0;
} struct_l1;
TEST(struct_l1)
// CHECK-LABEL: define swiftcc i64 @return_struct_l1()
// CHECK-LABEL: define swiftcc void @take_struct_l1(i64)

typedef struct {
  long long l0;
  long long l1;
} struct_l2;
TEST(struct_l2)
// CHECK-LABEL: define swiftcc { i64, i64 } @return_struct_l2()
// CHECK-LABEL: define swiftcc void @take_struct_l2(i64, i64)

typedef struct {
  long long l0;
  long long l1;
  long long l2;
} struct_l3;
TEST(struct_l3)
// CHECK-LABEL: define swiftcc { i64, i64, i64 } @return_struct_l3()
// CHECK-LABEL: define swiftcc void @take_struct_l3(i64, i64, i64)

typedef struct {
  long long l0;
  long long l1;
  long long l2;
  long long l3;
} struct_l4;
TEST(struct_l4)
// CHECK-LABEL: define swiftcc { i64, i64, i64, i64 } @return_struct_l4()
// CHECK-LABEL: define swiftcc void @take_struct_l4(i64, i64, i64, i64)

typedef struct {
  long long l0;
  long long l1;
  long long l2;
  long long l3;
  long long l4;
} struct_l5;
TEST(struct_l5)
// CHECK: define swiftcc void @return_struct_l5([[STRUCT5:%.*]]* noalias sret
// CHECK: define swiftcc void @take_struct_l5([[STRUCT5]]*


// Don't crash.
typedef union {
int4 v[2];
struct {
  int LSW;
  int d7;
  int d6;
  int d5;
  int d4;
  int d3;
  int d2;
  int MSW;
} s;
} union_het_vecint;
TEST(union_het_vecint)
// CHECK: define swiftcc void @return_union_het_vecint([[UNION:%.*]]* noalias sret
// CHECK: define swiftcc void @take_union_het_vecint([[UNION]]*
