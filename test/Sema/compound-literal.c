// RUN: clang -fsyntax-only -verify -pedantic %s

struct foo { int a, b; };

static struct foo t = (struct foo){0,0};
static struct foo t2 = {0,0}; 
static struct foo t3 = t2; // -expected-error {{initializer element is not a compile-time constant}}
static int *p = (int []){2,4}; 
static int x = (int){1}; // -expected-warning{{braces around scalar initializer}}

static int *p2 = (int []){2,x}; // -expected-error {{initializer element is not a compile-time constant}}
static int *p3 = (int []){2,"x"}; // -expected-warning {{incompatible pointer to integer conversion initializing 'char [2]', expected 'int'}}

typedef struct { } cache_t; // -expected-warning{{use of empty struct extension}}
static cache_t clo_I1_cache = ((cache_t) { } ); // -expected-warning{{use of GNU empty initializer extension}}

typedef struct Test {int a;int b;} Test;
static Test* ll = &(Test) {0,0};

extern void fooFunc(struct foo *pfoo);

int main(int argc, char **argv) {
 int *l = (int []){x, *p, *p2};
 fooFunc(&(struct foo){ 1, 2 });
}

struct Incomplete;
struct Incomplete* I1 = &(struct Incomplete){1, 2, 3}; // -expected-error {{variable has incomplete type}}
void IncompleteFunc(unsigned x) {
  struct Incomplete* I2 = (struct foo[x]){1, 2, 3}; // -expected-error {{variable-sized object may not be initialized}}
  (void){1,2,3}; // -expected-error {{variable has incomplete type}}
}
