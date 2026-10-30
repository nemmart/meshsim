// secp_advice: jolt-cpp's own secp256k1 advice (inlines/secp256k1.cpp evaluate(), compiled), for inline_rows.py.
// stdin: lines "<op> <a0> <a1> <a2> <a3> <b0> <b1> <b2> <b3>" (hex limbs, low first; op = the Operation number:
// 1 mul_q, 2 square_q, 3 div_q, 4 mul_r, 5 square_r, 6 div_r, 7 glv_r). stdout: "<error> <advice0..7>" in hex.
#include "tracer/inlines/secp256k1.hpp"
#include <cstdio>
using namespace tracer::inlines::secp256k1;
int main() {
  unsigned op; unsigned long long a[4], b[4];
  while (scanf("%u %llx %llx %llx %llx %llx %llx %llx %llx", &op, &a[0], &a[1], &a[2], &a[3], &b[0], &b[1], &b[2], &b[3]) == 9) {
    Limbs A{a[0], a[1], a[2], a[3]}, B{b[0], b[1], b[2], b[3]};
    Evaluation r{};
    Error e = evaluate((Operation)op, A, B, r);
    printf("%u", (unsigned)e);
    for (auto v : r.advice) printf(" %llx", (unsigned long long)v);
    printf("\n");
  }
}
