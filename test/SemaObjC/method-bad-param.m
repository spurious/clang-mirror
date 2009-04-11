// RUN: clang-cc -fsyntax-only -verify %s

@interface foo
@end

@implementation foo
@end

@interface bar
-(void) my_method:(foo) my_param; // expected-error {{Objective-C interface type 'foo' cannot be passed by value}}
- (foo)cccccc:(long)ddddd;  // expected-error {{Objective-C interface type 'foo' cannot be returned by value}}
@end

@implementation bar
-(void) my_method:(foo) my_param  // expected-error {{Objective-C interface type 'foo' cannot be passed by value}}
{
}
- (foo)cccccc:(long)ddddd // expected-error {{Objective-C interface type 'foo' cannot be returned by value}}
{
}
@end

void somefunc(foo x) {} // expected-error {{Objective-C interface type 'foo' cannot be passed by value}}
