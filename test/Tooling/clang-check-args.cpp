// RUN: clang-check . "%s" -- -c 2>&1 | FileCheck %s

// CHECK: C++ requires
invalid;

// FIXME: JSON doesn't like path separator '\', on Win32 hosts.
// XFAIL: win32
