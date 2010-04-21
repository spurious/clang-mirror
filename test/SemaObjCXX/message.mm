// RUN: %clang_cc1 -fsyntax-only -verify %s
@interface I1
- (int*)method;
@end

@implementation I1
- (int*)method {
  struct x { };
  [x method]; // expected-error{{receiver type 'x' is not an Objective-C class}}
  return 0;
}
@end

typedef struct { int x; } ivar;

@interface I2 {
  id ivar;
}
- (int*)method;
+ (void)method;
@end

struct I2_holder {
  I2_holder();

  I2 *get();
};

I2 *operator+(I2_holder, int);

@implementation I2
- (int*)method {
  [ivar method];

  // Test instance messages that start with a simple-type-specifier.
  [I2_holder().get() method];
  [I2_holder().get() + 17 method];
  return 0;
}
+ (void)method {
  [ivar method]; // expected-error{{receiver type 'ivar' (aka 'ivar') is not an Objective-C class}}
}
@end

// Class message sends
@interface I3
+ (int*)method;
@end

@interface I4 : I3
+ (int*)otherMethod;
@end

template<typename T>
struct identity {
  typedef T type;
};

@implementation I4
+ (int *)otherMethod {
  // Test class messages that use non-trivial simple-type-specifiers
  // or typename-specifiers.
  if (false) {
    if (true)
      return [typename identity<I3>::type method];

    return [::I3 method];
  }

  int* ip1 = {[super method]};
  int* ip2 = {[::I3 method]};
  int* ip3 = {[typename identity<I3>::type method]};
  int* ip4 = {[typename identity<I2_holder>::type().get() method]};
  int array[5] = {[3] = 2};
  return [super method];
}
@end
