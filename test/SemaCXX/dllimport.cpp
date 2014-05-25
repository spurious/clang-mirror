// RUN: %clang_cc1 -triple i686-win32     -fsyntax-only -verify -std=c++11 %s
// RUN: %clang_cc1 -triple x86_64-win32   -fsyntax-only -verify -std=c++1y %s
// RUN: %clang_cc1 -triple i686-mingw32   -fsyntax-only -verify -std=c++1y %s
// RUN: %clang_cc1 -triple x86_64-mingw32 -fsyntax-only -verify -std=c++11 %s

// Helper structs to make templates more expressive.
struct ImplicitInst_Imported {};
struct ExplicitDecl_Imported {};
struct ExplicitInst_Imported {};
struct ExplicitSpec_Imported {};
struct ExplicitSpec_Def_Imported {};
struct ExplicitSpec_InlineDef_Imported {};
struct ExplicitSpec_NotImported {};
namespace { struct Internal {}; }


// Invalid usage.
__declspec(dllimport) typedef int typedef1; // expected-warning{{'dllimport' attribute only applies to variables and functions}}
typedef __declspec(dllimport) int typedef2; // expected-warning{{'dllimport' attribute only applies to variables and functions}}
typedef int __declspec(dllimport) typedef3; // expected-warning{{'dllimport' attribute only applies to variables and functions}}
typedef __declspec(dllimport) void (*FunTy)(); // expected-warning{{'dllimport' attribute only applies to variables and functions}}
enum __declspec(dllimport) Enum {}; // expected-warning{{'dllimport' attribute only applies to variables and functions}}
#if __has_feature(cxx_strong_enums)
  enum class __declspec(dllimport) EnumClass {}; // expected-warning{{'dllimport' attribute only applies to variables and functions}}
#endif



//===----------------------------------------------------------------------===//
// Globals
//===----------------------------------------------------------------------===//

// Import declaration.
__declspec(dllimport) extern int ExternGlobalDecl;

// dllimport implies a declaration.
__declspec(dllimport) int GlobalDecl;
int **__attribute__((dllimport))* GlobalDeclChunkAttr;
int GlobalDeclAttr __attribute__((dllimport));

// Not allowed on definitions.
__declspec(dllimport) extern int ExternGlobalInit = 1; // expected-error{{definition of dllimport data}}
__declspec(dllimport) int GlobalInit1 = 1; // expected-error{{definition of dllimport data}}
int __declspec(dllimport) GlobalInit2 = 1; // expected-error{{definition of dllimport data}}

// Declare, then reject definition.
__declspec(dllimport) extern int ExternGlobalDeclInit; // expected-note{{previous declaration is here}} expected-note{{previous attribute is here}}
int ExternGlobalDeclInit = 1; // expected-warning{{'ExternGlobalDeclInit' redeclared without 'dllimport' attribute: previous 'dllimport' ignored}}

__declspec(dllimport) int GlobalDeclInit; // expected-note{{previous declaration is here}} expected-note{{previous attribute is here}}
int GlobalDeclInit = 1; // expected-warning{{'GlobalDeclInit' redeclared without 'dllimport' attribute: previous 'dllimport' ignored}}

int *__attribute__((dllimport)) GlobalDeclChunkAttrInit; // expected-note{{previous declaration is here}} expected-note{{previous attribute is here}}
int *GlobalDeclChunkAttrInit = 0; // expected-warning{{'GlobalDeclChunkAttrInit' redeclared without 'dllimport' attribute: previous 'dllimport' ignored}}

int GlobalDeclAttrInit __attribute__((dllimport)); // expected-note{{previous declaration is here}} expected-note{{previous attribute is here}}
int GlobalDeclAttrInit = 1; // expected-warning{{'GlobalDeclAttrInit' redeclared without 'dllimport' attribute: previous 'dllimport' ignored}}

// Redeclarations
__declspec(dllimport) extern int GlobalRedecl1;
__declspec(dllimport) extern int GlobalRedecl1;

__declspec(dllimport) int GlobalRedecl2a;
__declspec(dllimport) int GlobalRedecl2a;

int *__attribute__((dllimport)) GlobalRedecl2b;
int *__attribute__((dllimport)) GlobalRedecl2b;

int GlobalRedecl2c __attribute__((dllimport));
int GlobalRedecl2c __attribute__((dllimport));

