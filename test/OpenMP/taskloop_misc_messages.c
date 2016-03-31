// RUN: %clang_cc1 -fsyntax-only -fopenmp -triple x86_64-unknown-unknown -verify %s

// expected-error@+1 {{unexpected OpenMP directive '#pragma omp taskloop'}}
#pragma omp taskloop

// expected-error@+1 {{unexpected OpenMP directive '#pragma omp taskloop'}}
#pragma omp taskloop foo

void test_no_clause() {
  int i;
#pragma omp taskloop
  for (i = 0; i < 16; ++i)
    ;

// expected-error@+2 {{statement after '#pragma omp taskloop' must be a for loop}}
#pragma omp taskloop
  ++i;
}

void test_branch_protected_scope() {
  int i = 0;
L1:
  ++i;

  int x[24];

#pragma omp parallel
#pragma omp taskloop
  for (i = 0; i < 16; ++i) {
    if (i == 5)
      goto L1; // expected-error {{use of undeclared label 'L1'}}
    else if (i == 6)
      return; // expected-error {{cannot return from OpenMP region}}
    else if (i == 7)
      goto L2;
    else if (i == 8) {
    L2:
      x[i]++;
    }
  }

  if (x[0] == 0)
    goto L2; // expected-error {{use of undeclared label 'L2'}}
  else if (x[1] == 1)
    goto L1;
}

void test_invalid_clause() {
  int i;
#pragma omp parallel
// expected-warning@+1 {{extra tokens at the end of '#pragma omp taskloop' are ignored}}
#pragma omp taskloop foo bar
  for (i = 0; i < 16; ++i)
    ;
// expected-error@+1 {{directive '#pragma omp taskloop' cannot contain more than one 'nogroup' clause}}
#pragma omp taskloop nogroup nogroup
  for (i = 0; i < 16; ++i)
    ;
}

void test_non_identifiers() {
  int i, x;

#pragma omp parallel
// expected-warning@+1 {{extra tokens at the end of '#pragma omp taskloop' are ignored}}
#pragma omp taskloop;
  for (i = 0; i < 16; ++i)
    ;
// expected-warning@+3 {{extra tokens at the end of '#pragma omp taskloop' are ignored}}
// expected-error@+2 {{unexpected OpenMP clause 'linear' in directive '#pragma omp taskloop'}}
#pragma omp parallel
#pragma omp taskloop linear(x);
  for (i = 0; i < 16; ++i)
    ;

#pragma omp parallel
// expected-warning@+1 {{extra tokens at the end of '#pragma omp taskloop' are ignored}}
#pragma omp taskloop private(x);
  for (i = 0; i < 16; ++i)
    ;

#pragma omp parallel
// expected-warning@+1 {{extra tokens at the end of '#pragma omp taskloop' are ignored}}
#pragma omp taskloop, private(x);
  for (i = 0; i < 16; ++i)
    ;
}

extern int foo();

