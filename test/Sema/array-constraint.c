// RUN: clang -parse-ast-check %s

struct s; 
struct s* t (struct s z[]) {   // expected-error {{array has incomplete element type}}
  return z;                    // expected-error {{incompatible pointer type returning}}
}

void *k (void l[2]) {          // expected-error {{array has incomplete element type}}
  return l; 
}

