// RUN: clang-cc -triple x86_64-apple-darwin9 -emit-llvm -o %t %s &&
// RUNX: llvm-gcc -m64 -emit-llvm -S -o %t %s &&

// RUN: grep '@"OBJC_CLASS_$_A" = global' %t &&
// RUN: grep '@"OBJC_CLASS_$_B" = external global' %t &&

// FIXME: This is currently broken in clang, we are emitting two
// references to the same ivar (one using \01, and one not).
// RUNX: grep '@"OBJC_IVAR_$_A._ivar" = global .* section "__DATA, __objc_const", align 8' %t &&

// RUN: grep '@"OBJC_METACLASS_$_A" = global .* section "__DATA, __objc_data", align 8' %t &&
// RUN: grep '@"\\01L_OBJC_CLASSLIST_REFERENCES_$_[0-9]*" = internal global .* section "__DATA, __objc_classrefs, regular, no_dead_strip", align 8' %t &&
// RUN: grep '@"\\01L_OBJC_CLASS_NAME_[0-9]*" = internal global .* section "__TEXT,__cstring,cstring_literals", align 1' %t &&
// RUN: grep '@"\\01L_OBJC_LABEL_CATEGORY_$" = internal global .* section "__DATA, __objc_catlist, regular, no_dead_strip", align 8' %t &&
// RUN: grep '@"\\01L_OBJC_LABEL_CLASS_$" = internal global .* section "__DATA, __objc_classlist, regular, no_dead_strip", align 8' %t &&
// RUN: grep '@"\\01L_OBJC_METH_VAR_NAME_[0-9]*" = internal global .* section "__TEXT,__cstring,cstring_literals", align 1' %t &&
// RUN: grep '@"\\01L_OBJC_METH_VAR_TYPE_[0-9]*" = internal global .* section "__TEXT,__cstring,cstring_literals", align 1' %t &&
// RUN: grep '@"\\01L_OBJC_PROP_NAME_ATTR_[0-9]*" = internal global .* section "__TEXT,__cstring,cstring_literals", align 1' %t &&

// FIXME: clang is not currently using "optimized" message dispatch in 64-bit mode.
// RUNX: grep '@"\\01L_OBJC_SELECTOR_REFERENCES_[0-9]*" = internal global .* section "__DATA, __objc_selrefs, literal_pointers, no_dead_strip", align 8' %t &&

// RUN: grep '@"\\01l_OBJC_$_CATEGORY_A_$_Cat" = internal global .* section "__DATA, __objc_const", align 8' %t &&
// RUN: grep '@"\\01l_OBJC_$_CATEGORY_CLASS_METHODS_A_$_Cat" = internal global .* section "__DATA, __objc_const", align 8' %t &&
// RUN: grep '@"\\01l_OBJC_$_CATEGORY_INSTANCE_METHODS_A_$_Cat" = internal global .* section "__DATA, __objc_const", align 8' %t &&
// RUN: grep '@"\\01l_OBJC_$_CLASS_METHODS_A" = internal global .* section "__DATA, __objc_const", align 8' %t &&
// RUN: grep '@"\\01l_OBJC_$_INSTANCE_METHODS_A" = internal global .* section "__DATA, __objc_const", align 8' %t &&
// RUN: grep '@"\\01l_OBJC_$_INSTANCE_VARIABLES_A" = internal global .* section "__DATA, __objc_const", align 8' %t &&
// RUN: grep '@"\\01l_OBJC_$_PROP_LIST_A" = internal global .* section "__DATA, __objc_const", align 8' %t &&
// RUN: grep '@"\\01l_OBJC_$_PROTOCOL_CLASS_METHODS_P" = internal global .* section "__DATA, __objc_const", align 8' %t &&
// RUN: grep '@"\\01l_OBJC_$_PROTOCOL_INSTANCE_METHODS_P" = internal global .* section "__DATA, __objc_const", align 8' %t &&
// RUN: grep '@"\\01l_OBJC_CLASS_PROTOCOLS_$_A" = internal global .* section "__DATA, __objc_const", align 8' %t &&
// RUN: grep '@"\\01l_OBJC_CLASS_RO_$_A" = internal global .* section "__DATA, __objc_const", align 8' %t &&
// RUN: grep '@"\\01l_OBJC_LABEL_PROTOCOL_$_P" = weak hidden global .* section "__DATA, __objc_protolist, coalesced, no_dead_strip", align 8' %t &&
// RUN: grep '@"\\01l_OBJC_METACLASS_RO_$_A" = internal global .* section "__DATA, __objc_const", align 8' %t &&
// RUN: grep '@"\\01l_OBJC_PROTOCOL_$_P" = weak hidden global .* section "__DATA,__datacoal_nt,coalesced", align 8' %t &&
// RUN: grep '@_objc_empty_cache = external global' %t &&
// RUN: grep '@_objc_empty_vtable = external global' %t &&

// RUN: true

/*

Here is a handy command for looking at llvm-gcc's output:
llvm-gcc -m64 -emit-llvm -S -o - metadata-symbols-64.m | \
  grep '=.*global' | \
  sed -e 's#global.*, section#global ... section#' | \
  sort

*/

@interface B
@end
@interface C
@end

@protocol P
+(void) fm0;
-(void) im0;
@end

@interface A<P> {
  int _ivar;
}
 
@property (assign) int ivar;

+(void) fm0;
-(void) im0;
@end

@implementation A
@synthesize ivar = _ivar;
+(void) fm0 {
}
-(void) im0 {
}
@end

@implementation A (Cat)
+(void) fm1 {
}
-(void) im1 {
}
@end

void *f0() {
   [B im0];
   [C im1];
}