void test_collapse() {
  int i;
#pragma omp parallel
// expected-error@+1 {{expected '('}}
#pragma omp taskloop collapse
  for (i = 0; i < 16; ++i)
    ;
#pragma omp parallel
// expected-error@+1 {{expected expression}} expected-error@+1 {{expected ')'}} expected-note@+1 {{to match this '('}}
#pragma omp taskloop collapse(
  for (i = 0; i < 16; ++i)
    ;
#pragma omp parallel
// expected-error@+1 {{expected expression}}
#pragma omp taskloop collapse()
  for (i = 0; i < 16; ++i)
    ;
#pragma omp parallel
// expected-error@+1 {{expected expression}} expected-error@+1 {{expected ')'}} expected-note@+1 {{to match this '('}}
#pragma omp taskloop collapse(,
  for (i = 0; i < 16; ++i)
    ;
#pragma omp parallel
// expected-error@+1 {{expected expression}}  expected-error@+1 {{expected ')'}} expected-note@+1 {{to match this '('}}
#pragma omp taskloop collapse(, )
  for (i = 0; i < 16; ++i)
    ;
#pragma omp parallel
// expected-warning@+2 {{extra tokens at the end of '#pragma omp taskloop' are ignored}}
// expected-error@+1 {{expected '('}}
#pragma omp taskloop collapse 4)
  for (i = 0; i < 16; ++i)
    ;
#pragma omp parallel
// expected-error@+2 {{expected ')'}}
// expected-note@+1 {{to match this '('}} expected-note@+1 {{as specified in 'collapse' clause}}
#pragma omp taskloop collapse(4
  for (i = 0; i < 16; ++i)
    ; // expected-error {{expected 4 for loops after '#pragma omp taskloop', but found only 1}}
#pragma omp parallel
// expected-error@+2 {{expected ')'}}
// expected-note@+1 {{to match this '('}} expected-note@+1 {{as specified in 'collapse' clause}}
#pragma omp taskloop collapse(4,
  for (i = 0; i < 16; ++i)
    ; // expected-error {{expected 4 for loops after '#pragma omp taskloop', but found only 1}}
#pragma omp parallel
// expected-error@+2 {{expected ')'}}
// expected-note@+1 {{to match this '('}} expected-note@+1 {{as specified in 'collapse' clause}}
#pragma omp taskloop collapse(4, )
  for (i = 0; i < 16; ++i)
    ; // expected-error {{expected 4 for loops after '#pragma omp taskloop', but found only 1}}
#pragma omp parallel
// expected-note@+1 {{as specified in 'collapse' clause}}
#pragma omp taskloop collapse(4)
  for (i = 0; i < 16; ++i)
    ; // expected-error {{expected 4 for loops after '#pragma omp taskloop', but found only 1}}
#pragma omp parallel
// expected-error@+2 {{expected ')'}}
// expected-note@+1 {{to match this '('}} expected-note@+1 {{as specified in 'collapse' clause}}
#pragma omp taskloop collapse(4 4)
  for (i = 0; i < 16; ++i)
    ; // expected-error {{expected 4 for loops after '#pragma omp taskloop', but found only 1}}
#pragma omp parallel
// expected-error@+2 {{expected ')'}}
// expected-note@+1 {{to match this '('}} expected-note@+1 {{as specified in 'collapse' clause}}
#pragma omp taskloop collapse(4, , 4)
  for (i = 0; i < 16; ++i)
    ; // expected-error {{expected 4 for loops after '#pragma omp taskloop', but found only 1}}
#pragma omp parallel
#pragma omp taskloop collapse(4)
  for (int i1 = 0; i1 < 16; ++i1)
    for (int i2 = 0; i2 < 16; ++i2)
      for (int i3 = 0; i3 < 16; ++i3)
        for (int i4 = 0; i4 < 16; ++i4)
          foo();
#pragma omp parallel
// expected-error@+2 {{expected ')'}}
// expected-note@+1 {{to match this '('}} expected-note@+1 {{as specified in 'collapse' clause}}
#pragma omp taskloop collapse(4, 8)
  for (i = 0; i < 16; ++i)
    ; // expected-error {{expected 4 for loops after '#pragma omp taskloop', but found only 1}}
#pragma omp parallel
// expected-error@+1 {{expression is not an integer constant expression}}
#pragma omp taskloop collapse(2.5)
  for (i = 0; i < 16; ++i)
    ;
#pragma omp parallel
// expected-error@+1 {{expression is not an integer constant expression}}
#pragma omp taskloop collapse(foo())
  for (i = 0; i < 16; ++i)
    ;
#pragma omp parallel
// expected-error@+1 {{argument to 'collapse' clause must be a strictly positive integer value}}
#pragma omp taskloop collapse(-5)
  for (i = 0; i < 16; ++i)
    ;
#pragma omp parallel
// expected-error@+1 {{argument to 'collapse' clause must be a strictly positive integer value}}
#pragma omp taskloop collapse(0)
  for (i = 0; i < 16; ++i)
    ;
#pragma omp parallel
// expected-error@+1 {{argument to 'collapse' clause must be a strictly positive integer value}}
#pragma omp taskloop collapse(5 - 5)
  for (i = 0; i < 16; ++i)
    ;
}

void test_private() {
  int i;
#pragma omp parallel
// expected-error@+2 {{expected expression}}
// expected-error@+1 {{expected ')'}} expected-note@+1 {{to match this '('}}
#pragma omp taskloop private(
  for (i = 0; i < 16; ++i)
    ;
#pragma omp parallel
// expected-error@+2 {{expected ')'}} expected-note@+2 {{to match this '('}}
// expected-error@+1 {{expected expression}}
#pragma omp taskloop private(,
  for (i = 0; i < 16; ++i)
    ;
#pragma omp parallel
// expected-error@+1 {{expected expression}}
#pragma omp taskloop private(, )
  for (i = 0; i < 16; ++i)
    ;
#pragma omp parallel
// expected-error@+1 {{expected expression}}
#pragma omp taskloop private()
  for (i = 0; i < 16; ++i)
    ;
#pragma omp parallel
// expected-error@+1 {{expected expression}}
#pragma omp taskloop private(int)
  for (i = 0; i < 16; ++i)
    ;
#pragma omp parallel
// expected-error@+1 {{expected variable name}}
#pragma omp taskloop private(0)
  for (i = 0; i < 16; ++i)
    ;

  int x, y, z;
#pragma omp parallel
#pragma omp taskloop private(x)
  for (i = 0; i < 16; ++i)
    ;
#pragma omp parallel
#pragma omp taskloop private(x, y)
  for (i = 0; i < 16; ++i)
    ;
#pragma omp parallel
#pragma omp taskloop private(x, y, z)
  for (i = 0; i < 16; ++i) {
    x = y * i + z;
  }
}

