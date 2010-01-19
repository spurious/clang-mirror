// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -fobjc-nonfragile-abi -fblocks -emit-pch -x objective-c %s -o %t.ast
// RUN: c-index-test -test-load-tu %t.ast scan-function | FileCheck %s

















@interface Foo 
{
}

- foo;
+ fooC;

@end

@interface Bar : Foo 
{
}

@end

@interface Foo (FooCat)
- (int) catMethodWithFloat:(float) fArg;
- (float) floatMethod;
@end

@protocol Proto
- pMethod;
@end

@protocol SubP <Proto>
- spMethod;
@end

@interface Baz : Bar <SubP>
{
    int _anIVar;
}

- (Foo *) bazMethod;

@end

enum {
  someEnum
};























int main (int argc, const char * argv[]) {
	Baz * bee;
	id a = [bee foo];
	id <SubP> c = [Foo fooC];
	id <Proto> d;
	d = c;
	[d pMethod];
	[bee catMethodWithFloat:[bee floatMethod]];
  main(someEnum, (const char **)bee);
}

// CHECK: c-index-api-fn-scan.m:84:2: ObjCClassRef=Baz:48:1
// CHECK: c-index-api-fn-scan.m:84:3: ObjCClassRef=Baz:48:1
// CHECK: c-index-api-fn-scan.m:84:4: ObjCClassRef=Baz:48:1
// CHECK: c-index-api-fn-scan.m:84:6: VarDecl=bee:84:8
// CHECK: c-index-api-fn-scan.m:84:8: VarDecl=bee:84:8
// CHECK: c-index-api-fn-scan.m:84:9: VarDecl=bee:84:8
// CHECK: c-index-api-fn-scan.m:84:10: VarDecl=bee:84:8
// CHECK: <invalid loc>:85:2: TypedefDecl=id:0:0
// CHECK: <invalid loc>:85:3: TypedefDecl=id:0:0
// CHECK: c-index-api-fn-scan.m:85:5: VarDecl=a:85:5
// CHECK: c-index-api-fn-scan.m:85:6: VarDecl=a:85:5
// CHECK: c-index-api-fn-scan.m:85:7: VarDecl=a:85:5
// CHECK: c-index-api-fn-scan.m:85:8: VarDecl=a:85:5
// CHECK: c-index-api-fn-scan.m:85:9: ObjCMessageExpr=foo:24:1
// CHECK: c-index-api-fn-scan.m:85:10: DeclRefExpr=bee:84:8
// CHECK: c-index-api-fn-scan.m:85:11: DeclRefExpr=bee:84:8
// CHECK: c-index-api-fn-scan.m:85:12: DeclRefExpr=bee:84:8
// CHECK: c-index-api-fn-scan.m:85:13: ObjCMessageExpr=foo:24:1
// CHECK: c-index-api-fn-scan.m:85:14: ObjCMessageExpr=foo:24:1
// CHECK: c-index-api-fn-scan.m:85:15: ObjCMessageExpr=foo:24:1
// CHECK: c-index-api-fn-scan.m:85:16: ObjCMessageExpr=foo:24:1
// CHECK: c-index-api-fn-scan.m:85:17: ObjCMessageExpr=foo:24:1
// CHECK: <invalid loc>:86:2: TypedefDecl=id:0:0
// CHECK: <invalid loc>:86:3: TypedefDecl=id:0:0
// CHECK: c-index-api-fn-scan.m:86:5: VarDecl=c:86:12
// CHECK: c-index-api-fn-scan.m:86:6: ObjCProtocolRef=SubP:44:1
// CHECK: c-index-api-fn-scan.m:86:7: ObjCProtocolRef=SubP:44:1
// CHECK: c-index-api-fn-scan.m:86:8: ObjCProtocolRef=SubP:44:1
// CHECK: c-index-api-fn-scan.m:86:9: ObjCProtocolRef=SubP:44:1
// CHECK: c-index-api-fn-scan.m:86:10: VarDecl=c:86:12
// CHECK: c-index-api-fn-scan.m:86:12: VarDecl=c:86:12
// CHECK: c-index-api-fn-scan.m:86:13: VarDecl=c:86:12
// CHECK: c-index-api-fn-scan.m:86:14: VarDecl=c:86:12
// CHECK: c-index-api-fn-scan.m:86:15: VarDecl=c:86:12
// CHECK: c-index-api-fn-scan.m:86:16: ObjCMessageExpr=fooC:25:1
// CHECK: c-index-api-fn-scan.m:86:17: ObjCMessageExpr=fooC:25:1
// CHECK: c-index-api-fn-scan.m:86:18: ObjCMessageExpr=fooC:25:1
// CHECK: c-index-api-fn-scan.m:86:19: ObjCMessageExpr=fooC:25:1
// CHECK: c-index-api-fn-scan.m:86:20: ObjCMessageExpr=fooC:25:1
// CHECK: c-index-api-fn-scan.m:86:21: ObjCMessageExpr=fooC:25:1
// CHECK: c-index-api-fn-scan.m:86:22: ObjCMessageExpr=fooC:25:1
// CHECK: c-index-api-fn-scan.m:86:23: ObjCMessageExpr=fooC:25:1
// CHECK: c-index-api-fn-scan.m:86:24: ObjCMessageExpr=fooC:25:1
// CHECK: c-index-api-fn-scan.m:86:25: ObjCMessageExpr=fooC:25:1
// CHECK: <invalid loc>:87:2: TypedefDecl=id:0:0
// CHECK: <invalid loc>:87:3: TypedefDecl=id:0:0
// CHECK: c-index-api-fn-scan.m:87:5: VarDecl=d:87:13
// CHECK: c-index-api-fn-scan.m:87:6: ObjCProtocolRef=Proto:40:1
// CHECK: c-index-api-fn-scan.m:87:7: ObjCProtocolRef=Proto:40:1
// CHECK: c-index-api-fn-scan.m:87:8: ObjCProtocolRef=Proto:40:1
// CHECK: c-index-api-fn-scan.m:87:9: ObjCProtocolRef=Proto:40:1
// CHECK: c-index-api-fn-scan.m:87:10: ObjCProtocolRef=Proto:40:1
// CHECK: c-index-api-fn-scan.m:87:11: VarDecl=d:87:13
// CHECK: c-index-api-fn-scan.m:87:13: VarDecl=d:87:13
// CHECK: c-index-api-fn-scan.m:88:2: DeclRefExpr=d:87:13
// CHECK: c-index-api-fn-scan.m:88:6: DeclRefExpr=c:86:12
// CHECK: c-index-api-fn-scan.m:89:2: ObjCMessageExpr=pMethod:41:1
// CHECK: c-index-api-fn-scan.m:89:3: DeclRefExpr=d:87:13
// CHECK: c-index-api-fn-scan.m:89:4: ObjCMessageExpr=pMethod:41:1
// CHECK: c-index-api-fn-scan.m:89:5: ObjCMessageExpr=pMethod:41:1
// CHECK: c-index-api-fn-scan.m:89:6: ObjCMessageExpr=pMethod:41:1
// CHECK: c-index-api-fn-scan.m:89:7: ObjCMessageExpr=pMethod:41:1
// CHECK: c-index-api-fn-scan.m:89:8: ObjCMessageExpr=pMethod:41:1
// CHECK: c-index-api-fn-scan.m:89:9: ObjCMessageExpr=pMethod:41:1
// CHECK: c-index-api-fn-scan.m:89:10: ObjCMessageExpr=pMethod:41:1
// CHECK: c-index-api-fn-scan.m:89:11: ObjCMessageExpr=pMethod:41:1
// CHECK: c-index-api-fn-scan.m:89:12: ObjCMessageExpr=pMethod:41:1
// CHECK: c-index-api-fn-scan.m:90:2: ObjCMessageExpr=catMethodWithFloat::36:1
// CHECK: c-index-api-fn-scan.m:90:3: DeclRefExpr=bee:84:8
// CHECK: c-index-api-fn-scan.m:90:4: DeclRefExpr=bee:84:8
// CHECK: c-index-api-fn-scan.m:90:5: DeclRefExpr=bee:84:8
// CHECK: c-index-api-fn-scan.m:90:6: ObjCMessageExpr=catMethodWithFloat::36:1
// CHECK: c-index-api-fn-scan.m:90:7: ObjCMessageExpr=catMethodWithFloat::36:1
// CHECK: c-index-api-fn-scan.m:90:8: ObjCMessageExpr=catMethodWithFloat::36:1
// CHECK: c-index-api-fn-scan.m:90:9: ObjCMessageExpr=catMethodWithFloat::36:1
// CHECK: c-index-api-fn-scan.m:90:10: ObjCMessageExpr=catMethodWithFloat::36:1
// CHECK: c-index-api-fn-scan.m:90:11: ObjCMessageExpr=catMethodWithFloat::36:1
// CHECK: c-index-api-fn-scan.m:90:12: ObjCMessageExpr=catMethodWithFloat::36:1
// CHECK: c-index-api-fn-scan.m:90:13: ObjCMessageExpr=catMethodWithFloat::36:1
// CHECK: c-index-api-fn-scan.m:90:14: ObjCMessageExpr=catMethodWithFloat::36:1
// CHECK: c-index-api-fn-scan.m:90:15: ObjCMessageExpr=catMethodWithFloat::36:1
// CHECK: c-index-api-fn-scan.m:90:16: ObjCMessageExpr=catMethodWithFloat::36:1
// CHECK: c-index-api-fn-scan.m:90:17: ObjCMessageExpr=catMethodWithFloat::36:1
// CHECK: c-index-api-fn-scan.m:90:18: ObjCMessageExpr=catMethodWithFloat::36:1
// CHECK: c-index-api-fn-scan.m:90:19: ObjCMessageExpr=catMethodWithFloat::36:1
// CHECK: c-index-api-fn-scan.m:90:20: ObjCMessageExpr=catMethodWithFloat::36:1
// CHECK: c-index-api-fn-scan.m:90:21: ObjCMessageExpr=catMethodWithFloat::36:1
// CHECK: c-index-api-fn-scan.m:90:22: ObjCMessageExpr=catMethodWithFloat::36:1
// CHECK: c-index-api-fn-scan.m:90:23: ObjCMessageExpr=catMethodWithFloat::36:1
// CHECK: c-index-api-fn-scan.m:90:24: ObjCMessageExpr=catMethodWithFloat::36:1
// CHECK: c-index-api-fn-scan.m:90:25: ObjCMessageExpr=catMethodWithFloat::36:1
// CHECK: c-index-api-fn-scan.m:90:26: ObjCMessageExpr=floatMethod:37:1
// CHECK: c-index-api-fn-scan.m:90:27: DeclRefExpr=bee:84:8
// CHECK: c-index-api-fn-scan.m:90:28: DeclRefExpr=bee:84:8
// CHECK: c-index-api-fn-scan.m:90:29: DeclRefExpr=bee:84:8
// CHECK: c-index-api-fn-scan.m:90:30: ObjCMessageExpr=floatMethod:37:1
// CHECK: c-index-api-fn-scan.m:90:31: ObjCMessageExpr=floatMethod:37:1
// CHECK: c-index-api-fn-scan.m:90:32: ObjCMessageExpr=floatMethod:37:1
// CHECK: c-index-api-fn-scan.m:90:33: ObjCMessageExpr=floatMethod:37:1
// CHECK: c-index-api-fn-scan.m:90:34: ObjCMessageExpr=floatMethod:37:1
// CHECK: c-index-api-fn-scan.m:90:35: ObjCMessageExpr=floatMethod:37:1
// CHECK: c-index-api-fn-scan.m:90:36: ObjCMessageExpr=floatMethod:37:1
// CHECK: c-index-api-fn-scan.m:90:37: ObjCMessageExpr=floatMethod:37:1
// CHECK: c-index-api-fn-scan.m:90:38: ObjCMessageExpr=floatMethod:37:1
// CHECK: c-index-api-fn-scan.m:90:39: ObjCMessageExpr=floatMethod:37:1
// CHECK: c-index-api-fn-scan.m:90:40: ObjCMessageExpr=floatMethod:37:1
// CHECK: c-index-api-fn-scan.m:90:41: ObjCMessageExpr=floatMethod:37:1
// CHECK: c-index-api-fn-scan.m:90:42: ObjCMessageExpr=floatMethod:37:1
// CHECK: c-index-api-fn-scan.m:90:43: ObjCMessageExpr=catMethodWithFloat::36:1
// CHECK: c-index-api-fn-scan.m:91:3: DeclRefExpr=main:83:5
// CHECK: c-index-api-fn-scan.m:91:4: DeclRefExpr=main:83:5
// CHECK: c-index-api-fn-scan.m:91:5: DeclRefExpr=main:83:5
// CHECK: c-index-api-fn-scan.m:91:6: DeclRefExpr=main:83:5
// CHECK: c-index-api-fn-scan.m:91:8: DeclRefExpr=someEnum:58:3
// CHECK: c-index-api-fn-scan.m:91:9: DeclRefExpr=someEnum:58:3
// CHECK: c-index-api-fn-scan.m:91:10: DeclRefExpr=someEnum:58:3
// CHECK: c-index-api-fn-scan.m:91:11: DeclRefExpr=someEnum:58:3
// CHECK: c-index-api-fn-scan.m:91:12: DeclRefExpr=someEnum:58:3
// CHECK: c-index-api-fn-scan.m:91:13: DeclRefExpr=someEnum:58:3
// CHECK: c-index-api-fn-scan.m:91:14: DeclRefExpr=someEnum:58:3
// CHECK: c-index-api-fn-scan.m:91:15: DeclRefExpr=someEnum:58:3
// CHECK: c-index-api-fn-scan.m:91:33: DeclRefExpr=bee:84:8
// CHECK: c-index-api-fn-scan.m:91:34: DeclRefExpr=bee:84:8
// CHECK: c-index-api-fn-scan.m:91:35: DeclRefExpr=bee:84:8