// NB: MSVC issues a warning and makes GlobalRedecl3 dllexport. We follow GCC
// and drop the dllimport with a warning.
__declspec(dllimport) extern int GlobalRedecl3; // expected-note{{previous declaration is here}} expected-note{{previous attribute is here}}
                      extern int GlobalRedecl3; // expected-warning{{'GlobalRedecl3' redeclared without 'dllimport' attribute: previous 'dllimport' ignored}}

                      extern int GlobalRedecl4; // expected-note{{previous declaration is here}}
__declspec(dllimport) extern int GlobalRedecl4; // expected-error{{redeclaration of 'GlobalRedecl4' cannot add 'dllimport' attribute}}

// External linkage is required.
__declspec(dllimport) static int StaticGlobal; // expected-error{{'StaticGlobal' must have external linkage when declared 'dllimport'}}
__declspec(dllimport) Internal InternalTypeGlobal; // expected-error{{'InternalTypeGlobal' must have external linkage when declared 'dllimport'}}
namespace    { __declspec(dllimport) int InternalGlobal; } // expected-error{{'(anonymous namespace)::InternalGlobal' must have external linkage when declared 'dllimport'}}
namespace ns { __declspec(dllimport) int ExternalGlobal; }

__declspec(dllimport) auto InternalAutoTypeGlobal = Internal(); // expected-error{{'InternalAutoTypeGlobal' must have external linkage when declared 'dllimport'}}
                                                                // expected-error@-1{{definition of dllimport data}}

// Import in local scope.
__declspec(dllimport) float LocalRedecl1; // expected-note{{previous definition is here}}
__declspec(dllimport) float LocalRedecl2; // expected-note{{previous definition is here}}
__declspec(dllimport) float LocalRedecl3; // expected-note{{previous definition is here}}
void functionScope() {
  __declspec(dllimport) int LocalRedecl1; // expected-error{{redefinition of 'LocalRedecl1' with a different type: 'int' vs 'float'}}
  int *__attribute__((dllimport)) LocalRedecl2; // expected-error{{redefinition of 'LocalRedecl2' with a different type: 'int *' vs 'float'}}
  int LocalRedecl3 __attribute__((dllimport)); // expected-error{{redefinition of 'LocalRedecl3' with a different type: 'int' vs 'float'}}

  __declspec(dllimport)        int LocalVarDecl;
  __declspec(dllimport)        int LocalVarDef = 1; // expected-error{{definition of dllimport data}}
  __declspec(dllimport) extern int ExternLocalVarDecl;
  __declspec(dllimport) extern int ExternLocalVarDef = 1; // expected-error{{definition of dllimport data}}
  __declspec(dllimport) static int StaticLocalVar; // expected-error{{'StaticLocalVar' must have external linkage when declared 'dllimport'}}
}



//===----------------------------------------------------------------------===//
// Variable templates
//===----------------------------------------------------------------------===//
#if __has_feature(cxx_variable_templates)

// Import declaration.
template<typename T> __declspec(dllimport) extern int ExternVarTmplDecl;

// dllimport implies a declaration.
template<typename T> __declspec(dllimport) int VarTmplDecl;

// Not allowed on definitions.
template<typename T> __declspec(dllimport) extern int ExternVarTmplInit = 1; // expected-error{{definition of dllimport data}}
template<typename T> __declspec(dllimport) int VarTmplInit1 = 1; // expected-error{{definition of dllimport data}}
template<typename T> int __declspec(dllimport) VarTmplInit2 = 1; // expected-error{{definition of dllimport data}}

// Declare, then reject definition.
template<typename T> __declspec(dllimport) extern int ExternVarTmplDeclInit; // expected-note{{previous declaration is here}} expected-note{{previous attribute is here}}
template<typename T>                              int ExternVarTmplDeclInit = 1; // expected-warning{{'ExternVarTmplDeclInit' redeclared without 'dllimport' attribute: previous 'dllimport' ignored}}

template<typename T> __declspec(dllimport) int VarTmplDeclInit; // expected-note{{previous declaration is here}} expected-note{{previous attribute is here}}
template<typename T>                       int VarTmplDeclInit = 1; // expected-warning{{'VarTmplDeclInit' redeclared without 'dllimport' attribute: previous 'dllimport' ignored}}

// Redeclarations
template<typename T> __declspec(dllimport) extern int VarTmplRedecl1;
template<typename T> __declspec(dllimport) extern int VarTmplRedecl1;

template<typename T> __declspec(dllimport) int VarTmplRedecl2;
template<typename T> __declspec(dllimport) int VarTmplRedecl2;

