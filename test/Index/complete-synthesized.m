// Note: this test is line- and column-sensitive. Test commands are at
// the end.


@interface A
@property int prop1;
@end

@interface B : A {
  float _prop2;
}
@property float prop2;
@property short prop3;
@end

@interface B ()
@property double prop4;
@end

@implementation B
@synthesize prop2 = _prop2;

- (int)method {
  return _prop2;
}

@dynamic prop3;

- (short)method2 {
  return prop4;
}

- (short)method3 {
  return prop3;
}
@end

// RUN: c-index-test -code-completion-at=%s:24:1 -Xclang -fobjc-nonfragile-abi -fobjc-default-synthesize-properties %s | FileCheck %s
// RUN: c-index-test -code-completion-at=%s:30:2 -Xclang -fobjc-nonfragile-abi -fobjc-default-synthesize-properties %s | FileCheck %s
// RUN: c-index-test -code-completion-at=%s:34:2 -Xclang -fobjc-nonfragile-abi -fobjc-default-synthesize-properties %s | FileCheck %s

// CHECK: NotImplemented:{TypedText _Bool} (50)
// CHECK: ObjCIvarDecl:{ResultType float}{TypedText _prop2} (35)
// CHECK-NOT: prop2
// CHECK-NOT: prop3
// CHECK: ObjCIvarDecl:{ResultType double}{TypedText prop4} (37)
