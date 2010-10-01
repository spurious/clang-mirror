// RUN: %clang_cc1 -analyze -cfg-dump -cfg-add-implicit-dtors %s 2>&1 | FileCheck %s
// XPASS: *

class A {
public:
  A() {}
  ~A() {}
  operator int() const { return 1; }
};

extern const bool UV;

void test_const_ref() {
  A a;
  const A& b = a;
  const A& c = A();
}

void test_scope() {
  A a;
  { A c;
    A d;
  }
  A b;
}

void test_return() {
  A a;
  A b;
  if (UV) return;
  A c;
}

void test_goto() {
  A a;
l0:
  A b;
  { A a;
    if (UV) goto l0;
    if (UV) goto l1;
    A b;
  }
l1:
  A c;
}

void test_if_implicit_scope() {
  A a;
  if (A b = a)
    A c;
  else A c;
}

void test_if_jumps() {
  A a;
  if (A b = a) {
    A c;
    if (UV) return;
    A d;
  } else {
    A c;
    if (UV) return;
    A d;
  }
  A e;
}

void test_while_implicit_scope() {
  A a;
  while (A b = a)
    A c;
}

void test_while_jumps() {
  A a;
  while (A b = a) {
    A c;
    if (UV) break;
    if (UV) continue;
    if (UV) return;
    A d;
  }
  A e;
}

void test_do_implicit_scope() {
  do A a;
  while (UV);
}

void test_do_jumps() {
  A a;
  do {
    A b;
    if (UV) break;
    if (UV) continue;
    if (UV) return;
    A c;
  } while (UV);
  A d;
}

void test_switch_implicit_scope() {
  A a;
  switch (A b = a)
    A c;
}

void test_switch_jumps() {
  A a;
  switch (A b = a) {
  case 0: {
    A c;
    if (UV) break;
    if (UV) return;
    A f;
  }
  case 1:
    break;
  }
  A g;
}

