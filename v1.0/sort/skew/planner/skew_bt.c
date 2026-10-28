/* skew_bt.c -- stage 3: the balanced two-hop transfer (BT) of the research
 * note, re-planned for design v2.  See skew-v2.md 3 and 5.
 *
 * Cells: tile s's slice for destination d is cut into k = ceil(S/w) pieces of
 * w records, the last one possibly shorter (w >= 256, so k <= 256).  The
 * diagonal sweep gives every piece an intermediate m.  Step 1 (s -> m) and
 * step 2 (m -> d) each run in layers, one group run per layer, at most one
 * piece per pair per layer: a piece's step-1 layer is its rank among (s, m)'s
 * pieces in sweep order, its step-2 layer its rank among (m, d)'s pieces in
 * (step-1 layer, s) order.
 *
 * Descriptors: s -> m, one word per piece:  d | j << 6 | (size - 1) << 16
 *              (bits 26-28: the step-2 layer, stamped by m)
 *              m -> d, one half-word per step-2 layer: s | j << 6
 * The destination computes the landing, off[s][d] + j.w, from its own column.
 */
#include "skew.h"

static inline uint32_t mulhu(uint32_t a, uint32_t b)
{
    return (uint32_t)(((uint64_t)a * b) >> 32);
}

/* ceil(x / w) with the tile's magic (exact for x.w < 2^32) */
static inline uint32_t ceil_w(const Tile *T, uint32_t x)
{
    uint32_t f = mulhu(x, T->bt_magic);
    return f + (f * T->bt_w != x);
}

/* ------------------------------------------------------------- selection */
/* the cost estimate of BT at L layers, from what every tile has after R2:
 * n_max, M_max, z, Delta(W*).  version 0: the research note's formula as the
 * cost model used it (checkpoint); version 1: tuned (RESULTS.md, "Tuning"). */
#ifndef V1_LAYER
#define V1_LAYER      7700    /* set-up of one layer, one kind, steps 1 and 2 averaged [meas] */
#endif
#ifndef V1_ROUND
#define V1_ROUND      560     /* sweep: per round on the critical path [meas] */
#endif
#ifndef V1_PIECE
#define V1_PIECE      22      /* per own piece per descriptor scan [meas] */
#endif
#ifndef V1_FIXED
#define V1_FIXED      16000   /* sweep set-up, held statistics, R3, the scans' fixed part [meas] */
#endif

uint32_t bt_estimate(const Tile *T, uint32_t L, uint32_t *w_out, uint32_t version)
{
    uint32_t big = T->nmax > T->Mmax ? T->nmax : T->Mmax;
    uint32_t Ws = (big + 63) >> 6, z = T->r2[NG + 1], dl = T->r2[NG + 2], w;
    if (L == 1) {
        if (dl > 64) return NONE32;
        w = Ws;
    } else {
        if (64 * L <= z) return NONE32;
        uint32_t den = 64 * L - z;
        w = udiv32(64 * Ws + den - 1, den);
    }
    *w_out = w;
    uint32_t pieces = udiv32(T->nmax + w - 1, w) + z;
    if (version == 0) {
        uint32_t xfer = 2 * L * (16 * w * 12 + 2 * (C_TAIL + 900));
        uint32_t plan = 128 * (220 + 3 * 2) + C_SEND * 64 * 2 * L + 2 * (C_TAIL + C_B)
                      + red_cost(2 * L) + 30 * 2 * pieces;
        return xfer + plan;
    }
    /* version 1: measured set-up per layer and planner terms, and the memory
     * rule: room for one overflow layer (L + 1 <= LMAX), pieces <= PMAX, w >= 256 */
    if (L + 1 > LMAX || pieces > PMAX || w < 256) return NONE32;
    uint32_t P = T->nkind == 2 ? 12 : 4;     /* keys + values, or keys only */
    uint32_t xfer = 2 * L * (16 * w * P + T->nkind * (C_TAIL + V1_LAYER));
    uint32_t plan = 128 * V1_ROUND + L * V1_PIECE * pieces + V1_FIXED;
    return xfer + plan;
}

void bt_select(Tile *T, uint32_t version)
{
    uint32_t best = NONE32, bw = 0, bL = 0, Lhi = version ? LMAX : 10;
    for (uint32_t L = 1; L <= Lhi; L++) {
        uint32_t w, e = bt_estimate(T, L, &w, version);
        if (e < best) { best = e; bw = w; bL = L; }
    }
    T->bt_est = best; T->bt_w = bw; T->bt_L = bL;
    T->bt_magic = bw ? udiv32(0xFFFFFFFFu, bw) + 1 : 0;
}

