// RUN: clang-cc %s -print-stats &&
// RUN: clang-cc %s -disable-free &&
// RUN: clang-cc -emit-pth -o %t %s && 
// RUN: clang-cc -token-cache %t %s &&
// RUN: clang-cc -token-cache %t %s -E %s -o /dev/null
#ifdef __APPLE__
#include <Cocoa/Cocoa.h>
#endif

