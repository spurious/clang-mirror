// RUN: %clang_cc1 %s -triple=x86_64-linux-gnu -S -verify -o - -D NEED_SSE41
// RUN: %clang_cc1 %s -triple=x86_64-linux-gnu -S -verify -o - -D NEED_AVX_1
// RUN: %clang_cc1 %s -triple=x86_64-linux-gnu -S -verify -o - -D NEED_AVX_2
// RUN: %clang_cc1 %s -triple=x86_64-linux-gnu -S -verify -o - -D NEED_AVX_3
// RUN: %clang_cc1 %s -triple=x86_64-linux-gnu -S -verify -o - -D NEED_AVX_4

#define __MM_MALLOC_H
#include <x86intrin.h>

// Really, this needs AVX, but because targetting AVX includes all the SSE features too, and
// features are sorted by hash function, and we just return the first missing feature, then we end
// up returning the subfeature sse4.1 instead of avx.
#if NEED_SSE41
int baz(__m256i a) {
  return _mm256_extract_epi32(a, 3); // expected-error {{always_inline function '_mm256_extract_epi32' requires target feature 'sse4.1', but would be inlined into function 'baz' that is compiled without support for 'sse4.1'}}
}
#endif

#if NEED_AVX_1
__m128 need_avx(__m128 a, __m128 b) {
  return _mm_cmp_ps(a, b, 0); // expected-error {{'__builtin_ia32_cmpps' needs target feature avx}}
}
#endif

#if NEED_AVX_2
__m128 need_avx(__m128 a, __m128 b) {
  return _mm_cmp_ss(a, b, 0); // expected-error {{'__builtin_ia32_cmpss' needs target feature avx}}
}
#endif

#if NEED_AVX_3
__m128d need_avx(__m128d a, __m128d b) {
  return _mm_cmp_pd(a, b, 0); // expected-error {{'__builtin_ia32_cmppd' needs target feature avx}}
}
#endif

#if NEED_AVX_4
__m128d need_avx(__m128d a, __m128d b) {
  return _mm_cmp_sd(a, b, 0); // expected-error {{'__builtin_ia32_cmpsd' needs target feature avx}}
}
#endif
