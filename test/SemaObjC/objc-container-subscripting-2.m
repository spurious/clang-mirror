// RUN: %clang_cc1  -fsyntax-only -verify %s

typedef unsigned int size_t;
@protocol P @end

@interface NSMutableArray
- (id)objectAtIndexedSubscript:(size_t)index;
- (void)setObject:(id)object atIndexedSubscript:(size_t)index;
@end

@interface NSMutableDictionary
- (id)objectForKeyedSubscript:(id)key;
- (void)setObject:(id)object forKeyedSubscript:(size_t)key;
@end

id func() {
  NSMutableArray *array;
  float f; 
  array[f] = array; // expected-error {{expected method to write dictionary element not found on object of type 'NSMutableArray *'}}
  return array[3.14]; // expected-error {{expected method to read dictionary element not found on object of type 'NSMutableArray *'}}
}

void test_unused() {
  NSMutableArray *array;
  array[10]; // expected-warning {{container access result unused - container access should not be used for side effects}} 

  NSMutableDictionary *dict;
  dict[array]; // expected-warning {{container access result unused - container access should not be used for side effects}}
}

