// RUN: clang -parse-ast-check %s

extern void funcInt(int);
extern void funcFloat(float);
extern void funcDouble(double);
// figure out why "char *" doesn't work (with gcc, nothing to do with clang)
//extern void funcCharPtr(char *);

#define func(expr) \
  ({ \
    typeof(expr) tmp; \
    if (__builtin_types_compatible_p(typeof(expr), int)) funcInt(tmp); \
    else if (__builtin_types_compatible_p(typeof(expr), float)) funcFloat(tmp); \
    else if (__builtin_types_compatible_p(typeof(expr), double)) funcDouble(tmp); \
  })
#define func_choose(expr) \
  __builtin_choose_expr(__builtin_types_compatible_p(typeof(expr), int), funcInt(expr), \
    __builtin_choose_expr(__builtin_types_compatible_p(typeof(expr), float), funcFloat(expr), \
      __builtin_choose_expr(__builtin_types_compatible_p(typeof(expr), double), funcDouble(expr), \
  (void)0)))

static void test()
{
  int a;
  float b;
  double d;

  func(a);
  func(b);
  func(d);
  func_choose(a);
  func_choose(b);
  func_choose(d);

  int c; 
  struct xx { int a; } x, y;
  
  c = __builtin_choose_expr(a+3-7, b, x); // expected-error{{'__builtin_choose_expr' requires a constant expression}}
  c = __builtin_choose_expr(0, b, x); // expected-error{{incompatible types assigning 'struct xx' to 'int'}}
  c = __builtin_choose_expr(5+3-7, b, x);
  y = __builtin_choose_expr(4+3-7, b, x);

}

