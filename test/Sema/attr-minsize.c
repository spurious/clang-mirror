// RUN: %clang_cc1 -fsyntax-only -verify %s

int foo() __attribute__((__minsize__));

int var1 __attribute__((__minsize__)); // expected-error{{'__minsize__' attribute only applies to functions and methods}}
