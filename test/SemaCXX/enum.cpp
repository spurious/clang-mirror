// RUN: clang -fsyntax-only -verify %s
enum E {
  Val1,
  Val2
};

int& enumerator_type(int);
float& enumerator_type(E);

void f() {
  E e = Val1;
  float& fr = enumerator_type(Val2);
}

// <rdar://problem/6502934>
typedef enum Foo {
	A = 0,
	B = 1
} Foo;
	
	
void bar() {
	Foo myvar = A;
	myvar = B;
}

/// PR3688
struct s1 {
  enum e1 (*bar)(void); // expected-error{{ISO C++ forbids forward references to 'enum' types}}
};

enum e1 { YES, NO };

static enum e1 badfunc(struct s1 *q) {
  // FIXME: the message below should probably give context information
  // in those types.
  return q->bar(); // expected-error{{incompatible type returning 'enum e1', expected 'enum e1'}}
}

enum e2; // expected-error{{ISO C++ forbids forward references to 'enum' types}}
