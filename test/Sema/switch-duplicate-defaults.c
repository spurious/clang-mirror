// RUN: clang -parse-ast-check %s

void f (int z) { 
  switch(z) {
      default:  // expected-error {{first label is here}}
      default:  // expected-error {{multiple default labels in one switch}}
        break;
  }
} 

