// RUN: clang -fsyntax-only %s

void test1() {
  if (sizeof (int){ 1});   // sizeof compound literal
  if (sizeof (int));       // sizeof type

  (int)4;   // cast.
  (int){4}; // compound literal.

  // FIXME: change this to the struct version when we can.
  //int A = (struct{ int a;}){ 1}.a;
  int A = (int){ 1}.a;
}

int test2(int a, int b) {
  return a ? a,b : a;
}

int test3(int a, int b) {
  return a = b = c;
}
