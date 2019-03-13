// RUN: %clang_cc1 -fsyntax-only -verify %s
// expected-no-diagnostics

#ifndef __BIGGEST_ALIGNMENT__
#error __BIGGEST_ALIGNMENT__ not defined
#endif

#include <stddef.h>

_Static_assert(__BIGGEST_ALIGNMENT__ == _Alignof(max_align_t), "");