template<typename T> __declspec(dllimport) extern int VarTmplRedecl3; // expected-note{{previous declaration is here}} expected-note{{previous attribute is here}}
template<typename T>                       extern int VarTmplRedecl3; // expected-warning{{'VarTmplRedecl3' redeclared without 'dllimport' attribute: previous 'dllimport' ignored}}

template<typename T>                       extern int VarTmplRedecl4; // expected-note{{previous declaration is here}}
template<typename T> __declspec(dllimport) extern int VarTmplRedecl4; // expected-error{{redeclaration of 'VarTmplRedecl4' cannot add 'dllimport' attribute}}

// External linkage is required.
template<typename T> __declspec(dllimport) static int StaticVarTmpl; // expected-error{{'StaticVarTmpl' must have external linkage when declared 'dllimport'}}
template<typename T> __declspec(dllimport) Internal InternalTypeVarTmpl; // expected-error{{'InternalTypeVarTmpl' must have external linkage when declared 'dllimport'}}
namespace    { template<typename T> __declspec(dllimport) int InternalVarTmpl; } // expected-error{{'(anonymous namespace)::InternalVarTmpl' must have external linkage when declared 'dllimport'}}
namespace ns { template<typename T> __declspec(dllimport) int ExternalVarTmpl; }

template<typename T> __declspec(dllimport) auto InternalAutoTypeVarTmpl = Internal(); // expected-error{{definition of dllimport data}} // expected-error{{'InternalAutoTypeVarTmpl' must have external linkage when declared 'dllimport'}}


template<typename T> int VarTmpl;
template<typename T> __declspec(dllimport) int ImportedVarTmpl;

// Import implicit instantiation of an imported variable template.
int useVarTmpl() { return ImportedVarTmpl<ImplicitInst_Imported>; }

// Import explicit instantiation declaration of an imported variable template.
extern template int ImportedVarTmpl<ExplicitDecl_Imported>;

// An explicit instantiation definition of an imported variable template cannot
// be imported because the template must be defined which is illegal.

// Import specialization of an imported variable template.
template<> __declspec(dllimport) int ImportedVarTmpl<ExplicitSpec_Imported>;
template<> __declspec(dllimport) int ImportedVarTmpl<ExplicitSpec_Def_Imported> = 1; // expected-error{{definition of dllimport data}}

// Not importing specialization of an imported variable template without
// explicit dllimport.
template<> int ImportedVarTmpl<ExplicitSpec_NotImported>;


// Import explicit instantiation declaration of a non-imported variable template.
extern template __declspec(dllimport) int VarTmpl<ExplicitDecl_Imported>;

// Import explicit instantiation definition of a non-imported variable template.
template __declspec(dllimport) int VarTmpl<ExplicitInst_Imported>;

// Import specialization of a non-imported variable template.
template<> __declspec(dllimport) int VarTmpl<ExplicitSpec_Imported>;
template<> __declspec(dllimport) int VarTmpl<ExplicitSpec_Def_Imported> = 1; // expected-error{{definition of dllimport data}}

#endif // __has_feature(cxx_variable_templates)



//===----------------------------------------------------------------------===//
// Functions
//===----------------------------------------------------------------------===//

// Import function declaration. Check different placements.
__attribute__((dllimport)) void decl1A(); // Sanity check with __attribute__
__declspec(dllimport)      void decl1B();

void __attribute__((dllimport)) decl2A();
void __declspec(dllimport)      decl2B();

// Not allowed on function definitions.
__declspec(dllimport) void def() {} // expected-error{{dllimport cannot be applied to non-inline function definition}}

// extern  "C"
extern "C" __declspec(dllimport) void externC();

// Import inline function.
__declspec(dllimport) inline void inlineFunc1() {}
inline void __attribute__((dllimport)) inlineFunc2() {}

__declspec(dllimport) inline void inlineDecl();
                             void inlineDecl() {}

__declspec(dllimport) void inlineDef();
               inline void inlineDef() {}

// Redeclarations
__declspec(dllimport) void redecl1();
__declspec(dllimport) void redecl1();

// NB: MSVC issues a warning and makes redecl2/redecl3 dllexport. We follow GCC
// and drop the dllimport with a warning.
__declspec(dllimport) void redecl2(); // expected-note{{previous declaration is here}} expected-note{{previous attribute is here}}
                      void redecl2(); // expected-warning{{'redecl2' redeclared without 'dllimport' attribute: previous 'dllimport' ignored}}

