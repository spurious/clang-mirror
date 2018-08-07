// REQUIRES: amdgpu-registered-target
// RUN: %clang_cc1 -triple amdgcn-- -target-cpu tahiti -verify -S -o - %s

void test_ci_s_dcache_inv_vol()
{
  __builtin_amdgcn_s_dcache_inv_vol(); // expected-error {{'__builtin_amdgcn_s_dcache_inv_vol' needs target feature ci-insts}}
}
