// RUN: %clang_cc1 -emit-llvm %s -o - -triple=x86_64-apple-darwin9 | FileCheck %s

// Check mangling of Vtables, VTTs, and construction vtables that
// involve standard substitutions.

// CHECK: @_ZTVSd = weak_odr unnamed_addr constant 
// CHECK: @_ZTCSd0_Si = internal constant 
// CHECK: @_ZTCSd16_So = internal constant
// CHECK: @_ZTTSd = weak_odr constant
// CHECK: @_ZTVSo = weak_odr unnamed_addr constant
// CHECK: @_ZTTSo = weak_odr constant
// CHECK: @_ZTVSi = weak_odr unnamed_addr constant
// CHECK: @_ZTTSi = weak_odr constant
namespace std {
  struct A { A(); };
  
  // CHECK: define unnamed_addr void @_ZNSt1AC1Ev
  // CHECK: define unnamed_addr void @_ZNSt1AC2Ev
  A::A() { }
};

namespace std {
  template<typename> struct allocator { };
}

// CHECK: define void @_Z1fSaIcESaIiE
void f(std::allocator<char>, std::allocator<int>) { }

namespace std {
  template<typename, typename, typename> struct basic_string { };
}

// CHECK: define void @_Z1fSbIcciE
void f(std::basic_string<char, char, int>) { }

namespace std {
  template<typename> struct char_traits { };
  
  typedef std::basic_string<char, std::char_traits<char>, std::allocator<char> > string;
}

// CHECK: _Z1fSs
void f(std::string) { }

namespace std {
  template<typename, typename> struct basic_ios { 
    basic_ios(int);
    virtual ~basic_ios();
  };
  template<typename charT, typename traits = char_traits<charT> > 
  struct basic_istream : virtual public basic_ios<charT, traits> { 
    basic_istream(int x) : basic_ios<charT, traits>(x), stored(x) { }

    int stored;
  };
  template<typename charT, typename traits = char_traits<charT> > 
  struct basic_ostream : virtual public basic_ios<charT, traits> { 
    basic_ostream(int x) : basic_ios<charT, traits>(x), stored(x) { }

    float stored;
  };

  template<typename charT, typename traits = char_traits<charT> > 
    struct basic_iostream : public basic_istream<charT, traits>, 
                            public basic_ostream<charT, traits> { 
    basic_iostream(int x) : basic_istream<charT, traits>(x),
                            basic_ostream<charT, traits>(x),
                            basic_ios<charT, traits>(x) { }
  };
}

// CHECK: _Z1fSi
void f(std::basic_istream<char, std::char_traits<char> >) { }

// CHECK: _Z1fSo
void f(std::basic_ostream<char, std::char_traits<char> >) { }

// CHECK: _Z1fSd
void f(std::basic_iostream<char, std::char_traits<char> >) { }

extern "C++" {
namespace std
{
  typedef void (*terminate_handler) ();
  
  // CHECK: _ZSt13set_terminatePFvvE
  terminate_handler set_terminate(terminate_handler) { return 0; }
}
}

// Make sure we don't treat the following like std::string
// CHECK: define void @_Z1f12basic_stringIcSt11char_traitsIcESaIcEE
template<typename, typename, typename> struct basic_string { };
typedef basic_string<char, std::char_traits<char>, std::allocator<char> > not_string;
void f(not_string) { }

// Manglings for instantiations caused by this function are at the
// top of the test.
void create_streams() {
  std::basic_iostream<char> bio(17);
}

// Make sure we don't mangle 'std' as 'St' here.
namespace N {
  namespace std {
    struct A { void f(); };
    
    // CHECK: define void @_ZN1N3std1A1fEv
    void A::f() { }
  }
}
