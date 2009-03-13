// RUN: clang -fsyntax-only %s
typedef char one_byte;
typedef char (&two_bytes)[2];
typedef char (&four_bytes)[4];
typedef char (&eight_bytes)[8];

template<int N> struct A { };

namespace N1 {
  struct X { };
}

namespace N2 {
  struct Y { };

  two_bytes operator+(Y, Y);
}

namespace N3 {
  struct Z { };

  eight_bytes operator+(Z, Z);
}

namespace N4 {
  one_byte operator+(N1::X, N2::Y);

  template<typename T, typename U>
  struct BinOpOverload {
    typedef A<sizeof(T() + U())> type;
  };
}

namespace N1 {
  four_bytes operator+(X, X);
}

namespace N3 {
  eight_bytes operator+(Z, Z); // redeclaration
}

void test_bin_op_overload(A<1> *a1, A<2> *a2, A<4> *a4, A<8> *a8) {
  typedef N4::BinOpOverload<N1::X, N2::Y>::type XY;
  XY *xy = a1;
  typedef N4::BinOpOverload<N1::X, N1::X>::type XX;
  XX *xx = a4;
  typedef N4::BinOpOverload<N2::Y, N2::Y>::type YY;
  YY *yy = a2;
  typedef N4::BinOpOverload<N3::Z, N3::Z>::type ZZ;
  ZZ *zz = a8;
}

