// RUN: clang-cc %s -fsyntax-only -verify

// PR3310
struct a x1; // expected-note 2{{forward declaration of 'struct a'}}
static struct a x2; // expected-error{{variable has incomplete type 'struct a'}}
struct a x3[10]; // expected-error{{array has incomplete element type 'struct a'}}
struct a {int x;};
static struct a x2_okay;
struct a x3_okay[10];
struct b x4; // expected-error{{tentative definition has type 'struct b' that is never completed}} \
            // expected-note{{forward declaration of 'struct b'}}

const int a [1] = {1};
extern const int a[];

extern const int b[];
const int b [1] = {1};

extern const int c[] = {1}; // expected-warning{{'extern' variable has an initializer}}
const int c[];

int i1 = 1; // expected-note {{previous definition is here}}
int i1 = 2; // expected-error {{redefinition of 'i1'}}
int i1;
int i1;
extern int i1; // expected-note {{previous definition is here}}
static int i1; // expected-error{{static declaration of 'i1' follows non-static declaration}}

static int i2 = 5; // expected-note 1 {{previous definition is here}}
int i2 = 3; // expected-error{{non-static declaration of 'i2' follows static declaration}}

static int i3 = 5;
extern int i3;

__private_extern__ int pExtern;
int pExtern = 0;

int i4;
int i4;
extern int i4;

int (*pToArray)[];
int (*pToArray)[8];

int redef[10];
int redef[];  // expected-note {{previous definition is here}}
int redef[11]; // expected-error{{redefinition of 'redef'}}

void func() {
  extern int i1; // expected-note {{previous definition is here}}
  static int i1; // expected-error{{static declaration of 'i1' follows non-static declaration}}
}

void func2(void)
{
  extern double *p;
  extern double *p;
}

