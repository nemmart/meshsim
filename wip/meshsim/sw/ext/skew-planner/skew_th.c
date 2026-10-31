/* skew_th.c -- stage 2: the window two-hop table (TH), its R2 words, the
 * decision after R2, and TH's stream and pointer set-up.  See skew-v2.md 3. */
#include "skew.h"

static inline uint32_t mulhu(uint32_t a, uint32_t b)
{
    return (uint32_t)(((uint64_t)a * b) >> 32);
}


/* Window k of q (inbox records [kW_q, (k+1)W_q)) is carried by m = (q + k) mod 64
 * and staged there at WCAP.q + (position in window).  Per intermediate the tile
 * keeps its count of pieces and its largest piece (the only one when L = 1). */
void th_build(Tile *T)
{
    uint32_t me = T->me;
    for (int m = 0; m < NT; m++) { T->t_th.th_cnt[m] = 0; T->t_th.th_size[m] = 0; T->t_th.th_q[m] = 0xFF; }
    uint32_t zr = 0, zc = 0, lastW = 0, magic = 0;
    for (int q = 0; q < NT; q++) {
        uint32_t s = T->row[q];
        if (T->col[q]) zc++;
        if (!s) continue;
        zr++;
        uint32_t W = (T->M[q] + 63) >> 6, a = T->offo[q], b = a + s;
        if (W != lastW) { lastW = W; magic = udiv32(0xFFFFFFFFu, W) + 1; }   /* M_q mostly equal */
        uint32_t k0 = mulhu(a, magic), k1 = mulhu(b - 1, magic), x = k0 * W;
        for (uint32_t k = k0; k <= k1; k++, x += W) {
            uint32_t lo = a > x ? a : x, hi = b < x + W ? b : x + W, sz = hi - lo;
            uint32_t m = (q + k) & 63;
            T->t_th.th_cnt[m]++;
            if (sz > T->t_th.th_size[m]) {
                T->t_th.th_q[m] = (uint8_t)q;
                T->t_th.th_size[m] = (uint16_t)sz;
                T->t_th.th_src[m] = (uint16_t)(T->rowpos[q] + lo - a);
                T->t_th.th_stage[m] = (uint16_t)(WCAP * q + lo - x);
            }
        }
    }
    /* R2 words: layer-0 group maxima, L, and BT's selection inputs z and Delta(W*) */
    uint32_t L = 0;
    for (int m = 0; m < NT; m++) if (T->t_th.th_cnt[m] > L) L = T->t_th.th_cnt[m];
    for (int g = 0; g < NG; g++) {
        uint32_t mx = 0;
        for (int i = 0; i < 4; i++) {
            uint32_t v = T->t_th.th_size[(me + GROUP_OFF[4 * g + i]) & 63];
            if (v > mx) mx = v;
        }
        T->r2_own[g] = mx;
    }
    uint32_t big = T->nmax > T->Mmax ? T->nmax : T->Mmax;
    uint32_t Ws = (big + 63) >> 6, mw = udiv32(0xFFFFFFFFu, Ws) + 1;
    uint32_t dr = 0, dc = 0;
    for (int q = 0; q < NT; q++) {
        uint32_t a = T->row[q], c = T->col[q];
        if (a) { uint32_t f = mulhu(a, mw); dr += f + (f * Ws != a); }
        if (c) { uint32_t f = mulhu(c, mw); dc += f + (f * Ws != c); }
    }
    T->r2_own[NG] = L;
    T->r2_own[NG + 1] = zr > zc ? zr : zc;
    T->r2_own[NG + 2] = dr > dc ? dr : dc;
}

/* after R2 (every tile the same): TH if it has one layer and wins; otherwise
 * BT's estimate decides whether stage 3 runs */
void sw_stage2(Tile *T, uint32_t version)
{
    uint32_t base = T->costD <= T->costLB ? T->costD : T->costLB;
    uint32_t bpath = T->costD <= T->costLB ? PATH_DIRECT : PATH_LB;
    T->costTH = NONE32; T->costBT = NONE32;
    if (T->r2[NG] == 1) {
        T->costTH = run_cost(T->r2, 4, 0, 0) + C_TH1 + run_cost_flat(T->Wmax, 4) + C_TH2
                  + C_XCH + 900;                             /* the descriptors: build and exchange */
        if (T->nkind == 2)
            T->costTH += run_cost(T->r2, 8, 0, 0) + C_TH1 + run_cost_flat(T->Wmax, 8) + C_TH2;
        T->path = T->costTH < base ? PATH_TH : bpath;
        return;
    }
    bt_select(T, version);
    T->path = T->bt_est < base ? PATH_BT : bpath;
}

/* TH hop-1 descriptors: to each intermediate, staging position | size << 16 */
void th_desc(Tile *T)
{
    for (int m = 0; m < NT; m++)
        T->th_desc_out[m] = T->t_th.th_cnt[m] ? (uint32_t)T->t_th.th_stage[m] | ((uint32_t)T->t_th.th_size[m] << 16) : NONE32;
}

/* hop 1: the source's 64 streams and the intermediate's PTR[s] from desc_in */
void th_hop1(Tile *T, uint32_t kind)
{
    uint32_t me = T->me, base = kind ? 64 : 0, rem = 0;
    for (int i = 0; i < 64; i++) {
        uint32_t d = GROUP_OFF[i], m = (me + d) & 63;
        if (T->t_th.th_cnt[m]) {
            uint32_t src = kind ? A5 + 4 * (uint32_t)T->t_th.th_src[m] : A3 + 2 * (uint32_t)T->t_th.th_src[m];
            write_stream(T, base + i, src, T->t_th.th_size[m], d, kind, SK_MODE_SRC);
        } else write_stream(T, base + i, 0, 0, d, kind, SK_MODE_SRC);
    }
    for (int g = 0; g < NG; g++) HW_GROUP_R(T, (kind ? 16 : 0) + g, T->r2[g]);
    for (int s = 0; s < NT; s++) {
        uint32_t v = T->desc_in[s];
        if (v == NONE32) { HW_PTR(T, s, 0); continue; }
        HW_PTR(T, s, stage_addr(v & 0xFFFFu, kind));
        rem += v >> 16;
    }
    T->hw.sram_remaining = kind ? VALUE_PACKETS * rem : rem;
}

/* hop 2: the intermediate sends each destination its one window; the
 * destination lands window k of itself at inbox + k.W */
void th_hop2(Tile *T, uint32_t kind)
{
    uint32_t me = T->me, base = kind ? 64 : 0, mode = hop2_mode(T, kind);
    for (int i = 0; i < 64; i++) {
        uint32_t d = GROUP_OFF[i], q = (me + d) & 63, k = (me - q) & 63;
        uint32_t W = (T->M[q] + 63) >> 6, x = k * W, sz = 0;
        if (T->M[q] > x) { sz = T->M[q] - x; if (sz > W) sz = W; }
        write_stream(T, base + i, stage_addr(WCAP * q, kind), sz, d, kind, mode);
    }
    for (int g = 0; g < NG; g++) HW_GROUP_R(T, (kind ? 16 : 0) + g, T->Wmax);
    uint32_t W = (T->M[me] + 63) >> 6;
    for (int m = 0; m < NT; m++) {
        uint32_t k = (m - me) & 63;
        HW_PTR(T, m, kind ? A1 + 4 * k * W : A4 + 2 * k * W);
    }
    T->hw.sram_remaining = kind ? VALUE_PACKETS * T->M[me] : T->M[me];
    T->hw.augment_base = A4;
}
