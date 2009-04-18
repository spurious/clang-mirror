// Test this without pch.
// RUN: clang-cc -triple=x86_64-unknown-freebsd7.0 -include %S/va_arg.h %s

// Test with pch.
// RUN: clang-cc -triple=x86_64-unknown-freebsd7.0 -o %t %S/va_arg.h &&
// RUN: clang-cc -triple=x86_64-unknown-freebsd7.0 -include-pch %t %s 

// FIXME: Crash when emitting LLVM bitcode using PCH!
char *g0(char** argv, int argc) { return argv[argc]; }

char *g(char **argv) {
  f(g0, argv, 1, 2, 3);
}
