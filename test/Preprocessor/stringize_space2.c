/* RUN: clang -E %s | grep 'a c'
 */
#define t(x) #x
t(a
c)

