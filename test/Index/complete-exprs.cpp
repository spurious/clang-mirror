// Line- and column-sensitive test; run lines follow.

class string {
 public:
  string();
  string(const char *);
  string(const char *, int n);
};

template<typename T>
class vector {
public:
  vector(const T &, unsigned n);

  template<typename InputIterator>
  vector(InputIterator first, InputIterator last);
};

void f() {
  
}

// RUN: c-index-test -code-completion-at=%s:20:2 %s | FileCheck -check-prefix=CHECK-CC1 %s
// RUN: env CINDEXTEST_EDITING=1 CINDEXTEST_COMPLETION_CACHING=1 c-index-test -code-completion-at=%s:20:2 %s | FileCheck -check-prefix=CHECK-CC1 %s
// CHECK-CC1: ClassDecl:{TypedText string} (50)
// CHECK-CC1: CXXConstructor:{TypedText string}{LeftParen (}{RightParen )} (50)
// CHECK-CC1: CXXConstructor:{TypedText string}{LeftParen (}{Placeholder const char *}{RightParen )} (50)
// CHECK-CC1: CXXConstructor:{TypedText string}{LeftParen (}{Placeholder const char *}{Comma , }{Placeholder int n}{RightParen )} (50)
// CHECK-CC1: ClassTemplate:{TypedText vector}{LeftAngle <}{Placeholder typename T}{RightAngle >} (50)
// CHECK-CC1: CXXConstructor:{TypedText vector}{LeftAngle <}{Placeholder typename T}{RightAngle >}{LeftParen (}{Placeholder T const &}{Comma , }{Placeholder unsigned int n}{RightParen )} (50)
// CHECK-CC1: FunctionTemplate:{ResultType void}{TypedText vector}{LeftAngle <}{Placeholder typename T}{RightAngle >}{LeftParen (}{Placeholder InputIterator first}{Comma , }{Placeholder InputIterator last}{RightParen )} (50)

// RUN: c-index-test -code-completion-at=%s:19:1 %s | FileCheck -check-prefix=CHECK-CC2 %s
// RUN: env CINDEXTEST_EDITING=1 CINDEXTEST_COMPLETION_CACHING=1 c-index-test -code-completion-at=%s:19:1 %s | FileCheck -check-prefix=CHECK-CC2 %s
// CHECK-CC2: ClassDecl:{TypedText string} (50)
// CHECK-CC2-NOT: CXXConstructor
// CHECK-CC2: ClassTemplate:{TypedText vector}{LeftAngle <}{Placeholder typename T}{RightAngle >} (50)
