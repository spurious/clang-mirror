// RUN: %clang_cc1 -emit-pch %s -o %t
// RUN: %clang_cc1 -include-pch %t -verify %s

#ifndef HEADER_INCLUDED

#define HEADER_INCLUDED

@protocol NSObject
@end

__attribute__((objc_root_class))
@interface NSObject
@end

@interface PC1<T, U : NSObject *> : NSObject
// expected-note@-2{{type parameter 'U' declared here}}
@end

@interface PC1<T, U : NSObject *> (Cat1)
@end

#else

@interface PC1<T : NSObject *, // expected-error{{type bound 'NSObject *' for type parameter 'T' conflicts with implicit bound 'id}}
 // expected-note@15{{type parameter 'T' declared here}}
               U : id> (Cat2) // expected-error{{type bound 'id' for type parameter 'U' conflicts with previous bound 'NSObject *'}}
 // expected-note@15{{type parameter 'U' declared here}}
@end

#endif