__declspec(dllimport) void redecl3(); // expected-note{{previous declaration is here}} expected-note{{previous attribute is here}}
                      void redecl3() {} // expected-warning{{'redecl3' redeclared without 'dllimport' attribute: previous 'dllimport' ignored}}

                      void redecl4(); // expected-note{{previous declaration is here}}
__declspec(dllimport) void redecl4(); // expected-error{{redeclaration of 'redecl4' cannot add 'dllimport' attribute}}

                      void redecl5(); // expected-note{{previous declaration is here}}
__declspec(dllimport) inline void redecl5() {} // expected-error{{redeclaration of 'redecl5' cannot add 'dllimport' attribute}}

// Friend functions
struct FuncFriend {
  friend __declspec(dllimport) void friend1();
  friend __declspec(dllimport) void friend2(); // expected-note{{previous declaration is here}} expected-note{{previous attribute is here}}
  friend __declspec(dllimport) void friend3(); // expected-note{{previous declaration is here}} expected-note{{previous attribute is here}}
  friend                       void friend4(); // expected-note{{previous declaration is here}}
  friend                       void friend5(); // expected-note{{previous declaration is here}}
};
__declspec(dllimport) void friend1();
                      void friend2(); // expected-warning{{'friend2' redeclared without 'dllimport' attribute: previous 'dllimport' ignored}}
                      void friend3() {} // expected-warning{{'friend3' redeclared without 'dllimport' attribute: previous 'dllimport' ignored}}
__declspec(dllimport) void friend4(); // expected-error{{redeclaration of 'friend4' cannot add 'dllimport' attribute}}
__declspec(dllimport) inline void friend5() {} // expected-error{{redeclaration of 'friend5' cannot add 'dllimport' attribute}}

// Implicit declarations can be redeclared with dllimport.
__declspec(dllimport) void* operator new(__SIZE_TYPE__ n);

// External linkage is required.
__declspec(dllimport) static int staticFunc(); // expected-error{{'staticFunc' must have external linkage when declared 'dllimport'}}
__declspec(dllimport) Internal internalRetFunc(); // expected-error{{'internalRetFunc' must have external linkage when declared 'dllimport'}}
namespace    { __declspec(dllimport) void internalFunc(); } // expected-error{{'(anonymous namespace)::internalFunc' must have external linkage when declared 'dllimport'}}
namespace ns { __declspec(dllimport) void externalFunc(); }



//===----------------------------------------------------------------------===//
// Function templates
//===----------------------------------------------------------------------===//

// Import function template declaration. Check different placements.
template<typename T> __declspec(dllimport) void funcTmplDecl1();
template<typename T> void __declspec(dllimport) funcTmplDecl2();

// Import function template definition.
template<typename T> __declspec(dllimport) void funcTmplDef() {} // expected-error{{dllimport cannot be applied to non-inline function definition}}

// Import inline function template.
template<typename T> __declspec(dllimport) inline void inlineFuncTmpl1() {}
template<typename T> inline void __attribute__((dllimport)) inlineFuncTmpl2() {}

template<typename T> __declspec(dllimport) inline void inlineFuncTmplDecl();
template<typename T>                              void inlineFuncTmplDecl() {}

template<typename T> __declspec(dllimport) void inlineFuncTmplDef();
template<typename T>                inline void inlineFuncTmplDef() {}

// Redeclarations
template<typename T> __declspec(dllimport) void funcTmplRedecl1();
template<typename T> __declspec(dllimport) void funcTmplRedecl1();

template<typename T> __declspec(dllimport) void funcTmplRedecl2(); // expected-note{{previous declaration is here}} expected-note{{previous attribute is here}}
template<typename T>                       void funcTmplRedecl2(); // expected-warning{{'funcTmplRedecl2' redeclared without 'dllimport' attribute: previous 'dllimport' ignored}}

template<typename T> __declspec(dllimport) void funcTmplRedecl3(); // expected-note{{previous declaration is here}} expected-note{{previous attribute is here}}
template<typename T>                       void funcTmplRedecl3() {} // expected-warning{{'funcTmplRedecl3' redeclared without 'dllimport' attribute: previous 'dllimport' ignored}}

template<typename T>                       void funcTmplRedecl4(); // expected-note{{previous declaration is here}}
template<typename T> __declspec(dllimport) void funcTmplRedecl4(); // expected-error{{redeclaration of 'funcTmplRedecl4' cannot add 'dllimport' attribute}}

template<typename T>                       void funcTmplRedecl5(); // expected-note{{previous declaration is here}}
template<typename T> __declspec(dllimport) inline void funcTmplRedecl5() {} // expected-error{{redeclaration of 'funcTmplRedecl5' cannot add 'dllimport' attribute}}

