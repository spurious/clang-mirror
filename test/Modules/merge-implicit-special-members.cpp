// RUN: rm -rf %t
// RUN: %clang_cc1 -fmodules -fmodules-cache-path=%t -I%S/Inputs/merge-implicit-special-members -verify %s
// expected-no-diagnostics
#include "c.h"
int n = pthread_mutex_t().lock;
