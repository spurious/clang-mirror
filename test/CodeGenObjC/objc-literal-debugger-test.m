// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -fdebugger-objc-literal -emit-llvm -o - %s | FileCheck %s

int main() {
  // object literals.
  id l;
  l = @'a';
  l = @42;
  l = @-42;
  l = @42u;
  l = @3.141592654f;
  l = @__objc_yes;
  l = @__objc_no;
  l = @{ @"name":@666 };
  l = @[ @"foo", @"bar" ];

#if __has_feature(objc_boxed_expressions)
  // boxed expressions.
  id b;
  b = @('a');
  b = @(42);
  b = @(-42);
  b = @(42u);
  b = @(3.141592654f);
  b = @(__objc_yes);
  b = @(__objc_no);
  b = @("hello");
#endif
}

// CHECK: declare i8* @objc_msgSend(i8*, i8*, ...) nonlazybind
