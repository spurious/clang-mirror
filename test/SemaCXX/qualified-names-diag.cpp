// RUN: clang -fsyntax-only -verify %s
namespace foo {
  namespace wibble {
    struct x { int y; };

    namespace bar {
      namespace wonka {
        struct x {
          struct y { };
        };
      }
    }
  }
}

namespace bar {
  typedef int y;
}
void test() {
  foo::wibble::x a;
  ::bar::y b;
  a + b; // expected-error{{invalid operands to binary expression ('foo::wibble::x' (aka 'struct x') and '::bar::y' (aka 'int'))}}

  ::foo::wibble::bar::wonka::x::y c;
  c + b; // expected-error{{invalid operands to binary expression ('::foo::wibble::bar::wonka::x::y' (aka 'struct y') and '::bar::y' (aka 'int'))}}
}

int ::foo::wibble::bar::wonka::x::y::* ptrmem;
