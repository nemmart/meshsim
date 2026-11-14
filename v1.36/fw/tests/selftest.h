/* selftest.h -- the self-test's generator and hash (RECIPES.md 17-20; meshsim M14), shared by the tile programs and
 * RecipeTest's Java check. RecipeTest reads every ST_ constant from this file, so the C and the Java can't disagree;
 * the code below is the algorithm the Java mirrors (RecipeTest.SelfTest), and the 64-tile check compares the two.
 *
 * Generator: Marsaglia's 64-bit xorshift, x ^= x << A; x ^= x >> B; x ^= x << C, on the 32-bit core as two registers.
 * Seeding: st_seed() maps any 64-bit seed to a non-zero state and runs ST_WARMUP steps; a seed made from the tile index
 * is st_tile_seed(): the base seed, each half XORed with (me + 1) times an odd constant.
 * Hash: two 32-bit lanes, the low and the high word of each 64-bit record: h = rotl((h ^ w) * M, R), order-sensitive. */
#ifndef SELFTEST_H
#define SELFTEST_H
#include <stdint.h>
#define ST_XS_A     13
#define ST_XS_B     7
#define ST_XS_C     17
#define ST_WARMUP   4
#define ST_SEED_K0  0x9E3779B9u   /* tile seed, low half: base_lo ^ (me + 1) * K0 */
#define ST_SEED_K1  0x85EBCA6Bu   /* tile seed, high half: base_hi ^ (me + 1) * K1 */
#define ST_HASH_M0  0xCC9E2D51u   /* lane 0 (the low words): multiplier */
#define ST_HASH_M1  0x1B873593u   /* lane 1 (the high words) */
#define ST_HASH_R   15            /* rotate left after the multiply */
#define ST_HASH_H0  0x243F6A88u   /* lane 0's initial value */
#define ST_HASH_H1  0x85A308D3u   /* lane 1's */

/* RECIPE selftest_h */
/* one xorshift64 step on (hi, lo) */
static inline void st_next(uint32_t *hi, uint32_t *lo) {
    uint32_t h = *hi, l = *lo;
    h ^= h << ST_XS_A | l >> (32 - ST_XS_A); l ^= l << ST_XS_A;      /* x ^= x << A (h from the old l first) */
    l ^= l >> ST_XS_B | h << (32 - ST_XS_B); h ^= h >> ST_XS_B;      /* x ^= x >> B (l from the old h first) */
    h ^= h << ST_XS_C | l >> (32 - ST_XS_C); l ^= l << ST_XS_C;      /* x ^= x << C */
    *hi = h; *lo = l;
}
/* a 64-bit seed to a generator state: never all zero, then ST_WARMUP steps */
static inline void st_seed(uint32_t *hi, uint32_t *lo) {
    if ((*hi | *lo) == 0) *lo = 1;
    for (int i = 0; i < ST_WARMUP; i++) st_next(hi, lo);
}
/* a tile's own seed, from its index and a base seed (0 for none) */
static inline void st_tile_seed(unsigned me, uint32_t base_hi, uint32_t base_lo, uint32_t *hi, uint32_t *lo) {
    *lo = base_lo ^ (me + 1) * ST_SEED_K0;
    *hi = base_hi ^ (me + 1) * ST_SEED_K1;
    st_seed(hi, lo);
}
/* one hash step: a word into its lane */
static inline uint32_t st_mix(uint32_t h, uint32_t w, uint32_t m) {
    __asm__ ("" : "+r"(m));                        /* keep m in a register: one mul (Zmmul), not a shift-and-add chain */
    h = (h ^ w) * m;
    return h << ST_HASH_R | h >> (32 - ST_HASH_R);
}
/* END */
#endif
