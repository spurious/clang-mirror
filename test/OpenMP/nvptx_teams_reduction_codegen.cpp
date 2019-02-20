// Test target codegen - host bc file has to be created first.
// RUN: %clang_cc1 -verify -fopenmp -x c++ -triple powerpc64le-unknown-unknown -fopenmp-targets=nvptx64-nvidia-cuda -emit-llvm-bc %s -o %t-ppc-host.bc
// RUN: %clang_cc1 -verify -fopenmp -x c++ -triple nvptx64-unknown-unknown -fopenmp-targets=nvptx64-nvidia-cuda -emit-llvm %s -fopenmp-is-device -fopenmp-host-ir-file-path %t-ppc-host.bc -o - | FileCheck %s --check-prefix CHECK --check-prefix CHECK-64
// RUN: %clang_cc1 -verify -fopenmp -x c++ -triple i386-unknown-unknown -fopenmp-targets=nvptx-nvidia-cuda -emit-llvm-bc %s -o %t-x86-host.bc
// RUN: %clang_cc1 -verify -fopenmp -x c++ -triple nvptx-unknown-unknown -fopenmp-targets=nvptx-nvidia-cuda -emit-llvm %s -fopenmp-is-device -fopenmp-host-ir-file-path %t-x86-host.bc -o - | FileCheck %s --check-prefix CHECK --check-prefix CHECK-32
// RUN: %clang_cc1 -verify -fopenmp -fexceptions -fcxx-exceptions -x c++ -triple nvptx-unknown-unknown -fopenmp-targets=nvptx-nvidia-cuda -fopenmp-cuda-teams-reduction-recs-num=2048 -emit-llvm %s -fopenmp-is-device -fopenmp-host-ir-file-path %t-x86-host.bc -o - | FileCheck %s --check-prefix CHECK --check-prefix CHECK-32
// expected-no-diagnostics
#ifndef HEADER
#define HEADER

// CHECK-DAG: [[TEAM1_REDUCE_TY:%.+]] = type { [{{1024|2048}} x double] }
// CHECK-DAG: [[TEAM2_REDUCE_TY:%.+]] = type { [{{1024|2048}} x i8], [{{1024|2048}} x float] }
// CHECK-DAG: [[TEAM3_REDUCE_TY:%.+]] = type { [{{1024|2048}} x i32], [{{1024|2048}} x i16] }
// CHECK-DAG: [[TEAMS_REDUCE_UNION_TY:%.+]] = type { [[TEAM1_REDUCE_TY]] }
// CHECK-DAG: [[MAP_TY:%.+]] = type { [128 x i8] }

// CHECK-DAG: [[KERNEL_PTR:@.+]] = internal addrspace(3) global i8* null
// CHECK-DAG: [[KERNEL_SHARED1:@.+]] = internal unnamed_addr constant i16 1
// CHECK-DAG: [[KERNEL_SHARED2:@.+]] = internal unnamed_addr constant i16 1
// CHECK-DAG: [[KERNEL_SIZE1:@.+]] = internal unnamed_addr constant i{{64|32}} {{16|8}}
// CHECK-DAG: [[KERNEL_SIZE2:@.+]] = internal unnamed_addr constant i{{64|32}} 16

// Check for the data transfer medium in shared memory to transfer the reduction list to the first warp.
// CHECK-DAG: [[TRANSFER_STORAGE:@.+]] = common addrspace([[SHARED_ADDRSPACE:[0-9]+]]) global [32 x i32]

// Check that the execution mode of 2 target regions is set to Non-SPMD and the 3rd is in SPMD.
// CHECK-DAG: {{@__omp_offloading_.+l41}}_exec_mode = weak constant i8 1
// CHECK-DAG: {{@__omp_offloading_.+l47}}_exec_mode = weak constant i8 1
// CHECK-DAG: {{@__omp_offloading_.+l54}}_exec_mode = weak constant i8 0

// CHECK-DAG: [[TEAMS_RED_BUFFER:@.+]] = common global [[TEAMS_REDUCE_UNION_TY]] zeroinitializer

template<typename tx>
tx ftemplate(int n) {
  int a;
  short b;
  tx c;
  float d;
  double e;

  #pragma omp target
  #pragma omp teams reduction(+: e)
  {
    e += 5;
  }

  #pragma omp target
  #pragma omp teams reduction(^: c) reduction(*: d)
  {
    c ^= 2;
    d *= 33;
  }

  #pragma omp target
  #pragma omp teams reduction(|: a) reduction(max: b)
  #pragma omp parallel reduction(|: a) reduction(max: b)
  {
    a |= 1;
    b = 99 > b ? 99 : b;
  }

  return a+b+c+d+e;
}

