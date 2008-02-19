/* RUN: clang %s -std=c89 -pedantic -fsyntax-only -verify
 */
void test1() {
  {
    int i;
    i = i + 1;
    int j;          /* expected-warning {{mixing declarations and code}} */
  }
  {
    __extension__ int i;
    i = i + 1;
    int j;          /* expected-warning {{mixing declarations and code}} */
  }
  {
    int i;
    i = i + 1;
    __extension__ int j; /* expected-warning {{mixing declarations and code}} */
  }
}

long long test2;   /* expected-warning {{extension}} */


void test3(int i) {
  int A[i];        /* expected-warning {{variable length array}} */
}

int test4 = 0LL;		/* expected-warning {{long long}} */

/* PR1999 */
void test5(register);

/* PR2041 */
int *restrict;
int *__restrict;  /* expected-error {{expected identifier}} */
