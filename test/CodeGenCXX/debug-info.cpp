// RUN: clang-cc -emit-llvm-only -g
template<typename T> struct Identity {
  typedef T Type;
};

void f(Identity<int>::Type a) {}