int bar(int n){
  int a = 0;

  a += ftemplate<char>(n);

  return a;
}

  // CHECK-LABEL: define {{.*}}void {{@__omp_offloading_.+template.+l41}}_worker()

  // CHECK: define {{.*}}void [[T1:@__omp_offloading_.+template.+l41]](
  //
  // CHECK: {{call|invoke}} void [[T1]]_worker()
  //
  // CHECK: call void @__kmpc_kernel_init(
  //
  // CHECK: store double {{[0\.e\+]+}}, double* [[E:%.+]], align
  // CHECK: [[EV:%.+]] = load double, double* [[E]], align
  // CHECK: [[ADD:%.+]] = fadd double [[EV]], 5
  // CHECK: store double [[ADD]], double* [[E]], align
  // CHECK: [[GEP1:%.+]] = getelementptr inbounds [1 x i8*], [1 x i8*]* [[RED_LIST:%.+]], i{{[0-9]+}} 0, i{{[0-9]+}} 0
  // CHECK: [[BC:%.+]] = bitcast double* [[E]] to i8*
  // CHECK: store i8* [[BC]], i8** [[GEP1]],
  // CHECK: [[BC_RED_LIST:%.+]] = bitcast [1 x i8*]* [[RED_LIST]] to i8*
  // CHECK: [[RET:%.+]] = call i32 @__kmpc_nvptx_teams_reduce_nowait_v2(%struct.ident_t* [[LOC:@.+]], i32 [[GTID:%.+]], i8* bitcast ([[TEAMS_REDUCE_UNION_TY]]* [[TEAMS_RED_BUFFER]] to i8*), i32 {{1024|2048}}, i8* [[BC_RED_LIST]], void (i8*, i16, i16, i16)* [[SHUFFLE_AND_REDUCE:@.+]], void (i8*, i32)* [[INTER_WARP_COPY:@.+]], void (i8*, i32, i8*)* [[RED_LIST_TO_GLOBAL_COPY:@.+]], void (i8*, i32, i8*)* [[RED_LIST_TO_GLOBAL_RED:@.+]], void (i8*, i32, i8*)* [[GLOBAL_TO_RED_LIST_COPY:@.+]], void (i8*, i32, i8*)* [[GLOBAL_TO_RED_LIST_RED:@.+]])
  // CHECK: [[COND:%.+]] = icmp eq i32 [[RET]], 1
  // CHECK: br i1 [[COND]], label {{%?}}[[IFLABEL:.+]], label {{%?}}[[EXIT:.+]]
  //
  // CHECK: [[IFLABEL]]
  // CHECK: [[E_INV:%.+]] = load double, double* [[E_IN:%.+]], align
  // CHECK: [[EV:%.+]] = load double, double* [[E]], align
  // CHECK: [[ADD:%.+]] = fadd double [[E_INV]], [[EV]]
  // CHECK: store double [[ADD]], double* [[E_IN]], align
  // CHECK: call void @__kmpc_nvptx_end_reduce_nowait(i32 [[GTID]])
  // CHECK: br label %[[EXIT]]
  //
  // CHECK: [[EXIT]]
  // CHECK: call void @__kmpc_kernel_deinit(

  //
  // Reduction function
  // CHECK: define internal void [[REDUCTION_FUNC:@.+]](i8*, i8*)
  // CHECK: [[VAR_RHS_REF:%.+]] = getelementptr inbounds [1 x i8*], [1 x i8*]* [[RED_LIST_RHS:%.+]], i{{32|64}} 0, i{{32|64}} 0
  // CHECK: [[VAR_RHS_VOID:%.+]] = load i8*, i8** [[VAR_RHS_REF]],
  // CHECK: [[VAR_RHS:%.+]] = bitcast i8* [[VAR_RHS_VOID]] to double*
  //
  // CHECK: [[VAR_LHS_REF:%.+]] = getelementptr inbounds [1 x i8*], [1 x i8*]* [[RED_LIST_LHS:%.+]], i{{32|64}} 0, i{{32|64}} 0
  // CHECK: [[VAR_LHS_VOID:%.+]] = load i8*, i8** [[VAR_LHS_REF]],
  // CHECK: [[VAR_LHS:%.+]] = bitcast i8* [[VAR_LHS_VOID]] to double*
  //
  // CHECK: [[VAR_LHS_VAL:%.+]] = load double, double* [[VAR_LHS]],
  // CHECK: [[VAR_RHS_VAL:%.+]] = load double, double* [[VAR_RHS]],
  // CHECK: [[RES:%.+]] = fadd double [[VAR_LHS_VAL]], [[VAR_RHS_VAL]]
  // CHECK: store double [[RES]], double* [[VAR_LHS]],
  // CHECK: ret void

  //
  // Shuffle and reduce function
  // CHECK: define internal void [[SHUFFLE_AND_REDUCE]](i8*, i16 {{.*}}, i16 {{.*}}, i16 {{.*}})
  // CHECK: [[REMOTE_RED_LIST:%.+]] = alloca [1 x i8*], align
  // CHECK: [[REMOTE_ELT:%.+]] = alloca double
  //
  // CHECK: [[LANEID:%.+]] = load i16, i16* {{.+}}, align
  // CHECK: [[LANEOFFSET:%.+]] = load i16, i16* {{.+}}, align
  // CHECK: [[ALGVER:%.+]] = load i16, i16* {{.+}}, align
  //
  // CHECK: [[ELT_REF:%.+]] = getelementptr inbounds [1 x i8*], [1 x i8*]* [[RED_LIST:%.+]], i{{32|64}} 0, i{{32|64}} 0
  // CHECK: [[ELT_VOID:%.+]] = load i8*, i8** [[ELT_REF]],
  // CHECK: [[REMOTE_ELT_REF:%.+]] = getelementptr inbounds [1 x i8*], [1 x i8*]* [[REMOTE_RED_LIST:%.+]], i{{32|64}} 0, i{{32|64}} 0
  // CHECK: [[ELT:%.+]] = bitcast i8* [[ELT_VOID]] to double*
  //
  // CHECK: [[ELT_CAST:%.+]] = bitcast double* [[ELT]] to i64*
  // CHECK: [[REMOTE_ELT_CAST:%.+]] = bitcast double* [[REMOTE_ELT]] to i64*
  // CHECK: [[ELT_VAL:%.+]] = load i64, i64* [[ELT_CAST]], align
  // CHECK: [[WS32:%.+]] = call i32 @llvm.nvvm.read.ptx.sreg.warpsize()
  // CHECK: [[WS:%.+]] = trunc i32 [[WS32]] to i16
  // CHECK: [[REMOTE_ELT_VAL64:%.+]] = call i64 @__kmpc_shuffle_int64(i64 [[ELT_VAL]], i16 [[LANEOFFSET]], i16 [[WS]])
  //
  // CHECK: store i64 [[REMOTE_ELT_VAL64]], i64* [[REMOTE_ELT_CAST]], align
  // CHECK: [[REMOTE_ELT_VOID:%.+]] = bitcast double* [[REMOTE_ELT]] to i8*
  // CHECK: store i8* [[REMOTE_ELT_VOID]], i8** [[REMOTE_ELT_REF]], align
  //
  // Condition to reduce
  // CHECK: [[CONDALG0:%.+]] = icmp eq i16 [[ALGVER]], 0
  //
  // CHECK: [[COND1:%.+]] = icmp eq i16 [[ALGVER]], 1
  // CHECK: [[COND2:%.+]] = icmp ult i16 [[LANEID]], [[LANEOFFSET]]
  // CHECK: [[CONDALG1:%.+]] = and i1 [[COND1]], [[COND2]]
  //
  // CHECK: [[COND3:%.+]] = icmp eq i16 [[ALGVER]], 2
  // CHECK: [[COND4:%.+]] = and i16 [[LANEID]], 1
  // CHECK: [[COND5:%.+]] = icmp eq i16 [[COND4]], 0
  // CHECK: [[COND6:%.+]] = and i1 [[COND3]], [[COND5]]
  // CHECK: [[COND7:%.+]] = icmp sgt i16 [[LANEOFFSET]], 0
  // CHECK: [[CONDALG2:%.+]] = and i1 [[COND6]], [[COND7]]
  //
  // CHECK: [[COND8:%.+]] = or i1 [[CONDALG0]], [[CONDALG1]]
  // CHECK: [[SHOULD_REDUCE:%.+]] = or i1 [[COND8]], [[CONDALG2]]
  // CHECK: br i1 [[SHOULD_REDUCE]], label {{%?}}[[DO_REDUCE:.+]], label {{%?}}[[REDUCE_ELSE:.+]]
  //
  // CHECK: [[DO_REDUCE]]
  // CHECK: [[RED_LIST1_VOID:%.+]] = bitcast [1 x i8*]* [[RED_LIST]] to i8*
  // CHECK: [[RED_LIST2_VOID:%.+]] = bitcast [1 x i8*]* [[REMOTE_RED_LIST]] to i8*
  // CHECK: call void [[REDUCTION_FUNC]](i8* [[RED_LIST1_VOID]], i8* [[RED_LIST2_VOID]])
  // CHECK: br label {{%?}}[[REDUCE_CONT:.+]]
  //
  // CHECK: [[REDUCE_ELSE]]
  // CHECK: br label {{%?}}[[REDUCE_CONT]]
  //
  // CHECK: [[REDUCE_CONT]]
  // Now check if we should just copy over the remote reduction list
  // CHECK: [[COND1:%.+]] = icmp eq i16 [[ALGVER]], 1
  // CHECK: [[COND2:%.+]] = icmp uge i16 [[LANEID]], [[LANEOFFSET]]
  // CHECK: [[SHOULD_COPY:%.+]] = and i1 [[COND1]], [[COND2]]
  // CHECK: br i1 [[SHOULD_COPY]], label {{%?}}[[DO_COPY:.+]], label {{%?}}[[COPY_ELSE:.+]]
  //
  // CHECK: [[DO_COPY]]
  // CHECK: [[REMOTE_ELT_REF:%.+]] = getelementptr inbounds [1 x i8*], [1 x i8*]* [[REMOTE_RED_LIST]], i{{32|64}} 0, i{{32|64}} 0
  // CHECK: [[REMOTE_ELT_VOID:%.+]] = load i8*, i8** [[REMOTE_ELT_REF]],
  // CHECK: [[ELT_REF:%.+]] = getelementptr inbounds [1 x i8*], [1 x i8*]* [[RED_LIST]], i{{32|64}} 0, i{{32|64}} 0
  // CHECK: [[ELT_VOID:%.+]] = load i8*, i8** [[ELT_REF]],
  // CHECK: [[REMOTE_ELT:%.+]] = bitcast i8* [[REMOTE_ELT_VOID]] to double*
  // CHECK: [[ELT:%.+]] = bitcast i8* [[ELT_VOID]] to double*
  // CHECK: [[REMOTE_ELT_VAL:%.+]] = load double, double* [[REMOTE_ELT]], align
  // CHECK: store double [[REMOTE_ELT_VAL]], double* [[ELT]], align
  // CHECK: br label {{%?}}[[COPY_CONT:.+]]
  //
  // CHECK: [[COPY_ELSE]]
  // CHECK: br label {{%?}}[[COPY_CONT]]
  //
  // CHECK: [[COPY_CONT]]
  // CHECK: void

  //
  // Inter warp copy function
  // CHECK: define internal void [[INTER_WARP_COPY]](i8*, i32)
  // CHECK-DAG: [[LANEID:%.+]] = and i32 {{.+}}, 31
  // CHECK-DAG: [[WARPID:%.+]] = ashr i32 {{.+}}, 5
  // CHECK-DAG: [[RED_LIST:%.+]] = bitcast i8* {{.+}} to [1 x i8*]*
  // CHECK: store i32 0, i32* [[CNT_ADDR:%.+]],
  // CHECK: br label
  // CHECK: [[CNT:%.+]] = load i32, i32* [[CNT_ADDR]],
  // CHECK: [[DONE_COPY:%.+]] = icmp ult i32 [[CNT]], 2
  // CHECK: br i1 [[DONE_COPY]], label
  // CHECK: call void @__kmpc_barrier(%struct.ident_t* @
  // CHECK: [[IS_WARP_MASTER:%.+]] = icmp eq i32 [[LANEID]], 0
  // CHECK: br i1 [[IS_WARP_MASTER]], label {{%?}}[[DO_COPY:.+]], label {{%?}}[[COPY_ELSE:.+]]
  //
  // [[DO_COPY]]
  // CHECK: [[ELT_REF:%.+]] = getelementptr inbounds [1 x i8*], [1 x i8*]* [[RED_LIST]], i{{32|64}} 0, i{{32|64}} 0
  // CHECK: [[ELT_VOID:%.+]] = load i8*, i8** [[ELT_REF]],
  // CHECK: [[BASE_ELT:%.+]] = bitcast i8* [[ELT_VOID]] to i32*
  // CHECK: [[ELT:%.+]] = getelementptr i32, i32* [[BASE_ELT]], i32 [[CNT]]
  //
  // CHECK: [[MEDIUM_ELT:%.+]] = getelementptr inbounds [32 x i32], [32 x i32] addrspace([[SHARED_ADDRSPACE]])* [[TRANSFER_STORAGE]], i64 0, i32 [[WARPID]]
  // CHECK: [[ELT_VAL:%.+]] = load i32, i32* [[ELT]],
  // CHECK: store volatile i32 [[ELT_VAL]], i32 addrspace([[SHARED_ADDRSPACE]])* [[MEDIUM_ELT]],
  // CHECK: br label {{%?}}[[COPY_CONT:.+]]
  //
  // CHECK: [[COPY_ELSE]]
  // CHECK: br label {{%?}}[[COPY_CONT]]
  //
  // Barrier after copy to shared memory storage medium.
  // CHECK: [[COPY_CONT]]
  // CHECK: call void @__kmpc_barrier(%struct.ident_t* @
  // CHECK: [[ACTIVE_WARPS:%.+]] = load i32, i32*
  //
  // Read into warp 0.
  // CHECK: [[IS_W0_ACTIVE_THREAD:%.+]] = icmp ult i32 [[TID:%.+]], [[ACTIVE_WARPS]]
  // CHECK: br i1 [[IS_W0_ACTIVE_THREAD]], label {{%?}}[[DO_READ:.+]], label {{%?}}[[READ_ELSE:.+]]
  //
  // CHECK: [[DO_READ]]
  // CHECK: [[MEDIUM_ELT:%.+]] = getelementptr inbounds [32 x i32], [32 x i32] addrspace([[SHARED_ADDRSPACE]])* [[TRANSFER_STORAGE]], i64 0, i32 [[TID]]
  // CHECK: [[ELT_REF:%.+]] = getelementptr inbounds [1 x i8*], [1 x i8*]* [[RED_LIST:%.+]], i{{32|64}} 0, i{{32|64}} 0
  // CHECK: [[ELT_VOID:%.+]] = load i8*, i8** [[ELT_REF]],
  // CHECK: [[ELT_BASE:%.+]] = bitcast i8* [[ELT_VOID]] to i32*
  // CHECK: [[ELT:%.+]] = getelementptr i32, i32* [[ELT_BASE]], i32 [[CNT]]
  // CHECK: [[MEDIUM_ELT_VAL:%.+]] = load volatile i32, i32 addrspace([[SHARED_ADDRSPACE]])* [[MEDIUM_ELT]],
  // CHECK: store i32 [[MEDIUM_ELT_VAL]], i32* [[ELT]],
  // CHECK: br label {{%?}}[[READ_CONT:.+]]
  //
  // CHECK: [[READ_ELSE]]
  // CHECK: br label {{%?}}[[READ_CONT]]
  //
  // CHECK: [[READ_CONT]]
  // CHECK: [[NEXT:%.+]] = add nsw i32 [[CNT]], 1
  // CHECK: store i32 [[NEXT]], i32* [[CNT_ADDR]],
  // CHECK: br label
  // CHECK: ret

  // CHECK: define internal void [[RED_LIST_TO_GLOBAL_COPY]](i8*, i32, i8*)
  // CHECK: [[GLOBAL_PTR:%.+]] = alloca i8*,
  // CHECK: [[IDX_PTR:%.+]] = alloca i32,
  // CHECK: [[RL_PTR:%.+]] = alloca i8*,
  // CHECK: store i8* %{{.+}}, i8** [[GLOBAL_PTR]],
  // CHECK: store i32 %{{.+}}, i32* [[IDX_PTR]],
  // CHECK: store i8* %{{.+}}, i8** [[RL_PTR]],
  // CHECK: [[RL_BC:%.+]] = load i8*, i8** [[RL_PTR]],
  // CHECK: [[RL:%.+]] = bitcast i8* [[RL_BC]] to [1 x i8*]*
  // CHECK: [[GLOBAL_BC:%.+]] = load i8*, i8** [[GLOBAL_PTR]],
  // CHECK: [[GLOBAL:%.+]] = bitcast i8* [[GLOBAL_BC]] to [[TEAM1_REDUCE_TY]]*
  // CHECK: [[IDX:%.+]] = load i32, i32* [[IDX_PTR]],
  // CHECK: [[RL_RED1_PTR:%.+]] = getelementptr inbounds [1 x i8*], [1 x i8*]* [[RL]], i{{[0-9]+}} 0, i{{[0-9]+}} 0
  // CHECK: [[RL_RED1_BC:%.+]] = load i8*, i8** [[RL_RED1_PTR]],
  // CHECK: [[RL_RED1:%.+]] = bitcast i8* [[RL_RED1_BC]] to double*
  // CHECK: [[GLOBAL_RED1_PTR:%.+]] = getelementptr inbounds [[TEAM1_REDUCE_TY]], [[TEAM1_REDUCE_TY]]* [[GLOBAL]], i{{[0-9]+}} 0, i{{[0-9]+}} 0
  // CHECK: [[GLOBAL_RED1_IDX_PTR:%.+]] = getelementptr inbounds [{{1024|2048}} x double], [{{1024|2048}} x double]* [[GLOBAL_RED1_PTR]], i{{[0-9]+}} 0, i32 [[IDX]]
  // CHECK: [[LOC_RED1:%.+]] = load double, double* [[RL_RED1]],
  // CHECK: store double [[LOC_RED1]], double* [[GLOBAL_RED1_IDX_PTR]],
  // CHECK: ret void

  // CHECK: define internal void [[RED_LIST_TO_GLOBAL_RED]](i8*, i32, i8*)
  // CHECK: [[GLOBAL_PTR:%.+]] = alloca i8*,
  // CHECK: [[IDX_PTR:%.+]] = alloca i32,
  // CHECK: [[RL_PTR:%.+]] = alloca i8*,
  // CHECK: [[LOCAL_RL:%.+]] = alloca [1 x i8*],
  // CHECK: store i8* %{{.+}}, i8** [[GLOBAL_PTR]],
  // CHECK: store i32 %{{.+}}, i32* [[IDX_PTR]],
  // CHECK: store i8* %{{.+}}, i8** [[RL_PTR]],
  // CHECK: [[GLOBAL_BC:%.+]] = load i8*, i8** [[GLOBAL_PTR]],
  // CHECK: [[GLOBAL:%.+]] = bitcast i8* [[GLOBAL_BC]] to [[TEAM1_REDUCE_TY]]*
  // CHECK: [[IDX:%.+]] = load i32, i32* [[IDX_PTR]],
  // CHECK: [[LOCAL_RL_RED1_PTR:%.+]] = getelementptr inbounds [1 x i8*], [1 x i8*]* [[LOCAL_RL]], i{{[0-9]+}} 0, i{{[0-9]+}} 0
  // CHECK: [[GLOBAL_RED1_PTR:%.+]] = getelementptr inbounds [[TEAM1_REDUCE_TY]], [[TEAM1_REDUCE_TY]]* [[GLOBAL]], i{{[0-9]+}} 0, i{{[0-9]+}} 0
  // CHECK: [[GLOBAL_RED1_IDX_PTR:%.+]] = getelementptr inbounds [{{1024|2048}} x double], [{{1024|2048}} x double]* [[GLOBAL_RED1_PTR]], i{{[0-9]+}} 0, i32 [[IDX]]
  // CHECK: [[GLOBAL_RED1_IDX_PTR_BC:%.+]] = bitcast double* [[GLOBAL_RED1_IDX_PTR]] to i8*
  // CHECK: store i8* [[GLOBAL_RED1_IDX_PTR_BC]], i8** [[LOCAL_RL_RED1_PTR]]
  // CHECK: [[LOCAL_RL_BC:%.+]] = bitcast [1 x i8*]* [[LOCAL_RL]] to i8*
  // CHECK: [[RL_BC:%.+]] = load i8*, i8** [[RL_PTR]],
  // CHECK: call void [[REDUCTION_FUNC]](i8* [[LOCAL_RL_BC]], i8* [[RL_BC]])
  // CHECK: ret void

  // CHECK: define internal void [[GLOBAL_TO_RED_LIST_COPY]](i8*, i32, i8*)
  // CHECK: [[GLOBAL_PTR:%.+]] = alloca i8*,
  // CHECK: [[IDX_PTR:%.+]] = alloca i32,
  // CHECK: [[RL_PTR:%.+]] = alloca i8*,
  // CHECK: store i8* %{{.+}}, i8** [[GLOBAL_PTR]],
  // CHECK: store i32 %{{.+}}, i32* [[IDX_PTR]],
  // CHECK: store i8* %{{.+}}, i8** [[RL_PTR]],
  // CHECK: [[RL_BC:%.+]] = load i8*, i8** [[RL_PTR]],
  // CHECK: [[RL:%.+]] = bitcast i8* [[RL_BC]] to [1 x i8*]*
  // CHECK: [[GLOBAL_BC:%.+]] = load i8*, i8** [[GLOBAL_PTR]],
  // CHECK: [[GLOBAL:%.+]] = bitcast i8* [[GLOBAL_BC]] to [[TEAM1_REDUCE_TY]]*
  // CHECK: [[IDX:%.+]] = load i32, i32* [[IDX_PTR]],
  // CHECK: [[RL_RED1_PTR:%.+]] = getelementptr inbounds [1 x i8*], [1 x i8*]* [[RL]], i{{[0-9]+}} 0, i{{[0-9]+}} 0
  // CHECK: [[RL_RED1_BC:%.+]] = load i8*, i8** [[RL_RED1_PTR]],
  // CHECK: [[RL_RED1:%.+]] = bitcast i8* [[RL_RED1_BC]] to double*
  // CHECK: [[GLOBAL_RED1_PTR:%.+]] = getelementptr inbounds [[TEAM1_REDUCE_TY]], [[TEAM1_REDUCE_TY]]* [[GLOBAL]], i{{[0-9]+}} 0, i{{[0-9]+}} 0
  // CHECK: [[GLOBAL_RED1_IDX_PTR:%.+]] = getelementptr inbounds [{{1024|2048}} x double], [{{1024|2048}} x double]* [[GLOBAL_RED1_PTR]], i{{[0-9]+}} 0, i32 [[IDX]]
  // CHECK: [[GLOBAL_RED1:%.+]] = load double, double* [[GLOBAL_RED1_IDX_PTR]],
  // CHECK: store double [[GLOBAL_RED1]], double* [[RL_RED1]],
  // CHECK: ret void

  // CHECK: define internal void [[GLOBAL_TO_RED_LIST_RED]](i8*, i32, i8*)
  // CHECK: [[GLOBAL_PTR:%.+]] = alloca i8*,
  // CHECK: [[IDX_PTR:%.+]] = alloca i32,
  // CHECK: [[RL_PTR:%.+]] = alloca i8*,
  // CHECK: [[LOCAL_RL:%.+]] = alloca [1 x i8*],
  // CHECK: store i8* %{{.+}}, i8** [[GLOBAL_PTR]],
  // CHECK: store i32 %{{.+}}, i32* [[IDX_PTR]],
  // CHECK: store i8* %{{.+}}, i8** [[RL_PTR]],
  // CHECK: [[GLOBAL_BC:%.+]] = load i8*, i8** [[GLOBAL_PTR]],
  // CHECK: [[GLOBAL:%.+]] = bitcast i8* [[GLOBAL_BC]] to [[TEAM1_REDUCE_TY]]*
  // CHECK: [[IDX:%.+]] = load i32, i32* [[IDX_PTR]],
  // CHECK: [[LOCAL_RL_RED1_PTR:%.+]] = getelementptr inbounds [1 x i8*], [1 x i8*]* [[LOCAL_RL]], i{{[0-9]+}} 0, i{{[0-9]+}} 0
  // CHECK: [[GLOBAL_RED1_PTR:%.+]] = getelementptr inbounds [[TEAM1_REDUCE_TY]], [[TEAM1_REDUCE_TY]]* [[GLOBAL]], i{{[0-9]+}} 0, i{{[0-9]+}} 0
  // CHECK: [[GLOBAL_RED1_IDX_PTR:%.+]] = getelementptr inbounds [{{1024|2048}} x double], [{{1024|2048}} x double]* [[GLOBAL_RED1_PTR]], i{{[0-9]+}} 0, i32 [[IDX]]
  // CHECK: [[GLOBAL_RED1_IDX_PTR_BC:%.+]] = bitcast double* [[GLOBAL_RED1_IDX_PTR]] to i8*
  // CHECK: store i8* [[GLOBAL_RED1_IDX_PTR_BC]], i8** [[LOCAL_RL_RED1_PTR]]
  // CHECK: [[LOCAL_RL_BC:%.+]] = bitcast [1 x i8*]* [[LOCAL_RL]] to i8*
  // CHECK: [[RL_BC:%.+]] = load i8*, i8** [[RL_PTR]],
  // CHECK: call void [[REDUCTION_FUNC]](i8* [[RL_BC]], i8* [[LOCAL_RL_BC]])
  // CHECK: ret void

  // CHECK-LABEL: define {{.*}}void {{@__omp_offloading_.+template.+l47}}_worker()

  // CHECK: define {{.*}}void [[T2:@__omp_offloading_.+template.+l47]](
  //
  // CHECK: {{call|invoke}} void [[T2]]_worker()

  //
  // CHECK: call void @__kmpc_kernel_init(
  //
  // CHECK: store float {{1\.[0e\+]+}}, float* [[D:%.+]], align
  // CHECK: [[C_VAL:%.+]] = load i8, i8* [[C:%.+]], align
  // CHECK: [[CONV:%.+]] = sext i8 [[C_VAL]] to i32
  // CHECK: [[XOR:%.+]] = xor i32 [[CONV]], 2
  // CHECK: [[TRUNC:%.+]] = trunc i32 [[XOR]] to i8
  // CHECK: store i8 [[TRUNC]], i8* [[C]], align
  // CHECK: [[DV:%.+]] = load float, float* [[D]], align
  // CHECK: [[MUL:%.+]] = fmul float [[DV]], {{[0-9e\.\+]+}}
  // CHECK: store float [[MUL]], float* [[D]], align
  // CHECK: [[GEP1:%.+]] = getelementptr inbounds [2 x i8*], [2 x i8*]* [[RED_LIST:%.+]], i{{[0-9]+}} 0, i{{[0-9]+}} 0
  // CHECK: store i8* [[C]], i8** [[GEP1]],
  // CHECK: [[GEP2:%.+]] = getelementptr inbounds [2 x i8*], [2 x i8*]* [[RED_LIST:%.+]], i{{[0-9]+}} 0, i{{[0-9]+}} 1
  // CHECK: [[BC:%.+]] = bitcast float* [[D]] to i8*
  // CHECK: store i8* [[BC]], i8** [[GEP2]],
  // CHECK: [[BC_RED_LIST:%.+]] = bitcast [2 x i8*]* [[RED_LIST]] to i8*
  // CHECK: [[RET:%.+]] = call i32 @__kmpc_nvptx_teams_reduce_nowait_v2(%struct.ident_t* [[LOC:@.+]], i32 [[GTID:%.+]], i8* bitcast ([[TEAMS_REDUCE_UNION_TY]]* [[TEAMS_RED_BUFFER]] to i8*), i32 {{1024|2048}}, i8* [[BC_RED_LIST]], void (i8*, i16, i16, i16)* [[SHUFFLE_AND_REDUCE:@.+]], void (i8*, i32)* [[INTER_WARP_COPY:@.+]], void (i8*, i32, i8*)* [[RED_LIST_TO_GLOBAL_COPY:@.+]], void (i8*, i32, i8*)* [[RED_LIST_TO_GLOBAL_RED:@.+]], void (i8*, i32, i8*)* [[GLOBAL_TO_RED_LIST_COPY:@.+]], void (i8*, i32, i8*)* [[GLOBAL_TO_RED_LIST_RED:@.+]])
  // CHECK: [[COND:%.+]] = icmp eq i32 [[RET]], 1
  // CHECK: br i1 [[COND]], label {{%?}}[[IFLABEL:.+]], label {{%?}}[[EXIT:.+]]
  //
  // CHECK: [[IFLABEL]]
  // CHECK: [[C_INV8:%.+]] = load i8, i8* [[C_IN:%.+]], align
  // CHECK: [[C_INV:%.+]] = sext i8 [[C_INV8]] to i32
  // CHECK: [[CV8:%.+]] = load i8, i8* [[C]], align
  // CHECK: [[CV:%.+]] = sext i8 [[CV8]] to i32
  // CHECK: [[XOR:%.+]] = xor i32 [[C_INV]], [[CV]]
  // CHECK: [[TRUNC:%.+]] = trunc i32 [[XOR]] to i8
  // CHECK: store i8 [[TRUNC]], i8* [[C_IN]], align
  // CHECK: [[D_INV:%.+]] = load float, float* [[D_IN:%.+]], align
  // CHECK: [[DV:%.+]] = load float, float* [[D]], align
  // CHECK: [[MUL:%.+]] = fmul float [[D_INV]], [[DV]]
  // CHECK: store float [[MUL]], float* [[D_IN]], align
  // CHECK: call void @__kmpc_nvptx_end_reduce_nowait(i32 [[GTID]])
  // CHECK: br label %[[EXIT]]
  //
  // CHECK: [[EXIT]]
  // CHECK: call void @__kmpc_kernel_deinit(

  //
  // Reduction function
  // CHECK: define internal void [[REDUCTION_FUNC:@.+]](i8*, i8*)
  // CHECK: [[VAR1_RHS_REF:%.+]] = getelementptr inbounds [2 x i8*], [2 x i8*]* [[RED_LIST_RHS:%.+]], i{{32|64}} 0, i{{32|64}} 0
  // CHECK: [[VAR1_RHS:%.+]] = load i8*, i8** [[VAR1_RHS_REF]],
  //
  // CHECK: [[VAR1_LHS_REF:%.+]] = getelementptr inbounds [2 x i8*], [2 x i8*]* [[RED_LIST_LHS:%.+]], i{{32|64}} 0, i{{32|64}} 0
  // CHECK: [[VAR1_LHS:%.+]] = load i8*, i8** [[VAR1_LHS_REF]],
  //
  // CHECK: [[VAR2_RHS_REF:%.+]] = getelementptr inbounds [2 x i8*], [2 x i8*]* [[RED_LIST_RHS]], i{{32|64}} 0, i{{32|64}} 1
  // CHECK: [[VAR2_RHS_VOID:%.+]] = load i8*, i8** [[VAR2_RHS_REF]],
  // CHECK: [[VAR2_RHS:%.+]] = bitcast i8* [[VAR2_RHS_VOID]] to float*
  //
  // CHECK: [[VAR2_LHS_REF:%.+]] = getelementptr inbounds [2 x i8*], [2 x i8*]* [[RED_LIST_LHS]], i{{32|64}} 0, i{{32|64}} 1
  // CHECK: [[VAR2_LHS_VOID:%.+]] = load i8*, i8** [[VAR2_LHS_REF]],
  // CHECK: [[VAR2_LHS:%.+]] = bitcast i8* [[VAR2_LHS_VOID]] to float*
  //
  // CHECK: [[VAR1_LHS_VAL8:%.+]] = load i8, i8* [[VAR1_LHS]],
  // CHECK: [[VAR1_LHS_VAL:%.+]] = sext i8 [[VAR1_LHS_VAL8]] to i32
  // CHECK: [[VAR1_RHS_VAL8:%.+]] = load i8, i8* [[VAR1_RHS]],
  // CHECK: [[VAR1_RHS_VAL:%.+]] = sext i8 [[VAR1_RHS_VAL8]] to i32
  // CHECK: [[XOR:%.+]] = xor i32 [[VAR1_LHS_VAL]], [[VAR1_RHS_VAL]]
  // CHECK: [[RES:%.+]] = trunc i32 [[XOR]] to i8
  // CHECK: store i8 [[RES]], i8* [[VAR1_LHS]],
  //
  // CHECK: [[VAR2_LHS_VAL:%.+]] = load float, float* [[VAR2_LHS]],
  // CHECK: [[VAR2_RHS_VAL:%.+]] = load float, float* [[VAR2_RHS]],
  // CHECK: [[RES:%.+]] = fmul float [[VAR2_LHS_VAL]], [[VAR2_RHS_VAL]]
  // CHECK: store float [[RES]], float* [[VAR2_LHS]],
  // CHECK: ret void

  //
  // Shuffle and reduce function
  // CHECK: define internal void [[SHUFFLE_AND_REDUCE]](i8*, i16 {{.*}}, i16 {{.*}}, i16 {{.*}})
  // CHECK: [[REMOTE_RED_LIST:%.+]] = alloca [2 x i8*], align
  // CHECK: [[REMOTE_ELT1:%.+]] = alloca i8
  // CHECK: [[REMOTE_ELT2:%.+]] = alloca float
  //
  // CHECK: [[LANEID:%.+]] = load i16, i16* {{.+}}, align
  // CHECK: [[LANEOFFSET:%.+]] = load i16, i16* {{.+}}, align
  // CHECK: [[ALGVER:%.+]] = load i16, i16* {{.+}}, align
  //
  // CHECK: [[ELT_REF:%.+]] = getelementptr inbounds [2 x i8*], [2 x i8*]* [[RED_LIST:%.+]], i{{32|64}} 0, i{{32|64}} 0
  // CHECK: [[ELT_VOID:%.+]] = load i8*, i8** [[ELT_REF]],
  // CHECK: [[REMOTE_ELT_REF:%.+]] = getelementptr inbounds [2 x i8*], [2 x i8*]* [[REMOTE_RED_LIST:%.+]], i{{32|64}} 0, i{{32|64}} 0
  // CHECK: [[ELT_VAL:%.+]] = load i8, i8* [[ELT_VOID]], align
  //
  // CHECK: [[ELT_CAST:%.+]] = sext i8 [[ELT_VAL]] to i32
  // CHECK: [[WS32:%.+]] = call i32 @llvm.nvvm.read.ptx.sreg.warpsize()
  // CHECK: [[WS:%.+]] = trunc i32 [[WS32]] to i16
  // CHECK: [[REMOTE_ELT1_VAL32:%.+]] = call i32 @__kmpc_shuffle_int32(i32 [[ELT_CAST]], i16 [[LANEOFFSET]], i16 [[WS]])
  // CHECK: [[REMOTE_ELT1_VAL:%.+]] = trunc i32 [[REMOTE_ELT1_VAL32]] to i8
  //
  // CHECK: store i8 [[REMOTE_ELT1_VAL]], i8* [[REMOTE_ELT1]], align
  // CHECK: store i8* [[REMOTE_ELT1]], i8** [[REMOTE_ELT_REF]], align
  //
  // CHECK: [[ELT_REF:%.+]] = getelementptr inbounds [2 x i8*], [2 x i8*]* [[RED_LIST]], i{{32|64}} 0, i{{32|64}} 1
  // CHECK: [[ELT_VOID:%.+]] = load i8*, i8** [[ELT_REF]],
  // CHECK: [[REMOTE_ELT_REF:%.+]] = getelementptr inbounds [2 x i8*], [2 x i8*]* [[REMOTE_RED_LIST]], i{{32|64}} 0, i{{32|64}} 1
  // CHECK: [[ELT:%.+]] = bitcast i8* [[ELT_VOID]] to float*
  //
  // CHECK: [[ELT_CAST:%.+]] = bitcast float* [[ELT]] to i32*
  // CHECK: [[REMOTE_ELT2_CAST:%.+]] = bitcast float* [[REMOTE_ELT2]] to i32*
  // CHECK: [[ELT_VAL:%.+]] = load i32, i32* [[ELT_CAST]], align
  // CHECK: [[WS32:%.+]] = call i32 @llvm.nvvm.read.ptx.sreg.warpsize()
  // CHECK: [[WS:%.+]] = trunc i32 [[WS32]] to i16
  // CHECK: [[REMOTE_ELT2_VAL32:%.+]] = call i32 @__kmpc_shuffle_int32(i32 [[ELT_VAL]], i16 [[LANEOFFSET]], i16 [[WS]])
  //
  // CHECK: store i32 [[REMOTE_ELT2_VAL32]], i32* [[REMOTE_ELT2_CAST]], align
  // CHECK: [[REMOTE_ELT2C:%.+]] = bitcast float* [[REMOTE_ELT2]] to i8*
  // CHECK: store i8* [[REMOTE_ELT2C]], i8** [[REMOTE_ELT_REF]], align
  //
  // Condition to reduce
  // CHECK: [[CONDALG0:%.+]] = icmp eq i16 [[ALGVER]], 0
  //
  // CHECK: [[COND1:%.+]] = icmp eq i16 [[ALGVER]], 1
  // CHECK: [[COND2:%.+]] = icmp ult i16 [[LANEID]], [[LANEOFFSET]]
  // CHECK: [[CONDALG1:%.+]] = and i1 [[COND1]], [[COND2]]
  //
  // CHECK: [[COND3:%.+]] = icmp eq i16 [[ALGVER]], 2
  // CHECK: [[COND4:%.+]] = and i16 [[LANEID]], 1
  // CHECK: [[COND5:%.+]] = icmp eq i16 [[COND4]], 0
  // CHECK: [[COND6:%.+]] = and i1 [[COND3]], [[COND5]]
  // CHECK: [[COND7:%.+]] = icmp sgt i16 [[LANEOFFSET]], 0
  // CHECK: [[CONDALG2:%.+]] = and i1 [[COND6]], [[COND7]]
  //
  // CHECK: [[COND8:%.+]] = or i1 [[CONDALG0]], [[CONDALG1]]
  // CHECK: [[SHOULD_REDUCE:%.+]] = or i1 [[COND8]], [[CONDALG2]]
  // CHECK: br i1 [[SHOULD_REDUCE]], label {{%?}}[[DO_REDUCE:.+]], label {{%?}}[[REDUCE_ELSE:.+]]
  //
  // CHECK: [[DO_REDUCE]]
  // CHECK: [[RED_LIST1_VOID:%.+]] = bitcast [2 x i8*]* [[RED_LIST]] to i8*
  // CHECK: [[RED_LIST2_VOID:%.+]] = bitcast [2 x i8*]* [[REMOTE_RED_LIST]] to i8*
  // CHECK: call void [[REDUCTION_FUNC]](i8* [[RED_LIST1_VOID]], i8* [[RED_LIST2_VOID]])
  // CHECK: br label {{%?}}[[REDUCE_CONT:.+]]
  //
  // CHECK: [[REDUCE_ELSE]]
  // CHECK: br label {{%?}}[[REDUCE_CONT]]
  //
  // CHECK: [[REDUCE_CONT]]
  // Now check if we should just copy over the remote reduction list
  // CHECK: [[COND1:%.+]] = icmp eq i16 [[ALGVER]], 1
  // CHECK: [[COND2:%.+]] = icmp uge i16 [[LANEID]], [[LANEOFFSET]]
  // CHECK: [[SHOULD_COPY:%.+]] = and i1 [[COND1]], [[COND2]]
  // CHECK: br i1 [[SHOULD_COPY]], label {{%?}}[[DO_COPY:.+]], label {{%?}}[[COPY_ELSE:.+]]
  //
  // CHECK: [[DO_COPY]]
  // CHECK: [[REMOTE_ELT_REF:%.+]] = getelementptr inbounds [2 x i8*], [2 x i8*]* [[REMOTE_RED_LIST]], i{{32|64}} 0, i{{32|64}} 0
  // CHECK: [[REMOTE_ELT_VOID:%.+]] = load i8*, i8** [[REMOTE_ELT_REF]],
  // CHECK: [[ELT_REF:%.+]] = getelementptr inbounds [2 x i8*], [2 x i8*]* [[RED_LIST]], i{{32|64}} 0, i{{32|64}} 0
  // CHECK: [[ELT_VOID:%.+]] = load i8*, i8** [[ELT_REF]],
  // CHECK: [[REMOTE_ELT_VAL:%.+]] = load i8, i8* [[REMOTE_ELT_VOID]], align
  // CHECK: store i8 [[REMOTE_ELT_VAL]], i8* [[ELT_VOID]], align
  //
  // CHECK: [[REMOTE_ELT_REF:%.+]] = getelementptr inbounds [2 x i8*], [2 x i8*]* [[REMOTE_RED_LIST]], i{{32|64}} 0, i{{32|64}} 1
  // CHECK: [[REMOTE_ELT_VOID:%.+]] = load i8*, i8** [[REMOTE_ELT_REF]],
  // CHECK: [[ELT_REF:%.+]] = getelementptr inbounds [2 x i8*], [2 x i8*]* [[RED_LIST]], i{{32|64}} 0, i{{32|64}} 1
  // CHECK: [[ELT_VOID:%.+]] = load i8*, i8** [[ELT_REF]],
  // CHECK: [[REMOTE_ELT:%.+]] = bitcast i8* [[REMOTE_ELT_VOID]] to float*
  // CHECK: [[ELT:%.+]] = bitcast i8* [[ELT_VOID]] to float*
  // CHECK: [[REMOTE_ELT_VAL:%.+]] = load float, float* [[REMOTE_ELT]], align
  // CHECK: store float [[REMOTE_ELT_VAL]], float* [[ELT]], align
  // CHECK: br label {{%?}}[[COPY_CONT:.+]]
  //
  // CHECK: [[COPY_ELSE]]
  // CHECK: br label {{%?}}[[COPY_CONT]]
  //
  // CHECK: [[COPY_CONT]]
  // CHECK: void

  //
  // Inter warp copy function
  // CHECK: define internal void [[INTER_WARP_COPY]](i8*, i32)
  // CHECK-DAG: [[LANEID:%.+]] = and i32 {{.+}}, 31
  // CHECK-DAG: [[WARPID:%.+]] = ashr i32 {{.+}}, 5
  // CHECK-DAG: [[RED_LIST:%.+]] = bitcast i8* {{.+}} to [2 x i8*]*
  // CHECK: call void @__kmpc_barrier(%struct.ident_t* @
  // CHECK: [[IS_WARP_MASTER:%.+]] = icmp eq i32 [[LANEID]], 0
  // CHECK: br i1 [[IS_WARP_MASTER]], label {{%?}}[[DO_COPY:.+]], label {{%?}}[[COPY_ELSE:.+]]
  //
  // [[DO_COPY]]
  // CHECK: [[ELT_REF:%.+]] = getelementptr inbounds [2 x i8*], [2 x i8*]* [[RED_LIST]], i{{32|64}} 0, i{{32|64}} 0
  // CHECK: [[ELT_VOID:%.+]] = load i8*, i8** [[ELT_REF]],
  //
  // CHECK: [[MEDIUM_ELT64:%.+]] = getelementptr inbounds [32 x i32], [32 x i32] addrspace([[SHARED_ADDRSPACE]])* [[TRANSFER_STORAGE]], i64 0, i32 [[WARPID]]
  // CHECK: [[MEDIUM_ELT:%.+]] = bitcast i32 addrspace([[SHARED_ADDRSPACE]])* [[MEDIUM_ELT64]] to i8 addrspace([[SHARED_ADDRSPACE]])*
  // CHECK: [[ELT_VAL:%.+]] = load i8, i8* [[ELT_VOID]], align
  // CHECK: store volatile i8 [[ELT_VAL]], i8 addrspace([[SHARED_ADDRSPACE]])* [[MEDIUM_ELT]], align
  // CHECK: br label {{%?}}[[COPY_CONT:.+]]
  //
  // CHECK: [[COPY_ELSE]]
  // CHECK: br label {{%?}}[[COPY_CONT]]
  //
  // Barrier after copy to shared memory storage medium.
  // CHECK: [[COPY_CONT]]
  // CHECK: call void @__kmpc_barrier(%struct.ident_t* @
  // CHECK: [[ACTIVE_WARPS:%.+]] = load i32, i32*
  //
  // Read into warp 0.
  // CHECK: [[IS_W0_ACTIVE_THREAD:%.+]] = icmp ult i32 [[TID:%.+]], [[ACTIVE_WARPS]]
  // CHECK: br i1 [[IS_W0_ACTIVE_THREAD]], label {{%?}}[[DO_READ:.+]], label {{%?}}[[READ_ELSE:.+]]
  //
  // CHECK: [[DO_READ]]
  // CHECK: [[MEDIUM_ELT32:%.+]] = getelementptr inbounds [32 x i32], [32 x i32] addrspace([[SHARED_ADDRSPACE]])* [[TRANSFER_STORAGE]], i64 0, i32 [[TID]]
  // CHECK: [[MEDIUM_ELT:%.+]] = bitcast i32 addrspace([[SHARED_ADDRSPACE]])* [[MEDIUM_ELT32]] to i8 addrspace([[SHARED_ADDRSPACE]])*
  // CHECK: [[ELT_REF:%.+]] = getelementptr inbounds [2 x i8*], [2 x i8*]* [[RED_LIST:%.+]], i{{32|64}} 0, i{{32|64}} 0
  // CHECK: [[ELT_VOID:%.+]] = load i8*, i8** [[ELT_REF]],
  // CHECK: [[MEDIUM_ELT_VAL:%.+]] = load volatile i8, i8 addrspace([[SHARED_ADDRSPACE]])* [[MEDIUM_ELT]], align
  // CHECK: store i8 [[MEDIUM_ELT_VAL]], i8* [[ELT_VOID]], align
  // CHECK: br label {{%?}}[[READ_CONT:.+]]
  //
  // CHECK: [[READ_ELSE]]
  // CHECK: br label {{%?}}[[READ_CONT]]
  //
  // CHECK: [[READ_CONT]]
  // CHECK: call void @__kmpc_barrier(%struct.ident_t* @
  // CHECK: [[IS_WARP_MASTER:%.+]] = icmp eq i32 [[LANEID]], 0
  // CHECK: br i1 [[IS_WARP_MASTER]], label {{%?}}[[DO_COPY:.+]], label {{%?}}[[COPY_ELSE:.+]]
  //
  // [[DO_COPY]]
  // CHECK: [[ELT_REF:%.+]] = getelementptr inbounds [2 x i8*], [2 x i8*]* [[RED_LIST]], i{{32|64}} 0, i{{32|64}} 1
  // CHECK: [[ELT_VOID:%.+]] = load i8*, i8** [[ELT_REF]],
  // CHECK: [[ELT:%.+]] = bitcast i8* [[ELT_VOID]] to i32*
  //
  // CHECK: [[MEDIUM_ELT:%.+]] = getelementptr inbounds [32 x i32], [32 x i32] addrspace([[SHARED_ADDRSPACE]])* [[TRANSFER_STORAGE]], i64 0, i32 [[WARPID]]
  // CHECK: [[ELT_VAL:%.+]] = load i32, i32* [[ELT]], align
  // CHECK: store volatile i32 [[ELT_VAL]], i32 addrspace([[SHARED_ADDRSPACE]])* [[MEDIUM_ELT]], align
  // CHECK: br label {{%?}}[[COPY_CONT:.+]]
  //
  // CHECK: [[COPY_ELSE]]
  // CHECK: br label {{%?}}[[COPY_CONT]]
  //
  // Barrier after copy to shared memory storage medium.
  // CHECK: [[COPY_CONT]]
  // CHECK: call void @__kmpc_barrier(%struct.ident_t* @
  // CHECK: [[ACTIVE_WARPS:%.+]] = load i32, i32*
  //
  // Read into warp 0.
  // CHECK: [[IS_W0_ACTIVE_THREAD:%.+]] = icmp ult i32 [[TID:%.+]], [[ACTIVE_WARPS]]
  // CHECK: br i1 [[IS_W0_ACTIVE_THREAD]], label {{%?}}[[DO_READ:.+]], label {{%?}}[[READ_ELSE:.+]]
  //
  // CHECK: [[DO_READ]]
  // CHECK: [[MEDIUM_ELT:%.+]] = getelementptr inbounds [32 x i32], [32 x i32] addrspace([[SHARED_ADDRSPACE]])* [[TRANSFER_STORAGE]], i64 0, i32 [[TID]]
  // CHECK: [[ELT_REF:%.+]] = getelementptr inbounds [2 x i8*], [2 x i8*]* [[RED_LIST:%.+]], i{{32|64}} 0, i{{32|64}} 1
  // CHECK: [[ELT_VOID:%.+]] = load i8*, i8** [[ELT_REF]],
  // CHECK: [[ELT:%.+]] = bitcast i8* [[ELT_VOID]] to i32*
  // CHECK: [[MEDIUM_ELT_VAL:%.+]] = load volatile i32, i32 addrspace([[SHARED_ADDRSPACE]])* [[MEDIUM_ELT]], align
  // CHECK: store i32 [[MEDIUM_ELT_VAL]], i32* [[ELT]], align
  // CHECK: br label {{%?}}[[READ_CONT:.+]]
  //
  // CHECK: [[READ_ELSE]]
  // CHECK: br label {{%?}}[[READ_CONT]]
  //
  // CHECK: [[READ_CONT]]
  // CHECK: ret

  // CHECK: define internal void [[RED_LIST_TO_GLOBAL_COPY]](i8*, i32, i8*)
  // CHECK: [[GLOBAL_PTR:%.+]] = alloca i8*,
  // CHECK: [[IDX_PTR:%.+]] = alloca i32,
  // CHECK: [[RL_PTR:%.+]] = alloca i8*,
  // CHECK: store i8* %{{.+}}, i8** [[GLOBAL_PTR]],
  // CHECK: store i32 %{{.+}}, i32* [[IDX_PTR]],
  // CHECK: store i8* %{{.+}}, i8** [[RL_PTR]],
  // CHECK: [[RL_BC:%.+]] = load i8*, i8** [[RL_PTR]],
  // CHECK: [[RL:%.+]] = bitcast i8* [[RL_BC]] to [2 x i8*]*
  // CHECK: [[GLOBAL_BC:%.+]] = load i8*, i8** [[GLOBAL_PTR]],
  // CHECK: [[GLOBAL:%.+]] = bitcast i8* [[GLOBAL_BC]] to [[TEAM2_REDUCE_TY]]*
  // CHECK: [[IDX:%.+]] = load i32, i32* [[IDX_PTR]],
  // CHECK: [[RL_RED1_PTR:%.+]] = getelementptr inbounds [2 x i8*], [2 x i8*]* [[RL]], i{{[0-9]+}} 0, i{{[0-9]+}} 0
  // CHECK: [[RL_RED1:%.+]] = load i8*, i8** [[RL_RED1_PTR]],
  // CHECK: [[GLOBAL_RED1_PTR:%.+]] = getelementptr inbounds [[TEAM2_REDUCE_TY]], [[TEAM2_REDUCE_TY]]* [[GLOBAL]], i{{[0-9]+}} 0, i{{[0-9]+}} 0
  // CHECK: [[GLOBAL_RED1_IDX_PTR:%.+]] = getelementptr inbounds [{{1024|2048}} x i8], [{{1024|2048}} x i8]* [[GLOBAL_RED1_PTR]], i{{[0-9]+}} 0, i32 [[IDX]]
  // CHECK: [[LOC_RED1:%.+]] = load i8, i8* [[RL_RED1]],
  // CHECK: store i8 [[LOC_RED1]], i8* [[GLOBAL_RED1_IDX_PTR]],
  // CHECK: [[RL_RED1_PTR:%.+]] = getelementptr inbounds [2 x i8*], [2 x i8*]* [[RL]], i{{[0-9]+}} 0, i{{[0-9]+}} 1
  // CHECK: [[RL_RED1_BC:%.+]] = load i8*, i8** [[RL_RED1_PTR]],
  // CHECK: [[RL_RED1:%.+]] = bitcast i8* [[RL_RED1_BC]] to float*
  // CHECK: [[GLOBAL_RED1_PTR:%.+]] = getelementptr inbounds [[TEAM2_REDUCE_TY]], [[TEAM2_REDUCE_TY]]* [[GLOBAL]], i{{[0-9]+}} 0, i{{[0-9]+}} 1
  // CHECK: [[GLOBAL_RED1_IDX_PTR:%.+]] = getelementptr inbounds [{{1024|2048}} x float], [{{1024|2048}} x float]* [[GLOBAL_RED1_PTR]], i{{[0-9]+}} 0, i32 [[IDX]]
  // CHECK: [[LOC_RED1:%.+]] = load float, float* [[RL_RED1]],
  // CHECK: store float [[LOC_RED1]], float* [[GLOBAL_RED1_IDX_PTR]],
  // CHECK: ret void

  // CHECK: define internal void [[RED_LIST_TO_GLOBAL_RED]](i8*, i32, i8*)
  // CHECK: [[GLOBAL_PTR:%.+]] = alloca i8*,
  // CHECK: [[IDX_PTR:%.+]] = alloca i32,
  // CHECK: [[RL_PTR:%.+]] = alloca i8*,
  // CHECK: [[LOCAL_RL:%.+]] = alloca [2 x i8*],
  // CHECK: store i8* %{{.+}}, i8** [[GLOBAL_PTR]],
  // CHECK: store i32 %{{.+}}, i32* [[IDX_PTR]],
  // CHECK: store i8* %{{.+}}, i8** [[RL_PTR]],
  // CHECK: [[GLOBAL_BC:%.+]] = load i8*, i8** [[GLOBAL_PTR]],
  // CHECK: [[GLOBAL:%.+]] = bitcast i8* [[GLOBAL_BC]] to [[TEAM2_REDUCE_TY]]*
  // CHECK: [[IDX:%.+]] = load i32, i32* [[IDX_PTR]],
  // CHECK: [[LOCAL_RL_RED1_PTR:%.+]] = getelementptr inbounds [2 x i8*], [2 x i8*]* [[LOCAL_RL]], i{{[0-9]+}} 0, i{{[0-9]+}} 0
  // CHECK: [[GLOBAL_RED1_PTR:%.+]] = getelementptr inbounds [[TEAM2_REDUCE_TY]], [[TEAM2_REDUCE_TY]]* [[GLOBAL]], i{{[0-9]+}} 0, i{{[0-9]+}} 0
  // CHECK: [[GLOBAL_RED1_IDX_PTR:%.+]] = getelementptr inbounds [{{1024|2048}} x i8], [{{1024|2048}} x i8]* [[GLOBAL_RED1_PTR]], i{{[0-9]+}} 0, i32 [[IDX]]
  // CHECK: store i8* [[GLOBAL_RED1_IDX_PTR]], i8** [[LOCAL_RL_RED1_PTR]]
  // CHECK: [[LOCAL_RL_RED1_PTR:%.+]] = getelementptr inbounds [2 x i8*], [2 x i8*]* [[LOCAL_RL]], i{{[0-9]+}} 0, i{{[0-9]+}} 1
  // CHECK: [[GLOBAL_RED1_PTR:%.+]] = getelementptr inbounds [[TEAM2_REDUCE_TY]], [[TEAM2_REDUCE_TY]]* [[GLOBAL]], i{{[0-9]+}} 0, i{{[0-9]+}} 1
  // CHECK: [[GLOBAL_RED1_IDX_PTR:%.+]] = getelementptr inbounds [{{1024|2048}} x float], [{{1024|2048}} x float]* [[GLOBAL_RED1_PTR]], i{{[0-9]+}} 0, i32 [[IDX]]
  // CHECK: [[GLOBAL_RED1_IDX_PTR_BC:%.+]] = bitcast float* [[GLOBAL_RED1_IDX_PTR]] to i8*
  // CHECK: store i8* [[GLOBAL_RED1_IDX_PTR_BC]], i8** [[LOCAL_RL_RED1_PTR]]
  // CHECK: [[LOCAL_RL_BC:%.+]] = bitcast [2 x i8*]* [[LOCAL_RL]] to i8*
  // CHECK: [[RL_BC:%.+]] = load i8*, i8** [[RL_PTR]],
  // CHECK: call void [[REDUCTION_FUNC]](i8* [[LOCAL_RL_BC]], i8* [[RL_BC]])
  // CHECK: ret void

  // CHECK: define internal void [[GLOBAL_TO_RED_LIST_COPY]](i8*, i32, i8*)
  // CHECK: [[GLOBAL_PTR:%.+]] = alloca i8*,
  // CHECK: [[IDX_PTR:%.+]] = alloca i32,
  // CHECK: [[RL_PTR:%.+]] = alloca i8*,
  // CHECK: store i8* %{{.+}}, i8** [[GLOBAL_PTR]],
  // CHECK: store i32 %{{.+}}, i32* [[IDX_PTR]],
  // CHECK: store i8* %{{.+}}, i8** [[RL_PTR]],
  // CHECK: [[RL_BC:%.+]] = load i8*, i8** [[RL_PTR]],
  // CHECK: [[RL:%.+]] = bitcast i8* [[RL_BC]] to [2 x i8*]*
  // CHECK: [[GLOBAL_BC:%.+]] = load i8*, i8** [[GLOBAL_PTR]],
  // CHECK: [[GLOBAL:%.+]] = bitcast i8* [[GLOBAL_BC]] to [[TEAM2_REDUCE_TY]]*
  // CHECK: [[IDX:%.+]] = load i32, i32* [[IDX_PTR]],
  // CHECK: [[RL_RED1_PTR:%.+]] = getelementptr inbounds [2 x i8*], [2 x i8*]* [[RL]], i{{[0-9]+}} 0, i{{[0-9]+}} 0
  // CHECK: [[RL_RED1:%.+]] = load i8*, i8** [[RL_RED1_PTR]],
  // CHECK: [[GLOBAL_RED1_PTR:%.+]] = getelementptr inbounds [[TEAM2_REDUCE_TY]], [[TEAM2_REDUCE_TY]]* [[GLOBAL]], i{{[0-9]+}} 0, i{{[0-9]+}} 0
  // CHECK: [[GLOBAL_RED1_IDX_PTR:%.+]] = getelementptr inbounds [{{1024|2048}} x i8], [{{1024|2048}} x i8]* [[GLOBAL_RED1_PTR]], i{{[0-9]+}} 0, i32 [[IDX]]
  // CHECK: [[GLOBAL_RED1:%.+]] = load i8, i8* [[GLOBAL_RED1_IDX_PTR]],
  // CHECK: store i8 [[GLOBAL_RED1]], i8* [[RL_RED1]],
  // CHECK: [[RL_RED1_PTR:%.+]] = getelementptr inbounds [2 x i8*], [2 x i8*]* [[RL]], i{{[0-9]+}} 0, i{{[0-9]+}} 1
  // CHECK: [[RL_RED1_BC:%.+]] = load i8*, i8** [[RL_RED1_PTR]],
  // CHECK: [[RL_RED1:%.+]] = bitcast i8* [[RL_RED1_BC]] to float*
  // CHECK: [[GLOBAL_RED1_PTR:%.+]] = getelementptr inbounds [[TEAM2_REDUCE_TY]], [[TEAM2_REDUCE_TY]]* [[GLOBAL]], i{{[0-9]+}} 0, i{{[0-9]+}} 1
  // CHECK: [[GLOBAL_RED1_IDX_PTR:%.+]] = getelementptr inbounds [{{1024|2048}} x float], [{{1024|2048}} x float]* [[GLOBAL_RED1_PTR]], i{{[0-9]+}} 0, i32 [[IDX]]
  // CHECK: [[GLOBAL_RED1:%.+]] = load float, float* [[GLOBAL_RED1_IDX_PTR]],
  // CHECK: store float [[GLOBAL_RED1]], float* [[RL_RED1]],
  // CHECK: ret void

  // CHECK: define internal void [[GLOBAL_TO_RED_LIST_RED]](i8*, i32, i8*)
  // CHECK: [[GLOBAL_PTR:%.+]] = alloca i8*,
  // CHECK: [[IDX_PTR:%.+]] = alloca i32,
  // CHECK: [[RL_PTR:%.+]] = alloca i8*,
  // CHECK: [[LOCAL_RL:%.+]] = alloca [2 x i8*],
  // CHECK: store i8* %{{.+}}, i8** [[GLOBAL_PTR]],
  // CHECK: store i32 %{{.+}}, i32* [[IDX_PTR]],
  // CHECK: store i8* %{{.+}}, i8** [[RL_PTR]],
  // CHECK: [[GLOBAL_BC:%.+]] = load i8*, i8** [[GLOBAL_PTR]],
  // CHECK: [[GLOBAL:%.+]] = bitcast i8* [[GLOBAL_BC]] to [[TEAM2_REDUCE_TY]]*
  // CHECK: [[IDX:%.+]] = load i32, i32* [[IDX_PTR]],
  // CHECK: [[LOCAL_RL_RED1_PTR:%.+]] = getelementptr inbounds [2 x i8*], [2 x i8*]* [[LOCAL_RL]], i{{[0-9]+}} 0, i{{[0-9]+}} 0
  // CHECK: [[GLOBAL_RED1_PTR:%.+]] = getelementptr inbounds [[TEAM2_REDUCE_TY]], [[TEAM2_REDUCE_TY]]* [[GLOBAL]], i{{[0-9]+}} 0, i{{[0-9]+}} 0
  // CHECK: [[GLOBAL_RED1_IDX_PTR:%.+]] = getelementptr inbounds [{{1024|2048}} x i8], [{{1024|2048}} x i8]* [[GLOBAL_RED1_PTR]], i{{[0-9]+}} 0, i32 [[IDX]]
  // CHECK: store i8* [[GLOBAL_RED1_IDX_PTR]], i8** [[LOCAL_RL_RED1_PTR]]
  // CHECK: [[LOCAL_RL_RED1_PTR:%.+]] = getelementptr inbounds [2 x i8*], [2 x i8*]* [[LOCAL_RL]], i{{[0-9]+}} 0, i{{[0-9]+}} 1
  // CHECK: [[GLOBAL_RED1_PTR:%.+]] = getelementptr inbounds [[TEAM2_REDUCE_TY]], [[TEAM2_REDUCE_TY]]* [[GLOBAL]], i{{[0-9]+}} 0, i{{[0-9]+}} 1
  // CHECK: [[GLOBAL_RED1_IDX_PTR:%.+]] = getelementptr inbounds [{{1024|2048}} x float], [{{1024|2048}} x float]* [[GLOBAL_RED1_PTR]], i{{[0-9]+}} 0, i32 [[IDX]]
  // CHECK: [[GLOBAL_RED1_IDX_PTR_BC:%.+]] = bitcast float* [[GLOBAL_RED1_IDX_PTR]] to i8*
  // CHECK: store i8* [[GLOBAL_RED1_IDX_PTR_BC]], i8** [[LOCAL_RL_RED1_PTR]]
  // CHECK: [[LOCAL_RL_BC:%.+]] = bitcast [2 x i8*]* [[LOCAL_RL]] to i8*
  // CHECK: [[RL_BC:%.+]] = load i8*, i8** [[RL_PTR]],
  // CHECK: call void [[REDUCTION_FUNC]](i8* [[RL_BC]], i8* [[LOCAL_RL_BC]])
  // CHECK: ret void

  // CHECK-LABEL: define {{.*}}void {{@__omp_offloading_.+template.+l54}}(
  //
  // CHECK: call void @__kmpc_spmd_kernel_init(
  // CHECK: call void @__kmpc_data_sharing_init_stack_spmd()
  // CHECK-NOT: call void @__kmpc_get_team_static_memory
  // CHECK: store i32 0,
  // CHECK: store i32 0, i32* [[A_ADDR:%.+]], align
  // CHECK: store i16 -32768, i16* [[B_ADDR:%.+]], align
  // CHECK: call void [[OUTLINED:@.+]](i32* {{.+}}, i32* {{.+}}, i32* [[A_ADDR]], i16* [[B_ADDR]])
  // CHECK: [[GEP1:%.+]] = getelementptr inbounds [2 x i8*], [2 x i8*]* [[RED_LIST:%.+]], i{{[0-9]+}} 0, i{{[0-9]+}} 0
  // CHECK: [[BC:%.+]] = bitcast i32* [[A_ADDR]] to i8*
  // CHECK: store i8* [[BC]], i8** [[GEP1]],
  // CHECK: [[GEP2:%.+]] = getelementptr inbounds [2 x i8*], [2 x i8*]* [[RED_LIST:%.+]], i{{[0-9]+}} 0, i{{[0-9]+}} 1
  // CHECK: [[BC:%.+]] = bitcast i16* [[B_ADDR]] to i8*
  // CHECK: store i8* [[BC]], i8** [[GEP2]],
  // CHECK: [[BC_RED_LIST:%.+]] = bitcast [2 x i8*]* [[RED_LIST]] to i8*
  // CHECK: [[RET:%.+]] = call i32 @__kmpc_nvptx_teams_reduce_nowait_v2(%struct.ident_t* [[LOC:@.+]], i32 [[GTID:%.+]], i8* bitcast ([[TEAMS_REDUCE_UNION_TY]]* [[TEAMS_RED_BUFFER]] to i8*), i32 {{1024|2048}}, i8* [[BC_RED_LIST]], void (i8*, i16, i16, i16)* [[SHUFFLE_AND_REDUCE:@.+]], void (i8*, i32)* [[INTER_WARP_COPY:@.+]], void (i8*, i32, i8*)* [[RED_LIST_TO_GLOBAL_COPY:@.+]], void (i8*, i32, i8*)* [[RED_LIST_TO_GLOBAL_RED:@.+]], void (i8*, i32, i8*)* [[GLOBAL_TO_RED_LIST_COPY:@.+]], void (i8*, i32, i8*)* [[GLOBAL_TO_RED_LIST_RED:@.+]])
  // CHECK: [[COND:%.+]] = icmp eq i32 [[RET]], 1
  // CHECK: br i1 [[COND]], label {{%?}}[[IFLABEL:.+]], label {{%?}}[[EXIT:.+]]
  //
  // CHECK: [[IFLABEL]]
  // CHECK: [[A_INV:%.+]] = load i32, i32* [[A_IN:%.+]], align
  // CHECK: [[AV:%.+]] = load i32, i32* [[A_ADDR]], align
  // CHECK: [[OR:%.+]] = or i32 [[A_INV]], [[AV]]
  // CHECK: store i32 [[OR]], i32* [[A_IN]], align
  // CHECK: [[B_INV16:%.+]] = load i16, i16* [[B_IN:%.+]], align
  // CHECK: [[B_INV:%.+]] = sext i16 [[B_INV16]] to i32
  // CHECK: [[BV16:%.+]] = load i16, i16* [[B_ADDR]], align
  // CHECK: [[BV:%.+]] = sext i16 [[BV16]] to i32
  // CHECK: [[CMP:%.+]] = icmp sgt i32 [[B_INV]], [[BV]]
  // CHECK: br i1 [[CMP]], label {{%?}}[[DO_MAX:.+]], label {{%?}}[[MAX_ELSE:.+]]
  //
  // CHECK: [[DO_MAX]]
  // CHECK: [[MAX1:%.+]] = load i16, i16* [[B_IN]], align
  // CHECK: br label {{%?}}[[MAX_CONT:.+]]
  //
  // CHECK: [[MAX_ELSE]]
  // CHECK: [[MAX2:%.+]] = load i16, i16* [[B_ADDR]], align
  // CHECK: br label {{%?}}[[MAX_CONT]]
  //
  // CHECK: [[MAX_CONT]]
  // CHECK: [[B_MAX:%.+]] = phi i16 [ [[MAX1]], %[[DO_MAX]] ], [ [[MAX2]], %[[MAX_ELSE]] ]
  // CHECK: store i16 [[B_MAX]], i16* [[B_IN]], align
  // CHECK: call void @__kmpc_nvptx_end_reduce_nowait(i32 [[GTID]])
  // CHECK: br label %[[EXIT]]
  //
  // CHECK: [[EXIT]]
  // CHECK: call void @__kmpc_spmd_kernel_deinit_v2(i16 1)

  // CHECK: define internal void [[OUTLINED]](i32* noalias %{{.+}}, i32* noalias %{{.+}}, i32* dereferenceable{{.+}}, i16* dereferenceable{{.+}})
  //
  // CHECK: store i32 0, i32* [[A:%.+]], align
  // CHECK: store i16 -32768, i16* [[B:%.+]], align
  // CHECK: [[A_VAL:%.+]] = load i32, i32* [[A:%.+]], align
  // CHECK: [[OR:%.+]] = or i32 [[A_VAL]], 1
  // CHECK: store i32 [[OR]], i32* [[A]], align
  // CHECK: [[BV16:%.+]] = load i16, i16* [[B]], align
  // CHECK: [[BV:%.+]] = sext i16 [[BV16]] to i32
  // CHECK: [[CMP:%.+]] = icmp sgt i32 99, [[BV]]
  // CHECK: br i1 [[CMP]], label {{%?}}[[DO_MAX:.+]], label {{%?}}[[MAX_ELSE:.+]]
  //
  // CHECK: [[DO_MAX]]
  // CHECK: br label {{%?}}[[MAX_CONT:.+]]
  //
  // CHECK: [[MAX_ELSE]]
  // CHECK: [[BV:%.+]] = load i16, i16* [[B]], align
  // CHECK: [[MAX:%.+]] = sext i16 [[BV]] to i32
  // CHECK: br label {{%?}}[[MAX_CONT]]
  //
  // CHECK: [[MAX_CONT]]
  // CHECK: [[B_LVALUE:%.+]] = phi i32 [ 99, %[[DO_MAX]] ], [ [[MAX]], %[[MAX_ELSE]] ]
  // CHECK: [[TRUNC:%.+]] = trunc i32 [[B_LVALUE]] to i16
  // CHECK: store i16 [[TRUNC]], i16* [[B]], align
  // CHECK: [[PTR1:%.+]] = getelementptr inbounds [[RLT:.+]], [2 x i8*]* [[RL:%.+]], i{{.+}} 0, i[[SZ:.+]] 0
  // CHECK: [[A_CAST:%.+]] = bitcast i32* [[A]] to i8*
  // CHECK: store i8* [[A_CAST]], i8** [[PTR1]], align
  // CHECK: [[PTR2:%.+]] = getelementptr inbounds [[RLT]], [[RLT]]* [[RL]], i[[SZ]] 0, i[[SZ]] 1
  // CHECK: [[B_CAST:%.+]] = bitcast i16* [[B]] to i8*
  // CHECK: store i8* [[B_CAST]], i8** [[PTR2]], align
  // CHECK: [[ARG_RL:%.+]] = bitcast [[RLT]]* [[RL]] to i8*
  // CHECK: [[RET:%.+]] = call i32 @__kmpc_nvptx_parallel_reduce_nowait_v2(%struct.ident_t* [[LOC]], i32 {{.+}}, i32 2, i[[SZ]] {{8|16}}, i8* [[ARG_RL]], void (i8*, i16, i16, i16)* [[PAR_SHUFFLE_REDUCE_FN:@.+]], void (i8*, i32)* [[PAR_WARP_COPY_FN:@.+]])
  // CHECK: [[COND:%.+]] = icmp eq i32 [[RET]], 1
  // CHECK: br i1 [[COND]], label {{%?}}[[IFLABEL:.+]], label {{%?}}[[EXIT:.+]]
  //
  // CHECK: [[IFLABEL]]
  // CHECK: [[A_INV:%.+]] = load i32, i32* [[A_IN:%.+]], align
  // CHECK: [[AV:%.+]] = load i32, i32* [[A]], align
  // CHECK: [[OR:%.+]] = or i32 [[A_INV]], [[AV]]
  // CHECK: store i32 [[OR]], i32* [[A_IN]], align
  // CHECK: [[B_INV16:%.+]] = load i16, i16* [[B_IN:%.+]], align
  // CHECK: [[B_INV:%.+]] = sext i16 [[B_INV16]] to i32
  // CHECK: [[BV16:%.+]] = load i16, i16* [[B]], align
  // CHECK: [[BV:%.+]] = sext i16 [[BV16]] to i32
  // CHECK: [[CMP:%.+]] = icmp sgt i32 [[B_INV]], [[BV]]
  // CHECK: br i1 [[CMP]], label {{%?}}[[DO_MAX:.+]], label {{%?}}[[MAX_ELSE:.+]]
  //
  // CHECK: [[DO_MAX]]
  // CHECK: [[MAX1:%.+]] = load i16, i16* [[B_IN]], align
  // CHECK: br label {{%?}}[[MAX_CONT:.+]]
  //
  // CHECK: [[MAX_ELSE]]
  // CHECK: [[MAX2:%.+]] = load i16, i16* [[B]], align
  // CHECK: br label {{%?}}[[MAX_CONT]]
  //
  // CHECK: [[MAX_CONT]]
  // CHECK: [[B_MAX:%.+]] = phi i16 [ [[MAX1]], %[[DO_MAX]] ], [ [[MAX2]], %[[MAX_ELSE]] ]
  // CHECK: store i16 [[B_MAX]], i16* [[B_IN]], align
  // CHECK: call void @__kmpc_nvptx_end_reduce_nowait(
  // CHECK: br label %[[EXIT]]
  //
  // CHECK: [[EXIT]]
  // CHECK: ret void

  //
  // Reduction function
  // CHECK: define internal void [[PAR_REDUCTION_FUNC:@.+]](i8*, i8*)
  // CHECK: [[VAR1_RHS_REF:%.+]] = getelementptr inbounds [[RLT]], [[RLT]]* [[RED_LIST_RHS:%.+]], i[[SZ]] 0, i[[SZ]] 0
  // CHECK: [[VAR1_RHS_VOID:%.+]] = load i8*, i8** [[VAR1_RHS_REF]],
  // CHECK: [[VAR1_RHS:%.+]] = bitcast i8* [[VAR1_RHS_VOID]] to i32*
  //
  // CHECK: [[VAR1_LHS_REF:%.+]] = getelementptr inbounds [[RLT]], [[RLT]]* [[RED_LIST_LHS:%.+]], i[[SZ]] 0, i[[SZ]] 0
  // CHECK: [[VAR1_LHS_VOID:%.+]] = load i8*, i8** [[VAR1_LHS_REF]],
  // CHECK: [[VAR1_LHS:%.+]] = bitcast i8* [[VAR1_LHS_VOID]] to i32*
  //
  // CHECK: [[VAR2_RHS_REF:%.+]] = getelementptr inbounds [[RLT]], [[RLT]]* [[RED_LIST_RHS]], i[[SZ]] 0, i[[SZ]] 1
  // CHECK: [[VAR2_RHS_VOID:%.+]] = load i8*, i8** [[VAR2_RHS_REF]],
  // CHECK: [[VAR2_RHS:%.+]] = bitcast i8* [[VAR2_RHS_VOID]] to i16*
  //
  // CHECK: [[VAR2_LHS_REF:%.+]] = getelementptr inbounds [[RLT]], [[RLT]]* [[RED_LIST_LHS]], i[[SZ]] 0, i[[SZ]] 1
  // CHECK: [[VAR2_LHS_VOID:%.+]] = load i8*, i8** [[VAR2_LHS_REF]],
  // CHECK: [[VAR2_LHS:%.+]] = bitcast i8* [[VAR2_LHS_VOID]] to i16*
  //
  // CHECK: [[VAR1_LHS_VAL:%.+]] = load i32, i32* [[VAR1_LHS]],
  // CHECK: [[VAR1_RHS_VAL:%.+]] = load i32, i32* [[VAR1_RHS]],
  // CHECK: [[OR:%.+]] = or i32 [[VAR1_LHS_VAL]], [[VAR1_RHS_VAL]]
  // CHECK: store i32 [[OR]], i32* [[VAR1_LHS]],
  //
  // CHECK: [[VAR2_LHS_VAL16:%.+]] = load i16, i16* [[VAR2_LHS]],
  // CHECK: [[VAR2_LHS_VAL:%.+]] = sext i16 [[VAR2_LHS_VAL16]] to i32
  // CHECK: [[VAR2_RHS_VAL16:%.+]] = load i16, i16* [[VAR2_RHS]],
  // CHECK: [[VAR2_RHS_VAL:%.+]] = sext i16 [[VAR2_RHS_VAL16]] to i32
  //
  // CHECK: [[CMP:%.+]] = icmp sgt i32 [[VAR2_LHS_VAL]], [[VAR2_RHS_VAL]]
  // CHECK: br i1 [[CMP]], label {{%?}}[[DO_MAX:.+]], label {{%?}}[[MAX_ELSE:.+]]
  //
  // CHECK: [[DO_MAX]]
  // CHECK: [[MAX1:%.+]] = load i16, i16* [[VAR2_LHS]], align
  // CHECK: br label {{%?}}[[MAX_CONT:.+]]
  //
  // CHECK: [[MAX_ELSE]]
  // CHECK: [[MAX2:%.+]] = load i16, i16* [[VAR2_RHS]], align
  // CHECK: br label {{%?}}[[MAX_CONT]]
  //
  // CHECK: [[MAX_CONT]]
  // CHECK: [[MAXV:%.+]] = phi i16 [ [[MAX1]], %[[DO_MAX]] ], [ [[MAX2]], %[[MAX_ELSE]] ]
  // CHECK: store i16 [[MAXV]], i16* [[VAR2_LHS]],
  // CHECK: ret void
  //
  // Shuffle and reduce function
  // CHECK: define internal void [[PAR_SHUFFLE_REDUCE_FN]](i8*, i16 {{.*}}, i16 {{.*}}, i16 {{.*}})
  // CHECK: [[REMOTE_RED_LIST:%.+]] = alloca [[RLT]], align
  // CHECK: [[REMOTE_ELT1:%.+]] = alloca i32
  // CHECK: [[REMOTE_ELT2:%.+]] = alloca i16
  //
  // CHECK: [[LANEID:%.+]] = load i16, i16* {{.+}}, align
  // CHECK: [[LANEOFFSET:%.+]] = load i16, i16* {{.+}}, align
  // CHECK: [[ALGVER:%.+]] = load i16, i16* {{.+}}, align
  //
  // CHECK: [[ELT_REF:%.+]] = getelementptr inbounds [[RLT]], [[RLT]]* [[RED_LIST:%.+]], i[[SZ]] 0, i[[SZ]] 0
  // CHECK: [[ELT_VOID:%.+]] = load i8*, i8** [[ELT_REF]],
  // CHECK: [[REMOTE_ELT_REF:%.+]] = getelementptr inbounds [[RLT]], [[RLT]]* [[REMOTE_RED_LIST:%.+]], i[[SZ]] 0, i[[SZ]] 0
  // CHECK: [[ELT:%.+]] = bitcast i8* [[ELT_VOID]] to i32*
  // CHECK: [[ELT_VAL:%.+]] = load i32, i32* [[ELT]], align
  //
  // CHECK: [[WS32:%.+]] = call i32 @llvm.nvvm.read.ptx.sreg.warpsize()
  // CHECK: [[WS:%.+]] = trunc i32 [[WS32]] to i16
  // CHECK: [[REMOTE_ELT1_VAL:%.+]] = call i32 @__kmpc_shuffle_int32(i32 [[ELT_VAL]], i16 [[LANEOFFSET]], i16 [[WS]])
  //
  // CHECK: store i32 [[REMOTE_ELT1_VAL]], i32* [[REMOTE_ELT1]], align
  // CHECK: [[REMOTE_ELT1C:%.+]] = bitcast i32* [[REMOTE_ELT1]] to i8*
  // CHECK: store i8* [[REMOTE_ELT1C]], i8** [[REMOTE_ELT_REF]], align
  //
  // CHECK: [[ELT_REF:%.+]] = getelementptr inbounds [[RLT]], [[RLT]]* [[RED_LIST]], i[[SZ]] 0, i[[SZ]] 1
  // CHECK: [[ELT_VOID:%.+]] = load i8*, i8** [[ELT_REF]],
  // CHECK: [[REMOTE_ELT_REF:%.+]] = getelementptr inbounds [[RLT]], [[RLT]]* [[REMOTE_RED_LIST]], i[[SZ]] 0, i[[SZ]] 1
  // CHECK: [[ELT:%.+]] = bitcast i8* [[ELT_VOID]] to i16*
  // CHECK: [[ELT_VAL:%.+]] = load i16, i16* [[ELT]], align
  //
  // CHECK: [[ELT_CAST:%.+]] = sext i16 [[ELT_VAL]] to i32
  // CHECK: [[WS32:%.+]] = call i32 @llvm.nvvm.read.ptx.sreg.warpsize()
  // CHECK: [[WS:%.+]] = trunc i32 [[WS32]] to i16
  // CHECK: [[REMOTE_ELT2_VAL32:%.+]] = call i32 @__kmpc_shuffle_int32(i32 [[ELT_CAST]], i16 [[LANEOFFSET]], i16 [[WS]])
  // CHECK: [[REMOTE_ELT2_VAL:%.+]] = trunc i32 [[REMOTE_ELT2_VAL32]] to i16
  //
  // CHECK: store i16 [[REMOTE_ELT2_VAL]], i16* [[REMOTE_ELT2]], align
  // CHECK: [[REMOTE_ELT2C:%.+]] = bitcast i16* [[REMOTE_ELT2]] to i8*
  // CHECK: store i8* [[REMOTE_ELT2C]], i8** [[REMOTE_ELT_REF]], align
  //
  // Condition to reduce
  // CHECK: [[CONDALG0:%.+]] = icmp eq i16 [[ALGVER]], 0
  //
  // CHECK: [[COND1:%.+]] = icmp eq i16 [[ALGVER]], 1
  // CHECK: [[COND2:%.+]] = icmp ult i16 [[LANEID]], [[LANEOFFSET]]
  // CHECK: [[CONDALG1:%.+]] = and i1 [[COND1]], [[COND2]]
  //
  // CHECK: [[COND3:%.+]] = icmp eq i16 [[ALGVER]], 2
  // CHECK: [[COND4:%.+]] = and i16 [[LANEID]], 1
  // CHECK: [[COND5:%.+]] = icmp eq i16 [[COND4]], 0
  // CHECK: [[COND6:%.+]] = and i1 [[COND3]], [[COND5]]
  // CHECK: [[COND7:%.+]] = icmp sgt i16 [[LANEOFFSET]], 0
  // CHECK: [[CONDALG2:%.+]] = and i1 [[COND6]], [[COND7]]
  //
  // CHECK: [[COND8:%.+]] = or i1 [[CONDALG0]], [[CONDALG1]]
  // CHECK: [[SHOULD_REDUCE:%.+]] = or i1 [[COND8]], [[CONDALG2]]
  // CHECK: br i1 [[SHOULD_REDUCE]], label {{%?}}[[DO_REDUCE:.+]], label {{%?}}[[REDUCE_ELSE:.+]]
  //
  // CHECK: [[DO_REDUCE]]
  // CHECK: [[RED_LIST1_VOID:%.+]] = bitcast [[RLT]]* [[RED_LIST]] to i8*
  // CHECK: [[RED_LIST2_VOID:%.+]] = bitcast [[RLT]]* [[REMOTE_RED_LIST]] to i8*
  // CHECK: call void [[PAR_REDUCTION_FUNC]](i8* [[RED_LIST1_VOID]], i8* [[RED_LIST2_VOID]])
  // CHECK: br label {{%?}}[[REDUCE_CONT:.+]]
  //
  // CHECK: [[REDUCE_ELSE]]
  // CHECK: br label {{%?}}[[REDUCE_CONT]]
  //
  // CHECK: [[REDUCE_CONT]]
  // Now check if we should just copy over the remote reduction list
  // CHECK: [[COND1:%.+]] = icmp eq i16 [[ALGVER]], 1
  // CHECK: [[COND2:%.+]] = icmp uge i16 [[LANEID]], [[LANEOFFSET]]
  // CHECK: [[SHOULD_COPY:%.+]] = and i1 [[COND1]], [[COND2]]
  // CHECK: br i1 [[SHOULD_COPY]], label {{%?}}[[DO_COPY:.+]], label {{%?}}[[COPY_ELSE:.+]]
  //
  // CHECK: [[DO_COPY]]
  // CHECK: [[REMOTE_ELT_REF:%.+]] = getelementptr inbounds [[RLT]], [[RLT]]* [[REMOTE_RED_LIST]], i[[SZ]] 0, i[[SZ]] 0
  // CHECK: [[REMOTE_ELT_VOID:%.+]] = load i8*, i8** [[REMOTE_ELT_REF]],
  // CHECK: [[ELT_REF:%.+]] = getelementptr inbounds [[RLT]], [[RLT]]* [[RED_LIST]], i[[SZ]] 0, i[[SZ]] 0
  // CHECK: [[ELT_VOID:%.+]] = load i8*, i8** [[ELT_REF]],
  // CHECK: [[REMOTE_ELT:%.+]] = bitcast i8* [[REMOTE_ELT_VOID]] to i32*
  // CHECK: [[ELT:%.+]] = bitcast i8* [[ELT_VOID]] to i32*
  // CHECK: [[REMOTE_ELT_VAL:%.+]] = load i32, i32* [[REMOTE_ELT]], align
  // CHECK: store i32 [[REMOTE_ELT_VAL]], i32* [[ELT]], align
  //
  // CHECK: [[REMOTE_ELT_REF:%.+]] = getelementptr inbounds [[RLT]], [[RLT]]* [[REMOTE_RED_LIST]], i[[SZ]] 0, i[[SZ]] 1
  // CHECK: [[REMOTE_ELT_VOID:%.+]] = load i8*, i8** [[REMOTE_ELT_REF]],
  // CHECK: [[ELT_REF:%.+]] = getelementptr inbounds [[RLT]], [[RLT]]* [[RED_LIST]], i[[SZ]] 0, i[[SZ]] 1
  // CHECK: [[ELT_VOID:%.+]] = load i8*, i8** [[ELT_REF]],
  // CHECK: [[REMOTE_ELT:%.+]] = bitcast i8* [[REMOTE_ELT_VOID]] to i16*
  // CHECK: [[ELT:%.+]] = bitcast i8* [[ELT_VOID]] to i16*
  // CHECK: [[REMOTE_ELT_VAL:%.+]] = load i16, i16* [[REMOTE_ELT]], align
  // CHECK: store i16 [[REMOTE_ELT_VAL]], i16* [[ELT]], align
  // CHECK: br label {{%?}}[[COPY_CONT:.+]]
  //
  // CHECK: [[COPY_ELSE]]
  // CHECK: br label {{%?}}[[COPY_CONT]]
  //
  // CHECK: [[COPY_CONT]]
  // CHECK: void

  //
  // Inter warp copy function
  // CHECK: define internal void [[PAR_WARP_COPY_FN]](i8*, i32)
  // CHECK-DAG: [[LANEID:%.+]] = and i32 {{.+}}, 31
  // CHECK-DAG: [[WARPID:%.+]] = ashr i32 {{.+}}, 5
  // CHECK-DAG: [[RED_LIST:%.+]] = bitcast i8* {{.+}} to [[RLT]]*
  // CHECK: [[IS_WARP_MASTER:%.+]] = icmp eq i32 [[LANEID]], 0
  // CHECK: br i1 [[IS_WARP_MASTER]], label {{%?}}[[DO_COPY:.+]], label {{%?}}[[COPY_ELSE:.+]]
  //
  // [[DO_COPY]]
  // CHECK: [[ELT_REF:%.+]] = getelementptr inbounds [[RLT]], [[RLT]]* [[RED_LIST]], i[[SZ]] 0, i[[SZ]] 0
  // CHECK: [[ELT_VOID:%.+]] = load i8*, i8** [[ELT_REF]],
  // CHECK: [[ELT:%.+]] = bitcast i8* [[ELT_VOID]] to i32*
  //
  // CHECK: [[MEDIUM_ELT:%.+]] = getelementptr inbounds [32 x i32], [32 x i32] addrspace([[SHARED_ADDRSPACE]])* [[TRANSFER_STORAGE]], i64 0, i32 [[WARPID]]
  // CHECK: [[ELT_VAL:%.+]] = load i32, i32* [[ELT]], align
  // CHECK: store volatile i32 [[ELT_VAL]], i32 addrspace([[SHARED_ADDRSPACE]])* [[MEDIUM_ELT]], align
  // CHECK: br label {{%?}}[[COPY_CONT:.+]]
  //
  // CHECK: [[COPY_ELSE]]
  // CHECK: br label {{%?}}[[COPY_CONT]]
  //
  // Barrier after copy to shared memory storage medium.
  // CHECK: [[COPY_CONT]]
  // CHECK: call void @__kmpc_barrier(%struct.ident_t* @
  // CHECK: [[ACTIVE_WARPS:%.+]] = load i32, i32*
  //
  // Read into warp 0.
  // CHECK: [[IS_W0_ACTIVE_THREAD:%.+]] = icmp ult i32 [[TID:%.+]], [[ACTIVE_WARPS]]
  // CHECK: br i1 [[IS_W0_ACTIVE_THREAD]], label {{%?}}[[DO_READ:.+]], label {{%?}}[[READ_ELSE:.+]]
  //
  // CHECK: [[DO_READ]]
  // CHECK: [[MEDIUM_ELT:%.+]] = getelementptr inbounds [32 x i32], [32 x i32] addrspace([[SHARED_ADDRSPACE]])* [[TRANSFER_STORAGE]], i64 0, i32 [[TID]]
  // CHECK: [[ELT_REF:%.+]] = getelementptr inbounds [[RLT]], [[RLT]]* [[RED_LIST:%.+]], i[[SZ]] 0, i[[SZ]] 0
  // CHECK: [[ELT_VOID:%.+]] = load i8*, i8** [[ELT_REF]],
  // CHECK: [[ELT:%.+]] = bitcast i8* [[ELT_VOID]] to i32*
  // CHECK: [[MEDIUM_ELT_VAL:%.+]] = load volatile i32, i32 addrspace([[SHARED_ADDRSPACE]])* [[MEDIUM_ELT]], align
  // CHECK: store i32 [[MEDIUM_ELT_VAL]], i32* [[ELT]], align
  // CHECK: br label {{%?}}[[READ_CONT:.+]]
  //
  // CHECK: [[READ_ELSE]]
  // CHECK: br label {{%?}}[[READ_CONT]]
  //
  // CHECK: [[READ_CONT]]
  // CHECK: call void @__kmpc_barrier(%struct.ident_t* @
  // CHECK: [[IS_WARP_MASTER:%.+]] = icmp eq i32 [[LANEID]], 0
  // CHECK: br i1 [[IS_WARP_MASTER]], label {{%?}}[[DO_COPY:.+]], label {{%?}}[[COPY_ELSE:.+]]
  //
  // [[DO_COPY]]
  // CHECK: [[ELT_REF:%.+]] = getelementptr inbounds [[RLT]], [[RLT]]* [[RED_LIST]], i[[SZ]] 0, i[[SZ]] 1
  // CHECK: [[ELT_VOID:%.+]] = load i8*, i8** [[ELT_REF]],
  // CHECK: [[ELT:%.+]] = bitcast i8* [[ELT_VOID]] to i16*
  //
  // CHECK: [[MEDIUM_ELT32:%.+]] = getelementptr inbounds [32 x i32], [32 x i32] addrspace([[SHARED_ADDRSPACE]])* [[TRANSFER_STORAGE]], i64 0, i32 [[WARPID]]
  // CHECK: [[MEDIUM_ELT:%.+]] = bitcast i32 addrspace([[SHARED_ADDRSPACE]])* [[MEDIUM_ELT32]] to i16 addrspace([[SHARED_ADDRSPACE]])*
  // CHECK: [[ELT_VAL:%.+]] = load i16, i16* [[ELT]], align
  // CHECK: store volatile i16 [[ELT_VAL]], i16 addrspace([[SHARED_ADDRSPACE]])* [[MEDIUM_ELT]], align
  // CHECK: br label {{%?}}[[COPY_CONT:.+]]
  //
  // CHECK: [[COPY_ELSE]]
  // CHECK: br label {{%?}}[[COPY_CONT]]
  //
  // Barrier after copy to shared memory storage medium.
  // CHECK: [[COPY_CONT]]
  // CHECK: call void @__kmpc_barrier(%struct.ident_t* @
  // CHECK: [[ACTIVE_WARPS:%.+]] = load i32, i32*
  //
  // Read into warp 0.
  // CHECK: [[IS_W0_ACTIVE_THREAD:%.+]] = icmp ult i32 [[TID:%.+]], [[ACTIVE_WARPS]]
  // CHECK: br i1 [[IS_W0_ACTIVE_THREAD]], label {{%?}}[[DO_READ:.+]], label {{%?}}[[READ_ELSE:.+]]
  //
  // CHECK: [[DO_READ]]
  // CHECK: [[MEDIUM_ELT32:%.+]] = getelementptr inbounds [32 x i32], [32 x i32] addrspace([[SHARED_ADDRSPACE]])* [[TRANSFER_STORAGE]], i64 0, i32 [[TID]]
  // CHECK: [[MEDIUM_ELT:%.+]] = bitcast i32 addrspace([[SHARED_ADDRSPACE]])* [[MEDIUM_ELT32]] to i16 addrspace([[SHARED_ADDRSPACE]])*
  // CHECK: [[ELT_REF:%.+]] = getelementptr inbounds [[RLT]], [[RLT]]* [[RED_LIST:%.+]], i[[SZ]] 0, i[[SZ]] 1
  // CHECK: [[ELT_VOID:%.+]] = load i8*, i8** [[ELT_REF]],
  // CHECK: [[ELT:%.+]] = bitcast i8* [[ELT_VOID]] to i16*
  // CHECK: [[MEDIUM_ELT_VAL:%.+]] = load volatile i16, i16 addrspace([[SHARED_ADDRSPACE]])* [[MEDIUM_ELT]], align
  // CHECK: store i16 [[MEDIUM_ELT_VAL]], i16* [[ELT]], align
  // CHECK: br label {{%?}}[[READ_CONT:.+]]
  //
  // CHECK: [[READ_ELSE]]
  // CHECK: br label {{%?}}[[READ_CONT]]
  //
  // CHECK: [[READ_CONT]]
  // CHECK: ret

  //
  // Reduction function
  // CHECK: define internal void [[REDUCTION_FUNC:@.+]](i8*, i8*)
  // CHECK: [[VAR1_RHS_REF:%.+]] = getelementptr inbounds [2 x i8*], [2 x i8*]* [[RED_LIST_RHS:%.+]], i[[SZ]] 0, i[[SZ]] 0
  // CHECK: [[VAR1_RHS_VOID:%.+]] = load i8*, i8** [[VAR1_RHS_REF]],
  // CHECK: [[VAR1_RHS:%.+]] = bitcast i8* [[VAR1_RHS_VOID]] to i32*
  //
  // CHECK: [[VAR1_LHS_REF:%.+]] = getelementptr inbounds [2 x i8*], [2 x i8*]* [[RED_LIST_LHS:%.+]], i[[SZ]] 0, i[[SZ]] 0
  // CHECK: [[VAR1_LHS_VOID:%.+]] = load i8*, i8** [[VAR1_LHS_REF]],
  // CHECK: [[VAR1_LHS:%.+]] = bitcast i8* [[VAR1_LHS_VOID]] to i32*
  //
  // CHECK: [[VAR2_RHS_REF:%.+]] = getelementptr inbounds [2 x i8*], [2 x i8*]* [[RED_LIST_RHS]], i[[SZ]] 0, i[[SZ]] 1
  // CHECK: [[VAR2_RHS_VOID:%.+]] = load i8*, i8** [[VAR2_RHS_REF]],
  // CHECK: [[VAR2_RHS:%.+]] = bitcast i8* [[VAR2_RHS_VOID]] to i16*
  //
  // CHECK: [[VAR2_LHS_REF:%.+]] = getelementptr inbounds [2 x i8*], [2 x i8*]* [[RED_LIST_LHS]], i[[SZ]] 0, i[[SZ]] 1
  // CHECK: [[VAR2_LHS_VOID:%.+]] = load i8*, i8** [[VAR2_LHS_REF]],
  // CHECK: [[VAR2_LHS:%.+]] = bitcast i8* [[VAR2_LHS_VOID]] to i16*
  //
  // CHECK: [[VAR1_LHS_VAL:%.+]] = load i32, i32* [[VAR1_LHS]],
  // CHECK: [[VAR1_RHS_VAL:%.+]] = load i32, i32* [[VAR1_RHS]],
  // CHECK: [[OR:%.+]] = or i32 [[VAR1_LHS_VAL]], [[VAR1_RHS_VAL]]
  // CHECK: store i32 [[OR]], i32* [[VAR1_LHS]],
  //
  // CHECK: [[VAR2_LHS_VAL16:%.+]] = load i16, i16* [[VAR2_LHS]],
  // CHECK: [[VAR2_LHS_VAL:%.+]] = sext i16 [[VAR2_LHS_VAL16]] to i32
  // CHECK: [[VAR2_RHS_VAL16:%.+]] = load i16, i16* [[VAR2_RHS]],
  // CHECK: [[VAR2_RHS_VAL:%.+]] = sext i16 [[VAR2_RHS_VAL16]] to i32
  //
  // CHECK: [[CMP:%.+]] = icmp sgt i32 [[VAR2_LHS_VAL]], [[VAR2_RHS_VAL]]
  // CHECK: br i1 [[CMP]], label {{%?}}[[DO_MAX:.+]], label {{%?}}[[MAX_ELSE:.+]]
  //
  // CHECK: [[DO_MAX]]
  // CHECK: [[MAX1:%.+]] = load i16, i16* [[VAR2_LHS]], align
  // CHECK: br label {{%?}}[[MAX_CONT:.+]]
  //
  // CHECK: [[MAX_ELSE]]
  // CHECK: [[MAX2:%.+]] = load i16, i16* [[VAR2_RHS]], align
  // CHECK: br label {{%?}}[[MAX_CONT]]
  //
  // CHECK: [[MAX_CONT]]
  // CHECK: [[MAXV:%.+]] = phi i16 [ [[MAX1]], %[[DO_MAX]] ], [ [[MAX2]], %[[MAX_ELSE]] ]
  // CHECK: store i16 [[MAXV]], i16* [[VAR2_LHS]],
  // CHECK: ret void

  //
  // Shuffle and reduce function
  // CHECK: define internal void [[SHUFFLE_AND_REDUCE]](i8*, i16 {{.*}}, i16 {{.*}}, i16 {{.*}})
  // CHECK: [[REMOTE_RED_LIST:%.+]] = alloca [2 x i8*], align
  // CHECK: [[REMOTE_ELT1:%.+]] = alloca i32
  // CHECK: [[REMOTE_ELT2:%.+]] = alloca i16
  //
  // CHECK: [[LANEID:%.+]] = load i16, i16* {{.+}}, align
  // CHECK: [[LANEOFFSET:%.+]] = load i16, i16* {{.+}}, align
  // CHECK: [[ALGVER:%.+]] = load i16, i16* {{.+}}, align
  //
  // CHECK: [[ELT_REF:%.+]] = getelementptr inbounds [2 x i8*], [2 x i8*]* [[RED_LIST:%.+]], i[[SZ]] 0, i[[SZ]] 0
  // CHECK: [[ELT_VOID:%.+]] = load i8*, i8** [[ELT_REF]],
  // CHECK: [[REMOTE_ELT_REF:%.+]] = getelementptr inbounds [2 x i8*], [2 x i8*]* [[REMOTE_RED_LIST:%.+]], i[[SZ]] 0, i[[SZ]] 0
  // CHECK: [[ELT:%.+]] = bitcast i8* [[ELT_VOID]] to i32*
  // CHECK: [[ELT_VAL:%.+]] = load i32, i32* [[ELT]], align
  //
  // CHECK: [[WS32:%.+]] = call i32 @llvm.nvvm.read.ptx.sreg.warpsize()
  // CHECK: [[WS:%.+]] = trunc i32 [[WS32]] to i16
  // CHECK: [[REMOTE_ELT1_VAL:%.+]] = call i32 @__kmpc_shuffle_int32(i32 [[ELT_VAL]], i16 [[LANEOFFSET]], i16 [[WS]])
  //
  // CHECK: store i32 [[REMOTE_ELT1_VAL]], i32* [[REMOTE_ELT1]], align
  // CHECK: [[REMOTE_ELT1C:%.+]] = bitcast i32* [[REMOTE_ELT1]] to i8*
  // CHECK: store i8* [[REMOTE_ELT1C]], i8** [[REMOTE_ELT_REF]], align
  //
  // CHECK: [[ELT_REF:%.+]] = getelementptr inbounds [2 x i8*], [2 x i8*]* [[RED_LIST]], i[[SZ]] 0, i[[SZ]] 1
  // CHECK: [[ELT_VOID:%.+]] = load i8*, i8** [[ELT_REF]],
  // CHECK: [[REMOTE_ELT_REF:%.+]] = getelementptr inbounds [2 x i8*], [2 x i8*]* [[REMOTE_RED_LIST]], i[[SZ]] 0, i[[SZ]] 1
  // CHECK: [[ELT:%.+]] = bitcast i8* [[ELT_VOID]] to i16*
  // CHECK: [[ELT_VAL:%.+]] = load i16, i16* [[ELT]], align
  //
  // CHECK: [[ELT_CAST:%.+]] = sext i16 [[ELT_VAL]] to i32
  // CHECK: [[WS32:%.+]] = call i32 @llvm.nvvm.read.ptx.sreg.warpsize()
  // CHECK: [[WS:%.+]] = trunc i32 [[WS32]] to i16
  // CHECK: [[REMOTE_ELT2_VAL32:%.+]] = call i32 @__kmpc_shuffle_int32(i32 [[ELT_CAST]], i16 [[LANEOFFSET]], i16 [[WS]])
  // CHECK: [[REMOTE_ELT2_VAL:%.+]] = trunc i32 [[REMOTE_ELT2_VAL32]] to i16
  //
  // CHECK: store i16 [[REMOTE_ELT2_VAL]], i16* [[REMOTE_ELT2]], align
  // CHECK: [[REMOTE_ELT2C:%.+]] = bitcast i16* [[REMOTE_ELT2]] to i8*
  // CHECK: store i8* [[REMOTE_ELT2C]], i8** [[REMOTE_ELT_REF]], align
  //
  // Condition to reduce
  // CHECK: [[CONDALG0:%.+]] = icmp eq i16 [[ALGVER]], 0
  //
  // CHECK: [[COND1:%.+]] = icmp eq i16 [[ALGVER]], 1
  // CHECK: [[COND2:%.+]] = icmp ult i16 [[LANEID]], [[LANEOFFSET]]
  // CHECK: [[CONDALG1:%.+]] = and i1 [[COND1]], [[COND2]]
  //
  // CHECK: [[COND3:%.+]] = icmp eq i16 [[ALGVER]], 2
  // CHECK: [[COND4:%.+]] = and i16 [[LANEID]], 1
  // CHECK: [[COND5:%.+]] = icmp eq i16 [[COND4]], 0
  // CHECK: [[COND6:%.+]] = and i1 [[COND3]], [[COND5]]
  // CHECK: [[COND7:%.+]] = icmp sgt i16 [[LANEOFFSET]], 0
  // CHECK: [[CONDALG2:%.+]] = and i1 [[COND6]], [[COND7]]
  //
  // CHECK: [[COND8:%.+]] = or i1 [[CONDALG0]], [[CONDALG1]]
  // CHECK: [[SHOULD_REDUCE:%.+]] = or i1 [[COND8]], [[CONDALG2]]
  // CHECK: br i1 [[SHOULD_REDUCE]], label {{%?}}[[DO_REDUCE:.+]], label {{%?}}[[REDUCE_ELSE:.+]]
  //
  // CHECK: [[DO_REDUCE]]
  // CHECK: [[RED_LIST1_VOID:%.+]] = bitcast [2 x i8*]* [[RED_LIST]] to i8*
  // CHECK: [[RED_LIST2_VOID:%.+]] = bitcast [2 x i8*]* [[REMOTE_RED_LIST]] to i8*
  // CHECK: call void [[REDUCTION_FUNC]](i8* [[RED_LIST1_VOID]], i8* [[RED_LIST2_VOID]])
  // CHECK: br label {{%?}}[[REDUCE_CONT:.+]]
  //
  // CHECK: [[REDUCE_ELSE]]
  // CHECK: br label {{%?}}[[REDUCE_CONT]]
  //
  // CHECK: [[REDUCE_CONT]]
  // Now check if we should just copy over the remote reduction list
  // CHECK: [[COND1:%.+]] = icmp eq i16 [[ALGVER]], 1
  // CHECK: [[COND2:%.+]] = icmp uge i16 [[LANEID]], [[LANEOFFSET]]
  // CHECK: [[SHOULD_COPY:%.+]] = and i1 [[COND1]], [[COND2]]
  // CHECK: br i1 [[SHOULD_COPY]], label {{%?}}[[DO_COPY:.+]], label {{%?}}[[COPY_ELSE:.+]]
  //
  // CHECK: [[DO_COPY]]
  // CHECK: [[REMOTE_ELT_REF:%.+]] = getelementptr inbounds [2 x i8*], [2 x i8*]* [[REMOTE_RED_LIST]], i[[SZ]] 0, i[[SZ]] 0
  // CHECK: [[REMOTE_ELT_VOID:%.+]] = load i8*, i8** [[REMOTE_ELT_REF]],
  // CHECK: [[ELT_REF:%.+]] = getelementptr inbounds [2 x i8*], [2 x i8*]* [[RED_LIST]], i[[SZ]] 0, i[[SZ]] 0
  // CHECK: [[ELT_VOID:%.+]] = load i8*, i8** [[ELT_REF]],
  // CHECK: [[REMOTE_ELT:%.+]] = bitcast i8* [[REMOTE_ELT_VOID]] to i32*
  // CHECK: [[ELT:%.+]] = bitcast i8* [[ELT_VOID]] to i32*
  // CHECK: [[REMOTE_ELT_VAL:%.+]] = load i32, i32* [[REMOTE_ELT]], align
  // CHECK: store i32 [[REMOTE_ELT_VAL]], i32* [[ELT]], align
  //
  // CHECK: [[REMOTE_ELT_REF:%.+]] = getelementptr inbounds [2 x i8*], [2 x i8*]* [[REMOTE_RED_LIST]], i[[SZ]] 0, i[[SZ]] 1
  // CHECK: [[REMOTE_ELT_VOID:%.+]] = load i8*, i8** [[REMOTE_ELT_REF]],
  // CHECK: [[ELT_REF:%.+]] = getelementptr inbounds [2 x i8*], [2 x i8*]* [[RED_LIST]], i[[SZ]] 0, i[[SZ]] 1
  // CHECK: [[ELT_VOID:%.+]] = load i8*, i8** [[ELT_REF]],
  // CHECK: [[REMOTE_ELT:%.+]] = bitcast i8* [[REMOTE_ELT_VOID]] to i16*
  // CHECK: [[ELT:%.+]] = bitcast i8* [[ELT_VOID]] to i16*
  // CHECK: [[REMOTE_ELT_VAL:%.+]] = load i16, i16* [[REMOTE_ELT]], align
  // CHECK: store i16 [[REMOTE_ELT_VAL]], i16* [[ELT]], align
  // CHECK: br label {{%?}}[[COPY_CONT:.+]]
  //
  // CHECK: [[COPY_ELSE]]
  // CHECK: br label {{%?}}[[COPY_CONT]]
  //
  // CHECK: [[COPY_CONT]]
  // CHECK: void

  //
  // Inter warp copy function
  // CHECK: define internal void [[INTER_WARP_COPY]](i8*, i32)
  // CHECK-DAG: [[LANEID:%.+]] = and i32 {{.+}}, 31
  // CHECK-DAG: [[WARPID:%.+]] = ashr i32 {{.+}}, 5
  // CHECK-DAG: [[RED_LIST:%.+]] = bitcast i8* {{.+}} to [[RLT]]*
  // CHECK: call void @__kmpc_barrier(%struct.ident_t* @
  // CHECK: [[IS_WARP_MASTER:%.+]] = icmp eq i32 [[LANEID]], 0
  // CHECK: br i1 [[IS_WARP_MASTER]], label {{%?}}[[DO_COPY:.+]], label {{%?}}[[COPY_ELSE:.+]]
  //
  // [[DO_COPY]]
  // CHECK: [[ELT_REF:%.+]] = getelementptr inbounds [[RLT]], [[RLT]]* [[RED_LIST]], i{{32|64}} 0, i{{32|64}} 0
  // CHECK: [[ELT_VOID:%.+]] = load i8*, i8** [[ELT_REF]],
  // CHECK: [[ELT:%.+]] = bitcast i8* [[ELT_VOID]] to i32*
  //
  // CHECK: [[MEDIUM_ELT:%.+]] = getelementptr inbounds [32 x i32], [32 x i32] addrspace([[SHARED_ADDRSPACE]])* [[TRANSFER_STORAGE]], i64 0, i32 [[WARPID]]
  // CHECK: [[ELT_VAL:%.+]] = load i32, i32* [[ELT]], align
  // CHECK: store volatile i32 [[ELT_VAL]], i32 addrspace([[SHARED_ADDRSPACE]])* [[MEDIUM_ELT]], align
  // CHECK: br label {{%?}}[[COPY_CONT:.+]]
  //
  // CHECK: [[COPY_ELSE]]
  // CHECK: br label {{%?}}[[COPY_CONT]]
  //
  // Barrier after copy to shared memory storage medium.
  // CHECK: [[COPY_CONT]]
  // CHECK: call void @__kmpc_barrier(%struct.ident_t* @
  // CHECK: [[ACTIVE_WARPS:%.+]] = load i32, i32*
  //
  // Read into warp 0.
  // CHECK: [[IS_W0_ACTIVE_THREAD:%.+]] = icmp ult i32 [[TID:%.+]], [[ACTIVE_WARPS]]
  // CHECK: br i1 [[IS_W0_ACTIVE_THREAD]], label {{%?}}[[DO_READ:.+]], label {{%?}}[[READ_ELSE:.+]]
  //
  // CHECK: [[DO_READ]]
  // CHECK: [[MEDIUM_ELT:%.+]] = getelementptr inbounds [32 x i32], [32 x i32] addrspace([[SHARED_ADDRSPACE]])* [[TRANSFER_STORAGE]], i64 0, i32 [[TID]]
  // CHECK: [[ELT_REF:%.+]] = getelementptr inbounds [[RLT]], [[RLT]]* [[RED_LIST:%.+]], i{{32|64}} 0, i{{32|64}} 0
  // CHECK: [[ELT_VOID:%.+]] = load i8*, i8** [[ELT_REF]],
  // CHECK: [[ELT:%.+]] = bitcast i8* [[ELT_VOID]] to i32*
  // CHECK: [[MEDIUM_ELT_VAL:%.+]] = load volatile i32, i32 addrspace([[SHARED_ADDRSPACE]])* [[MEDIUM_ELT]], align
  // CHECK: store i32 [[MEDIUM_ELT_VAL]], i32* [[ELT]], align
  // CHECK: br label {{%?}}[[READ_CONT:.+]]
  //
  // CHECK: [[READ_ELSE]]
  // CHECK: br label {{%?}}[[READ_CONT]]
  //
  // CHECK: [[READ_CONT]]
  // CHECK: call void @__kmpc_barrier(%struct.ident_t* @
  // CHECK: [[IS_WARP_MASTER:%.+]] = icmp eq i32 [[LANEID]], 0
  // CHECK: br i1 [[IS_WARP_MASTER]], label {{%?}}[[DO_COPY:.+]], label {{%?}}[[COPY_ELSE:.+]]
  //
  // [[DO_COPY]]
  // CHECK: [[ELT_REF:%.+]] = getelementptr inbounds [[RLT]], [[RLT]]* [[RED_LIST]], i{{32|64}} 0, i{{32|64}} 1
  // CHECK: [[ELT_VOID:%.+]] = load i8*, i8** [[ELT_REF]],
  // CHECK: [[ELT:%.+]] = bitcast i8* [[ELT_VOID]] to i16*
  //
  // CHECK: [[MEDIUM_ELT32:%.+]] = getelementptr inbounds [32 x i32], [32 x i32] addrspace([[SHARED_ADDRSPACE]])* [[TRANSFER_STORAGE]], i64 0, i32 [[WARPID]]
  // CHECK: [[MEDIUM_ELT:%.+]] = bitcast i32 addrspace([[SHARED_ADDRSPACE]])* [[MEDIUM_ELT32]] to i16 addrspace([[SHARED_ADDRSPACE]])*
  // CHECK: [[ELT_VAL:%.+]] = load i16, i16* [[ELT]], align
  // CHECK: store volatile i16 [[ELT_VAL]], i16 addrspace([[SHARED_ADDRSPACE]])* [[MEDIUM_ELT]], align
  // CHECK: br label {{%?}}[[COPY_CONT:.+]]
  //
  // CHECK: [[COPY_ELSE]]
  // CHECK: br label {{%?}}[[COPY_CONT]]
  //
  // Barrier after copy to shared memory storage medium.
  // CHECK: [[COPY_CONT]]
  // CHECK: call void @__kmpc_barrier(%struct.ident_t* @
  // CHECK: [[ACTIVE_WARPS:%.+]] = load i32, i32*
  //
  // Read into warp 0.
  // CHECK: [[IS_W0_ACTIVE_THREAD:%.+]] = icmp ult i32 [[TID:%.+]], [[ACTIVE_WARPS]]
  // CHECK: br i1 [[IS_W0_ACTIVE_THREAD]], label {{%?}}[[DO_READ:.+]], label {{%?}}[[READ_ELSE:.+]]
  //
  // CHECK: [[DO_READ]]
  // CHECK: [[MEDIUM_ELT32:%.+]] = getelementptr inbounds [32 x i32], [32 x i32] addrspace([[SHARED_ADDRSPACE]])* [[TRANSFER_STORAGE]], i64 0, i32 [[TID]]
  // CHECK: [[MEDIUM_ELT:%.+]] = bitcast i32 addrspace([[SHARED_ADDRSPACE]])* [[MEDIUM_ELT32]] to i16 addrspace([[SHARED_ADDRSPACE]])*
  // CHECK: [[ELT_REF:%.+]] = getelementptr inbounds [[RLT]], [[RLT]]* [[RED_LIST:%.+]], i{{32|64}} 0, i{{32|64}} 1
  // CHECK: [[ELT_VOID:%.+]] = load i8*, i8** [[ELT_REF]],
  // CHECK: [[ELT:%.+]] = bitcast i8* [[ELT_VOID]] to i16*
  // CHECK: [[MEDIUM_ELT_VAL:%.+]] = load volatile i16, i16 addrspace([[SHARED_ADDRSPACE]])* [[MEDIUM_ELT]], align
  // CHECK: store i16 [[MEDIUM_ELT_VAL]], i16* [[ELT]], align
  // CHECK: br label {{%?}}[[READ_CONT:.+]]
  //
  // CHECK: [[READ_ELSE]]
  // CHECK: br label {{%?}}[[READ_CONT]]
  //
  // CHECK: [[READ_CONT]]
  // CHECK: ret

  // CHECK: define internal void [[RED_LIST_TO_GLOBAL_COPY]](i8*, i32, i8*)
  // CHECK: [[GLOBAL_PTR:%.+]] = alloca i8*,
  // CHECK: [[IDX_PTR:%.+]] = alloca i32,
  // CHECK: [[RL_PTR:%.+]] = alloca i8*,
  // CHECK: store i8* %{{.+}}, i8** [[GLOBAL_PTR]],
  // CHECK: store i32 %{{.+}}, i32* [[IDX_PTR]],
  // CHECK: store i8* %{{.+}}, i8** [[RL_PTR]],
  // CHECK: [[RL_BC:%.+]] = load i8*, i8** [[RL_PTR]],
  // CHECK: [[RL:%.+]] = bitcast i8* [[RL_BC]] to [2 x i8*]*
  // CHECK: [[GLOBAL_BC:%.+]] = load i8*, i8** [[GLOBAL_PTR]],
  // CHECK: [[GLOBAL:%.+]] = bitcast i8* [[GLOBAL_BC]] to [[TEAM3_REDUCE_TY]]*
  // CHECK: [[IDX:%.+]] = load i32, i32* [[IDX_PTR]],
  // CHECK: [[RL_RED1_PTR:%.+]] = getelementptr inbounds [2 x i8*], [2 x i8*]* [[RL]], i{{[0-9]+}} 0, i{{[0-9]+}} 0
  // CHECK: [[RL_RED1_BC:%.+]] = load i8*, i8** [[RL_RED1_PTR]],
  // CHECK: [[RL_RED1:%.+]] = bitcast i8* [[RL_RED1_BC]] to i32*
  // CHECK: [[GLOBAL_RED1_PTR:%.+]] = getelementptr inbounds [[TEAM3_REDUCE_TY]], [[TEAM3_REDUCE_TY]]* [[GLOBAL]], i{{[0-9]+}} 0, i{{[0-9]+}} 0
  // CHECK: [[GLOBAL_RED1_IDX_PTR:%.+]] = getelementptr inbounds [{{1024|2048}} x i32], [{{1024|2048}} x i32]* [[GLOBAL_RED1_PTR]], i{{[0-9]+}} 0, i32 [[IDX]]
  // CHECK: [[LOC_RED1:%.+]] = load i32, i32* [[RL_RED1]],
  // CHECK: store i32 [[LOC_RED1]], i32* [[GLOBAL_RED1_IDX_PTR]],
  // CHECK: [[RL_RED1_PTR:%.+]] = getelementptr inbounds [2 x i8*], [2 x i8*]* [[RL]], i{{[0-9]+}} 0, i{{[0-9]+}} 1
  // CHECK: [[RL_RED1_BC:%.+]] = load i8*, i8** [[RL_RED1_PTR]],
  // CHECK: [[RL_RED1:%.+]] = bitcast i8* [[RL_RED1_BC]] to i16*
  // CHECK: [[GLOBAL_RED1_PTR:%.+]] = getelementptr inbounds [[TEAM3_REDUCE_TY]], [[TEAM3_REDUCE_TY]]* [[GLOBAL]], i{{[0-9]+}} 0, i{{[0-9]+}} 1
  // CHECK: [[GLOBAL_RED1_IDX_PTR:%.+]] = getelementptr inbounds [{{1024|2048}} x i16], [{{1024|2048}} x i16]* [[GLOBAL_RED1_PTR]], i{{[0-9]+}} 0, i32 [[IDX]]
  // CHECK: [[LOC_RED1:%.+]] = load i16, i16* [[RL_RED1]],
  // CHECK: store i16 [[LOC_RED1]], i16* [[GLOBAL_RED1_IDX_PTR]],
  // CHECK: ret void

  // CHECK: define internal void [[RED_LIST_TO_GLOBAL_RED]](i8*, i32, i8*)
  // CHECK: [[GLOBAL_PTR:%.+]] = alloca i8*,
  // CHECK: [[IDX_PTR:%.+]] = alloca i32,
  // CHECK: [[RL_PTR:%.+]] = alloca i8*,
  // CHECK: [[LOCAL_RL:%.+]] = alloca [2 x i8*],
  // CHECK: store i8* %{{.+}}, i8** [[GLOBAL_PTR]],
  // CHECK: store i32 %{{.+}}, i32* [[IDX_PTR]],
  // CHECK: store i8* %{{.+}}, i8** [[RL_PTR]],
  // CHECK: [[GLOBAL_BC:%.+]] = load i8*, i8** [[GLOBAL_PTR]],
  // CHECK: [[GLOBAL:%.+]] = bitcast i8* [[GLOBAL_BC]] to [[TEAM3_REDUCE_TY]]*
  // CHECK: [[IDX:%.+]] = load i32, i32* [[IDX_PTR]],
  // CHECK: [[LOCAL_RL_RED1_PTR:%.+]] = getelementptr inbounds [2 x i8*], [2 x i8*]* [[LOCAL_RL]], i{{[0-9]+}} 0, i{{[0-9]+}} 0
  // CHECK: [[GLOBAL_RED1_PTR:%.+]] = getelementptr inbounds [[TEAM3_REDUCE_TY]], [[TEAM3_REDUCE_TY]]* [[GLOBAL]], i{{[0-9]+}} 0, i{{[0-9]+}} 0
  // CHECK: [[GLOBAL_RED1_IDX_PTR:%.+]] = getelementptr inbounds [{{1024|2048}} x i32], [{{1024|2048}} x i32]* [[GLOBAL_RED1_PTR]], i{{[0-9]+}} 0, i32 [[IDX]]
  // CHECK: [[GLOBAL_RED1_IDX_PTR_BC:%.+]] = bitcast i32* [[GLOBAL_RED1_IDX_PTR]] to i8*
  // CHECK: store i8* [[GLOBAL_RED1_IDX_PTR_BC]], i8** [[LOCAL_RL_RED1_PTR]]
  // CHECK: [[LOCAL_RL_RED1_PTR:%.+]] = getelementptr inbounds [2 x i8*], [2 x i8*]* [[LOCAL_RL]], i{{[0-9]+}} 0, i{{[0-9]+}} 1
  // CHECK: [[GLOBAL_RED1_PTR:%.+]] = getelementptr inbounds [[TEAM3_REDUCE_TY]], [[TEAM3_REDUCE_TY]]* [[GLOBAL]], i{{[0-9]+}} 0, i{{[0-9]+}} 1
  // CHECK: [[GLOBAL_RED1_IDX_PTR:%.+]] = getelementptr inbounds [{{1024|2048}} x i16], [{{1024|2048}} x i16]* [[GLOBAL_RED1_PTR]], i{{[0-9]+}} 0, i32 [[IDX]]
  // CHECK: [[GLOBAL_RED1_IDX_PTR_BC:%.+]] = bitcast i16* [[GLOBAL_RED1_IDX_PTR]] to i8*
  // CHECK: store i8* [[GLOBAL_RED1_IDX_PTR_BC]], i8** [[LOCAL_RL_RED1_PTR]]
  // CHECK: [[LOCAL_RL_BC:%.+]] = bitcast [2 x i8*]* [[LOCAL_RL]] to i8*
  // CHECK: [[RL_BC:%.+]] = load i8*, i8** [[RL_PTR]],
  // CHECK: call void [[REDUCTION_FUNC]](i8* [[LOCAL_RL_BC]], i8* [[RL_BC]])
  // CHECK: ret void

  // CHECK: define internal void [[GLOBAL_TO_RED_LIST_COPY]](i8*, i32, i8*)
  // CHECK: [[GLOBAL_PTR:%.+]] = alloca i8*,
  // CHECK: [[IDX_PTR:%.+]] = alloca i32,
  // CHECK: [[RL_PTR:%.+]] = alloca i8*,
  // CHECK: store i8* %{{.+}}, i8** [[GLOBAL_PTR]],
  // CHECK: store i32 %{{.+}}, i32* [[IDX_PTR]],
  // CHECK: store i8* %{{.+}}, i8** [[RL_PTR]],
  // CHECK: [[RL_BC:%.+]] = load i8*, i8** [[RL_PTR]],
  // CHECK: [[RL:%.+]] = bitcast i8* [[RL_BC]] to [2 x i8*]*
  // CHECK: [[GLOBAL_BC:%.+]] = load i8*, i8** [[GLOBAL_PTR]],
  // CHECK: [[GLOBAL:%.+]] = bitcast i8* [[GLOBAL_BC]] to [[TEAM3_REDUCE_TY]]*
  // CHECK: [[IDX:%.+]] = load i32, i32* [[IDX_PTR]],
  // CHECK: [[RL_RED1_PTR:%.+]] = getelementptr inbounds [2 x i8*], [2 x i8*]* [[RL]], i{{[0-9]+}} 0, i{{[0-9]+}} 0
  // CHECK: [[RL_RED1_BC:%.+]] = load i8*, i8** [[RL_RED1_PTR]],
  // CHECK: [[RL_RED1:%.+]] = bitcast i8* [[RL_RED1_BC]] to i32*
  // CHECK: [[GLOBAL_RED1_PTR:%.+]] = getelementptr inbounds [[TEAM3_REDUCE_TY]], [[TEAM3_REDUCE_TY]]* [[GLOBAL]], i{{[0-9]+}} 0, i{{[0-9]+}} 0
  // CHECK: [[GLOBAL_RED1_IDX_PTR:%.+]] = getelementptr inbounds [{{1024|2048}} x i32], [{{1024|2048}} x i32]* [[GLOBAL_RED1_PTR]], i{{[0-9]+}} 0, i32 [[IDX]]
  // CHECK: [[GLOBAL_RED1:%.+]] = load i32, i32* [[GLOBAL_RED1_IDX_PTR]],
  // CHECK: store i32 [[GLOBAL_RED1]], i32* [[RL_RED1]],
  // CHECK: [[RL_RED1_PTR:%.+]] = getelementptr inbounds [2 x i8*], [2 x i8*]* [[RL]], i{{[0-9]+}} 0, i{{[0-9]+}} 1
  // CHECK: [[RL_RED1_BC:%.+]] = load i8*, i8** [[RL_RED1_PTR]],
  // CHECK: [[RL_RED1:%.+]] = bitcast i8* [[RL_RED1_BC]] to i16*
  // CHECK: [[GLOBAL_RED1_PTR:%.+]] = getelementptr inbounds [[TEAM3_REDUCE_TY]], [[TEAM3_REDUCE_TY]]* [[GLOBAL]], i{{[0-9]+}} 0, i{{[0-9]+}} 1
  // CHECK: [[GLOBAL_RED1_IDX_PTR:%.+]] = getelementptr inbounds [{{1024|2048}} x i16], [{{1024|2048}} x i16]* [[GLOBAL_RED1_PTR]], i{{[0-9]+}} 0, i32 [[IDX]]
  // CHECK: [[GLOBAL_RED1:%.+]] = load i16, i16* [[GLOBAL_RED1_IDX_PTR]],
  // CHECK: store i16 [[GLOBAL_RED1]], i16* [[RL_RED1]],
  // CHECK: ret void

  // CHECK: define internal void [[GLOBAL_TO_RED_LIST_RED]](i8*, i32, i8*)
  // CHECK: [[GLOBAL_PTR:%.+]] = alloca i8*,
  // CHECK: [[IDX_PTR:%.+]] = alloca i32,
  // CHECK: [[RL_PTR:%.+]] = alloca i8*,
  // CHECK: [[LOCAL_RL:%.+]] = alloca [2 x i8*],
  // CHECK: store i8* %{{.+}}, i8** [[GLOBAL_PTR]],
  // CHECK: store i32 %{{.+}}, i32* [[IDX_PTR]],
  // CHECK: store i8* %{{.+}}, i8** [[RL_PTR]],
  // CHECK: [[GLOBAL_BC:%.+]] = load i8*, i8** [[GLOBAL_PTR]],
  // CHECK: [[GLOBAL:%.+]] = bitcast i8* [[GLOBAL_BC]] to [[TEAM3_REDUCE_TY]]*
  // CHECK: [[IDX:%.+]] = load i32, i32* [[IDX_PTR]],
  // CHECK: [[LOCAL_RL_RED1_PTR:%.+]] = getelementptr inbounds [2 x i8*], [2 x i8*]* [[LOCAL_RL]], i{{[0-9]+}} 0, i{{[0-9]+}} 0
  // CHECK: [[GLOBAL_RED1_PTR:%.+]] = getelementptr inbounds [[TEAM3_REDUCE_TY]], [[TEAM3_REDUCE_TY]]* [[GLOBAL]], i{{[0-9]+}} 0, i{{[0-9]+}} 0
  // CHECK: [[GLOBAL_RED1_IDX_PTR:%.+]] = getelementptr inbounds [{{1024|2048}} x i32], [{{1024|2048}} x i32]* [[GLOBAL_RED1_PTR]], i{{[0-9]+}} 0, i32 [[IDX]]
  // CHECK: [[GLOBAL_RED1_IDX_PTR_BC:%.+]] = bitcast i32* [[GLOBAL_RED1_IDX_PTR]] to i8*
  // CHECK: store i8* [[GLOBAL_RED1_IDX_PTR_BC]], i8** [[LOCAL_RL_RED1_PTR]]
  // CHECK: [[LOCAL_RL_RED1_PTR:%.+]] = getelementptr inbounds [2 x i8*], [2 x i8*]* [[LOCAL_RL]], i{{[0-9]+}} 0, i{{[0-9]+}} 1
  // CHECK: [[GLOBAL_RED1_PTR:%.+]] = getelementptr inbounds [[TEAM3_REDUCE_TY]], [[TEAM3_REDUCE_TY]]* [[GLOBAL]], i{{[0-9]+}} 0, i{{[0-9]+}} 1
  // CHECK: [[GLOBAL_RED1_IDX_PTR:%.+]] = getelementptr inbounds [{{1024|2048}} x i16], [{{1024|2048}} x i16]* [[GLOBAL_RED1_PTR]], i{{[0-9]+}} 0, i32 [[IDX]]
  // CHECK: [[GLOBAL_RED1_IDX_PTR_BC:%.+]] = bitcast i16* [[GLOBAL_RED1_IDX_PTR]] to i8*
  // CHECK: store i8* [[GLOBAL_RED1_IDX_PTR_BC]], i8** [[LOCAL_RL_RED1_PTR]]
  // CHECK: [[LOCAL_RL_BC:%.+]] = bitcast [2 x i8*]* [[LOCAL_RL]] to i8*
  // CHECK: [[RL_BC:%.+]] = load i8*, i8** [[RL_PTR]],
  // CHECK: call void [[REDUCTION_FUNC]](i8* [[RL_BC]], i8* [[LOCAL_RL_BC]])
  // CHECK: ret void

#endif
