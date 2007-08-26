// RUN: clang -parse-ast-check %s

int foo(int X, int Y);

void bar(volatile int *VP, int *P, int A,
         _Complex double C, volatile _Complex double VC) {
  
  VP == P;             // expected-warning {{expression result unused}} \
                          expected-warning {{comparison}}
  (void)A;
  (void)foo(1,2);      // no warning.
  
  A == foo(1, 2);      // expected-warning {{expression result unused}}

  foo(1,2)+foo(4,3);   // expected-warning {{expression result unused}}


  *P;                  // expected-warning {{expression result unused}}
  *VP;                 // no warning.
  P[4];                // expected-warning {{expression result unused}}
  VP[4];               // no warning.

  // FIXME: SEMA explodes on these.
  //__real__ C;
  //__real__ VC;
}

extern void t1();
extern void t2();
void t3(int c) {
  c ? t1() : t2();
}

