// RUN: %clang_cc1 -Wcstring-format-directive -verify -fsyntax-only %s
// rdar://18182443

typedef __builtin_va_list __darwin_va_list;
typedef __builtin_va_list va_list;

@interface NSString @end

@interface NSString (NSStringExtensionMethods)
- (NSString *)stringByAppendingFormat:(NSString *)format, ... __attribute__((format(__NSString__, 1, 2)));
- (instancetype)initWithFormat:(NSString *)format, ... __attribute__((format(__NSString__, 1, 2))); // expected-note {{method 'initWithFormat:' declared here}}
- (instancetype)initWithFormat:(NSString *)format arguments:(va_list)argList __attribute__((format(__NSString__, 1, 0)));
- (instancetype)initWithFormat:(NSString *)format locale:(id)locale, ... __attribute__((format(__NSString__, 1, 3)));
- (instancetype)initWithFormat:(NSString *)format locale:(id)locale arguments:(va_list)argList __attribute__((format(__NSString__, 1, 0)));
+ (instancetype)stringWithFormat:(NSString *)format, ... __attribute__((format(__NSString__, 1, 2))); // expected-note {{method 'stringWithFormat:' declared here}}
+ (instancetype)localizedStringWithFormat:(NSString *)format, ... __attribute__((format(__NSString__, 1, 2)));
@end

@interface NSMutableString : NSString
@end

@interface NSMutableString (NSMutableStringExtensionMethods)

- (void)appendFormat:(NSString *)format, ... __attribute__((format(__NSString__, 1, 2)));

@end

NSString *ns(NSString *pns) {
  [pns initWithFormat: @"Number %d length %c name %s", 1, 'a', "something"]; // expected-warning {{using %s directive in NSString which is being passed as a formatting argument to the formatting method}}
  return [NSString stringWithFormat : @"Hello%s", " There"]; // expected-warning {{using %s directive in NSString which is being passed as a formatting argument to the formatting method}}
}


typedef const struct __CFString * CFStringRef;
typedef struct __CFString * CFMutableStringRef;
typedef const struct __CFAllocator * CFAllocatorRef;


typedef const struct __CFDictionary * CFDictionaryRef;


extern
CFStringRef CFStringCreateWithFormat(CFAllocatorRef alloc, CFDictionaryRef formatOptions, CFStringRef format, ...) __attribute__((format(CFString, 3, 4)));

extern
CFStringRef CFStringCreateWithFormatAndArguments(CFAllocatorRef alloc, CFDictionaryRef formatOptions, CFStringRef format, va_list arguments) __attribute__((format(CFString, 3, 0))); // expected-note {{'CFStringCreateWithFormatAndArguments' declared here}}

extern
void CFStringAppendFormat(CFMutableStringRef theString, CFDictionaryRef formatOptions, CFStringRef format, ...) __attribute__((format(CFString, 3, 4)));

extern
void CFStringAppendFormatAndArguments(CFMutableStringRef theString, CFDictionaryRef formatOptions, CFStringRef format, va_list arguments) __attribute__((format(CFString, 3, 0))); // expected-note {{'CFStringAppendFormatAndArguments' declared here}}

void foo(va_list argList) {
  CFAllocatorRef alloc;
  CFStringCreateWithFormatAndArguments (alloc, 0, (CFStringRef)@"%s\n", argList); // expected-warning {{using %s directive in CFString which is being passed as a formatting argument to the formatting CFfunction}}
  CFStringAppendFormatAndArguments ((CFMutableStringRef)@"AAAA", 0, (CFStringRef)"Hello %s there %d\n", argList); // expected-warning {{using %s directive in CFString which is being passed as a formatting argument to the formatting CFfunction}}
  CFStringCreateWithFormatAndArguments (alloc, 0, (CFStringRef)@"%c\n", argList);
  CFStringAppendFormatAndArguments ((CFMutableStringRef)@"AAAA", 0, (CFStringRef)"%d\n", argList);
}
