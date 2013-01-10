// RUN: %clang_cc1 -S -emit-llvm -o %t.ll %s -triple x86_64-apple-darwin10 
// RUN: %clang_cc1 -std=c++11 -S -emit-llvm -o %t-c++11.ll %s -triple x86_64-apple-darwin10 
// RUN: diff %t.ll  %t-c++11.ll

// rdar://12897704

struct sAFSearchPos {
    unsigned char *pos;
    unsigned char count;
};

static volatile struct sAFSearchPos testPositions;

static volatile struct sAFSearchPos arrayPositions[100][10][5];

int main() {
  return testPositions.count + arrayPositions[10][4][3].count; 
}
