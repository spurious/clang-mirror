// RUN: %clang_cc1 -emit-llvm -o - %s | FileCheck %s

typedef float float4 __attribute__((ext_vector_type(4)));

struct __attribute__((packed, aligned(4))) struct1 {
  float4 position;
};
int x = __alignof(struct struct1);

float4 f(struct struct1* x) { return x->position; }

void func(struct struct1* p, float *a, float *b, float c) {
  p->position.x = c;
  *a = p->position.y;
  *b = p->position[0];
  p->position[2] = c;
  // FIXME: We should be able to come up with a more aggressive alignment
  // estimate.
  // CHECK: @func
  // CHECK: load <4 x float>* {{%.*}}, align 1
  // CHECK: store <4 x float> {{%.*}}, <4 x float>* {{%.*}}, align 1
  // CHECK: load <4 x float>* {{%.*}}, align 1
  // CHECK: load <4 x float>* {{%.*}}, align 1
  // CHECK: load <4 x float>* {{%.*}}, align 1
  // CHECK: store <4 x float> {{%.*}}, <4 x float>* {{%.*}}, align 1
  // CHECK: ret void
}
