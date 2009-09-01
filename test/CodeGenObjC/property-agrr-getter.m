// RUN: clang-cc -fnext-runtime -emit-llvm -o %t %s

typedef struct {
  unsigned f0;
} s0;

@interface A
- (s0) f0;
@end

@implementation A
-(s0) f0{ while (1) {} }
- (unsigned) bar {
  return self.f0.f0;
}
@end


typedef struct _NSSize {
    float width;
    float height;
} NSSize;


@interface AnObject
{
 NSSize size;
}

@property NSSize size;

@end

float f ()
{
  AnObject* obj;
  return (obj.size).width;
}