// Function template friends
struct FuncTmplFriend {
  template<typename T> friend __declspec(dllimport) void funcTmplFriend1();
  template<typename T> friend __declspec(dllimport) void funcTmplFriend2(); // expected-note{{previous declaration is here}} expected-note{{previous attribute is here}}
  template<typename T> friend __declspec(dllimport) void funcTmplFriend3(); // expected-note{{previous declaration is here}} expected-note{{previous attribute is here}}
  template<typename T> friend                       void funcTmplFriend4(); // expected-note{{previous declaration is here}}
  template<typename T> friend __declspec(dllimport) inline void funcTmplFriend5();
};
template<typename T> __declspec(dllimport) void funcTmplFriend1();
template<typename T>                       void funcTmplFriend2(); // expected-warning{{'funcTmplFriend2' redeclared without 'dllimport' attribute: previous 'dllimport' ignored}}
template<typename T>                       void funcTmplFriend3() {} // expected-warning{{'funcTmplFriend3' redeclared without 'dllimport' attribute: previous 'dllimport' ignored}}
template<typename T> __declspec(dllimport) void funcTmplFriend4(); // expected-error{{redeclaration of 'funcTmplFriend4' cannot add 'dllimport' attribute}}
template<typename T>                       inline void funcTmplFriend5() {}

// External linkage is required.
template<typename T> __declspec(dllimport) static int staticFuncTmpl(); // expected-error{{'staticFuncTmpl' must have external linkage when declared 'dllimport'}}
template<typename T> __declspec(dllimport) Internal internalRetFuncTmpl(); // expected-error{{'internalRetFuncTmpl' must have external linkage when declared 'dllimport'}}
namespace    { template<typename T> __declspec(dllimport) void internalFuncTmpl(); } // expected-error{{'(anonymous namespace)::internalFuncTmpl' must have external linkage when declared 'dllimport'}}
namespace ns { template<typename T> __declspec(dllimport) void externalFuncTmpl(); }


template<typename T> void funcTmpl() {}
template<typename T> inline void inlineFuncTmpl() {}
template<typename T> __declspec(dllimport) void importedFuncTmplDecl();
template<typename T> __declspec(dllimport) inline void importedFuncTmpl() {}

// Import implicit instantiation of an imported function template.
void useFunTmplDecl() { importedFuncTmplDecl<ImplicitInst_Imported>(); }
void useFunTmplDef() { importedFuncTmpl<ImplicitInst_Imported>(); }

// Import explicit instantiation declaration of an imported function template.
extern template void importedFuncTmpl<ExplicitDecl_Imported>();

// Import explicit instantiation definition of an imported function template.
// NB: MSVC fails this instantiation without explicit dllimport which is most
// likely a bug because an implicit instantiation is accepted.
template void importedFuncTmpl<ExplicitInst_Imported>();

// Import specialization of an imported function template. A definition must be
// declared inline.
template<> __declspec(dllimport) void importedFuncTmpl<ExplicitSpec_Imported>();
template<> __declspec(dllimport) void importedFuncTmpl<ExplicitSpec_Def_Imported>() {} // expected-error{{dllimport cannot be applied to non-inline function definition}}
template<> __declspec(dllimport) inline void importedFuncTmpl<ExplicitSpec_InlineDef_Imported>() {}

// Not importing specialization of an imported function template without
// explicit dllimport.
template<> void importedFuncTmpl<ExplicitSpec_NotImported>() {}


// Import explicit instantiation declaration of a non-imported function template.
extern template __declspec(dllimport) void funcTmpl<ExplicitDecl_Imported>();
extern template __declspec(dllimport) void inlineFuncTmpl<ExplicitDecl_Imported>();

// Import explicit instantiation definition of a non-imported function template.
template __declspec(dllimport) void funcTmpl<ExplicitInst_Imported>();
template __declspec(dllimport) void inlineFuncTmpl<ExplicitInst_Imported>();

// Import specialization of a non-imported function template. A definition must
// be declared inline.
template<> __declspec(dllimport) void funcTmpl<ExplicitSpec_Imported>();
template<> __declspec(dllimport) void funcTmpl<ExplicitSpec_Def_Imported>() {} // expected-error{{dllimport cannot be applied to non-inline function definition}}
template<> __declspec(dllimport) inline void funcTmpl<ExplicitSpec_InlineDef_Imported>() {}
