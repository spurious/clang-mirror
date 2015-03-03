struct foo;
void func(foo *f) {
}
class bar;
void func(bar *f) {
}
union baz;
void func(baz *f) {
}

class B {
public:
  virtual ~B();
};

B::~B() {
}

struct C {
  static int s;
  virtual ~C();
};

C::~C() {
}

struct D {
  D();
  virtual ~D();
  void func() {
  }
};

struct E {
  E();
  virtual ~E();
  virtual void func() {
  }
};

struct F {
  struct inner {
  };
  static const int i = 2;
  virtual ~F();
};

struct G {
  virtual void func();
  struct inner {
    int j;
  };
};

struct H {};
struct I : virtual H {};
struct J : I {};
J j;

struct A {
  int one;
  static const int HdrSize = 52;
  int two;
  A() {
    int x = 1;
  }
};

void f1() {
  D x;
  x.func();
  E y;
  int i = F::i;
  F::inner z;
}

int main(int argc, char **argv) {
  B b;
  G::inner c_i;
  if (argc) {
    A a;
  }
  return 0;
}

// RUN: %clang -target x86_64-unknown_unknown -emit-llvm -g -S %s -o - | FileCheck %s
// RUN: %clang -target i686-cygwin -emit-llvm -g -S %s -o - | FileCheck %s
// RUN: %clang -target armv7l-unknown-linux-gnueabihf -emit-llvm -g -S %s -o - | FileCheck %s

// CHECK: invoke {{.+}} @_ZN1BD1Ev(%class.B* %b)
// CHECK-NEXT: unwind label %{{.+}}, !dbg ![[EXCEPTLOC:.*]]
// CHECK: store i32 0, i32* %{{.+}}, !dbg ![[RETLOC:.*]]
// CHECK: !MDCompositeType(tag: DW_TAG_structure_type, name: "foo"
// CHECK: !MDCompositeType(tag: DW_TAG_class_type, name: "bar"
// CHECK: !MDCompositeType(tag: DW_TAG_union_type, name: "baz"
// CHECK: !MDCompositeType(tag: DW_TAG_class_type, name: "B"
// CHECK-NOT:              DIFlagFwdDecl
// CHECK-SAME:             ){{$}}
// CHECK: !MDDerivedType(tag: DW_TAG_member, name: "_vptr$B",
// CHECK-SAME:           DIFlagArtificial

// CHECK: ![[INT:[0-9]+]] = !MDBasicType(name: "int"

// CHECK: [[C:![0-9]*]] = !MDCompositeType(tag: DW_TAG_structure_type, name: "C",
// CHECK-NOT:                              DIFlagFwdDecl
// CHECK-SAME:                             elements: [[C_MEM:![0-9]*]]
// CHECK-SAME:                             vtableHolder: !"_ZTS1C"
// CHECK-SAME:                             identifier: "_ZTS1C"
// CHECK: [[C_MEM]] = !{[[C_VPTR:![0-9]*]], [[C_S:![0-9]*]], [[C_DTOR:![0-9]*]]}
// CHECK: [[C_VPTR]] = !MDDerivedType(tag: DW_TAG_member, name: "_vptr$C"
// CHECK-SAME:                        DIFlagArtificial
// CHECK: [[C_S]] = !MDDerivedType(tag: DW_TAG_member, name: "s"
// CHECK-SAME:                     baseType: ![[INT]]
// CHECK-SAME:                     DIFlagStaticMember
// CHECK: [[C_DTOR]] = !MDSubprogram(name: "~C"

// CHECK: !MDCompositeType(tag: DW_TAG_structure_type, name: "D"
// CHECK-SAME:             DIFlagFwdDecl
// CHECK-SAME:             identifier: "_ZTS1D"
// CHECK: !MDCompositeType(tag: DW_TAG_structure_type, name: "E"
// CHECK-SAME:             DIFlagFwdDecl
// CHECK-SAME:             identifier: "_ZTS1E"
// CHECK: [[F:![0-9]*]] = !MDCompositeType(tag: DW_TAG_structure_type, name: "F"
// CHECK-SAME:                             DIFlagFwdDecl
// CHECK-SAME:                             identifier: "_ZTS1F"

// CHECK: !MDCompositeType(tag: DW_TAG_structure_type, name: "G"
// CHECK-SAME:             DIFlagFwdDecl
// CHECK-SAME:             identifier: "_ZTS1G"
// CHECK: !MDCompositeType(tag: DW_TAG_structure_type, name: "inner"
// CHECK: line: 50
// CHECK-NOT: DIFlagFwdDecl
// CHECK-SAME: elements: [[G_INNER_MEM:![0-9]*]]
// CHECK-SAME: identifier: "_ZTSN1G5innerE"
// CHECK: [[G_INNER_MEM]] = !{[[G_INNER_I:![0-9]*]]}
// CHECK: [[G_INNER_I]] = !MDDerivedType(tag: DW_TAG_member, name: "j"
// CHECK-SAME:                           baseType: ![[INT]]

// CHECK: !MDCompositeType(tag: DW_TAG_structure_type, name: "A"
// CHECK: !MDDerivedType(tag: DW_TAG_member, name: "HdrSize"
// CHECK: !MDCompositeType(tag: DW_TAG_structure_type, name: "I"
// CHECK-NOT:              DIFlagFwdDecl
// CHECK-SAME:             ){{$}}
//
// CHECK: !MDSubprogram(name: "func",{{.*}} scope: !"_ZTS1D"
// CHECK-SAME:          isDefinition: true
// CHECK-SAME:          declaration: [[D_FUNC_DECL:![0-9]*]]
// CHECK: [[D_FUNC_DECL]] = !MDSubprogram(name: "func",{{.*}} scope: !"_ZTS1D"
// CHECK-SAME:                            isDefinition: false

// CHECK: ![[EXCEPTLOC]] = !MDLocation(line: 84,
// CHECK: ![[RETLOC]] = !MDLocation(line: 83,
