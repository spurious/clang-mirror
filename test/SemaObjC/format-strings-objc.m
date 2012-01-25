// RUN: %clang_cc1 -fsyntax-only -verify %s

//===----------------------------------------------------------------------===//
// The following code is reduced using delta-debugging from
// Foundation.h (Mac OS X).
//
// It includes the basic definitions for the test cases below.
// Not including Foundation.h directly makes this test case both svelt and
// portable to non-Mac platforms.
//===----------------------------------------------------------------------===//

typedef signed char BOOL;
typedef unsigned int NSUInteger;
@class NSString, Protocol;
extern void NSLog(NSString *format, ...) __attribute__((format(__NSString__, 1, 2)));
extern void NSLog(NSString *format, ...) __attribute__((format(__NSString__, 1, 2)));
typedef struct _NSZone NSZone;
@class NSInvocation, NSMethodSignature, NSCoder, NSString, NSEnumerator;
@protocol NSObject  - (BOOL)isEqual:(id)object; @end
@protocol NSCopying  - (id)copyWithZone:(NSZone *)zone; @end
@protocol NSMutableCopying  - (id)mutableCopyWithZone:(NSZone *)zone; @end
@protocol NSCoding  - (void)encodeWithCoder:(NSCoder *)aCoder; @end
@interface NSObject <NSObject> {} @end
typedef float CGFloat;
@interface NSString : NSObject <NSCopying, NSMutableCopying, NSCoding>    - (NSUInteger)length; @end
@interface NSSimpleCString : NSString {} @end
@interface NSConstantString : NSSimpleCString @end
extern void *_NSConstantStringClassReference;

typedef const struct __CFString * CFStringRef;
extern void CFStringCreateWithFormat(CFStringRef format, ...) __attribute__((format(CFString, 1, 2)));

int printf(const char * restrict, ...) ;

//===----------------------------------------------------------------------===//
// Test cases.
//===----------------------------------------------------------------------===//

void check_nslog(unsigned k) {
  NSLog(@"%d%%", k); // no-warning
  NSLog(@"%s%lb%d", "unix", 10,20); // expected-warning {{invalid conversion specifier 'b'}}
}

// Check type validation
extern void NSLog2(int format, ...) __attribute__((format(__NSString__, 1, 2))); // expected-error {{format argument not an NSString}}
extern void CFStringCreateWithFormat2(int *format, ...) __attribute__((format(CFString, 1, 2))); // expected-error {{format argument not a CFString}}

// <rdar://problem/7068334> - Catch use of long long with int arguments.
void rdar_7068334() {
  long long test = 500;  
  printf("%i ",test); // expected-warning{{format specifies type 'int' but the argument has type 'long long'}}
  NSLog(@"%i ",test); // expected-warning{{format specifies type 'int' but the argument has type 'long long'}}
}

// <rdar://problem/7697748>
void rdar_7697748() {
  NSLog(@"%@!"); // expected-warning{{more '%' conversions than data arguments}}
}

@protocol Foo;

void test_p_conversion_with_objc_pointer(id x, id<Foo> y) {
  printf("%p", x); // no-warning
  printf("%p", y); // no-warning
}

// <rdar://problem/10696348>, PR 10274 - CFString and NSString formats are ignored
extern void MyNSLog(NSString *format, ...) __attribute__((format(__NSString__, 1, 2)));
extern void MyCFStringCreateWithFormat(CFStringRef format, ...) __attribute__((format(__CFString__, 1, 2)));

void check_mylog() {
  MyNSLog(@"%@"); // expected-warning {{more '%' conversions than data arguments}}
  // FIXME: find a way to test CFString too, but I don't know how to create constant CFString.
}

// PR 10275 - format function attribute isn't checked in Objective-C methods
@interface Foo
+ (id)fooWithFormat:(NSString *)fmt, ... __attribute__((format(__NSString__, 1, 2)));
+ (id)fooWithCStringFormat:(const char *)format, ... __attribute__((format(__printf__, 1, 2)));
@end

void check_method() {
  [Foo fooWithFormat:@"%@"]; // expected-warning {{more '%' conversions than data arguments}}
  [Foo fooWithCStringFormat:"%@"]; // expected-warning {{invalid conversion specifier '@'}}
}

// Warn about using BOOL with %@
void rdar10743758(id x) {
  NSLog(@"%@ %@", x, (BOOL) 1); // expected-warning {{format specifies type 'id' but the argument has type 'BOOL' (aka 'signed char')}}
}

NSString *test_literal_propagation(void) {
  const char * const s1 = "constant string %s"; // expected-note {{format string is defined here}}
  printf(s1); // expected-warning {{more '%' conversions than data arguments}}
  const char * const s5 = "constant string %s"; // expected-note {{format string is defined here}}
  const char * const s2 = s5;
  printf(s2); // expected-warning {{more '%' conversions than data arguments}}

  const char * const s3 = (const char *)0;
  printf(s3); // expected-warning {{format string is not a string literal}}

  NSString * const ns1 = @"constant string %s"; // expected-note {{format string is defined here}}
  NSLog(ns1); // expected-warning {{more '%' conversions than data arguments}}
  NSString * const ns5 = @"constant string %s"; // expected-note {{format string is defined here}}
  NSString * const ns2 = ns5;
  NSLog(ns2); // expected-warning {{more '%' conversions than data arguments}}
  NSString * ns3 = ns1;
  NSLog(ns3); // expected-warning {{format string is not a string literal}}}
}
