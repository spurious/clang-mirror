// RUN: clang -fsyntax-only -verify %s

// See if aliasing can confuse this baby.
typedef char c;
typedef c *cp;
typedef cp *cpp;
typedef cpp *cppp;
typedef cppp &cpppr;
typedef const cppp &cpppcr;
typedef const char cc;
typedef cc *ccp;
typedef volatile ccp ccvp;
typedef ccvp *ccvpp;
typedef const volatile ccvpp ccvpcvp;
typedef ccvpcvp *ccvpcvpp;
typedef int iar[100];
typedef iar &iarr;
typedef int (*f)(int);

char ***good_const_cast_test(ccvpcvpp var)
{
  // Cast away deep consts and volatiles.
  char ***var2 = const_cast<cppp>(var);
  char ***const &var3 = static_cast<cpppcr>(var2); // Different bug.
  // Const reference to reference.
  char ***&var4 = const_cast<cpppr>(var3);
  // Drop reference. Intentionally without qualifier change.
  char *** var5 = const_cast<cppp>(var4);
  const int ar[100] = {0};
  int (&rar)[100] = const_cast<iarr>(ar); // expected-warning {{statement was disambiguated as declaration}} expected-error {{const_cast from 'int const [100]' to 'iarr' is not allowed}}
  // Array decay. Intentionally without qualifier change.
  int *pi = const_cast<int*>(ar);
  f fp = 0;
  // Don't misidentify fn** as a function pointer.
  f *fpp = const_cast<f*>(&fp);
  return var4;
}

short *bad_const_cast_test(char const *volatile *const volatile *var)
{
  // Different pointer levels.
  char **var2 = const_cast<char**>(var); // expected-error {{const_cast from 'char const *volatile *const volatile *' to 'char **' is not allowed}}
  // Different final type.
  short ***var3 = const_cast<short***>(var); // expected-error {{const_cast from 'char const *volatile *const volatile *' to 'short ***' is not allowed}}
  // Rvalue to reference.
  char ***&var4 = const_cast<cpppr>(&var2); // expected-error {{const_cast from rvalue to reference type 'cpppr'}}
  // Non-pointer.
  char v = const_cast<char>(**var2); // expected-error {{const_cast to 'char', which is not a reference, pointer-to-object, or pointer-to-data-member}}
  const int *ar[100] = {0};
  // Not even lenient g++ accepts this.
  int *(*rar)[100] = const_cast<int *(*)[100]>(&ar); // expected-error {{const_cast from 'int const *(*)[100]' to 'int *(*)[100]' is not allowed}}
  f fp1 = 0;
  // Function pointers.
  f fp2 = const_cast<f>(fp1); // expected-error {{const_cast to 'f', which is not a reference, pointer-to-object, or pointer-to-data-member}}
  return **var3;
}
