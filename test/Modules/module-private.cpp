// RUN: mkdir -p %t
// RUN: %clang_cc1 -x c++ -emit-module -o %t/left.pcm %s -D MODULE_LEFT
// RUN: %clang_cc1 -x c++ -emit-module -o %t/right.pcm %s -D MODULE_RIGHT
// RUN: %clang_cc1 -I %t %s -verify

#if defined(MODULE_LEFT)

__module_private__ struct HiddenStruct;

struct HiddenStruct {
};


int &f0(int);

template<typename T>
__module_private__ void f1(T*);

template<typename T>
void f1(T*);

template<typename T>
__module_private__ class vector;

template<typename T>
class vector {
};

vector<float> vec_float;

typedef __module_private__ int Integer;
typedef int Integer;

#elif defined(MODULE_RIGHT)
__module_private__ double &f0(double);
double &f0(double);

__module_private__ int hidden_var;

inline void test_f0_in_right() {
  double &dr = f0(hidden_var);
}
#else
__import_module__ left;
__import_module__ right;

void test() {
  int &ir = f0(1.0); // okay: f0() from 'right' is not visible
}

int test_broken() {
  HiddenStruct hidden; // expected-error{{use of undeclared identifier 'HiddenStruct'}}

  Integer i; // expected-error{{use of undeclared identifier 'Integer'}}

  int *ip = 0;
  f1(ip); // expected-error{{use of undeclared identifier 'f1'}}

  vector<int> vec; // expected-error{{use of undeclared identifier 'vector'}} \
  // expected-error{{expected '(' for function-style cast or type construction}} \
  // expected-error{{use of undeclared identifier 'vec'}}

  return hidden_var; // expected-error{{use of undeclared identifier 'hidden_var'}}
}

// Check for private redeclarations of public entities.
template<typename T>
class public_class_template; // expected-note{{previous declaration is here}}

template<typename T>
__module_private__ class public_class_template; // expected-error{{__module_private__ declaration of 'public_class_template' follows public declaration}}


typedef int public_typedef; // expected-note{{previous declaration is here}}
typedef __module_private__ int public_typedef; // expected-error{{__module_private__ declaration of 'public_typedef' follows public declaration}}

extern int public_var; // expected-note{{previous declaration is here}}
extern __module_private__ int public_var; // expected-error{{__module_private__ declaration of 'public_var' follows public declaration}}

void public_func(); // expected-note{{previous declaration is here}}
__module_private__ void public_func(); // expected-error{{__module_private__ declaration of 'public_func' follows public declaration}}

template<typename T>
void public_func_template(); // expected-note{{previous declaration is here}}
template<typename T>
__module_private__ void public_func_template(); // expected-error{{__module_private__ declaration of 'public_func_template' follows public declaration}}

struct public_struct; // expected-note{{previous declaration is here}}
__module_private__ struct public_struct; // expected-error{{__module_private__ declaration of 'public_struct' follows public declaration}}

// Check for attempts to make specializations private
template<> __module_private__ void public_func_template<int>(); // expected-error{{template specialization cannot be declared __module_private__}}

template<typename T>
struct public_class {
  struct inner_struct;
  static int static_var;

  friend __module_private__ void public_func(); // expected-error{{friend cannot be declared __module_private__}}
  friend __module_private__ struct public_struct; // expected-error{{friend cannot be declared __module_private__}}
};

template<> __module_private__ struct public_class<int>::inner_struct { }; // expected-error{{member specialization cannot be declared __module_private__}}
template<> __module_private__ int public_class<int>::static_var = 17; // expected-error{{member specialization cannot be declared __module_private__}}

template<>
__module_private__ struct public_class<float> { }; // expected-error{{template specialization cannot be declared __module_private__}}

template<typename T>
__module_private__ struct public_class<T *> { }; // expected-error{{partial specialization cannot be declared __module_private__}}

#endif
