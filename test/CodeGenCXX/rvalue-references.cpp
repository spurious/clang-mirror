// RUN: %clang_cc1 -std=c++0x -triple x86_64-apple-darwin10 -emit-llvm -o - %s | FileCheck %s


struct Spacer { int x; };
struct A { double array[2]; };
struct B : Spacer, A { };

B &getB();

// CHECK: define %struct.A* @_Z4getAv()
// CHECK: call %struct.B* @_Z4getBv()
// CHECK-NEXT: bitcast %struct.B*
// CHECK-NEXT: getelementptr i8*
// CHECK-NEXT: bitcast i8* {{.*}} to %struct.A*
// CHECK-NEXT: ret %struct.A*
A &&getA() { return static_cast<A&&>(getB()); }

int &getIntLValue();
int &&getIntXValue();
int getIntPRValue();

// CHECK: define i32* @_Z2f0v()
// CHECK: call i32* @_Z12getIntLValuev()
// CHECK-NEXT: ret i32*
int &&f0() { return static_cast<int&&>(getIntLValue()); }

// CHECK: define i32* @_Z2f1v()
// CHECK: call i32* @_Z12getIntXValuev()
// CHECK-NEXT: ret i32*
int &&f1() { return static_cast<int&&>(getIntXValue()); }

// CHECK: define i32* @_Z2f2v
// CHECK: call i32 @_Z13getIntPRValuev()
// CHECK-NEXT: store i32 {{.*}}, i32*
// CHECK-NEXT: ret i32*
int &&f2() { return static_cast<int&&>(getIntPRValue()); }

bool ok;

class C
{
   int* state_;

   C(const C&) = delete;
   C& operator=(const C&) = delete;
public:
  C(int state) : state_(new int(state)) { }
  
  C(C&& a) {
    state_ = a.state_; 
    a.state_ = 0;
  }

  ~C() {
    delete state_; 
    state_ = 0;
  }
};

C test();

// CHECK: define void @_Z15elide_copy_initv
void elide_copy_init() {
  ok = false;
  // CHECK: call void @_Z4testv
  C a = test();
  // CHECK-NEXT: call void @_ZN1CD1Ev
  // CHECK-NEXT: ret void
}

// CHECK: define void @_Z16test_move_returnv
C test_move_return() {
  // CHECK: call void @_ZN1CC1Ei
  C a1(3);
  // CHECK: call void @_ZN1CC1Ei
  C a2(4);
  if (ok)
    // CHECK: call void @_ZN1CC1EOS_
    return a1;
  // CHECK: call void @_ZN1CC1EOS_
  return a2;
  // CHECK: call void @_ZN1CD1Ev
  // CHECK: call void @_ZN1CD1Ev
  //CHECK:  ret void
}
