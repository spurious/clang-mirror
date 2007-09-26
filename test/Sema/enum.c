// RUN: clang %s -parse-ast -verify -pedantic

enum e {A, 
        B = 42LL << 32,        // expected-warning {{ISO C restricts enumerator values to range of 'int'}}
      C = -4, D = 12456 };

enum f { a = -2147483648, b = 2147483647 }; // ok.

enum g {  // too negative
   c = -2147483649,         // expected-warning {{ISO C restricts enumerator values to range of 'int'}}
   d = 2147483647 };
enum h { e = -2147483648, // too pos
   f = 2147483648           // expected-warning {{ISO C restricts enumerator values to range of 'int'}}
}; 

// minll maxull
enum x                      // expected-warning {{enumeration values exceed range of largest integer}}
{ y = -9223372036854775807LL-1,  // expected-warning {{ISO C restricts enumerator values to range of 'int'}}
z = 9223372036854775808ULL };    // expected-warning {{ISO C restricts enumerator values to range of 'int'}}

int test() {
  return sizeof(enum e) ;
}

