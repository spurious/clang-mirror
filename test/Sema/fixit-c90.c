/* RUN: clang -fsyntax-only -std=c90 -pedantic -fixit %s -o - | clang-cc -pedantic -x c -std=c90 -Werror -
 */
/* This is a test of the various code modification hints that are
   provided as part of warning or extension diagnostics. Eventually,
   we would like to actually try to perform the suggested
   modifications and compile the result to test that no warnings
   remain. */

enum e0 {
  e1,
};