void test_lastprivate() {
  int i;
#pragma omp parallel
// expected-error@+2 {{expected ')'}} expected-note@+2 {{to match this '('}}
// expected-error@+1 {{expected expression}}
#pragma omp taskloop lastprivate(
  for (i = 0; i < 16; ++i)
    ;

#pragma omp parallel
// expected-error@+2 {{expected ')'}} expected-note@+2 {{to match this '('}}
// expected-error@+1 {{expected expression}}
#pragma omp taskloop lastprivate(,
  for (i = 0; i < 16; ++i)
    ;
#pragma omp parallel
// expected-error@+1 {{expected expression}}
#pragma omp taskloop lastprivate(, )
  for (i = 0; i < 16; ++i)
    ;
#pragma omp parallel
// expected-error@+1 {{expected expression}}
#pragma omp taskloop lastprivate()
  for (i = 0; i < 16; ++i)
    ;
#pragma omp parallel
// expected-error@+1 {{expected expression}}
#pragma omp taskloop lastprivate(int)
  for (i = 0; i < 16; ++i)
    ;
#pragma omp parallel
// expected-error@+1 {{expected variable name}}
#pragma omp taskloop lastprivate(0)
  for (i = 0; i < 16; ++i)
    ;

  int x, y, z;
#pragma omp parallel
#pragma omp taskloop lastprivate(x)
  for (i = 0; i < 16; ++i)
    ;
#pragma omp parallel
#pragma omp taskloop lastprivate(x, y)
  for (i = 0; i < 16; ++i)
    ;
#pragma omp parallel
#pragma omp taskloop lastprivate(x, y, z)
  for (i = 0; i < 16; ++i)
    ;
}

void test_firstprivate() {
  int i;
#pragma omp parallel
// expected-error@+2 {{expected ')'}} expected-note@+2 {{to match this '('}}
// expected-error@+1 {{expected expression}}
#pragma omp taskloop firstprivate(
  for (i = 0; i < 16; ++i)
    ;

#pragma omp parallel
// expected-error@+2 {{expected ')'}} expected-note@+2 {{to match this '('}}
// expected-error@+1 {{expected expression}}
#pragma omp taskloop firstprivate(,
  for (i = 0; i < 16; ++i)
    ;
#pragma omp parallel
// expected-error@+1 {{expected expression}}
#pragma omp taskloop firstprivate(, )
  for (i = 0; i < 16; ++i)
    ;
#pragma omp parallel
// expected-error@+1 {{expected expression}}
#pragma omp taskloop firstprivate()
  for (i = 0; i < 16; ++i)
    ;
#pragma omp parallel
// expected-error@+1 {{expected expression}}
#pragma omp taskloop firstprivate(int)
  for (i = 0; i < 16; ++i)
    ;
#pragma omp parallel
// expected-error@+1 {{expected variable name}}
#pragma omp taskloop firstprivate(0)
  for (i = 0; i < 16; ++i)
    ;

  int x, y, z;
#pragma omp parallel
#pragma omp taskloop lastprivate(x) firstprivate(x)
  for (i = 0; i < 16; ++i)
    ;
#pragma omp parallel
#pragma omp taskloop lastprivate(x, y) firstprivate(x, y)
  for (i = 0; i < 16; ++i)
    ;
#pragma omp parallel
#pragma omp taskloop lastprivate(x, y, z) firstprivate(x, y, z)
  for (i = 0; i < 16; ++i)
    ;
}

void test_loop_messages() {
  float a[100], b[100], c[100];
#pragma omp parallel
// expected-error@+2 {{variable must be of integer or pointer type}}
#pragma omp taskloop
  for (float fi = 0; fi < 10.0; fi++) {
    c[(int)fi] = a[(int)fi] + b[(int)fi];
  }
#pragma omp parallel
// expected-error@+2 {{variable must be of integer or pointer type}}
#pragma omp taskloop
  for (double fi = 0; fi < 10.0; fi++) {
    c[(int)fi] = a[(int)fi] + b[(int)fi];
  }

  // expected-warning@+2 {{OpenMP loop iteration variable cannot have more than 64 bits size and will be narrowed}}
  #pragma omp taskloop
  for (__int128 ii = 0; ii < 10; ii++) {
    c[ii] = a[ii] + b[ii];
  }
}

