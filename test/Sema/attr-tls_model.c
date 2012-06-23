// RUN: %clang -Xclang -verify -fsyntax-only %s

#if !__has_attribute(tls_model)
#error "Should support tls_model attribute"
#endif

int f() __attribute((tls_model("global-dynamic"))); // expected-error {{'tls_model' attribute only applies to thread-local variables}}

int x __attribute((tls_model("global-dynamic"))); // expected-error {{'tls_model' attribute only applies to thread-local variables}}
static __thread int y __attribute((tls_model("global-dynamic"))); // no-warning

static __thread int y __attribute((tls_model("local", "dynamic"))); // expected-error {{attribute takes one argument}}
static __thread int y __attribute((tls_model(123))); // expected-error {{argument to tls_model attribute was not a string literal}}
static __thread int y __attribute((tls_model("foobar"))); // expected-error {{tls_model must be "global-dynamic", "local-dynamic", "initial-exec" or "local-exec"}}
