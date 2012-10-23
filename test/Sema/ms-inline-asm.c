// RUN: %clang_cc1 %s -triple x86_64-apple-darwin10 -fms-extensions -fenable-experimental-ms-inline-asm -Wno-microsoft -verify -fsyntax-only

void t1(void) { 
 __asm __asm // expected-error {{__asm used with no assembly instructions}}
}

void f() {
  __asm { 
    mov eax, eax
    .unknowndirective // expected-error {{unknown directive}}
  }
  f();
  __asm {
    mov eax, 1+=2 // expected-error 2 {{unknown token in expression}}
  }
  f();
  __asm {
    mov eax, 1+++ // expected-error 2 {{unknown token in expression}}
  }
}
