// RUN: %clang_cc1 -rewrite-objc %s -o -
// radar 7490331

@interface Foo {
        int a;
        id b;
}
- (void)bar;
- (void)baz:(id)q;
@end

@implementation Foo
- (void)bar {
        a = 42;
        [self baz:b];
}
- (void)baz:(id)q {
}
@end