// CHECK:  [ B2 (ENTRY) ]
// CHECK:     Predecessors (0):
// CHECK:     Successors (1): B1
// CHECK:  [ B1 ]
// CHECK:       1: A a;
// CHECK:       2: const A &b = a;
// CHECK:       3: const A &c = A();
// CHECK:       4: [B1.3].~A() (Implicit destructor)
// CHECK:       5: [B1.1].~A() (Implicit destructor)
// CHECK:     Predecessors (1): B2
// CHECK:     Successors (1): B0
// CHECK:  [ B0 (EXIT) ]
// CHECK:     Predecessors (1): B1
// CHECK:     Successors (0):
// CHECK:  [ B2 (ENTRY) ]
// CHECK:     Predecessors (0):
// CHECK:     Successors (1): B1
// CHECK:  [ B1 ]
// CHECK:       1: A a;
// CHECK:       2: A c;
// CHECK:       3: A d;
// CHECK:       4: [B1.3].~A() (Implicit destructor)
// CHECK:       5: [B1.2].~A() (Implicit destructor)
// CHECK:       6: A b;
// CHECK:       7: [B1.6].~A() (Implicit destructor)
// CHECK:       8: [B1.1].~A() (Implicit destructor)
// CHECK:     Predecessors (1): B2
// CHECK:     Successors (1): B0
// CHECK:  [ B0 (EXIT) ]
// CHECK:     Predecessors (1): B1
// CHECK:     Successors (0):
// CHECK:  [ B4 (ENTRY) ]
// CHECK:     Predecessors (0):
// CHECK:     Successors (1): B3
// CHECK:  [ B1 ]
// CHECK:       1: A c;
// CHECK:       2: [B1.1].~A() (Implicit destructor)
// CHECK:       3: [B3.2].~A() (Implicit destructor)
// CHECK:       4: [B3.1].~A() (Implicit destructor)
// CHECK:     Predecessors (1): B3
// CHECK:     Successors (1): B0
// CHECK:  [ B2 ]
// CHECK:       1: return;
// CHECK:       2: [B3.2].~A() (Implicit destructor)
// CHECK:       3: [B3.1].~A() (Implicit destructor)
// CHECK:     Predecessors (1): B3
// CHECK:     Successors (1): B0
// CHECK:  [ B3 ]
// CHECK:       1: A a;
// CHECK:       2: A b;
// CHECK:       3: UV
// CHECK:       T: if [B3.3]
// CHECK:     Predecessors (1): B4
// CHECK:     Successors (2): B2 B1
// CHECK:  [ B0 (EXIT) ]
// CHECK:     Predecessors (2): B1 B2
// CHECK:     Successors (0):
// CHECK:  [ B8 (ENTRY) ]
// CHECK:     Predecessors (0):
// CHECK:     Successors (1): B7
// CHECK:  [ B1 ]
// CHECK:     l1:
// CHECK:       1: A c;
// CHECK:       2: [B1.1].~A() (Implicit destructor)
// CHECK:       3: [B6.1].~A() (Implicit destructor)
// CHECK:       4: [B7.1].~A() (Implicit destructor)
// CHECK:     Predecessors (2): B2 B3
// CHECK:     Successors (1): B0
// CHECK:  [ B2 ]
// CHECK:       1: A b;
// CHECK:       2: [B2.1].~A() (Implicit destructor)
// CHECK:       3: [B6.2].~A() (Implicit destructor)
// CHECK:     Predecessors (1): B4
// CHECK:     Successors (1): B1
// CHECK:  [ B3 ]
// CHECK:       1: [B6.2].~A() (Implicit destructor)
// CHECK:       T: goto l1;
// CHECK:     Predecessors (1): B4
// CHECK:     Successors (1): B1
// CHECK:  [ B4 ]
// CHECK:       1: UV
// CHECK:       T: if [B4.1]
// CHECK:     Predecessors (1): B6
// CHECK:     Successors (2): B3 B2
// CHECK:  [ B5 ]
// CHECK:       1: [B6.2].~A() (Implicit destructor)
// CHECK:       2: [B6.1].~A() (Implicit destructor)
// CHECK:       T: goto l0;
// CHECK:     Predecessors (1): B6
// CHECK:     Successors (1): B6
// CHECK:  [ B6 ]
// CHECK:     l0:
// CHECK:       1: A b;
// CHECK:       2: A a;
// CHECK:       3: UV
// CHECK:       T: if [B6.3]
// CHECK:     Predecessors (2): B7 B5
// CHECK:     Successors (2): B5 B4
// CHECK:  [ B7 ]
// CHECK:       1: A a;
// CHECK:     Predecessors (1): B8
// CHECK:     Successors (1): B6
// CHECK:  [ B0 (EXIT) ]
// CHECK:     Predecessors (1): B1
// CHECK:     Successors (0):
// CHECK:  [ B5 (ENTRY) ]
// CHECK:     Predecessors (0):
// CHECK:     Successors (1): B4
// CHECK:  [ B1 ]
// CHECK:       1: [B4.3].~A() (Implicit destructor)
// CHECK:       2: [B4.1].~A() (Implicit destructor)
// CHECK:     Predecessors (2): B2 B3
// CHECK:     Successors (1): B0
// CHECK:  [ B2 ]
// CHECK:       1: A c;
// CHECK:       2: [B2.1].~A() (Implicit destructor)
// CHECK:     Predecessors (1): B4
// CHECK:     Successors (1): B1
// CHECK:  [ B3 ]
// CHECK:       1: A c;
// CHECK:       2: [B3.1].~A() (Implicit destructor)
// CHECK:     Predecessors (1): B4
// CHECK:     Successors (1): B1
// CHECK:  [ B4 ]
// CHECK:       1: A a;
// CHECK:       2: a
// CHECK:       3: if ([B4.5])
// CHECK: [B3.1]else
// CHECK: [B2.1]      4: b.operator int()
// CHECK:       5: [B4.4]
// CHECK:       T: if [B4.5]
// CHECK:     Predecessors (1): B5
// CHECK:     Successors (2): B3 B2
// CHECK:  [ B0 (EXIT) ]
// CHECK:     Predecessors (1): B1
// CHECK:     Successors (0):
// CHECK:  [ B9 (ENTRY) ]
// CHECK:     Predecessors (0):
// CHECK:     Successors (1): B8
// CHECK:  [ B1 ]
// CHECK:       1: [B8.3].~A() (Implicit destructor)
// CHECK:       2: A e;
// CHECK:       3: [B1.2].~A() (Implicit destructor)
// CHECK:       4: [B8.1].~A() (Implicit destructor)
// CHECK:     Predecessors (2): B2 B5
// CHECK:     Successors (1): B0
// CHECK:  [ B2 ]
// CHECK:       1: A d;
// CHECK:       2: [B2.1].~A() (Implicit destructor)
// CHECK:       3: [B4.1].~A() (Implicit destructor)
// CHECK:     Predecessors (1): B4
// CHECK:     Successors (1): B1
// CHECK:  [ B3 ]
// CHECK:       1: return;
// CHECK:       2: [B4.1].~A() (Implicit destructor)
// CHECK:       3: [B8.3].~A() (Implicit destructor)
// CHECK:       4: [B8.1].~A() (Implicit destructor)
// CHECK:     Predecessors (1): B4
// CHECK:     Successors (1): B0
// CHECK:  [ B4 ]
// CHECK:       1: A c;
// CHECK:       2: UV
// CHECK:       T: if [B4.2]
// CHECK:     Predecessors (1): B8
// CHECK:     Successors (2): B3 B2
// CHECK:  [ B5 ]
// CHECK:       1: A d;
// CHECK:       2: [B5.1].~A() (Implicit destructor)
// CHECK:       3: [B7.1].~A() (Implicit destructor)
// CHECK:     Predecessors (1): B7
// CHECK:     Successors (1): B1
// CHECK:  [ B6 ]
// CHECK:       1: return;
// CHECK:       2: [B7.1].~A() (Implicit destructor)
// CHECK:       3: [B8.3].~A() (Implicit destructor)
// CHECK:       4: [B8.1].~A() (Implicit destructor)
// CHECK:     Predecessors (1): B7
// CHECK:     Successors (1): B0
// CHECK:  [ B7 ]
// CHECK:       1: A c;
// CHECK:       2: UV
// CHECK:       T: if [B7.2]
// CHECK:     Predecessors (1): B8
// CHECK:     Successors (2): B6 B5
// CHECK:  [ B8 ]
// CHECK:       1: A a;
// CHECK:       2: a
// CHECK:       3: if ([B8.5]) {
// CHECK: [B7.1]    if ([B7.2])
// CHECK: [B6.1][B5.1]} else {
// CHECK: [B4.1]    if ([B4.2])
// CHECK: [B3.1][B2.1]}
// CHECK:       4: b.operator int()
// CHECK:       5: [B8.4]
// CHECK:       T: if [B8.5]
// CHECK:     Predecessors (1): B9
// CHECK:     Successors (2): B7 B4
// CHECK:  [ B0 (EXIT) ]
// CHECK:     Predecessors (3): B1 B3 B6
// CHECK:     Successors (0):
// CHECK:  [ B6 (ENTRY) ]
// CHECK:     Predecessors (0):
// CHECK:     Successors (1): B5
// CHECK:  [ B1 ]
// CHECK:       1: [B2.2].~A() (Implicit destructor)
// CHECK:       2: [B5.1].~A() (Implicit destructor)
// CHECK:     Predecessors (1): B2
// CHECK:     Successors (1): B0
// CHECK:  [ B2 ]
// CHECK:       1: a
// CHECK:       2: while ([B2.4])
// CHECK: [B4.1]      3: b.operator int()
// CHECK:       4: [B2.3]
// CHECK:       T: while [B2.4]
// CHECK:     Predecessors (2): B3 B5
// CHECK:     Successors (2): B4 B1
// CHECK:  [ B3 ]
// CHECK:     Predecessors (1): B4
// CHECK:     Successors (1): B2
// CHECK:  [ B4 ]
// CHECK:       1: A c;
// CHECK:       2: [B4.1].~A() (Implicit destructor)
// CHECK:       3: [B2.2].~A() (Implicit destructor)
// CHECK:     Predecessors (1): B2
// CHECK:     Successors (1): B3
// CHECK:  [ B5 ]
// CHECK:       1: A a;
// CHECK:     Predecessors (1): B6
// CHECK:     Successors (1): B2
// CHECK:  [ B0 (EXIT) ]
// CHECK:     Predecessors (1): B1
// CHECK:     Successors (0):
// CHECK:  [ B12 (ENTRY) ]
// CHECK:     Predecessors (0):
// CHECK:     Successors (1): B11
// CHECK:  [ B1 ]
// CHECK:       1: [B2.2].~A() (Implicit destructor)
// CHECK:       2: A e;
// CHECK:       3: [B1.2].~A() (Implicit destructor)
// CHECK:       4: [B11.1].~A() (Implicit destructor)
// CHECK:     Predecessors (2): B9 B2
// CHECK:     Successors (1): B0
// CHECK:  [ B2 ]
// CHECK:       1: a
// CHECK:       2: while ([B2.4])
// CHECK:     {
// CHECK: [B10.1]        if ([B10.2])
// CHECK:             break;
// CHECK:         if ([B8.1])
// CHECK:             continue;
// CHECK:         if ([B6.1])
// CHECK: [B5.1][B4.1]    }
// CHECK:       3: b.operator int()
// CHECK:       4: [B2.3]
// CHECK:       T: while [B2.4]
// CHECK:     Predecessors (2): B3 B11
// CHECK:     Successors (2): B10 B1
// CHECK:  [ B3 ]
// CHECK:     Predecessors (2): B4 B7
// CHECK:     Successors (1): B2
// CHECK:  [ B4 ]
// CHECK:       1: A d;
// CHECK:       2: [B4.1].~A() (Implicit destructor)
// CHECK:       3: [B10.1].~A() (Implicit destructor)
// CHECK:       4: [B2.2].~A() (Implicit destructor)
// CHECK:     Predecessors (1): B6
// CHECK:     Successors (1): B3
// CHECK:  [ B5 ]
// CHECK:       1: return;
// CHECK:       2: [B10.1].~A() (Implicit destructor)
// CHECK:       3: [B2.2].~A() (Implicit destructor)
// CHECK:       4: [B11.1].~A() (Implicit destructor)
// CHECK:     Predecessors (1): B6
// CHECK:     Successors (1): B0
// CHECK:  [ B6 ]
// CHECK:       1: UV
// CHECK:       T: if [B6.1]
// CHECK:     Predecessors (1): B8
// CHECK:     Successors (2): B5 B4
// CHECK:  [ B7 ]
// CHECK:       1: [B10.1].~A() (Implicit destructor)
// CHECK:       2: [B2.2].~A() (Implicit destructor)
// CHECK:       T: continue;
// CHECK:     Predecessors (1): B8
// CHECK:     Successors (1): B3
// CHECK:  [ B8 ]
// CHECK:       1: UV
// CHECK:       T: if [B8.1]
// CHECK:     Predecessors (1): B10
// CHECK:     Successors (2): B7 B6
// CHECK:  [ B9 ]
// CHECK:       1: [B10.1].~A() (Implicit destructor)
// CHECK:       T: break;
// CHECK:     Predecessors (1): B10
// CHECK:     Successors (1): B1
// CHECK:  [ B10 ]
// CHECK:       1: A c;
// CHECK:       2: UV
// CHECK:       T: if [B10.2]
// CHECK:     Predecessors (1): B2
// CHECK:     Successors (2): B9 B8
// CHECK:  [ B11 ]
// CHECK:       1: A a;
// CHECK:     Predecessors (1): B12
// CHECK:     Successors (1): B2
// CHECK:  [ B0 (EXIT) ]
// CHECK:     Predecessors (2): B1 B5
// CHECK:     Successors (0):
// CHECK:  [ B4 (ENTRY) ]
// CHECK:     Predecessors (0):
// CHECK:     Successors (1): B2
// CHECK:  [ B1 ]
// CHECK:       1: UV
// CHECK:       T: do ... while [B1.1]
// CHECK:     Predecessors (1): B2
// CHECK:     Successors (2): B3 B0
// CHECK:  [ B2 ]
// CHECK:       1: A a;
// CHECK:       2: [B2.1].~A() (Implicit destructor)
// CHECK:     Predecessors (2): B3 B4
// CHECK:     Successors (1): B1
// CHECK:  [ B3 ]
// CHECK:     Predecessors (1): B1
// CHECK:     Successors (1): B2
// CHECK:  [ B0 (EXIT) ]
// CHECK:     Predecessors (1): B1
// CHECK:     Successors (0):
// CHECK:  [ B12 (ENTRY) ]
// CHECK:     Predecessors (0):
// CHECK:     Successors (1): B11
// CHECK:  [ B1 ]
// CHECK:       1: A d;
// CHECK:       2: [B1.1].~A() (Implicit destructor)
// CHECK:       3: [B11.1].~A() (Implicit destructor)
// CHECK:     Predecessors (2): B8 B2
// CHECK:     Successors (1): B0
// CHECK:  [ B2 ]
// CHECK:       1: UV
// CHECK:       T: do ... while [B2.1]
// CHECK:     Predecessors (2): B3 B6
// CHECK:     Successors (2): B10 B1
// CHECK:  [ B3 ]
// CHECK:       1: A c;
// CHECK:       2: [B3.1].~A() (Implicit destructor)
// CHECK:       3: [B9.1].~A() (Implicit destructor)
// CHECK:     Predecessors (1): B5
// CHECK:     Successors (1): B2
// CHECK:  [ B4 ]
// CHECK:       1: return;
// CHECK:       2: [B9.1].~A() (Implicit destructor)
// CHECK:       3: [B11.1].~A() (Implicit destructor)
// CHECK:     Predecessors (1): B5
// CHECK:     Successors (1): B0
// CHECK:  [ B5 ]
// CHECK:       1: UV
// CHECK:       T: if [B5.1]
// CHECK:     Predecessors (1): B7
// CHECK:     Successors (2): B4 B3
// CHECK:  [ B6 ]
// CHECK:       1: [B9.1].~A() (Implicit destructor)
// CHECK:       T: continue;
// CHECK:     Predecessors (1): B7
// CHECK:     Successors (1): B2
// CHECK:  [ B7 ]
// CHECK:       1: UV
// CHECK:       T: if [B7.1]
// CHECK:     Predecessors (1): B9
// CHECK:     Successors (2): B6 B5
// CHECK:  [ B8 ]
// CHECK:       1: [B9.1].~A() (Implicit destructor)
// CHECK:       T: break;
// CHECK:     Predecessors (1): B9
// CHECK:     Successors (1): B1
// CHECK:  [ B9 ]
// CHECK:       1: A b;
// CHECK:       2: UV
// CHECK:       T: if [B9.2]
// CHECK:     Predecessors (2): B10 B11
// CHECK:     Successors (2): B8 B7
// CHECK:  [ B10 ]
// CHECK:     Predecessors (1): B2
// CHECK:     Successors (1): B9
// CHECK:  [ B11 ]
// CHECK:       1: A a;
// CHECK:     Predecessors (1): B12
// CHECK:     Successors (1): B9
// CHECK:  [ B0 (EXIT) ]
// CHECK:     Predecessors (2): B1 B4
// CHECK:     Successors (0):
// CHECK:  [ B4 (ENTRY) ]
// CHECK:     Predecessors (0):
// CHECK:     Successors (1): B2
// CHECK:  [ B1 ]
// CHECK:       1: [B2.3].~A() (Implicit destructor)
// CHECK:       2: [B2.1].~A() (Implicit destructor)
// CHECK:     Predecessors (2): B3 B2
// CHECK:     Successors (1): B0
// CHECK:  [ B2 ]
// CHECK:       1: A a;
// CHECK:       2: a
// CHECK:       3: switch ([B2.4])
// CHECK: [B3.1]      4: b.operator int()
// CHECK:       T: switch [B2.4]
// CHECK:     Predecessors (1): B4
// CHECK:     Successors (1): B1
// CHECK:  [ B3 ]
// CHECK:       1: A c;
// CHECK:       2: [B3.1].~A() (Implicit destructor)
// CHECK:     Predecessors (0):
// CHECK:     Successors (1): B1
// CHECK:  [ B0 (EXIT) ]
// CHECK:     Predecessors (1): B1
// CHECK:     Successors (0):
// CHECK:  [ B9 (ENTRY) ]
// CHECK:     Predecessors (0):
// CHECK:     Successors (1): B2
// CHECK:  [ B1 ]
// CHECK:       1: [B2.3].~A() (Implicit destructor)
// CHECK:       2: A g;
// CHECK:       3: [B1.2].~A() (Implicit destructor)
// CHECK:       4: [B2.1].~A() (Implicit destructor)
// CHECK:     Predecessors (3): B3 B7 B2
// CHECK:     Successors (1): B0
// CHECK:  [ B2 ]
// CHECK:       1: A a;
// CHECK:       2: a
// CHECK:       3: switch ([B2.4]) {
// CHECK:   case 0:
// CHECK:     {
// CHECK: [B8.1]        if ([B8.2])
// CHECK:             break;
// CHECK:         if ([B6.1])
// CHECK: [B5.1][B4.1]    }
// CHECK:   case 1:
// CHECK:     break;
// CHECK: }
// CHECK:       4: b.operator int()
// CHECK:       T: switch [B2.4]
// CHECK:     Predecessors (1): B9
// CHECK:     Successors (3): B3 B8
// CHECK:      B1
// CHECK:  [ B3 ]
// CHECK:     case 1:
// CHECK:       T: break;
// CHECK:     Predecessors (2): B2 B4
// CHECK:     Successors (1): B1
// CHECK:  [ B4 ]
// CHECK:       1: A f;
// CHECK:       2: [B4.1].~A() (Implicit destructor)
// CHECK:       3: [B8.1].~A() (Implicit destructor)
// CHECK:     Predecessors (1): B6
// CHECK:     Successors (1): B3
// CHECK:  [ B5 ]
// CHECK:       1: return;
// CHECK:       2: [B8.1].~A() (Implicit destructor)
// CHECK:       3: [B2.3].~A() (Implicit destructor)
// CHECK:       4: [B2.1].~A() (Implicit destructor)
// CHECK:     Predecessors (1): B6
// CHECK:     Successors (1): B0
// CHECK:  [ B6 ]
// CHECK:       1: UV
// CHECK:       T: if [B6.1]
// CHECK:     Predecessors (1): B8
// CHECK:     Successors (2): B5 B4
// CHECK:  [ B7 ]
// CHECK:       1: [B8.1].~A() (Implicit destructor)
// CHECK:       T: break;
// CHECK:     Predecessors (1): B8
// CHECK:     Successors (1): B1
// CHECK:  [ B8 ]
// CHECK:     case 0:
// CHECK:       1: A c;
// CHECK:       2: UV
// CHECK:       T: if [B8.2]
// CHECK:     Predecessors (1): B2
// CHECK:     Successors (2): B7 B6
// CHECK:  [ B0 (EXIT) ]
// CHECK:     Predecessors (2): B1 B5
// CHECK:     Successors (0):
