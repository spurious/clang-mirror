//RUN: clang -fsyntax-only -verify %s

#include <stdarg.h>

void a(const char *a, ...) __attribute__((format(printf, 1,2))); // no-error
void b(const char *a, ...) __attribute__((format(printf, 1,1))); // expected-error {{'format' attribute parameter 3 is out of bounds}}
void c(const char *a, ...) __attribute__((format(printf, 0,2))); // expected-error {{'format' attribute parameter 2 is out of bounds}}
void d(const char *a, int c) __attribute__((format(printf, 1,2))); // expected-error {{format attribute requires variadic function}}
void e(char *str, int c, ...) __attribute__((format(printf, 2,3))); // expected-error {{format argument not a string type}}

typedef const char* xpto;
void f(xpto c, va_list list) __attribute__((format(printf, 1, 0))); // no-error
void g(xpto c) __attribute__((format(printf, 1, 0))); // no-error

void y(char *str) __attribute__((format(strftime, 1,0))); // no-error
void z(char *str, int c, ...) __attribute__((format(strftime, 1,2))); // expected-error {{strftime format attribute requires 3rd parameter to be 0}}
