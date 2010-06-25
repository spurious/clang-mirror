// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -emit-llvm -o - %s | FileCheck %s
// PR7490

int main() {
  // CHECK: {{for.cond:|:4}}
  // CHECK: %{{.*}} = icmp ult i64 %{{.*}}, 1133
  // CHECK: {{for.body:|:6}}
  // CHECK: store i8 0
  // CHECK: br label %{{for.inc|7}}
  // CHECK: {{for.inc:|:7}}
  // CHECK: %{{.*}} = add i64 %{{.*}}, 1
  // CHECK: store i64 %{{.*}}
  // CHECK: br label %{{for.cond|4}}
  // CHECK: {{for.end:|:12}}
  volatile char *buckets = new char[1133]();
}
