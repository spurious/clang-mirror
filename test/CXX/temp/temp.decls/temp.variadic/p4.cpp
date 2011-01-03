// RUN: %clang_cc1 -std=c++0x -fsyntax-only -fexceptions -verify %s

template<typename... Types> struct tuple;
template<int I> struct int_c;

template<typename T>
struct identity {
  typedef T type;
};

template<typename T, typename U>
struct is_same {
  static const bool value = false;
};

template<typename T>
struct is_same<T, T> {
  static const bool value = true;
};

// FIXME: Many more bullets to go

// In an initializer-list (8.5); the pattern is an initializer-clause.
// Note: this also covers expression-lists, since expression-list is
// just defined as initializer-list.
void five_args(int, int, int, int, int); // expected-note{{candidate function not viable: requires 5 arguments, but 6 were provided}}

template<int ...Values>
void initializer_list_expansion() {
  int values[5] = { Values... }; // expected-error{{excess elements in array initializer}}
  five_args(Values...); // expected-error{{no matching function for call to 'five_args'}}
}

template void initializer_list_expansion<1, 2, 3, 4, 5>();
template void initializer_list_expansion<1, 2, 3, 4, 5, 6>(); // expected-note{{in instantiation of function template specialization 'initializer_list_expansion<1, 2, 3, 4, 5, 6>' requested here}}

// In a template-argument-list (14.3); the pattern is a template-argument.
template<typename ...Types>
struct tuple_of_refs {
  typedef tuple<Types& ...> types;
};

tuple<int&, float&> *t_int_ref_float_ref;
tuple_of_refs<int&, float&>::types *t_int_ref_float_ref_2 =  t_int_ref_float_ref;

template<typename ...Types>
struct extract_nested_types {
  typedef tuple<typename Types::type...> types;
};

tuple<int, float> *t_int_float;
extract_nested_types<identity<int>, identity<float> >::types *t_int_float_2 
  = t_int_float;

template<int ...N>
struct tuple_of_ints {
  typedef tuple<int_c<N>...> type;
};

int check_temp_arg_1[is_same<tuple_of_ints<1, 2, 3, 4, 5>::type, 
                             tuple<int_c<1>, int_c<2>, int_c<3>, int_c<4>, 
                                   int_c<5>>>::value? 1 : -1];

// In a dynamic-exception-specification (15.4); the pattern is a type-id.
template<typename ...Types>
struct f_with_except {
  virtual void f() throw(Types...); // expected-note{{overridden virtual function is here}}
};

struct check_f_with_except_1 : f_with_except<int, float> {
  virtual void f() throw(int, float);
};

struct check_f_with_except_2 : f_with_except<int, float> {
  virtual void f() throw(int);
};

struct check_f_with_except_3 : f_with_except<int, float> {
  virtual void f() throw(int, float, double); // expected-error{{exception specification of overriding function is more lax than base version}}
};
