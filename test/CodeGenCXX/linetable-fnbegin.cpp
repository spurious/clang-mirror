// RUN: %clang_cc1 -emit-llvm -g %s -o - | FileCheck %s
// Test that the line table info for Foo<T>::bar() is pointing to the
// right header file.
// CHECK: define{{.*}} @_ZN3FooIiE3barEv
// CHECK-NOT: define
// CHECK: ret {{.*}}, !dbg ![[DBG:.*]]
// CHECK: ![[HPP:.*]] = metadata !{metadata !"./template.hpp",
// CHECK:![[BLOCK:.*]] = metadata !{{{.*}}, metadata ![[HPP]], {{.*}}} ; [ DW_TAG_lexical_block ]
// CHECK: [[DBG]] = metadata !{i32 23, i32 0, metadata ![[BLOCK]], null}
# 1 "./template.h" 1
template <typename T>
class Foo {
public:
 int bar();
};
# 21 "./template.hpp"
template <typename T>
int Foo<T>::bar() {
}
int main (int argc, const char * argv[])
{
  Foo<int> f;
  f.bar();
}
