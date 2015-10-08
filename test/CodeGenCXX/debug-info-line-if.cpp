// RUN: %clang_cc1 -debug-info-kind=limited -std=c++11 -S -emit-llvm %s -o - | FileCheck %s
// PR19864
extern int v[2];
int a = 0, b = 0;
int main() {
#line 100
  for (int x : v)
    if (x)
      ++b; // CHECK: add nsw{{.*}}, 1
    else
      ++a; // CHECK: add nsw{{.*}}, 1
  // The continuation block if the if statement should not share the
  // location of the ++a statement. The branch back to the start of the loop
  // should be attributed to the loop header line.

  // CHECK: br label
  // CHECK: br label
  // CHECK: br label {{.*}}, !dbg [[DBG1:!.*]]

#line 200
  while (a)
    if (b)
      ++b; // CHECK: add nsw{{.*}}, 1
    else
      ++a; // CHECK: add nsw{{.*}}, 1

  // CHECK: br label
  // CHECK: br label {{.*}}, !dbg [[DBG2:!.*]]

#line 300
  for (; a; )
    if (b)
      ++b; // CHECK: add nsw{{.*}}, 1
    else
      ++a; // CHECK: add nsw{{.*}}, 1

  // CHECK: br label
  // CHECK: br label {{.*}}, !dbg [[DBG3:!.*]]

#line 400
  int x[] = {1, 2};
  for (int y : x)
    if (b)
      ++b; // CHECK: add nsw{{.*}}, 1
    else
      ++a; // CHECK: add nsw{{.*}}, 1

  // CHECK: br label
  // CHECK: br label {{.*}}, !dbg [[DBG4:!.*]]

  // CHECK: [[DBG1]] = !DILocation(line: 100, scope: !{{.*}})
  // CHECK: [[DBG2]] = !DILocation(line: 200, scope: !{{.*}})
  // CHECK: [[DBG3]] = !DILocation(line: 300, scope: !{{.*}})
  // CHECK: [[DBG4]] = !DILocation(line: 401, scope: !{{.*}})
}
