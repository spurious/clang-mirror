// RUN: %clang_cc1 -emit-llvm -triple i686-pc-win32 -fms-extensions -o - %s | FileCheck %s

extern "C" {

#pragma const_seg(".my_const")
#pragma bss_seg(".my_bss")
int D = 1;
#pragma data_seg(".data")
int a = 1;
#pragma data_seg(push, label, ".data2")
extern const int b;
const int b = 1;
const char* s = "my string!";
#pragma data_seg(push, ".my_seg")
int c = 1;
#pragma data_seg(pop, label)
int d = 1;
int e;
#pragma bss_seg(".c")
int f;
void g(void){}
#pragma code_seg(".my_code")
void h(void){}
#pragma bss_seg()
int i;
#pragma bss_seg(".bss1")
#pragma bss_seg(push, test, ".bss2")
#pragma bss_seg()
#pragma bss_seg()
int TEST1;
#pragma bss_seg(pop)
int TEST2;

#pragma section("read_flag_section", read)
// Even though they are not declared const, these become constant since they are
// in a read-only section.
__declspec(allocate("read_flag_section")) int unreferenced = 0;
extern __declspec(allocate("read_flag_section")) int referenced = 42;
int *user() { return &referenced; }

#pragma section("no_section_attributes")
// A pragma section with no section attributes is read/write.
__declspec(allocate("no_section_attributes")) int implicitly_read_write = 42;

#pragma section("long_section", long)
// Pragma section ignores "long".
__declspec(allocate("long_section")) long long_var = 42;

#pragma section("short_section", short)
// Pragma section ignores "short".
__declspec(allocate("short_section")) short short_var = 42;
}

//CHECK: @D = global i32 1
//CHECK: @a = global i32 1, section ".data"
//CHECK: @b = constant i32 1, section ".my_const"
//CHECK: @[[MYSTR:.*]] = {{.*}} unnamed_addr constant [11 x i8] c"my string!\00"
//CHECK: @s = global i8* getelementptr inbounds ([11 x i8]* @[[MYSTR]], i32 0, i32 0), section ".data2"
//CHECK: @c = global i32 1, section ".my_seg"
//CHECK: @d = global i32 1, section ".data"
//CHECK: @e = global i32 0, section ".my_bss"
//CHECK: @f = global i32 0, section ".c"
//CHECK: @i = global i32 0
//CHECK: @TEST1 = global i32 0
//CHECK: @TEST2 = global i32 0, section ".bss1"
//CHECK: @unreferenced = constant i32 0, section "read_flag_section"
//CHECK: @referenced = constant i32 42, section "read_flag_section"
//CHECK: @implicitly_read_write = global i32 42, section "no_section_attributes"
//CHECK: @long_var = global i32 42, section "long_section"
//CHECK: @short_var = global i16 42, section "short_section"
//CHECK: define void @g()
//CHECK: define void @h() {{.*}} section ".my_code"
