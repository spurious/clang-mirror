// RUN: clang -triple=i686-apple-darwin9 -fnext-runtime -emit-llvm -o %t %s &&
// RUN: grep -e "\^i" %t | count 1 &&
// RUN: grep -e "\[0i\]" %t | count 1

int main() {
  int n;
  
  const char * inc = @encode(int[]);
  const char * vla = @encode(int[n]);
}

// PR3648
int a[sizeof(@encode(int)) == 2 ? 1 : -1]; // Type is char[2]
char (*c)[2] = &@encode(int); // @encode is an lvalue
