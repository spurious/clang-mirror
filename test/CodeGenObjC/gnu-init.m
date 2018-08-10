// RUN: %clang_cc1 -triple x86_64-unknown-freebsd -S -emit-llvm -fobjc-runtime=gnustep-2.0 -o - %s | FileCheck %s -check-prefix=CHECK-NEW
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc -S -emit-llvm -fobjc-runtime=gnustep-2.0 -o - %s | FileCheck %s -check-prefix=CHECK-WIN
// RUN: %clang_cc1 -triple x86_64-unknown-freebsd -S -emit-llvm -fobjc-runtime=gnustep-1.8 -o - %s | FileCheck %s -check-prefix=CHECK-OLD

// Almost minimal Objective-C file, check that it emits calls to the correct
// runtime entry points.
@interface X @end
@implementation X @end


// Check that we emit a class ref
// CHECK-NEW: @._OBJC_REF_CLASS_X 
// CHECK-NEW-SAME: section "__objc_class_refs"

// Check that we get a class ref to the defined class.
// CHECK-NEW: @._OBJC_INIT_CLASS_X = global 
// CHECK-NEW-SAME* @._OBJC_CLASS_X, section "__objc_classes"

// Check that we emit the section start and end symbols as hidden globals.
// CHECK-NEW: @__start___objc_selectors = external hidden global i8*
// CHECK-NEW: @__stop___objc_selectors = external hidden global i8*
// CHECK-NEW: @__start___objc_classes = external hidden global i8*
// CHECK-NEW: @__stop___objc_classes = external hidden global i8*
// CHECK-NEW: @__start___objc_class_refs = external hidden global i8*
// CHECK-NEW: @__stop___objc_class_refs = external hidden global i8*
// CHECK-NEW: @__start___objc_cats = external hidden global i8*
// CHECK-NEW: @__stop___objc_cats = external hidden global i8*
// CHECK-NEW: @__start___objc_protocols = external hidden global i8*
// CHECK-NEW: @__stop___objc_protocols = external hidden global i8*
// CHECK-NEW: @__start___objc_protocol_refs = external hidden global i8*
// CHECK-NEW: @__stop___objc_protocol_refs = external hidden global i8*
// CHECK-NEW: @__start___objc_class_aliases = external hidden global i8*
// CHECK-NEW: @__stop___objc_class_aliases = external hidden global i8*
// CHECK-NEW: @__start___objc_constant_string = external hidden global i8*
// CHECK-NEW: @__stop___objc_constant_string = external hidden global i8*

// Check that we emit the init structure correctly, including in a comdat.
// CHECK-NEW: @.objc_init = linkonce_odr hidden global { i64, i8**, i8**, i8**, i8**, i8**, i8**, i8**, i8**, i8**, i8**, i8**, i8**, i8**, i8**, i8**, i8** } { i64 0, i8** @__start___objc_selectors, i8** @__stop___objc_selectors, i8** @__start___objc_classes, i8** @__stop___objc_classes, i8** @__start___objc_class_refs, i8** @__stop___objc_class_refs, i8** @__start___objc_cats, i8** @__stop___objc_cats, i8** @__start___objc_protocols, i8** @__stop___objc_protocols, i8** @__start___objc_protocol_refs, i8** @__stop___objc_protocol_refs, i8** @__start___objc_class_aliases, i8** @__stop___objc_class_aliases, i8** @__start___objc_constant_string, i8** @__stop___objc_constant_string }, comdat, align 8

// Check that the load function is manually inserted into .ctors.
// CHECK-NEW: @.objc_ctor = linkonce hidden constant void ()* @.objcv2_load_function, section ".ctors", comdat


