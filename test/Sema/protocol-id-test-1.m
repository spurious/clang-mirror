// RUN: clang -verify %s

@interface FF
- (void) Meth;
@end

@protocol P
@end

@interface INTF<P>
- (void)IMeth;
@end

@implementation INTF
- (void)IMeth {INTF<P> *pi;  [pi Meth]; } // expected-warning {{method '-Meth' not found in protocol (return type defaults to 'id')}}
@end
