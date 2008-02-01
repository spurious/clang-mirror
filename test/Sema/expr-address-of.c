// RUN: clang %s -verify -fsyntax-only
struct entry { int value; };
void add_one(int *p) { (*p)++; }

void test() {
 register struct entry *p;
 add_one(&p->value);
}

void foo() {
  register int x[10];
  &x[10];              // expected-error {{address of register variable requested}}
    
  register int *y;
  
  int *x2 = &y; // expected-error {{address of register variable requested}}
  int *x3 = &y[10];
}