// Make sure that we provide null versions of everything so the __start /
// __stop symbols work.
// CHECK-NEW: @.objc_null_selector = linkonce_odr hidden global { i8*, i8* } zeroinitializer, section "__objc_selectors", comdat, align 8
// CHECK-NEW: @.objc_null_category = linkonce_odr hidden global { i8*, i8*, i8*, i8*, i8*, i8*, i8* } zeroinitializer, section "__objc_cats", comdat, align 8
// CHECK-NEW: @.objc_null_protocol = linkonce_odr hidden global { i8*, i8*, i8*, i8*, i8*, i8*, i8*, i8*, i8*, i8*, i8* } zeroinitializer, section "__objc_protocols", comdat, align 8
// CHECK-NEW: @.objc_null_protocol_ref = linkonce_odr hidden global { i8* } zeroinitializer, section "__objc_protocol_refs", comdat, align 8
// CHECK-NEW: @.objc_null_class_alias = linkonce_odr hidden global { i8*, i8* } zeroinitializer, section "__objc_class_aliases", comdat, align 8
// CHECK-NEW: @.objc_null_constant_string = linkonce_odr hidden global { i8*, i32, i32, i32, i32, i8* } zeroinitializer, section "__objc_constant_string", comdat, align 8
// Make sure that the null symbols are not going to be removed, even by linking.
// CHECK-NEW: @llvm.used = appending global [8 x i8*] [i8* bitcast ({ { i8*, i8*, i8*, i64, i64, i64, i8*, i8*, i8*, i8*, i8*, i8*, i8*, i8*, i8*, i64, i8* }*, i8*, i8*, i64, i64, i64, i8*, i8*, i8*, i8*, i8*, i8*, i8*, i8*, i8*, i64, i8* }** @._OBJC_INIT_CLASS_X to i8*), i8* bitcast (void ()** @.objc_ctor to i8*), i8* bitcast ({ i8*, i8* }* @.objc_null_selector to i8*), i8* bitcast ({ i8*, i8*, i8*, i8*, i8*, i8*, i8* }* @.objc_null_category to i8*), i8* bitcast ({ i8*, i8*, i8*, i8*, i8*, i8*, i8*, i8*, i8*, i8*, i8* }* @.objc_null_protocol to i8*), i8* bitcast ({ i8* }* @.objc_null_protocol_ref to i8*), i8* bitcast ({ i8*, i8* }* @.objc_null_class_alias to i8*), i8* bitcast ({ i8*, i32, i32, i32, i32, i8* }* @.objc_null_constant_string to i8*)], section "llvm.metadata"
// Make sure that the load function and the reference to it are marked as used.
// CHECK-NEW: @llvm.compiler.used = appending global [1 x i8*] [i8* bitcast (void ()* @.objcv2_load_function to i8*)], section "llvm.metadata"

// Check that we emit the load function in a comdat and that it does the right thing.
// CHECK-NEW: define linkonce_odr hidden void @.objcv2_load_function() comdat {
// CHECK-NEW-NEXT: entry:
// CHECK-NEW-NEXT: call void @__objc_load(
// CHECK-NEW-SAME: @.objc_init
// CHECK-NEW-NEXT: ret void

// CHECK-OLD: @4 = internal global { i64, i64, i8*, { i64, { i8*, i8* }*, i16, i16, [4 x i8*] }* } { i64 9, i64 32, i8* getelementptr inbounds ([{{[0-9]+}} x i8], [{{[0-9]+}} x i8]* @.objc_source_file_name, i64 0, i64 0), { i64, { i8*, i8* }*, i16, i16, [4 x i8*] }* @3 }, align 8
// CHECK-OLD: @llvm.global_ctors = appending global [1 x { i32, void ()*, i8* }] [{ i32, void ()*, i8* } { i32 65535, void ()* @.objc_load_function, i8* null }]

// CHECK-OLD: define internal void @.objc_load_function() {
// CHECK-OLD-NEXT: entry:
// CHECK-OLD-NEXT: call void ({ i64, i64, i8*, { i64, { i8*, i8* }*, i16, i16, [4 x i8*] }* }*, ...) @__objc_exec_class({ i64, i64, i8*, { i64, { i8*, i8* }*, i16, i16, [4 x i8*] }* }* @4)



