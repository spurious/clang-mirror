// RUN: clang -rewrite-test -verify %s

@interface Intf
{
  id ivar;
  id ivar1[12];

  id **ivar3;

  id (*ivar4) (id, id);
}
@end

@implementation Intf
@end
