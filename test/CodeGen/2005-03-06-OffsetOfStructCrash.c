// RUN: %clang_cc1 %s -emit-llvm -o -

struct Y {};
struct XXX {
  struct  Y F;
};

void test1() {
   (int)&((struct XXX*)(((void *)0)))->F;
}

void test2() {
   &((struct XXX*)(((void *)0)))->F;
}
