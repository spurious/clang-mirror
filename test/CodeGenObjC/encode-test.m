// RUN: clang -fnext-runtime -emit-llvm -o %t %s &&
// RUN: grep -e "\^{Innermost=CC}" %t | count 1 &&
// RUN: grep -e "{Derived=#ib32b8b3b8sb16b8b8b2b8ccb6}" %t | count 1 &&
// RUN: grep -e "{B1=#@c}" %t | count 1

@class Int1;

struct Innermost {
  unsigned char a, b;
};

@interface Int1 {
  signed char a, b;
  struct Innermost *innermost;
}
@end

@implementation Int1
@end

@interface Base
{
    struct objc_class *isa;
    int full;
    int full2: 32;
    int _refs: 8;
    int field2: 3;
    unsigned f3: 8;
    short cc;
    unsigned g: 16;
    int r2: 8;
    int r3: 8;
    int r4: 2;
    int r5: 8;
    char c;
}
@end

@interface Derived: Base
{
    char d;
    int _field3: 6;
}
@end

@implementation Base
@end

@implementation Derived
@end

@interface B1 
{
    struct objc_class *isa;
    Int1 *sBase;
    char c;
}
@end

@implementation B1
@end

int main()
{
	const char *en = @encode(Derived);
	const char *eb = @encode(B1);
}

