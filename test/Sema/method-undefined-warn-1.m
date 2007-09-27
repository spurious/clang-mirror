@interface INTF
- (void) meth;
- (void) meth : (int) arg1;
- (int)  int_meth;	// expected-warning {{method definition for 'int_meth' not found}}
+ (int) cls_meth;	// expected-warning {{method definition for 'cls_meth' not found}}
+ (void) cls_meth1 : (int) arg1; // expected-warning {{method definition for 'cls_meth1:' not found}}
@end

@implementation INTF
- (void) meth {}
- (void) meth : (int) arg2{}
- (void) cls_meth1 : (int) arg2{}
@end


@interface INTF1
- (void) meth;
- (void) meth : (int) arg1;
- (int)  int_meth;      // expected-warning {{method definition for 'int_meth' not found}}
+ (int) cls_meth;       // expected-warning {{method definition for 'cls_meth' not found}}
+ (void) cls_meth1 : (int) arg1; // expected-warning {{method definition for 'cls_meth1:' not found}}
@end

@implementation INTF1
- (void) meth {}
- (void) meth : (int) arg2{}
- (void) cls_meth1 : (int) arg2{}
@end


@interface INTF2
- (void) meth;
- (void) meth : (int) arg1;
- (void) cls_meth1 : (int) arg1; 
@end

@implementation INTF2
- (void) meth {}
- (void) meth : (int) arg2{}
- (void) cls_meth1 : (int) arg2{}
@end

