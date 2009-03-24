// RUN: clang-cc -fsyntax-only -verify %s

@interface Super @end
Super s1; // expected-error{{Objective-C type cannot be statically allocated}}

extern Super e1; // expected-error{{Objective-C type cannot be statically allocated}}

struct S {
  Super s1; // expected-error{{Objective-C type cannot be statically allocated}}
};

@protocol P1 @end

@interface INTF
{
  Super ivar1; // expected-error{{Objective-C type cannot be statically allocated}}
}
@end

struct whatever {
  Super objField; // expected-error{{Objective-C type cannot be statically allocated}}
};

@interface MyIntf
{
  Super<P1> ivar1; // expected-error{{Objective-C type cannot be statically allocated}}
}
@end

Super foo(Super parm1) { // expected-error{{Objective-C type cannot be passed by value}}
	Super p1; // expected-error{{Objective-C type cannot be statically allocated}}
	return p1;
}
