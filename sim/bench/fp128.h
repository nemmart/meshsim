// Fp128 for p = 2^128 - 159, a Crandall prime (DECISIONS D14): multiply, then reduce by folding, no Montgomery.
//
// 2^128 = +159 (mod p), so a 256-bit product hi * 2^128 + lo reduces as lo + 159 * hi (< 2^136 + 2^128, ~137 bits); the
// bits above 128 (at most 9) fold again, 159 * top (< 2^17); a carry out of that adds 159 once more (the value is then
// tiny, so no further carry); and one conditional subtraction of p, done as "s + 159 carries out of 128 bits".
// Inputs may be any 128-bit values (reduced or not); the result is in [0, p).
//
// mul_u128:  unsigned __int128 throughout.
// mul_acc:   the same columns, 64 x 64 -> 128 products summed in unsigned __int128 accumulators (no carry compares).
// mul_limbs: explicit 64-bit limbs, 64 x 64 -> 128 products and add-with-carry chains (_addcarry_u64).
// mul_ref:   the slow reference, double-and-add with modular additions only (shares nothing with the fold).
#pragma once

#include <x86intrin.h>

#include <cstdint>

namespace fp {

using u64 = uint64_t;
using u128 = unsigned __int128;

inline constexpr u64 kC = 159;
inline constexpr u128 kP = ~u128(0) - (kC - 1);  // 2^128 - 159

inline u128 make(u64 hi, u64 lo) { return u128(hi) << 64 | lo; }

inline u128 mul_u128(u128 a, u128 b) {
    const u64 a0 = u64(a), a1 = u64(a >> 64), b0 = u64(b), b1 = u64(b >> 64);
    const u128 p00 = u128(a0) * b0, p01 = u128(a0) * b1, p10 = u128(a1) * b0, p11 = u128(a1) * b1;
    const u128 mid = u128(u64(p01)) + u64(p10) + (p00 >> 64);  // < 3 * 2^64
    const u128 lo = u128(u64(p00)) | mid << 64;
    const u128 hi = p11 + (p01 >> 64) + (p10 >> 64) + (mid >> 64);  // the product is < 2^256: no carry out
    // fold 1: s = lo + 159 * hi, as 128 bits plus `top` above them
    const u128 h0 = u128(u64(hi)) * kC;        // < 2^72
    const u128 h1 = u128(u64(hi >> 64)) * kC;  // < 2^72, weight 2^64
    u128 s = lo + h0;
    u64 top = s < h0;
    const u128 h1s = h1 << 64;
    s += h1s;
    top += (s < h1s) + u64(h1 >> 64);
    // fold 2: top < 2^9
    const u128 f = u128(top) * kC;
    s += f;
    s += u128(s < f) * kC;  // a carry out means 2^128 more, i.e. +159; s is then < 2^17, so no further carry
    // normalise: s >= p iff s + 159 carries out of 128 bits, and then s - p = s + 159 (mod 2^128)
    const u128 q = s + kC;
    return q < s ? q : s;
}

inline u128 mul_acc(u128 a, u128 b) {
    const u64 a0 = u64(a), a1 = u64(a >> 64), b0 = u64(b), b1 = u64(b >> 64);
    const u128 p00 = u128(a0) * b0, p01 = u128(a0) * b1, p10 = u128(a1) * b0, p11 = u128(a1) * b1;
    // the 256-bit product, column by column: each sum fits 128 bits
    u128 acc = (p00 >> 64) + u64(p01) + u64(p10);
    const u64 r0 = u64(p00), r1 = u64(acc);
    acc = (acc >> 64) + (p01 >> 64) + (p10 >> 64) + u64(p11);
    const u64 r2 = u64(acc);
    const u64 r3 = u64((acc >> 64) + (p11 >> 64));
    // fold 1: [r0 r1] + 159 * [r2 r3]
    acc = u128(r2) * kC + r0;
    u64 s0 = u64(acc);
    acc = (acc >> 64) + u128(r3) * kC + r1;
    u64 s1 = u64(acc);
    const u64 top = u64(acc >> 64);  // < 2^9
    // fold 2
    acc = u128(s0) + top * kC;
    s0 = u64(acc);
    acc = (acc >> 64) + s1;
    s1 = u64(acc);
    s0 += u64(acc >> 64) * kC;  // after a carry out [s0 s1] < 2^17: no further carry
    // normalise
    acc = u128(s0) + kC;
    const u64 t0 = u64(acc);
    acc = (acc >> 64) + s1;
    return (acc >> 64) ? make(u64(acc), t0) : make(s1, s0);
}

inline u128 mul_limbs(u128 a, u128 b) {
    const u64 a0 = u64(a), a1 = u64(a >> 64), b0 = u64(b), b1 = u64(b >> 64);
    const u128 p00 = u128(a0) * b0, p01 = u128(a0) * b1, p10 = u128(a1) * b0, p11 = u128(a1) * b1;
    unsigned long long r1, r2, r3, s0, s1, t0, t1;
    const u64 r0 = u64(p00);
    unsigned char c = _addcarry_u64(0, u64(p00 >> 64), u64(p01), &r1);
    c = _addcarry_u64(c, u64(p01 >> 64), u64(p11), &r2);
    r3 = u64(p11 >> 64) + c;
    c = _addcarry_u64(0, r1, u64(p10), &r1);
    c = _addcarry_u64(c, r2, u64(p10 >> 64), &r2);
    r3 += c;
    // fold 1: [r0 r1] + 159 * [r2 r3] = [s0 s1 s2]
    const u128 m0 = u128(r2) * kC, m1 = u128(r3) * kC;
    c = _addcarry_u64(0, r0, u64(m0), &s0);
    c = _addcarry_u64(c, r1, u64(m0 >> 64), &s1);
    u64 s2 = u64(m1 >> 64) + c;
    c = _addcarry_u64(0, s1, u64(m1), &s1);
    s2 += c;
    // fold 2: s2 < 2^9
    c = _addcarry_u64(0, s0, s2 * kC, &s0);
    c = _addcarry_u64(c, s1, 0, &s1);
    s0 += u64(c) * kC;  // after a carry out [s0 s1] < 2^17: no further carry
    // normalise
    c = _addcarry_u64(0, s0, kC, &t0);
    c = _addcarry_u64(c, s1, 0, &t1);
    return c ? make(t1, t0) : make(s1, s0);
}

inline u128 reduce_once(u128 x) { return x >= kP ? x - kP : x; }

inline u128 add_ref(u128 x, u128 y) {  // x, y < p
    u128 s = x + y;
    if (s < x) return s + kC;  // x + y - p = s + 2^128 - p
    return s >= kP ? s - kP : s;
}

inline u128 mul_ref(u128 a, u128 b) {
    a = reduce_once(a), b = reduce_once(b);
    u128 r = 0;
    for (int i = 127; i >= 0; --i) {
        r = add_ref(r, r);
        if ((b >> i) & 1) r = add_ref(r, a);
    }
    return r;
}

}  // namespace fp
