// RUN: clang -rewrite-test %s

@protocol P1
- (void) MyProtoMeth : (int **) arg1 : (void*) arg2;
+ (void) MyProtoMeth : (int **) arg1 : (void*) arg2;
@end

@interface Intf <P1>
- (char *) MyMeth : (double) arg1 : (char *[12]) arg2;
- (id) address:(void *)location with:(unsigned **)arg2;
@end

@implementation Intf
- (char *) MyMeth : (double) arg1 : (char *[12]) arg2{}
- (void) MyProtoMeth : (int **) arg1 : (void*) arg2 {}
+ (void) MyProtoMeth : (int **) arg1 : (void*) arg2 {}
- (id) address:(void *)location with:(unsigned **)arg2{}
@end
