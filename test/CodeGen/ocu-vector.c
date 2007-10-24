// RUN: clang -emit-llvm %s

typedef __attribute__(( ocu_vector_type(4) )) float float4;
//typedef __attribute__(( ocu_vector_type(3) )) float float3;
typedef __attribute__(( ocu_vector_type(2) )) float float2;


float4 test1(float4 V) {
  return V.wzyx+V;
}

float2 vec2, vec2_2;
float4 vec4, vec4_2;
float f;

static void test2() {
    vec2 = vec4.rg;  // shorten
    f = vec2.x;      // extract elt
    vec4 = vec4.yyyy;  // splat
    
    vec2.x = f;      // insert one.
    vec2.yx = vec2; // reverse
}

static void test3(float4 *out) {
  *out = ((float4) {1.0f, 2.0f, 3.0f, 4.0f });
}

static void test4(float4 *out) {
  float a = 1.0f;
  float b = 2.0f;
  float c = 3.0f;
  float d = 4.0f;
  *out = ((float4) {a,b,c,d});
}