void bt_force(Tile *T, uint32_t L)
{
    uint32_t w;
    T->bt_est = bt_estimate(T, L, &w, 0);
    T->bt_w = w; T->bt_L = L;
    T->bt_magic = udiv32(0xFFFFFFFFu, w) + 1;
}

/* ----------------------------------------------------------------- sweep */
/* Counts are kept as level masks: rmask[v] = {m : r[m] <= v} for the tile's own
 * row, and the column token cmask[v] = {m : c[m] <= v}.  The lowest level v
 * with rmask[v] & cmask[v] nonempty is the lowest max(r, c); within it the
 * lowest u with (rmask[u] | cmask[u]) gives the lowest min(r, c), i.e. r + c.
 * The first such m in rotated order from m0 = me + d is taken; one piece at a
 * time is the same choice as the research note's batches.  The level only
 * rises within a cell, so the search starts where the last piece's ended. */
static const uint8_t DEBRUIJN[32] = {
    0, 1, 28, 2, 29, 14, 24, 3, 30, 22, 20, 15, 25, 17, 4, 8,
    31, 27, 13, 23, 21, 19, 16, 7, 26, 12, 18, 6, 11, 5, 10, 9 };

static inline uint32_t ctz32(uint32_t x)            /* x != 0 */
{
    return DEBRUIJN[((x & (0u - x)) * 0x077CB531u) >> 27];
}

void bt_sweep_init(Tile *T)
{
    for (int m = 0; m < NT; m++) {
        uint32_t rw = T->row[m];
        T->r[m] = 0;
        T->kc[m] = rw ? (uint8_t)(ceil_w(T, rw) - 1) : 0;
    }
    for (int v = 0; v < VL; v++) { T->v.rmask[v][0] = 0xFFFFFFFFu; T->v.rmask[v][1] = 0xFFFFFFFFu; }
    T->pcur = 0;
}

void bt_sweep_cell(Tile *T, uint32_t pass, uint32_t t)
{
    uint32_t me = T->me, d = (me + t) & 63;
    if (!T->row[d]) return;
    uint32_t k = T->kc[d] + 1u;
    if ((k >= 4) != (pass == 0)) return;
    if (T->pcur + k > PMAX) { T->err |= ERR_PIECES; return; }
    uint32_t (*R)[2] = T->v.rmask, (*C)[2] = T->u.sw.cmask;
    uint8_t *r = T->r, *pm = T->piece_m + T->pcur;
    uint32_t m0 = (me + d) & 63, sh = m0 & 31, v = 0;
    T->pcur += k;
    for (uint32_t n = 0; n < k; ) {
        uint32_t u, lo, hi;
        for (;; v++) {
            if (v == VL) { T->err |= ERR_COUNT; return; }
            lo = R[v][0] & C[v][0]; hi = R[v][1] & C[v][1];
            if (lo | hi) break;
        }
        for (u = 0; u < v; u++) {
            uint32_t l2 = lo & (R[u][0] | C[u][0]), h2 = hi & (R[u][1] | C[u][1]);
            if (l2 | h2) { lo = l2; hi = h2; break; }
        }
        /* rotate right by m0 so bit 0 is m0; take the candidates lowest bit first */
        if (m0 & 32) { uint32_t x = lo; lo = hi; hi = x; }
        if (sh) { uint32_t x = lo; lo = (lo >> sh) | (hi << (32 - sh)); hi = (hi >> sh) | (x << (32 - sh)); }
        do {
            uint32_t p;
            if (lo) { p = ctz32(lo); lo &= lo - 1; } else { p = 32 + ctz32(hi); hi &= hi - 1; }
            uint32_t m = (m0 + p) & 63, a = r[m], c = a == v ? u : v, bit = 1u << (m & 31), wd = m >> 5;
            R[a][wd] &= ~bit;
            C[c][wd] &= ~bit;
            r[m] = (uint8_t)(a + 1);
            *pm++ = (uint8_t)m;
            n++;
        } while (n < k && (lo | hi));
    }
}

/* after the sweep: own L1, and the nonempty cells in sweep order (the masks' room) */
void bt_sweep_done(Tile *T)
{
    uint32_t L = 0, n = 0, me = T->me;
    for (int m = 0; m < NT; m++) if (T->r[m] > L) L = T->r[m];
    T->L1own = L;
    if (L > LMAX) T->err |= ERR_LAYERS;
    for (uint32_t pass = 0; pass < 2; pass++)
        for (uint32_t t = 0; t < NT; t++) {
            uint32_t d = (me + t) & 63;
            if (T->row[d] && ((T->kc[d] >= 3) == (pass == 0))) T->v.order[n++] = (uint8_t)d;
        }
    T->ncell = n;
}

