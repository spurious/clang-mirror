// RUN: %clang -no-canonical-prefixes -target x86_64-apple-macosx10.7.0 -rewrite-legacy-objc %s -o - -### 2>&1 | \
// RUN:   FileCheck -check-prefix=TEST0 %s
// RUN: %clang -x objective-c -arch i386 -rewrite-legacy-objc %s -o - -### 2>&1 | \
// RUN:   FileCheck -check-prefix=TEST1 %s
// TEST0: clang{{.*}}" "-cc1"
// TEST0: "-rewrite-objc"
// FIXME: CHECK-NOT is broken somehow, it doesn't work here. Check adjacency instead.
// TEST0: "-fmessage-length" "0" "-stack-protector" "1" "-mstackrealign" "-fblocks" "-fobjc-runtime=macosx-fragile" "-fobjc-subscripting-legacy-runtime" "-fencode-extended-block-signature" "-fno-objc-infer-related-result-type" "-fobjc-exceptions" "-fexceptions" "-fdiagnostics-show-option"
// TEST0: rewrite-legacy-objc.m"
// TEST1: "-fmessage-length" "0" "-stack-protector" "1" "-mstackrealign" "-fblocks" "-fobjc-runtime=macosx-fragile" "-fobjc-subscripting-legacy-runtime" 
