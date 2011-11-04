// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -fsyntax-only -fobjc-arc -x objective-c %s.result
// RUN: arcmt-test --args -triple x86_64-apple-darwin10 -fsyntax-only -fobjc-gc-only -x objective-c %s > %t
// RUN: diff %t %s.result

#include "Common.h"

void test1(CFTypeRef *cft) {
  id x = NSMakeCollectable(cft);
}