/* ------------------------------------------------- own pieces, sweep order */
/* step-1 descriptors of one layer: desc_out[m] = the piece of rank `layer` for m */
void bt_desc(Tile *T, uint32_t layer)
{
    uint32_t w = T->bt_w, mx = 0, *out = T->u.desc_out;
    uint8_t *r = T->r;
    const uint8_t *pm = T->piece_m;
    for (int m = 0; m < NT; m++) { r[m] = 0; out[m] = NONE32; }
    for (uint32_t c = 0; c < T->ncell; c++) {
        uint32_t d = T->v.order[c], k = T->kc[d] + 1u;
        for (uint32_t j = 0; j < k; j++) {
            uint32_t m = *pm++, a = r[m];
            r[m] = (uint8_t)(a + 1);
            if (a != layer) continue;
            uint32_t sz = j + 1 < k ? w : T->row[d] - j * w;
            out[m] = d | (j << 6) | ((sz - 1) << 16);
            if (sz > mx) mx = sz;
        }
    }
    T->R1own[layer] = mx;
}

/* step 1, at the source: the streams of layers first .. first + NBANK - 1, layer l
 * in STREAM bank (l - first) (streams 64(l - first) ..), its GROUP entries
 * 16(l - first) .. at the layer maximum R1[l]; one scan of the own pieces */
void bt_hop1_src(Tile *T, uint32_t first, uint32_t kind)
{
    uint32_t me = T->me, w = T->bt_w, L1 = T->r3[0];
    uint32_t base = kind ? A5 : A3, scale = kind ? 4 : 2;
    uint8_t *r = T->r;
    const uint8_t *pm = T->piece_m;
    for (int i = 0; i < 64 * NBANK; i++) HW_STREAM_HI(T, i, 0);
    for (int m = 0; m < NT; m++) r[m] = 0;
    for (uint32_t c = 0; c < T->ncell; c++) {
        uint32_t d = T->v.order[c], k = T->kc[d] + 1u, src = base + scale * T->rowpos[d];
        for (uint32_t j = 0; j < k; j++, src += scale * w) {
            uint32_t m = *pm++, l = r[m] - first;
            r[m]++;
            if (l >= NBANK) continue;
            uint32_t sz = j + 1 < k ? w : T->row[d] - j * w, off = (m - me) & 63;
            write_stream(T, 64 * l + STREAM_OF[off], src, sz, off, kind, 2u);
        }
    }
    for (uint32_t l = 0; l < NBANK && first + l < L1; l++) {
        uint32_t R = T->r3[3 + first + l];
        for (int g = 0; g < NG; g++) HW_GROUP_R(T, 16 * l + g, R);
    }
}

/* staging: first fit over three segments, [0,32768) in A0, [32768,65536) in A3,
 * [65536,69632) in A7's top 64 KB (values); keys use the same index in A0-A1 */
static inline uint32_t alloc3(uint32_t *c, uint32_t sz)
{
    uint32_t p;
    if (c[0] + sz <= SEG_A0) { p = c[0]; c[0] += sz; }
    else if (c[1] + sz <= SEG_A3) { p = c[1]; c[1] += sz; }
    else { p = c[2]; c[2] += sz; }
    return p;
}

/* at m, after all step-1 descriptors have landed in held[][]: stamp each piece's
 * step-2 layer (bits 26-28) -- its rank among (m, d)'s pieces in (step-1
 * layer, s) order -- the step-2 layer maxima, own L2, staging high-water */
void bt_held_stats(Tile *T)
{
    uint8_t *cnt2 = T->r;                      /* r is free again: counter per destination */
    uint32_t c[3] = { 0, SEG_A0, SEG_A3 }, L2 = 0, *h = &T->held[0][0];
    for (int d = 0; d < NT; d++) cnt2[d] = 0;
    for (int l = 0; l <= LMAX; l++) T->R2own[l] = 0;
    for (uint32_t i = 0; i < LMAX * NT; i++) {
        uint32_t v = h[i];
        if (v == NONE32) continue;
        uint32_t d = v & 63, sz = ((v >> 16) & 1023) + 1, l2 = cnt2[d];
        cnt2[d] = (uint8_t)(l2 + 1);
        if (l2 >= LMAX) { T->err |= ERR_LAYERS; continue; }
        h[i] = v | (l2 << 26);
        if (l2 >= L2) L2 = l2 + 1;
        if (sz > T->R2own[l2]) T->R2own[l2] = sz;
        alloc3(c, sz);
    }
    T->L2own = L2;
    T->stage_own = c[2];
    if (c[2] > SEG_END) T->err |= ERR_STAGE;
}

