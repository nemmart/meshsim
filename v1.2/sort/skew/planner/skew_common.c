/* skew_common.c -- helpers, the cost formulas and stage 1 of the switch.
 * RV32I + Zmmul: no divide.  See skew.h. */
#include "skew.h"

/* schedule-4: stream 4g+i carries offset GROUP_OFF[4g+i] */
const uint8_t GROUP_OFF[64] = {
    1, 29, 35, 63,   2, 30, 34, 62,   3, 31, 33, 61,   5, 23, 41, 59,
    6, 22, 42, 58,   7, 21, 43, 57,   8, 28, 36, 56,   9, 27, 37, 55,
   10, 26, 38, 54,  11, 25, 39, 53,  12, 24, 40, 52,  13, 15, 49, 51,
   14, 18, 46, 50,  16, 20, 44, 48,  17, 19, 45, 47,   0,  4, 32, 60 };
const uint8_t STREAM_OF[64] = {
   60,  0,  4,  8, 61, 12, 16, 20, 24, 28, 32, 36, 40, 44, 48, 45,
   52, 56, 49, 57, 53, 21, 17, 13, 41, 37, 33, 29, 25,  1,  5,  9,
   62, 10,  6,  2, 26, 30, 34, 38, 42, 14, 18, 22, 54, 58, 50, 59,
   55, 46, 51, 47, 43, 39, 35, 31, 27, 23, 19, 15, 63, 11,  7,  3 };

uint32_t udiv32(uint32_t a, uint32_t b)
{
    uint32_t q = 0, r = 0;
    for (int i = 31; i >= 0; i--) {
        r = (r << 1) | ((a >> i) & 1u);
        if (r >= b) { r -= b; q |= 1u << i; }
    }
    return q;
}

/* one group run: 16 groups at period P (gap 0), late starts when a group is
 * shorter than the next group's prefetch, an optional 17th group (the LB
 * loopback stream), and the run's tail */
uint32_t run_cost(const uint32_t *R, uint32_t P, uint32_t extraR, uint32_t extraP)
{
    uint32_t c = 0, late = 0;
    for (int g = 0; g < NG; g++) {
        uint32_t x = R[g] * P;
        c += x;
        if ((g < NG - 1 || extraP) && x < C_PREFETCH) late += C_PREFETCH - x;
    }
    if (extraP) c += extraR * extraP;
    return c + late + C_TAIL;
}

uint32_t run_cost_flat(uint32_t R, uint32_t P)
{
    uint32_t x = R * P, late = x < C_PREFETCH ? (C_PREFETCH - x) * (NG - 1) : 0;
    return x * NG + late + C_TAIL;
}

/* the mesh reduction of a v-word max vector (walk-through 5.4), [estimate] */
uint32_t red_cost(uint32_t v)
{
    return 660 + 52 * v + ((3 * v) >> 1);
}

uint32_t stage_addr(uint32_t pos, uint32_t kind)
{
    if (kind == KIND_K) return A0 + 2 * pos;
    if (pos < SEG_A0) return A0 + 4 * pos;
    if (pos < SEG_A3) return A3 + 4 * (pos - SEG_A0);
    return A7TOP + 4 * (pos - SEG_A3);
}

/* own prefixes and the tile's RED contribution: 16 direct maxima (as in XB),
 * group 15 without the loopback, the loopback stream */
void sw_prepare(Tile *T)
{
    uint32_t me = T->me, a = 0, b = 0;
    for (int q = 0; q < NT; q++) { T->rowpos[q] = a; a += T->row[q]; }
    for (int p = 0; p < NT; p++) { T->inpos[p] = b; b += T->col[p]; }
    for (int g = 0; g < NG; g++) {
        uint32_t mx = 0;
        for (int i = 0; i < 4; i++) {
            uint32_t v = T->row[(me + GROUP_OFF[4 * g + i]) & 63];
            if (v > mx) mx = v;
        }
        T->red_own[g] = mx;
    }
    uint32_t m15 = 0;
    for (int i = 1; i < 4; i++) {
        uint32_t v = T->row[(me + GROUP_OFF[60 + i]) & 63];
        if (v > m15) m15 = v;
    }
    T->red_own[NG] = m15;
    T->red_own[NG + 1] = T->row[me];
    /* globals every tile already has: n_t and M_q */
    uint32_t nm = 0, Mm = 0;
    for (int t = 0; t < NT; t++) {
        if (T->n[t] > nm) nm = T->n[t];
        if (T->M[t] > Mm) Mm = T->M[t];
    }
    T->nmax = nm; T->Mmax = Mm; T->Wmax = (Mm + 63) >> 6;
}

/* stage 1, after RED: direct, LB, and a floor on any two-hop path */
void sw_stage1(Tile *T)
{
    uint32_t R[NG];
    for (int g = 0; g < NG; g++) R[g] = T->Rdir[g];
    uint32_t two = T->nkind == 2;            /* the format: values all-to-all or not */
    T->costD = run_cost(R, 4, 0, 0) + C_B + (two ? run_cost(R, 8, 0, 0) + C_B : 0);
    R[NG - 1] = T->Rp15;
    T->costLB = run_cost(R, 4, T->Rloop, 1) + C_B + (two ? run_cost(R, 8, T->Rloop, 2) + C_B : 0);
    /* hop 1 sends n_max from one tile and hop 2 lands M_max at one tile, at one
     * record per cycle (keys) or per two (values); one set-up each; planning
     * at least the XC exchange, R2 and the least TH table */
    uint32_t nM = T->nmax + T->Mmax;
    T->floor2 = (two ? 3 : 1) * nM + T->nkind * (2 * C_TAIL + C_TH1 + C_TH2)
              + 2 * C_XCH + red_cost(NG + 3) + C_TH_LOCAL;
    uint32_t base = T->costD <= T->costLB ? T->costD : T->costLB;
    if (base <= T->floor2) T->path = T->costD <= T->costLB ? PATH_DIRECT : PATH_LB;
    else T->path = PATH_PLAN;
}
