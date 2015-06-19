// RUN: %clang_cc1 -fblocks -Werror=nullability-declspec -verify %s

struct X { };

__nullable int *ip1; // expected-error{{nullability specifier '__nullable' cannot be applied to non-pointer type 'int'; did you mean to apply the specifier to the pointer?}}
__nullable int (*fp1)(int); // expected-error{{nullability specifier '__nullable' cannot be applied to non-pointer type 'int'; did you mean to apply the specifier to the function pointer?}}
__nonnull int (^bp1)(int); // expected-error{{nullability specifier '__nonnull' cannot be applied to non-pointer type 'int'; did you mean to apply the specifier to the block pointer?}}
__nonnull int X::*pmd1; // expected-error{{nullability specifier '__nonnull' cannot be applied to non-pointer type 'int'; did you mean to apply the specifier to the member pointer?}}
__nonnull int (X::*pmf1)(int); // expected-error{{nullability specifier '__nonnull' cannot be applied to non-pointer type 'int'; did you mean to apply the specifier to the member function pointer?}}
