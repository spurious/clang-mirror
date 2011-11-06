// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -fsyntax-only -fobjc-arc -x objective-c %s.result
// RUN: arcmt-test --args -triple x86_64-apple-darwin10 -fsyntax-only -fobjc-gc-only -x objective-c %s > %t
// RUN: diff %t %s.result
// RUN: arcmt-test --args -triple x86_64-apple-darwin10 -fsyntax-only -fobjc-gc-only -x objective-c++ %s > %t
// RUN: diff %t %s.result

#include "Common.h"
#include "GC.h"

void test1(CFTypeRef *cft) {
  id x = NSMakeCollectable(cft);
}

@interface I1 {
  __strong I1 *myivar;
}
@end

@implementation I1
-(void)dealloc {
  // dealloc
  test1(0);
}

-(void)finalize {
  // finalize
  test1(0);
}
@end

@interface I2
@property (retain) id prop;
@end

@implementation I2
@synthesize prop;

-(void)finalize {
  self.prop = 0;
  // finalize
  test1(0);
}
@end
