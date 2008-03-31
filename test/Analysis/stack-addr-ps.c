// RUN: clang -grsimple -verify %s

int* f1() {
  int x = 0;
  return &x; // expected-warning{{Address of stack-allocated variable returned.}} expected-warning{{address of stack memory associated with local variable 'x' returned}}
}

int* f2(int y) {
  return &y;  // expected-warning{{Address of stack-allocated variable returned.}} expected-warning{{address of stack memory associated with local variable 'y' returned}}
}

int* f3(int x, int *y) {
  int w = 0;
  
  if (x)
    y = &w;
    
  return y; // expected-warning{{Address of stack-allocated variable returned.}}
}


