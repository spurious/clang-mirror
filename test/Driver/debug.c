// RUN: cd %S && %clang -### -g %s -c 2>&1 | grep '"-fdebug-compilation-dir" "'%S'"'
// RUN: (sv PWD=/foo && %clang -### -g %s -c 2>&1 | grep '"-fdebug-compilation-dir" "/foo"') || \
// RUN: PWD=/foo %clang -### -g %s -c 2>&1 | grep '"-fdebug-compilation-dir" "/foo"'

// This test uses grep instead of FileCheck so that we get %S -> dirname
// substitution.

// "PWD=/foo gcc" wouldn't necessarily work. You would need to pick a different
// path to the same directory (try a symlink).
