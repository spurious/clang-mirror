// RUN: clang -checker-cfref -verify %s

#include <CoreFoundation/CFString.h>
#include <Foundation/NSString.h>
#include <Foundation/NSObjCRuntime.h>
#include <Foundation/NSArray.h>

NSComparisonResult f1(NSString* s) {
  NSString *aString = nil;
  return [s compare:aString]; // expected-warning {{Argument to 'NSString' method 'compare:' cannot be nil.}}
}

NSComparisonResult f2(NSString* s) {
  NSString *aString = nil;
  return [s caseInsensitiveCompare:aString]; // expected-warning {{Argument to 'NSString' method 'caseInsensitiveCompare:' cannot be nil.}}
}

NSComparisonResult f3(NSString* s, NSStringCompareOptions op) {
  NSString *aString = nil;
  return [s compare:aString options:op]; // expected-warning {{Argument to 'NSString' method 'compare:options:' cannot be nil.}}
}

NSComparisonResult f4(NSString* s, NSStringCompareOptions op, NSRange R) {
  NSString *aString = nil;
  return [s compare:aString options:op range:R]; // expected-warning {{Argument to 'NSString' method 'compare:options:range:' cannot be nil.}}
}

NSComparisonResult f5(NSString* s, NSStringCompareOptions op, NSRange R) {
  NSString *aString = nil;
  return [s compare:aString options:op range:R locale:nil]; // expected-warning {{Argument to 'NSString' method 'compare:options:range:locale:' cannot be nil.}}
}

NSComparisonResult f6(NSString* s) {
  return [s componentsSeparatedByCharactersInSet:nil]; // expected-warning {{Argument to 'NSString' method 'componentsSeparatedByCharactersInSet:' cannot be nil.}}
}

NSString* f7(NSString* s1, NSString* s2, NSString* s3) {

  NSString* s4 = CFStringCreateWithFormat(kCFAllocatorDefault, NULL,
                                          L"%@ %@ (%@)", 
                                          s1, s2, s3);

  CFRetain(s4);
  return s4; // expected-warning{{leak}}
}
