// RUN: clang -fsyntax-only -verify %s

struct S;
typedef int FOO();

@interface INTF
{
	struct F {} JJ;
	int arr[];  // expected-error {{field 'arr' has incomplete type}}
	struct S IC;  // expected-error {{field 'IC' has incomplete type}}
	struct T { // expected-note {{previous definition is here}}
	  struct T {} X;  // expected-error {{nested redefinition of 'T'}}
	}YYY; 
	FOO    BADFUNC;  // expected-error {{field 'BADFUNC' declared as a function}}
	int kaka;	// expected-note {{previous definition is here}}
	int kaka;	// expected-error {{duplicate member 'kaka'}}
	char ch[];	// expected-error {{field 'ch' has incomplete type}}
}
@end
