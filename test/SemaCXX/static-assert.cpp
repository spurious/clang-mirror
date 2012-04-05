// RUN: %clang_cc1 -fsyntax-only -verify %s -std=c++11

int f(); // expected-note {{declared here}}

static_assert(f(), "f"); // expected-error {{static_assert expression is not an integral constant expression}} expected-note {{non-constexpr function 'f' cannot be used in a constant expression}}
static_assert(true, "true is not false");
static_assert(false, "false is false"); // expected-error {{static_assert failed "false is false"}}

void g() {
    static_assert(false, "false is false"); // expected-error {{static_assert failed "false is false"}}
}

class C {
    static_assert(false, "false is false"); // expected-error {{static_assert failed "false is false"}}
};

template<int N> struct T {
    static_assert(N == 2, "N is not 2!"); // expected-error {{static_assert failed "N is not 2!"}}
};

T<1> t1; // expected-note {{in instantiation of template class 'T<1>' requested here}}
T<2> t2;

template<typename T> struct S {
    static_assert(sizeof(T) > sizeof(char), "Type not big enough!"); // expected-error {{static_assert failed "Type not big enough!"}}
};

S<char> s1; // expected-note {{in instantiation of template class 'S<char>' requested here}}
S<int> s2;

static_assert(false, L"\xFFFFFFFF"); // expected-error {{static_assert failed L"\xFFFFFFFF"}}
static_assert(false, u"\U000317FF"); // expected-error {{static_assert failed u"\U000317FF"}}
// FIXME: render this as u8"\u03A9"
static_assert(false, u8"Ω"); // expected-error {{static_assert failed u8"\316\251"}}
static_assert(false, L"\u1234"); // expected-error {{static_assert failed L"\x1234"}}
static_assert(false, L"\x1ff" "0\x123" "fx\xfffff" "goop"); // expected-error {{static_assert failed L"\x1FF""0\x123""fx\xFFFFFgoop"}}
