/* transfer.c -- XB, RED, the transfer plan, stage 1, K and V (worker 6; reorganised in M7; v1.2: WORKER8).
 * XB (walk-through 5.3): each tile sends each owner its count, one 32-bit reply from the send port per offset in
 *   schedule order (each step a permutation), landing at SK.col[me] on the owner; then plan_transfer(), which fills the
 *   skew planner's Tile, runs stage 1's preparation (sw_prepare) and writes the direct path's tables.
 * RED (5.4): this tile's RV-word vector (16 group maxima, group 15 without the loopback, the loopback stream; skew-v2.md
 *   2.2) max-reduced over a mesh tree and broadcast back by tile 0; then stage 1 (sw_stage1): the direct or LB tables
 *   and K configured (config.c), or stage 2 wanted (main.c then loads TH).
 * K (5.5): start-type (RECIPES 9): counter 1 first, then the start pulse, then the arrivals and DMA run done; for k = 2,
 *   V configure.
 * V (5.6): start-type, as K, one 128-bit packet per value. */
#include "sort.h"

void xb(void) {
    /* this tile's count to each owner, then the one-word exchange. The loop before xchg's counter-3 store is
     * fixed-length and data-independent, so every tile reaches the store at GO + the same cycles (+ its 0/+1 skew),
     * well before the first count can arrive (as the start-type commands' identical paths, 6.3). */
    for (int q = 0; q < NT; q++) SK.row[q] = S.cut[q + 1] - S.cut[q];
    xchg(SK.row, SK.col);                          /* every tile's count has landed */
    plan_transfer();
}

void red(void) {
    tile->WRITE_COUNTER[CTR_CM - 1] = S.red_in;        /* first: before any child's packet can land */
    uint32_t g[RV];
    tree_reduce(SK.red_own, RV, g, 1);
    for (int i = 0; i < NG; i++) SK.Rdir[i] = g[i];
    SK.Rp15 = g[NG]; SK.Rloop = g[NG + 1];
    decide_stage1();                               /* direct or LB: tables, K configure; or stage 2 */
}


/* RED's tree (walk-through 5.4). Rows reduce toward column 0 (cols 1-4 leftward, cols 5-7 rightward, col 7 into the
 * next row's col 0 over the +1 ring link); column 0 toward tile 0 (rows 1-4 upward, rows 5-7 downward, 56 -> 0).
 * Sets the children's buffers (a mask of RB_R, RB_L, RB_D, RB_U: from me+1, me-1, me+8, me-8), their number, and the
 * parent with the buffer this tile's vector lands in there (tile 0: the broadcast). */
static void red_role(unsigned me) {
    unsigned col = me & 7, row = me >> 3, mask = 0;
    if (col == 0) mask |= 1 << RB_R | 1 << RB_L; else if (col <= 3) mask |= 1 << RB_R; else if (col >= 6) mask |= 1 << RB_L;
    if (col == 0) { if (row == 0) mask |= 1 << RB_D | 1 << RB_U; else if (row <= 3) mask |= 1 << RB_D; else if (row >= 6) mask |= 1 << RB_U; }
    S.red_mask = mask;
    S.red_children = (mask & 1) + (mask >> 1 & 1) + (mask >> 2 & 1) + (mask >> 3 & 1);
    if (me == 0) S.red_parent = DST_BROADCAST;
    else if (col >= 1 && col <= 4) { S.red_parent = me - 1; S.red_pbuf = RB_R; }
    else if (col >= 5) { S.red_parent = (me + 1) & 63; S.red_pbuf = RB_L; }
    else if (row <= 4) { S.red_parent = me - 8; S.red_pbuf = RB_D; }
    else { S.red_parent = (me + 8) & 63; S.red_pbuf = RB_U; }
}

