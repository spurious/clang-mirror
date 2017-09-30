// RUN: %clang_cc1 -triple x86_64-unknown-unknown -fsyntax-only -verify -fms-extensions %s
typedef long HRESULT;
typedef unsigned long ULONG;
typedef struct _GUID {
  unsigned long Data1;
  unsigned short Data2;
  unsigned short Data3;
  unsigned char Data4[8];
} GUID;
typedef GUID IID;

// remove stdcall, since the warnings have nothing to do with
// what is being tested.
#define __stdcall

extern "C" {
extern "C++" {
// expected-warning@+1 {{__declspec attribute 'novtable'}}
struct __declspec(uuid("00000000-0000-0000-C000-000000000046")) __declspec(novtable)
    IUnknown {
public:
  virtual HRESULT __stdcall QueryInterface(
      const IID &riid,
      void **ppvObject) = 0;

  virtual ULONG __stdcall AddRef(void) = 0;

  virtual ULONG __stdcall Release(void) = 0;

  template <class Q>
  HRESULT __stdcall QueryInterface(Q **pp) {
    return QueryInterface(__uuidof(Q), (void **)pp);
  }
};
}
}

__interface ISfFileIOPropertyPage : public IUnknown{};

