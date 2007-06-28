/*
  RUN: clang -E %s | grep bar &&
  RUN: clang -E %s | grep foo &&
  RUN: clang -E %s | not grep abc &&
  RUN: clang -E %s | not grep xyz &&
  RUN: clang -parse-ast-check %s
 */

/* abc

next comment ends with normal escaped newline:
*/

/* expected-warning {{escaped newline}} expected-warning {{backslash and newline}}  *\  
/

bar

/* xyz

next comment ends with a trigraph escaped newline: */

/* expected-warning {{escaped newline between}}   expected-warning {{backslash and newline separated by space}}    expected-warning {{trigraph ends block comment}}   *??/    
/

foo /* expected-error {{expected '=', ',', ';', 'asm', or '__attribute__' after declarator}} */