// Make sure all of our section boundary variables are emitted correctly.
// CHECK-WIN: @__start___objc_selectors = linkonce_odr hidden global %.objc_section_sentinel zeroinitializer, section "__objc_selectors$a", comdat, align 1
// CHECK-WIN: @__stop__objc_selectors = linkonce_odr hidden global %.objc_section_sentinel zeroinitializer, section "__objc_selectors$z", comdat, align 1
// CHECK-WIN: @__start___objc_classes = linkonce_odr hidden global %.objc_section_sentinel zeroinitializer, section "__objc_classes$a", comdat, align 1
// CHECK-WIN: @__stop__objc_classes = linkonce_odr hidden global %.objc_section_sentinel zeroinitializer, section "__objc_classes$z", comdat, align 1
// CHECK-WIN: @__start___objc_class_refs = linkonce_odr hidden global %.objc_section_sentinel zeroinitializer, section "__objc_class_refs$a", comdat, align 1
// CHECK-WIN: @__stop__objc_class_refs = linkonce_odr hidden global %.objc_section_sentinel zeroinitializer, section "__objc_class_refs$z", comdat, align 1
// CHECK-WIN: @__start___objc_cats = linkonce_odr hidden global %.objc_section_sentinel zeroinitializer, section "__objc_cats$a", comdat, align 1
// CHECK-WIN: @__stop__objc_cats = linkonce_odr hidden global %.objc_section_sentinel zeroinitializer, section "__objc_cats$z", comdat, align 1
// CHECK-WIN: @__start___objc_protocols = linkonce_odr hidden global %.objc_section_sentinel zeroinitializer, section "__objc_protocols$a", comdat, align 1
// CHECK-WIN: @__stop__objc_protocols = linkonce_odr hidden global %.objc_section_sentinel zeroinitializer, section "__objc_protocols$z", comdat, align 1
// CHECK-WIN: @__start___objc_protocol_refs = linkonce_odr hidden global %.objc_section_sentinel zeroinitializer, section "__objc_protocol_refs$a", comdat, align 1
// CHECK-WIN: @__stop__objc_protocol_refs = linkonce_odr hidden global %.objc_section_sentinel zeroinitializer, section "__objc_protocol_refs$z", comdat, align 1
// CHECK-WIN: @__start___objc_class_aliases = linkonce_odr hidden global %.objc_section_sentinel zeroinitializer, section "__objc_class_aliases$a", comdat, align 1
// CHECK-WIN: @__stop__objc_class_aliases = linkonce_odr hidden global %.objc_section_sentinel zeroinitializer, section "__objc_class_aliases$z", comdat, align 1
// CHECK-WIN: @__start___objc_constant_string = linkonce_odr hidden global %.objc_section_sentinel zeroinitializer, section "__objc_constant_string$a", comdat, align 1
// CHECK-WIN: @__stop__objc_constant_string = linkonce_odr hidden global %.objc_section_sentinel zeroinitializer, section "__objc_constant_string$z", comdat, align 1
// CHECK-WIN: @.objc_init = linkonce_odr hidden global { i64, %.objc_section_sentinel*, %.objc_section_sentinel*, %.objc_section_sentinel*, %.objc_section_sentinel*, %.objc_section_sentinel*, %.objc_section_sentinel*, %.objc_section_sentinel*, %.objc_section_sentinel*, %.objc_section_sentinel*, %.objc_section_sentinel*, %.objc_section_sentinel*, %.objc_section_sentinel*, %.objc_section_sentinel*, %.objc_section_sentinel*, %.objc_section_sentinel*, %.objc_section_sentinel* } { i64 0, %.objc_section_sentinel* @__start___objc_selectors, %.objc_section_sentinel* @__stop__objc_selectors, %.objc_section_sentinel* @__start___objc_classes, %.objc_section_sentinel* @__stop__objc_classes, %.objc_section_sentinel* @__start___objc_class_refs, %.objc_section_sentinel* @__stop__objc_class_refs, %.objc_section_sentinel* @__start___objc_cats, %.objc_section_sentinel* @__stop__objc_cats, %.objc_section_sentinel* @__start___objc_protocols, %.objc_section_sentinel* @__stop__objc_protocols, %.objc_section_sentinel* @__start___objc_protocol_refs, %.objc_section_sentinel* @__stop__objc_protocol_refs, %.objc_section_sentinel* @__start___objc_class_aliases, %.objc_section_sentinel* @__stop__objc_class_aliases, %.objc_section_sentinel* @__start___objc_constant_string, %.objc_section_sentinel* @__stop__objc_constant_string }, comdat, align 8

// Make sure our init variable is in the correct section for late library init.
// CHECK-WIN: @.objc_ctor = linkonce hidden constant void ()* @.objcv2_load_function, section ".CRT$XCLz", comdat

// We shouldn't have emitted any null placeholders on Windows.
// CHECK-WIN: @llvm.used = appending global [2 x i8*] [i8* bitcast ({ { i8*, i8*, i8*, i32, i32, i32, i8*, i8*, i8*, i8*, i8*, i8*, i8*, i8*, i8*, i32, i8* }*, i8*, i8*, i32, i32, i32, i8*, i8*, i8*, i8*, i8*, i8*, i8*, i8*, i8*, i32, i8* }** @._OBJC_INIT_CLASS_X to i8*), i8* bitcast (void ()** @.objc_ctor to i8*)], section "llvm.metadata"
// CHECK-WIN: @llvm.compiler.used = appending global [1 x i8*] [i8* bitcast (void ()* @.objcv2_load_function to i8*)], section "llvm.metadata"

// Check our load function is in a comdat.
// CHECK-WIN: define linkonce_odr hidden void @.objcv2_load_function() comdat {

// Make sure we have dllimport on the load function
// CHECK-WIN: declare dllimport void @__objc_load

