// RUN: clang -parse-ast-check %s -pedantic

static __inline void __attribute__((__always_inline__, __nodebug__)) // expected-warning {{extension used}}
foo (void)
{
}
