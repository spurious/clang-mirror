// RUN: clang %s -emit-llvm

// PR1895
// sizeof function
int zxcv(void);
int x=sizeof(zxcv);
int y=__alignof__(zxcv);


void *test(int *i) {
 short a = 1;
 i += a;
 i + a;
 a + i;
}

_Bool test2b; 
int test2() {if (test2b);}

// PR1921
int test3() {
  const unsigned char *bp;
  bp -= (short)1;
}

