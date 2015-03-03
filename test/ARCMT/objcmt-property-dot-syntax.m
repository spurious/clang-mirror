// RUN: rm -rf %t
// RUN: %clang_cc1 -objcmt-migrate-property-dot-syntax -mt-migrate-directory %t %s -x objective-c -fobjc-runtime-has-weak -fobjc-arc -triple x86_64-apple-darwin11
// RUN: c-arcmt-test -mt-migrate-directory %t | arcmt-test -verify-transformed-files %s.result
// RUN: %clang_cc1 -fblocks -triple x86_64-apple-darwin10 -fsyntax-only -x objective-c -fobjc-runtime-has-weak -fobjc-arc %s.result

// rdar://18498572
@interface NSObject @end

@interface P : NSObject
{
  P* obj;
  int i1, i2, i3;
}
@property int count;
@property (copy) P* PropertyReturnsPObj;
- (P*) MethodReturnsPObj;
@end

P* fun();

@implementation P
- (int) Meth : (P*)array {
  [obj setCount : 100];

  [(P*)0 setCount : [array count]];

  [[obj PropertyReturnsPObj] setCount : [array count]];

  [obj setCount : (i1+i2*i3 - 100)];

  return [obj count] -
         [(P*)0 count] + [array count] +
         [fun() count] - 
         [[obj PropertyReturnsPObj] count] +
         [self->obj count];
}

- (P*) MethodReturnsPObj { return 0; }
@end

// rdar://19140267
@interface Sub : P
@end

@implementation Sub
- (int) Meth : (P*)array {
  [super setCount : 100];

  [super setCount : [array count]];

  [[super PropertyReturnsPObj] setCount : [array count]];

  [super setCount : (i1+i2*i3 - 100)];

  return [super count] -
         [(P*)0 count] + [array count] +
         [fun() count] -
         [[super PropertyReturnsPObj] count] +
         [self->obj count];
}
@end


@interface Rdar19038838
@property id newItem; // should be marked objc_method_family(none), but isn't.
@end

id testRdar19038838(Rdar19038838 *obj) {
  return [obj newItem];
}

// rdar://19381786
@interface rdar19381786 : NSObject
{
  rdar19381786* obj;
}
@property int count;
@end

@protocol PR 
@property int count;
@end

@implementation rdar19381786
-(void)test:(id)some : (id<PR>)qsome : (SEL)selsome
{
  [obj setCount : 100];
  [some setCount : [some count]];
  [qsome setCount : [qsome count]];
}
@end

// rdar://19140114
int NSOnState;
int ArrNSOnState[4];
@interface rdar19140114 : NSObject
{
  rdar19140114* menuItem;
}
@property int state;
@end

@implementation rdar19140114
- (void) Meth {
  [menuItem setState:NSOnState];
  [menuItem setState :NSOnState];
  [menuItem setState     :ArrNSOnState[NSOnState]];
  [menuItem setState : NSOnState];
  [menuItem setState:    NSOnState];
  [menuItem setState: NSOnState];
  [menuItem setState     :    NSOnState];
}
@end
