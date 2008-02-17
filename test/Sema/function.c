// RUN: clang %s -fsyntax-only -verify -pedantic
// PR1892
void f(double a[restrict][5]);  // should promote to restrict ptr.
void f(double (* restrict a)[5]);

int foo (__const char *__path);
int foo(__const char *__restrict __file);

void func(const char*); //expected-error{{previous declaration is here}}
void func(char*); //expected-error{{conflicting types for 'func'}}

void g(int (*)(const void **, const void **));
void g(int (*compar)()) {
}

void h();  //expected-error{{previous declaration is here}}
void h (const char *fmt, ...) {} //expected-error{{conflicting types for 'h'}}

// PR1965
int t5(b);          // expected-error {{parameter list without types}}
int t6(int x, g);   // expected-warning {{type specifier required for parameter 'g'}}

int t7(, );       // expected-error {{expected parameter declarator}} expected-error {{expected parameter declarator}}
int t8(, int a);  // expected-error {{expected parameter declarator}}
int t9(int a, );  // expected-error {{expected parameter declarator}}


// PR2042
void t10(){}
void t11(){t10(1);}

