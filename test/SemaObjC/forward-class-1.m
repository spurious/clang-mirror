// RUN: %clang_cc1 -fsyntax-only -verify %s

@class FOO, BAR; // expected-note {{forward class is declared here}}
@class FOO, BAR;

@interface INTF : FOO	// expected-error {{attempting to use the forward class 'FOO' as superclass of 'INTF'}}
@end

@interface FOO 
- (BAR*) Meth1;
- (FOO*) Meth2;
@end

@interface INTF1 : FOO	
@end

@interface INTF2 : INTF1 // expected-note {{previous definition is here}}
@end


@class INTF1, INTF2;

@interface INTF2 : INTF1 // expected-error {{duplicate interface definition for class 'INTF2'}}
@end

// 2nd test of a forward class declaration matching a typedef name
// referring to class object.
// FIXME. This may become a negative test should we decide to make this an error.
//
@interface NSObject @end

@protocol XCElementP @end

typedef NSObject <XCElementP> XCElement;

@interface XCElementMainImp  {
  XCElement * _editingElement;
}
@end

@class XCElement;

@implementation XCElementMainImp
- (XCElement *)editingElement  { return _editingElement;  }
@end


// rdar://9653341
@class B; // expected-note {{forward class is declared here}}
@interface A : B {} // expected-error {{attempting to use the forward class 'B' as superclass of 'A'}}
@end

@interface B : A {}
@end

@implementation A @end
@implementation B @end

