// RUN: %clang -cc1 -fsyntax-only -F %S/Inputs -Wsign-conversion -verify %s

// Check that TestFramework is treated as a system header.
#include <TestFramework/TestFramework.h>

int f1() {
  return test_framework_func(1) + another_test_framework_func(2);
}
