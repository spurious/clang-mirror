// RUN: clang -fsyntax-only -verify -pedantic %s

char *funk(int format);
enum Test {A=-1};
char *funk(enum Test x);

int eli(float b); // expected-error {{previous declaration is here}}
int b(int c) {return 1;}

int foo();
int foo()
{
    int eli(int (int)); // expected-error {{conflicting types for 'eli'}}
    eli(b);
	return 0;	
}

int bar();
int bar(int i) // expected-error {{previous definition is here}}
{
	return 0;
}
int bar() // expected-error {{redefinition of 'bar'}} expected-error {{conflicting types for 'bar'}}
{
	return 0;
}

int foobar(int); // expected-error {{previous declaration is here}}
int foobar() // expected-error {{conflicting types for 'foobar'}}
{
	return 0;
}

int wibble(); // expected-error {{previous declaration is here}}
float wibble() // expected-error {{conflicting types for 'wibble'}}
{
	return 0.0f;
}
