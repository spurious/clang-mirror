// RUN: %clang_cc1 -triple i386-mingw32 -fsyntax-only -verify -fms-extensions -Wno-unused-value -Wno-missing-declarations -x objective-c++ %s
__stdcall int func0();
int __stdcall func();
typedef int (__cdecl *tptr)();
void (*__fastcall fastpfunc)();
struct __declspec(uuid("00000000-0000-0000-C000-000000000046")) __declspec(novtable) IUnknown {};
extern __declspec(dllimport) void __stdcall VarR4FromDec();
__declspec(deprecated) __declspec(deprecated) char * __cdecl ltoa( long _Val, char * _DstBuf, int _Radix);
__declspec(noalias) __declspec(restrict) void * __cdecl xxx( void * _Memory );
typedef __w64 unsigned long ULONG_PTR, *PULONG_PTR;
void * __ptr64 PtrToPtr64(const void *p)
{
  return((void * __ptr64) (unsigned __int64) (ULONG_PTR)p );
}
void __forceinline InterlockedBitTestAndSet (long *Base, long Bit)
{
  __asm {
    mov eax, Bit
    mov ecx, Base
    lock bts [ecx], eax
    setc al
  };
}

void *_alloca(int);

void foo() {
  __declspec(align(16)) int *buffer = (int *)_alloca(9);
}

typedef bool (__stdcall __stdcall *blarg)(int);


// Charify extension.
#define FOO(x) #@x
char x = FOO(a);

typedef enum E { e1 };


void uuidof_test1()
{  
  __uuidof(0);  // expected-error {{you need to include <guiddef.h> before using the '__uuidof' operator}}
}

typedef struct _GUID
{
    unsigned long  Data1;
    unsigned short Data2;
    unsigned short Data3;
    unsigned char  Data4[8];
} GUID;
struct __declspec(uuid("00000000-0000-0000-3231-000000000046")) A { };
struct __declspec(uuid("00000000-0000-0000-1234-000000000047")) B { };
class C {};

void uuidof_test2()
{
  A* a = new A;
  B b;
  __uuidof(A);
  __uuidof(*a);
  __uuidof(B);
  __uuidof(&b);
  _uuidof(0);

   // FIXME, this must not compile
  _uuidof(1);
   // FIXME, this must not compile
  _uuidof(C);

   C c;
   // FIXME, this must not compile
  _uuidof(c);
}
