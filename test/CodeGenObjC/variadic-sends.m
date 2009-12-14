// RUN: clang -cc1 -triple i386-unknown-unknown -emit-llvm -o - %s | FileCheck -check-prefix=CHECK-X86-32 %s
// RUN: clang -cc1 -triple x86_64-unknown-unknown -emit-llvm -o - %s | FileCheck -check-prefix=CHECK-X86-64 %s

@interface A
-(void) im0;
-(void) im1: (int) x;
-(void) im2: (int) x, ...;
@end

void f0(A *a) {
  // CHECK-X86-32: call void bitcast (i8* (i8*, i8*, ...)* @objc_msgSend to void (i8*, i8*)*)
  // CHECK-X86-64: call void bitcast (i8* (i8*, i8*, ...)* @objc_msgSend to void (i8*, i8*)*)
  [a im0];
}

void f1(A *a) {
  // CHECK-X86-32: call void bitcast (i8* (i8*, i8*, ...)* @objc_msgSend to void (i8*, i8*, i32)*)
  // CHECK-X86-64: call void bitcast (i8* (i8*, i8*, ...)* @objc_msgSend to void (i8*, i8*, i32)*)
  [a im1: 1];
}

void f2(A *a) {
  // CHECK-X86-32: call void (i8*, i8*, i32, i32, ...)* bitcast (i8* (i8*, i8*, ...)* @objc_msgSend to void (i8*, i8*, i32, i32, ...)*)
  // CHECK-X86-64: call void (i8*, i8*, i32, i32, ...)* bitcast (i8* (i8*, i8*, ...)* @objc_msgSend to void (i8*, i8*, i32, i32, ...)*)
  [a im2: 1, 2];
}

@interface B : A @end
@implementation B : A
-(void) foo {
  // CHECK-X86-32: call void bitcast (i8* (%struct._objc_method_description*, i8*, ...)* @objc_msgSendSuper to void (%struct._objc_method_description*, i8*, i32)*)
  // CHECK-X86-64: call void bitcast (i8* (%struct._objc_method_description*, i8*, ...)* @objc_msgSendSuper to void (%struct._objc_method_description*, i8*, i32)*)
  [super im1: 1];
}
-(void) bar {
  // CHECK-X86-32: call void (%struct._objc_method_description*, i8*, i32, i32, ...)* bitcast (i8* (%struct._objc_method_description*, i8*, ...)* @objc_msgSendSuper to void (%struct._objc_method_description*, i8*, i32, i32, ...)*)
  // CHECK-X86-64: call void (%struct._objc_method_description*, i8*, i32, i32, ...)* bitcast (i8* (%struct._objc_method_description*, i8*, ...)* @objc_msgSendSuper to void (%struct._objc_method_description*, i8*, i32, i32, ...)*)
  [super im2: 1, 2];
}

@end
