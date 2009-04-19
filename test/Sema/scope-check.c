// RUN: clang-cc -fsyntax-only -verify -std=gnu99 %s

int test1(int x) {
  goto L;    // expected-error{{illegal goto into protected scope}}
  int a[x];  // expected-note {{jump bypasses initialization of variable length array}}
  int b[x];  // expected-note {{jump bypasses initialization of variable length array}}
  L:
  return sizeof a;
}

int test2(int x) {
  goto L;            // expected-error{{illegal goto into protected scope}}
  typedef int a[x];  // expected-note {{jump bypasses initialization of VLA typedef}}
  L:
  return sizeof(a);
}

void test3clean(int*);

int test3() {
  goto L;            // expected-error{{illegal goto into protected scope}}
int a __attribute((cleanup(test3clean))); // expected-note {{jump bypasses initialization of declaration with __attribute__((cleanup))}}
L:
  return a;
}

int test4(int x) {
  goto L;       // expected-error{{illegal goto into protected scope}}
int a[x];       // expected-note {{jump bypasses initialization of variable length array}}
  test4(x);
L:
  return sizeof a;
}

int test5(int x) {
  int a[x];
  test5(x);
  goto L;  // Ok.
L:
  goto L;  // Ok.
  return sizeof a;
}

int test6() { 
  // just plain invalid.
  goto x;  // expected-error {{use of undeclared label 'x'}}
}

void test7(int x) {
  switch (x) {
  case 1: ;
    int a[x];       // expected-note {{jump bypasses initialization of variable length array}}
  case 2:           // expected-error {{illegal switch case into protected scope}}
    a[1] = 2;
    break;
  }
}

int test8(int x) {
  // For statement.
  goto L2;     // expected-error {{illegal goto into protected scope}}
  for (int arr[x];   // expected-note {{jump bypasses initialization of variable length array}}  
       ; ++x)
    L2:;

  // Statement expressions.
  goto L3;   // expected-error {{illegal goto into protected scope}}
  int Y = ({  int a[x];   // expected-note {{jump bypasses initialization of variable length array}}  
           L3: 4; });
  
  goto L4; // expected-error {{illegal goto into protected scope}}
  {
    int A[x],  // expected-note {{jump bypasses initialization of variable length array}}
        B[x];  // expected-note {{jump bypasses initialization of variable length array}}
  L4: ;
  }
  
  {
  L5: ;// ok
    int A[x], B = ({ if (x)
                       goto L5;
                     else 
                       goto L6;
                   4; }); 
  L6:; // ok.
    if (x) goto L6; // ok
  }
  
  {
  L7: ;// ok
    int A[x], B = ({ if (x)
                       goto L7;
                     else 
                       goto L8;  // expected-error {{illegal goto into protected scope}}
                     4; }),
        C[x];   // expected-note {{jump bypasses initialization of variable length array}}
  L8:; // bad
  }
 
  {
  L9: ;// ok
    int A[({ if (x)
               goto L9;
             else
               // FIXME:
               goto L10;  // fixme-error {{illegal goto into protected scope}}
           4; })];
  L10:; // bad
  }
  
  {
    // FIXME: Crashes goto checker.
    //goto L11;// ok
    //int A[({   L11: 4; })];
  }
  
  {
    goto L12;
    
    int y = 4;   // fixme-warn: skips initializer.
  L12:
    ;
  }
  
  // Statement expressions 2.
  goto L1;     // expected-error {{illegal goto into protected scope}}
  return x == ({
                 int a[x];   // expected-note {{jump bypasses initialization of variable length array}}  
               L1:
                 42; });
}

void test9(int n, void *P) {
  int Y;
  int Z = 4;
  goto *P;  // ok.

L2: ;
  int a[n];  // expected-note {{jump bypasses initialization of variable length array}}

L3:
  goto *P;  // expected-error {{illegal indirect goto in protected scope, unknown effect on scopes}}
  
  void *Ptrs[] = {
    &&L2,
    &&L3   // FIXME: Not Ok.
  };
}

