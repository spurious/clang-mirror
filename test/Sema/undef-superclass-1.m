@class SUPER, Y;

@interface INTF :SUPER  // expected-error {{cannot find interface declaration for 'SUPER', superclass of 'INTF'}}
@end

@interface SUPER @end

@interface INTF1 : SUPER
@end

@interface INTF2 : INTF1
@end

@interface INTF3 : Y // expected-error {{cannot find interface declaration for 'Y', superclass of 'INTF3'}}
@end

@interface INTF1  // expected-error {{duplicate interface declaration for class 'INTF1'}}
@end
