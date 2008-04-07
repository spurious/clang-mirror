// RUN: clang -fsyntax-only -pedantic -verify %s

#define nil (void *)0;

extern void foo();

@protocol MyProtocol
- (void) method;
@end

@interface MyClass
@end

@interface MyClass (Addition) <MyProtocol>
- (void) method;
@end

@interface MyOtherClass : MyClass
@end

int main()
{
  id <MyProtocol> obj_id_p = nil;
  MyClass *obj_c_cat_p = nil;
  MyOtherClass *obj_c_super_p = nil;
  MyOtherClass<MyProtocol> *obj_c_super_p_q = nil;

  obj_c_cat_p = obj_id_p;   // expected-error {{incompatible type assigning 'id<MyProtocol>', expected 'MyClass *'}}
  obj_c_super_p = obj_id_p;  // expected-error {{incompatible type assigning 'id<MyProtocol>', expected 'MyOtherClass *'}}
  obj_id_p = obj_c_cat_p;  /* Ok */
  obj_id_p = obj_c_super_p; /* Ok */

  if (obj_c_cat_p == obj_id_p) foo(); /* Ok */
  if (obj_c_super_p == obj_id_p) foo() ; /* Ok */
  if (obj_id_p == obj_c_cat_p)  foo(); /* Ok */
  if (obj_id_p == obj_c_super_p)  foo(); /* Ok */

  obj_c_cat_p = obj_c_super_p; // ok.
  obj_c_cat_p = obj_c_super_p_q; // ok.
  
  return 0;
}
