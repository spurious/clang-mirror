// RUN: %clang_cc1 -fsyntax-only -Wnull-character %s 2>&1 | FileCheck -strict-whitespace %s
// CHECK: L"a<U+0000>b"
wchar_t const *w = L"a b";
