// RUN: %clang_cc1 -std=c++11 -fsyntax-only -verify -x c++ %s

// This file is encoded using ISO-8859-1

int main() {
  '�'; // expected-error {{illegal sequence in character literal}}
  u'�'; // expected-error {{illegal sequence in character literal}}
  U'�'; // expected-error {{illegal sequence in character literal}}
  L'�'; // expected-error {{illegal sequence in character literal}}
}
