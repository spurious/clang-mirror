// RUN: clang -triple x86_64-unknown-unknown -emit-llvm -o %t %s

@interface I0 {
  struct { int a; } a;
}
@end 

@class I2;

@interface I1 {
  I2 *_imageBrowser;
}
@end 

@implementation I1 
@end 

@interface I2 : I0 
@end 

@implementation I2 
@end 


