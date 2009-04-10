// RUN: clang-cc -emit-pch -triple=i686-apple-darwin9 -o %t %S/variables.h &&
// RUN: clang-cc -triple=i686-apple-darwin9 -include-pch %t -fsyntax-only -verify %s 

int *ip2 = &x;
float *fp = &ip; // expected-warning{{incompatible pointer types}}
// FIXME:variables.h expected-note{{previous}}
double z; // expected-error{{redefinition}}

//double VeryHappy; // FIXME: xpected-error{{redefinition}}

