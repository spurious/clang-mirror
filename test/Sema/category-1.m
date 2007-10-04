// RUN: clang -fsyntax-only -verify %s

@interface MyClass1 @end

@protocol p1,p2,p3;

@interface MyClass1 (Category1)  <p1> // expected-error {{cannot find protocol definition for 'p1', referenced by 'Category1'}}
@end

@interface MyClass1 (Category1)  // expected-error {{duplicate interface declaration for category 'MyClass1(Category1)'}}
@end

@interface MyClass1 (Category3) 
@end

@interface MyClass1 (Category4) @end
@interface MyClass1 (Category5) @end
@interface MyClass1 (Category6) @end
@interface MyClass1 (Category7) @end
@interface MyClass1 (Category8) @end


@interface MyClass1 (Category4) @end // expected-error {{duplicate interface declaration for category 'MyClass1(Category4)'}}
@interface MyClass1 (Category7) @end // expected-error {{duplicate interface declaration for category 'MyClass1(Category7)'}}
@interface MyClass1 (Category8) @end // expected-error {{duplicate interface declaration for category 'MyClass1(Category8)'}}


@protocol p3 @end

@interface MyClass1 (Category) <p2, p3> @end  // expected-error {{cannot find protocol definition for 'p2', referenced by 'Category'}}

@interface MyClass  (Category) @end // expected-error {{cannot find interface declaration for 'MyClass'}}

@class MyClass2;

@interface MyClass2  (Category) @end  // expected-error {{cannot find interface declaration for 'MyClass2'}}


