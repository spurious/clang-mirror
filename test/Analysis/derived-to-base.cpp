// RUN: %clang_cc1 -analyze -analyzer-checker=core,debug.ExprInspection -verify %s
// RUN: %clang_cc1 -analyze -analyzer-checker=core,debug.ExprInspection -DCONSTRUCTORS=1 -analyzer-config c++-inlining=constructors -verify %s

void clang_analyzer_eval(bool);

class A {
protected:
  int x;
};

class B : public A {
public:
  void f();
};

void B::f() {
  x = 3;
}


class C : public B {
public:
  void g() {
    // This used to crash because we are upcasting through two bases.
    x = 5;
  }
};


namespace VirtualBaseClasses {
  class A {
  protected:
    int x;
  };

  class B : public virtual A {
  public:
    int getX() { return x; }
  };

  class C : public virtual A {
  public:
    void setX() { x = 42; }
  };

  class D : public B, public C {};
  class DV : virtual public B, public C {};
  class DV2 : public B, virtual public C {};

  void test() {
    D d;
    d.setX();
    clang_analyzer_eval(d.getX() == 42); // expected-warning{{TRUE}}

    DV dv;
    dv.setX();
    clang_analyzer_eval(dv.getX() == 42); // expected-warning{{TRUE}}

    DV2 dv2;
    dv2.setX();
    clang_analyzer_eval(dv2.getX() == 42); // expected-warning{{TRUE}}
  }


  // Make sure we're consistent about the offset of the A subobject within an
  // Intermediate virtual base class.
  class Padding1 { int unused; };
  class Padding2 { int unused; };
  class Intermediate : public Padding1, public A, public Padding2 {};

  class BI : public virtual Intermediate {
  public:
    int getX() { return x; }
  };

  class CI : public virtual Intermediate {
  public:
    void setX() { x = 42; }
  };

  class DI : public BI, public CI {};

  void testIntermediate() {
    DI d;
    d.setX();
    clang_analyzer_eval(d.getX() == 42); // expected-warning{{TRUE}}
  }
}


namespace DynamicVirtualUpcast {
  class A {
  public:
    virtual ~A();
  };

  class B : virtual public A {};
  class C : virtual public B {};
  class D : virtual public C {};

  bool testCast(A *a) {
    return dynamic_cast<B*>(a) && dynamic_cast<C*>(a);
  }

  void test() {
    D d;
    clang_analyzer_eval(testCast(&d)); // expected-warning{{TRUE}}
  }
}

namespace DynamicMultipleInheritanceUpcast {
  class B {
  public:
    virtual ~B();
  };
  class C {
  public:
    virtual ~C();
  };
  class D : public B, public C {};

  bool testCast(B *a) {
    return dynamic_cast<C*>(a);
  }

  void test() {
    D d;
    clang_analyzer_eval(testCast(&d)); // expected-warning{{TRUE}}
  }


  class DV : virtual public B, virtual public C {};

  void testVirtual() {
    DV d;
    clang_analyzer_eval(testCast(&d)); // expected-warning{{TRUE}}
  }
}

namespace LazyBindings {
  struct Base {
    int x;
  };

  struct Derived : public Base {
    int y;
  };

  struct DoubleDerived : public Derived {
    int z;
  };

  int getX(const Base &obj) {
    return obj.x;
  }

  int getY(const Derived &obj) {
    return obj.y;
  }

  void testDerived() {
    Derived d;
    d.x = 1;
    d.y = 2;
    clang_analyzer_eval(getX(d) == 1); // expected-warning{{TRUE}}
    clang_analyzer_eval(getY(d) == 2); // expected-warning{{TRUE}}

    Base b(d);
    clang_analyzer_eval(getX(b) == 1); // expected-warning{{TRUE}}

    Derived d2(d);
    clang_analyzer_eval(getX(d2) == 1); // expected-warning{{TRUE}}
    clang_analyzer_eval(getY(d2) == 2); // expected-warning{{TRUE}}
  }

  void testDoubleDerived() {
    DoubleDerived d;
    d.x = 1;
    d.y = 2;
    clang_analyzer_eval(getX(d) == 1); // expected-warning{{TRUE}}
    clang_analyzer_eval(getY(d) == 2); // expected-warning{{TRUE}}

    Base b(d);
    clang_analyzer_eval(getX(b) == 1); // expected-warning{{TRUE}}

    Derived d2(d);
    clang_analyzer_eval(getX(d2) == 1); // expected-warning{{TRUE}}
    clang_analyzer_eval(getY(d2) == 2); // expected-warning{{TRUE}}

    DoubleDerived d3(d);
    clang_analyzer_eval(getX(d3) == 1); // expected-warning{{TRUE}}
    clang_analyzer_eval(getY(d3) == 2); // expected-warning{{TRUE}}
  }

