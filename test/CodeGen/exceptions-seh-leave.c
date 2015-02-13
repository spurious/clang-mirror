// RUN: %clang_cc1 %s -triple x86_64-pc-win32 -fms-extensions -emit-llvm -o - | FileCheck %s

// FIXME: Rewrite CHECKs for unnamed BBs and Insts.
// REQUIRES: asserts

void g();

//////////////////////////////////////////////////////////////////////////////
// __leave with __except

// Nothing in the __try block can trap, so __try.cont isn't created.
int __leave_with___except_simple() {
  int myres = 0;
  __try {
    myres = 15;
    __leave;
    myres = 23;
  } __except (1) {
    return 0;
  }
  return 1;
}
// CHECK-LABEL: define i32 @__leave_with___except_simple()
// CHECK: store i32 15, i32* %myres
// CHECK-NEXT: br label %__try.__leave
// CHECK-NOT: store i32 23
// CHECK: __try.__leave:
// CHECK-NEXT: ret i32 1


// The "normal" case.
int __leave_with___except() {
  int myres = 0;
  __try {
    g();
    __leave;
    myres = 23;
  } __except (1) {
    return 0;
  }
  return 1;
}
// CHECK-LABEL: define i32 @__leave_with___except()
// CHECK: invoke void bitcast (void (...)* @g to void ()*)()
// CHECK-NEXT:       to label %[[cont:.*]] unwind label %{{.*}}
// For __excepts, instead of an explicit __try.__leave label, we could use
// use invoke.cont as __leave jump target instead.  However, not doing this
// keeps the CodeGen code simpler, __leave is very rare, and SimplifyCFG will
// simplify this anyways.
// CHECK: [[cont]]
// CHECK-NEXT: br label %__try.__leave
// CHECK-NOT: store i32 23
// CHECK: __try.__leave:
// CHECK-NEXT: br label %__try.cont


//////////////////////////////////////////////////////////////////////////////
// __leave with __finally

void abort(void) __attribute__((noreturn));

// Nothing in the __try block can trap, so __finally.cont and friends aren't
// created.
int __leave_with___finally_simple() {
  int myres = 0;
  __try {
    myres = 15;
    __leave;
    myres = 23;
  } __finally {
    return 0;
  }
  return 1;
}
// CHECK-LABEL: define i32 @__leave_with___finally_simple()
// CHECK: store i32 15, i32* %myres
// CHECK-NEXT: br label %__try.__leave
// CHECK-NOT: store i32 23
// CHECK: __try.__leave:
// CHECK-NEXT: store i8 0, i8* %abnormal.termination.slot
// CHECK-NEXT: br label %__finally

// __finally block doesn't return, __finally.cont doesn't exist.
int __leave_with___finally_noreturn() {
  int myres = 0;
  __try {
    myres = 15;
    __leave;
    myres = 23;
  } __finally {
    abort();
  }
  return 1;
}
// CHECK-LABEL: define i32 @__leave_with___finally_noreturn()
// CHECK: store i32 15, i32* %myres
// CHECK-NEXT: br label %__try.__leave
// CHECK-NOT: store i32 23
// CHECK: __try.__leave:
// CHECK-NEXT: store i8 0, i8* %abnormal.termination.slot
// CHECK-NEXT: br label %__finally

// The "normal" case.
int __leave_with___finally() {
  int myres = 0;
  __try {
    g();
    __leave;
    myres = 23;
  } __finally {
    return 0;
  }
  return 1;
}
// CHECK-LABEL: define i32 @__leave_with___finally()
// CHECK: invoke void bitcast (void (...)* @g to void ()*)()
// CHECK-NEXT:       to label %[[cont:.*]] unwind label %{{.*}}
// For __finally, there needs to be an explicit __try.__leave, because
// abnormal.termination.slot needs to be set there.
// CHECK: [[cont]]
// CHECK-NEXT: br label %__try.__leave
// CHECK-NOT: store i32 23
// CHECK: __try.__leave:
// CHECK-NEXT: store i8 0, i8* %abnormal.termination.slot
// CHECK-NEXT: br label %__finally


//////////////////////////////////////////////////////////////////////////////
// Mixed, nested cases.

// FIXME: Test with outer __finally once PR22553 is fixed.

int nested___except___finally() {
  int myres = 0;
  __try {
    __try {
      g();
    } __finally {
      g();
      __leave;  // Refers to the outer __try, not the __finally!
      myres = 23;
      return 0;
    }

    myres = 51;
  } __except (1) {
  }
  return 1;
}
// The order of basic blocks in the below doesn't matter.
// CHECK-LABEL: define i32 @nested___except___finally()

// CHECK-LABEL: invoke void bitcast (void (...)* @g to void ()*)()
// CHECK-NEXT:       to label %[[g1_cont:.*]] unwind label %[[g1_lpad:.*]]

// CHECK: [[g1_cont]]:
// CHECK-NEXT: store i8 0, i8* %abnormal.termination.slot
// CHECK-NEXT: br label %__finally

// CHECK-LABEL: __finally:
// CHECK-NEXT: invoke void bitcast (void (...)* @g to void ()*)() #3
// CHECK-NEXT:       to label %[[g2_cont:.*]] unwind label %[[g2_lpad:.*]]

// CHECK: [[g2_cont]]:
// CHECK-NOT: store i32 23
// CHECK: br label %__try.__leave

// CHECK: [[g1_lpad]]:
// CHECK: store i8 1, i8* %abnormal.termination.slot
// CHECK-NEXT:  br label %__finally

// CHECK: [[g2_lpad]]:
// CHECK-NOT: %abnormal.termination.slot
// CHECK: br label %__except

// CHECK-LABEL: __except:
// CHECK-NEXT: br label %__try.cont

// CHECK-LABEL: __try.__leave:
// CHECK-NEXT: br label %__try.cont

int nested___except___except() {
  int myres = 0;
  __try {
    __try {
      g();
      myres = 16;
    } __except (1) {
      g();
      __leave;  // Refers to the outer __try, not the __except we're in!
      myres = 23;
      return 0;
    }

    myres = 51;
  } __except (1) {
  }
  return 1;
}
// The order of basic blocks in the below doesn't matter.
// CHECK-LABEL: define i32 @nested___except___except()

// CHECK-LABEL: invoke void bitcast (void (...)* @g to void ()*)()
// CHECK-NEXT:       to label %[[g1_cont:.*]] unwind label %[[g1_lpad:.*]]

// CHECK: [[g1_cont]]:
// CHECK: store i32 16, i32* %myres
// CHECK-NEXT: br label %__try.cont

// CHECK: [[g1_lpad]]:
// CHECK:  br label %__except

// CHECK-LABEL: __except:
// CHECK-NEXT: invoke void bitcast (void (...)* @g to void ()*)() #3
// CHECK-NEXT:       to label %[[g2_cont:.*]] unwind label %[[g2_lpad:.*]]

// CHECK: [[g2_cont]]:
// CHECK-NOT: store i32 23
// CHECK: br label %__try.__leave

// CHECK: [[g2_lpad]]:
// CHECK: br label %__except3

// CHECK-LABEL: __except3:
// CHECK-NEXT: br label %__try.cont4

// CHECK-LABEL: __try.cont:
// CHECK-NEXT: store i32 51, i32* %myres
// CHECK-NEXT: br label %__try.__leave

// CHECK-LABEL: __try.__leave:
// CHECK-NEXT: br label %__try.cont4