/* after R3 (L1, L2, staging max, R1[], R2[]; every tile the same): exact cost */
void bt_decide(Tile *T)
{
    uint32_t L1 = T->r3[0], L2 = T->r3[1], st = T->r3[2];
    const uint32_t *R1 = &T->r3[3], *R2 = &T->r3[3 + LMAX + 1];
    uint32_t base = T->costD <= T->costLB ? T->costD : T->costLB;
    uint32_t bpath = T->costD <= T->costLB ? PATH_DIRECT : PATH_LB;
    if (L1 > LMAX || L2 > LMAX || st > SEG_END) { T->costBT = NONE32; T->path = bpath; return; }
    uint32_t two = T->nkind == 2;
    uint32_t c = T->nkind * C_BT1SRC * ((L1 + NBANK - 1) / NBANK);
    for (uint32_t l = 0; l < L1; l++)
        c += run_cost_flat(R1[l], 4) + C_BT1 + (two ? run_cost_flat(R1[l], 8) + C_BT1 : 0);
    for (uint32_t l = 0; l < L2; l++)
        c += run_cost_flat(R2[l], 4) + C_BT2 + (two ? run_cost_flat(R2[l], 8) + C_BT2 : 0);
    T->costBT = c;
    T->path = c < base ? PATH_BT : bpath;
}

/* ------------------------------------------------------------ layer set-up */
/* step 1, one layer, at m: this layer's pieces land at their staging places */
void bt_hop1_m(Tile *T, uint32_t layer, uint32_t kind)
{
    uint32_t rem = 0, *c = T->stc;
    if (layer == 0) { c[0] = 0; c[1] = SEG_A0; c[2] = SEG_A3; }
    const uint32_t *h = T->held[layer];
    for (uint32_t s = 0; s < NT; s++) {
        uint32_t v = h[s];
        if (v == NONE32) { HW_PTR(T, s, 0); continue; }
        uint32_t sz = ((v >> 16) & 1023) + 1;
        HW_PTR(T, s, stage_addr(alloc3(c, sz), kind));
        rem += sz;
    }
    T->hw.sram_remaining = kind ? 2 * rem : rem;
}

/* step 2, one layer, at m: streams for the pieces stamped with this layer, at
 * their staging places (replayed in step-1 order), and out2[d] = (s, j) */
void bt_hop2_m(Tile *T, uint32_t layer, uint32_t kind)
{
    uint32_t me = T->me, c[3] = { 0, SEG_A0, SEG_A3 }, n = T->r3[0] * NT;
    /* the streams' fixed fields, mode and read size, as one constant held across the loop (as M11's mode-2 constant
     * was): holding the mode on its own pushed one of the loop's constants out of its register, one more instruction
     * per held entry (meshsim M12, DEV 125) */
    const uint32_t hi0 = stream_hi_fixed(hop2_mode(T, kind), kind);
    uint16_t *out = T->u.h2.out2;
    for (int i = 0; i < 64; i++) HW_STREAM_HI(T, i, 0);
    for (int d = 0; d < NT; d++) out[d] = NONE16;
    const uint32_t *h = &T->held[0][0];
    for (uint32_t i = 0; i < n; i++) {
        uint32_t v = h[i];
        if (v == NONE32) continue;
        uint32_t sz = ((v >> 16) & 1023) + 1, pos = alloc3(c, sz);
        if (((v >> 26) & 7) != layer) continue;
        uint32_t d = v & 63, off = (d - me) & 63;
        write_stream_hi(T, STREAM_OF[off], stage_addr(pos, kind), sz, off, hi0);
        out[d] = (uint16_t)((i & 63) | (((v >> 6) & 1023) << 6));
    }
    uint32_t R = T->r3[3 + LMAX + 1 + layer];
    for (int g = 0; g < NG; g++) HW_GROUP_R(T, g, R);
}

/* step 2, one layer, at d: PTR[m] = the landing of the piece m sends */
void bt_hop2_d(Tile *T, uint32_t layer, uint32_t kind)
{
    uint32_t w = T->bt_w, rem = 0;
    (void)layer;
    const uint16_t *in = T->u.h2.in2;
    for (uint32_t m = 0; m < NT; m++) {
        uint32_t v = in[m];
        if (v == NONE16) { HW_PTR(T, m, 0); continue; }
        uint32_t s = v & 63, x = (v >> 6) * w, sz = T->col[s] - x;
        if (sz > w) sz = w;
        uint32_t land = T->inpos[s] + x;
        HW_PTR(T, m, kind ? A1 + 4 * land : A4 + 2 * land);
        rem += sz;
    }
    T->hw.sram_remaining = kind ? 2 * rem : rem;
    T->hw.augment_base = A4;
}
