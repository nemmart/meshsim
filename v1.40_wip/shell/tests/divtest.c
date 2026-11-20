/* divtest.c: div.c's cores and helpers against the host's own division (SHELL-SPEC section 12): divisor 1, a divisor
 * larger than the dividend, the maximum values, powers of two, negative operands, and random operands; division by
 * zero and signed overflow against RISC-V's results. A hosted program; it links div.o alone. */
#include <stdint.h>
#include <stdio.h>

typedef uint32_t u32;
typedef uint64_t u64;
u32 udiv32(u32 n, u32 d, u32 *rem);
u64 udiv64(u64 n, u64 d, u64 *rem);
u32 __udivsi3(u32, u32);
u32 __umodsi3(u32, u32);
int32_t __divsi3(int32_t, int32_t);
int32_t __modsi3(int32_t, int32_t);
u64 __udivdi3(u64, u64);
u64 __umoddi3(u64, u64);
int64_t __divdi3(int64_t, int64_t);
int64_t __moddi3(int64_t, int64_t);

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { if (fails++ < 20) { printf(__VA_ARGS__); printf("\n"); } } } while (0)

/* RISC-V's results, as the reference */
static int32_t rv_div32(int32_t a, int32_t b) { return b == 0 ? -1 : (a == INT32_MIN && b == -1) ? a : a / b; }
static int32_t rv_rem32(int32_t a, int32_t b) { return b == 0 ? a : (a == INT32_MIN && b == -1) ? 0 : a % b; }
static int64_t rv_div64(int64_t a, int64_t b) { return b == 0 ? -1 : (a == INT64_MIN && b == -1) ? a : a / b; }
static int64_t rv_rem64(int64_t a, int64_t b) { return b == 0 ? a : (a == INT64_MIN && b == -1) ? 0 : a % b; }

static void one32(u32 a, u32 b) {
    u32 r;
    const u32 q = udiv32(a, b, &r);
    CHECK(q == (b ? a / b : 0xFFFFFFFFu) && r == (b ? a % b : a), "udiv32(%u, %u) = %u r %u", a, b, q, r);
    CHECK(__udivsi3(a, b) == q && __umodsi3(a, b) == r, "__udivsi3/__umodsi3(%u, %u)", a, b);
    const int32_t sa = (int32_t)a, sb = (int32_t)b;
    CHECK(__divsi3(sa, sb) == rv_div32(sa, sb), "__divsi3(%d, %d) = %d", sa, sb, __divsi3(sa, sb));
    CHECK(__modsi3(sa, sb) == rv_rem32(sa, sb), "__modsi3(%d, %d) = %d", sa, sb, __modsi3(sa, sb));
}

static void one64(u64 a, u64 b) {
    u64 r;
    const u64 q = udiv64(a, b, &r);
    CHECK(q == (b ? a / b : ~0ull) && r == (b ? a % b : a), "udiv64(%llu, %llu)", (unsigned long long)a, (unsigned long long)b);
    CHECK(__udivdi3(a, b) == q && __umoddi3(a, b) == r, "__udivdi3/__umoddi3(%llu, %llu)", (unsigned long long)a, (unsigned long long)b);
    const int64_t sa = (int64_t)a, sb = (int64_t)b;
    CHECK(__divdi3(sa, sb) == rv_div64(sa, sb), "__divdi3(%lld, %lld)", (long long)sa, (long long)sb);
    CHECK(__moddi3(sa, sb) == rv_rem64(sa, sb), "__moddi3(%lld, %lld)", (long long)sa, (long long)sb);
}

static u64 rng = 0x9E3779B97F4A7C15ull;
static u64 next(void) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return rng; }

int main(void) {
    static const u64 edge[] = {0, 1, 2, 3, 7, 10, 255, 256, 4095, 4096, 4097, 0x7FFFFFFFull, 0x80000000ull,
                               0x80000001ull, 0xFFFFFFFEull, 0xFFFFFFFFull, 0x100000000ull, 0x7FFFFFFFFFFFFFFFull,
                               0x8000000000000000ull, 0xFFFFFFFFFFFFFFFEull, 0xFFFFFFFFFFFFFFFFull};
    const int ne = (int)(sizeof edge / sizeof edge[0]);
    for (int i = 0; i < ne; i++)
        for (int j = 0; j < ne; j++) {
            one32((u32)edge[i], (u32)edge[j]);
            one64(edge[i], edge[j]);
        }
    for (int i = 0; i < 64; i++)                       /* powers of two, and their neighbours */
        for (int j = 0; j < 64; j++) {
            const u64 a = 1ull << i, b = 1ull << j;
            one64(a, b); one64(a - 1, b); one64(a + 1, b - 1 ? b - 1 : 1);
            if (i < 32 && j < 32) { one32((u32)a, (u32)b); one32((u32)a - 1, (u32)b); }
        }
    for (int i = 0; i < 200000; i++) {                 /* random, of every size; negatives among them */
        const u64 a = next() >> (next() & 63), b = next() >> (next() & 63);
        one32((u32)a, (u32)b);
        one64(a, b);
        one64(a, b & 0xFFFF);
    }
    printf("divtest: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