void plan_transfer(void) {
    Tile *T = &SK;
    const uint32_t *cut = S.cut;
    unsigned me = tile_index(), k128 = (S.fmt == FMT_K128);
    T->me = me;
    T->nkind = k128 ? 2 : 1;
    for (int p = 0; p < NT; p++) T->n[p] = S.n[p];
    /* the split's targets M[q] (planner globals): balanced floor(q N / 64) steps, or the input's shape */
    uint32_t N = 0, acc = 0, R0 = 0;
    for (int p = 0; p < NT; p++) N += T->n[p];
    for (int q = 0; q < NT; q++) {                 /* R_(q+1) = floor((q + 1) N / 64), without multiplies */
        acc += N;
        uint32_t R1 = acc >> 6;
        T->M[q] = S.shaped ? T->n[q] : R1 - R0;
        R0 = R1;
    }
    sw_prepare(T);                                 /* rowpos, inpos, red_own[RV], nmax, Mmax, Wmax */
    /* the tables are free: the last run (L1's) was waited for (rule 6) */
    /* receive side: one run per source, source-major, in BUF_Y (key i of the buffer gets ordinal i: mode 3). V's
     * receive pointers (BUF_VR + 4 * the same prefix) are written in K's tail, after K's arrivals. */
    /* The table-window loops step explicit pointers: indexing tables->X[i] from the window's base (0xFFFF2000) costs
     * GCC a lui, a shift and an add per store (v1.1's window at 0x2000 compiled to a stepped pointer; REPORT.md) */
    uint32_t n = 0;
    volatile uint32_t *ptr = tables->PTR;
    for (int p = 0; p < NT; p++) { *ptr++ = BUF_Y + (T->inpos[p] << 1); n += T->col[p]; }
    S.n_recv = n;
    /* the direct path's streams: K 0-63 and V 64-127, stream 4g+i carrying offset GROUP_OFF[4g+i]; every packet
     * counted on counter 1 at its receiver. Sources aligned: keys at even chunks, values at multiples of 4 (D26).
     * STREAM_DEST is not written: modes 1-5 ignore it (spec 4.2). */
    const uint32_t khi = STREAM_COUNT_W(0, SIZE_64, CTR_SRAM, KEY_MODE(k128), 0);   /* the keys' mode: 3 (augmented) with 128-bit values */
    const uint32_t vhi = STREAM_COUNT_W(0, SIZE_128, CTR_SRAM, MODE_SRC, 0);
    volatile uint32_t *ksrc = &tables->STREAM_SRC[STR_K], *kcnt = &tables->STREAM_COUNT[STR_K];
    volatile uint32_t *vsrc = &tables->STREAM_SRC[STR_V], *vcnt = &tables->STREAM_COUNT[STR_V];
    for (int s = 0; s < 64; s++) {
        unsigned off = GROUP_OFF[s], dst = (me + off) & 63;
        uint32_t c0 = cut[dst], dcnt = STREAM_COUNT_W(T->row[dst], 0, 0, 0, off);
        *ksrc++ = STREAM_SRC_W(BUF_X + (c0 << 1), 0); *kcnt++ = dcnt | khi;
        if (k128) { *vsrc++ = STREAM_SRC_W(BUF_VS + (c0 << 2), 0); *vcnt++ = dcnt | vhi; }
    }
    red_role(me);
    S.red_in = S.red_children * (RV / 2);
}

/* After RED (T->Rdir, Rp15, Rloop global): stage 1 of the switch (skew-v2.md 6). PATH_PLAN means stage 2 runs (XC,
 * TH); otherwise the direct or LB tables are written and K configured at once, in RED's tail. */
void decide_stage1(void) {
    Tile *T = &SK;
    sw_stage1(T);
    S.plan_wanted = (T->path == PATH_PLAN);
    S.path = T->path;
    if (!S.plan_wanted) config_direct_lb();
}

/* K and V (walk-through 5.5, 5.6), start-type (RECIPES 9), on the direct and LB paths. */
void k_start(void) {
    tile->WRITE_COUNTER[CTR_SRAM - 1] = S.n_recv;  /* the counter first (a store of 0 does nothing), */
    start_run();                                   /* then the start pulse: identical code from WFI on every tile */
    wait_run(SRAM_EV(S.n_recv));                   /* the arrivals (an unarmed counter never fires) and our own run */
    if (S.fmt != FMT_K128) return;
    /* V configure (walk-through 5.5 step 4): receive PTR in BUF_VR, source-major as K's, 16-byte aligned (one size per
     * pointer: V's PTRs take 128-bit packets only, D26); histogram digit off, mode 1 (K's digit-1 counts in HIST must
     * survive for L2); V's group run (the start command loads it). */
    volatile uint32_t *ptr = tables->PTR;
    for (int p = 0; p < NT; p++) *ptr++ = BUF_VR + (SK.inpos[p] << 2);
    tile->DMA_CONFIG = DMA_CFG_RUN(RUN_GROUPS, GRP_V, S.groups);
}

void v_start(void) {
    tile->WRITE_COUNTER[CTR_SRAM - 1] = S.n_recv;  /* one 128-bit packet per value */
    start_run();
    wait_run(SRAM_EV(S.n_recv));
}
