// RUN: clang -warn-dead-stores -verify %s

void f1() {
  int k, y;
  int abc=1;
  long idx=abc+3*5; // expected-warning {{value stored to variable is never used}}
}

void f2(void *b) {
 char *c = (char*)b; // no-warning
 char *d = b+1; // expected-warning {{value stored to variable is never used}}
 printf("%s", c);
}

void f3() {
  int r;
  if ((r = f()) != 0) { // no-warning
    int y = r; // no-warning
    printf("the error is: %d\n", y);
  }
}

void f4(int k) {
  
  k = 1;
  
  if (k)
    f1();
    
  k = 2;  // expected-warning {{value stored to variable is never used}}
}
