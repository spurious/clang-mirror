// RUN: clang %s -parse-ast -verify

void bar (void *); 
void f11 (z)       // expected-error {{may not have 'void' type}}
void z; 
{ bar (&z); }
