/* div.c: our own division (SHELL-SPEC section 3, owner 2026-11-10). The controller has no divide instruction and we
 * link no runtime library, so the helpers the compilers call for / and % are defined here, over two cores written
 * without / or %. Division by zero and signed overflow give RISC-V's div/divu results. Speed does not matter: division
 * is only used for output. */
#include "shell.h"

typedef int s32;
typedef long long s64;

u32 udiv32(u32 n, u32 d, u32 *rem) {
    if (d == 0) { if (rem) *rem = n; return 0xFFFFFFFFu; }
    u32 q = 0, r = 0;
    for (int i = 31; i >= 0; i--) {
        r = (r << 1) | ((n >> i) & 1u);
        if (r >= d) { r -= d; q |= 1u << i; }
    }
    if (rem) *rem = r;
    return q;
}

u64 udiv64(u64 n, u64 d, u64 *rem) {
    if (d == 0) { if (rem) *rem = n; return ~0ull; }
    /* shifts by constants only: a 64-bit shift by a variable is a call to __lshrdi3 or __ashldi3 with GCC on RV32 */
    u64 q = 0, r = 0;
    for (int i = 0; i < 64; i++) {
        const u64 top = r >> 63;                 /* r may pass 2^63 before the subtraction */
        r = (r << 1) | (n >> 63);
        n <<= 1;
        q <<= 1;
        if (top || r >= d) { r -= d; q |= 1; }
    }
    if (rem) *rem = r;
    return q;
}

/* the helpers, under the names GCC and clang share on RISC-V */
u32 __udivsi3(u32 a, u32 b);
u32 __umodsi3(u32 a, u32 b);
s32 __divsi3(s32 a, s32 b);
s32 __modsi3(s32 a, s32 b);
u64 __udivdi3(u64 a, u64 b);
u64 __umoddi3(u64 a, u64 b);
s64 __divdi3(s64 a, s64 b);
s64 __moddi3(s64 a, s64 b);

u32 __udivsi3(u32 a, u32 b) { return udiv32(a, b, 0); }
u32 __umodsi3(u32 a, u32 b) { u32 r; udiv32(a, b, &r); return r; }
u64 __udivdi3(u64 a, u64 b) { return udiv64(a, b, 0); }
u64 __umoddi3(u64 a, u64 b) { u64 r; udiv64(a, b, &r); return r; }

/* Signed: on magnitudes, the quotient negative when the signs differ, the remainder with the dividend's sign. By zero:
 * quotient -1, remainder the dividend. The most negative value / -1: the dividend, remainder 0; the magnitudes give
 * that by themselves, in two's complement. */
s32 __divsi3(s32 a, s32 b) {
    if (b == 0) return -1;
    const u32 ua = a < 0 ? 0u - (u32)a : (u32)a, ub = b < 0 ? 0u - (u32)b : (u32)b;
    const u32 q = udiv32(ua, ub, 0);
    return (s32)((a < 0) != (b < 0) ? 0u - q : q);
}
s32 __modsi3(s32 a, s32 b) {
    if (b == 0) return a;
    const u32 ua = a < 0 ? 0u - (u32)a : (u32)a, ub = b < 0 ? 0u - (u32)b : (u32)b;
    u32 r;
    udiv32(ua, ub, &r);
    return (s32)(a < 0 ? 0u - r : r);
}
s64 __divdi3(s64 a, s64 b) {
    if (b == 0) return -1;
    const u64 ua = a < 0 ? 0ull - (u64)a : (u64)a, ub = b < 0 ? 0ull - (u64)b : (u64)b;
    const u64 q = udiv64(ua, ub, 0);
    return (s64)((a < 0) != (b < 0) ? 0ull - q : q);
}
s64 __moddi3(s64 a, s64 b) {
    if (b == 0) return a;
    const u64 ua = a < 0 ? 0ull - (u64)a : (u64)a, ub = b < 0 ? 0ull - (u64)b : (u64)b;
    u64 r;
    udiv64(ua, ub, &r);
    return (s64)(a < 0 ? 0ull - r : r);
}
