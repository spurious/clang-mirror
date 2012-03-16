// RUN: %clang_cc1 -x objective-c -Wno-return-type -fblocks -fms-extensions -rewrite-objc %s -o %t-rw.cpp
// RUN: %clang_cc1 -fsyntax-only -fcxx-exceptions -fexceptions  -Wno-address-of-temporary -D"id=void*" -D"SEL=void*" -D"__declspec(X)=" %t-rw.cpp

void *sel_registerName(const char *);

id SYNCH_EXPR();
void SYNCH_BODY();
void  SYNCH_BEFORE();
void  SYNC_AFTER();

void foo(id sem)
{
  SYNCH_BEFORE();
  @synchronized (SYNCH_EXPR()) { 
    SYNCH_BODY();
    return;
  }
 SYNC_AFTER();
 @synchronized ([sem self]) {
    SYNCH_BODY();
    return;
 }
}