  namespace WithOffset {
    struct Offset {
      int padding;
    };

    struct OffsetDerived : private Offset, public Base {
      int y;
    };

    struct DoubleOffsetDerived : public OffsetDerived {
      int z;
    };

    int getY(const OffsetDerived &obj) {
      return obj.y;
    }

    void testDerived() {
      OffsetDerived d;
      d.x = 1;
      d.y = 2;
      clang_analyzer_eval(getX(d) == 1); // expected-warning{{TRUE}}
      clang_analyzer_eval(getY(d) == 2); // expected-warning{{TRUE}}

      Base b(d);
      clang_analyzer_eval(getX(b) == 1); // expected-warning{{TRUE}}

      OffsetDerived d2(d);
      clang_analyzer_eval(getX(d2) == 1); // expected-warning{{TRUE}}
      clang_analyzer_eval(getY(d2) == 2); // expected-warning{{TRUE}}
    }

    void testDoubleDerived() {
      DoubleOffsetDerived d;
      d.x = 1;
      d.y = 2;
      clang_analyzer_eval(getX(d) == 1); // expected-warning{{TRUE}}
      clang_analyzer_eval(getY(d) == 2); // expected-warning{{TRUE}}

      Base b(d);
      clang_analyzer_eval(getX(b) == 1); // expected-warning{{TRUE}}

      OffsetDerived d2(d);
      clang_analyzer_eval(getX(d2) == 1); // expected-warning{{TRUE}}
      clang_analyzer_eval(getY(d2) == 2); // expected-warning{{TRUE}}

      DoubleOffsetDerived d3(d);
      clang_analyzer_eval(getX(d3) == 1); // expected-warning{{TRUE}}
      clang_analyzer_eval(getY(d3) == 2); // expected-warning{{TRUE}}
    }
  }

  namespace WithVTable {
    struct DerivedVTBL : public Base {
      int y;
      virtual void method();
    };

    struct DoubleDerivedVTBL : public DerivedVTBL {
      int z;
    };

    int getY(const DerivedVTBL &obj) {
      return obj.y;
    }

    int getZ(const DoubleDerivedVTBL &obj) {
      return obj.z;
    }

    void testDerived() {
      DerivedVTBL d;
      d.x = 1;
      d.y = 2;
      clang_analyzer_eval(getX(d) == 1); // expected-warning{{TRUE}}
      clang_analyzer_eval(getY(d) == 2); // expected-warning{{TRUE}}

      Base b(d);
      clang_analyzer_eval(getX(b) == 1); // expected-warning{{TRUE}}

#if CONSTRUCTORS
      DerivedVTBL d2(d);
      clang_analyzer_eval(getX(d2) == 1); // expected-warning{{TRUE}}
      clang_analyzer_eval(getY(d2) == 2); // expected-warning{{TRUE}}
#endif
    }

#if CONSTRUCTORS
    void testDoubleDerived() {
      DoubleDerivedVTBL d;
      d.x = 1;
      d.y = 2;
      d.z = 3;
      clang_analyzer_eval(getX(d) == 1); // expected-warning{{TRUE}}
      clang_analyzer_eval(getY(d) == 2); // expected-warning{{TRUE}}
      clang_analyzer_eval(getZ(d) == 3); // expected-warning{{TRUE}}

      Base b(d);
      clang_analyzer_eval(getX(b) == 1); // expected-warning{{TRUE}}

      DerivedVTBL d2(d);
      clang_analyzer_eval(getX(d2) == 1); // expected-warning{{TRUE}}
      clang_analyzer_eval(getY(d2) == 2); // expected-warning{{TRUE}}

      DoubleDerivedVTBL d3(d);
      clang_analyzer_eval(getX(d3) == 1); // expected-warning{{TRUE}}
      clang_analyzer_eval(getY(d3) == 2); // expected-warning{{TRUE}}
      clang_analyzer_eval(getZ(d3) == 3); // expected-warning{{TRUE}}
    }
#endif
  }
}
