/* transfer.c -- the XFER overlay (worker 6; reorganised in M7): XB, RED, the transfer plan, stage 1, K and V.
 * XB (walk-through 5.3): each tile sends each owner its count, one mode-7 word from the send port per offset in
 *   schedule order (each step a permutation), landing at SK.col[me] on the owner; then plan_transfer(), which fills the
 *   skew planner's Tile, runs stage 1's preparation (sw_prepare) and writes the direct path's tables.
 * RED (5.4): this tile's RV-word vector (16 group maxima, group 15 without the loopback, the loopback stream; skew-v2.md
 *   2.2) max-reduced over a mesh tree and broadcast back by tile 0; then stage 1 (sw_stage1): the direct or LB tables
 *   and K configured (config.c), or stage 2 wanted (main.c then loads TH).
 * K (5.5): start-type: DMA_SRAM_REMAINING first, then the start pulse, WFI for the arrivals; for k = 2, V configure.
 * V (5.6): start-type, as K, with two packets per value. */
#include "sort.h"

void xb(void) {
    /* this tile's count to each owner, then the one-word exchange. The loop before xchg's DMA_REPLY_REMAINING store is
     * fixed-length and data-independent, so every tile reaches the store at GO + the same cycles (+ its 0/+1 skew),
     * well before the first count can arrive (as the start-type commands' identical paths, 6.3). */
    for (int q = 0; q < NT; q++) SK.row[q] = S.cut[q + 1] - S.cut[q];
    xchg(SK.row, SK.col);                          /* every tile's count has landed */
    plan_transfer();
}

void red(void) {
    DMA_CM_REMAINING = S.red_in;                       /* first: before any child's packet can land */
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
    wait_tables();
    /* receive side: one run per source, source-major, in BUF_Y (key i of the buffer gets ordinal i: mode 4). V's
     * receive pointers (BUF_VR + 4 * the same prefix) are written in K's tail, after K's arrivals. */
    uint32_t n = 0;
    for (int p = 0; p < NT; p++) { PTR[p] = BUF_Y + (T->inpos[p] << 1); n += T->col[p]; }
    S.n_recv = n;
    /* the direct path's streams: K 0-63 and V 64-127, stream 4g+i carrying offset GROUP_OFF[4g+i] */
    const uint32_t khi = STREAM_HI(0, 0, KEY_MODE(k128), SIZE_64, 0);   /* the keys' mode: 4 (augmented) with 128-bit values */
    for (int s = 0; s < 64; s++) {
        unsigned off = GROUP_OFF[s], dst = (me + off) & 63;
        uint32_t c0 = cut[dst], cnt = T->row[dst];
        set_stream(STR_K + s, STREAM_LO(BUF_X + (c0 << 1), 0), STREAM_HI(cnt, off, 0, 0, 0) | khi);
        if (k128) set_stream(STR_V + s, STREAM_LO(BUF_VS + (c0 << 2), 0), STREAM_HI(cnt, off, MODE_SRC, SIZE_128, 0));
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

/* K and V (walk-through 5.5, 5.6), start-type, on the direct and LB paths (moved here from exchange.c in M7: XFER
 * stays loaded from XB to L2 on these paths). */
void k_start(void) {
    DMA_SRAM_REMAINING = S.n_recv;                     /* the counter first, */
    start_run();                                   /* then the start pulse: identical code from WFI on every tile */
    if (S.n_recv) wfi();                           /* a count of 0 makes no edge */
    wait_tables();                                 /* our own sends have all issued, and the PTR updates are done */
    if (S.fmt != FMT_K128) return;
    /* V configure (walk-through 5.5 step 4): receive PTR in BUF_VR, source-major as K's; histogram digit off, mode 2
     * (K's digit-1 counts in HIST must survive for L2); V's group run (the start command loads it). */
    for (int p = 0; p < NT; p++) PTR[p] = BUF_VR + (SK.inpos[p] << 2);
    DMA_CONFIG = DMA_CFG_RUN(RUN_GROUPS, GRP_V, S.groups);
}

void v_start(void) {
    DMA_SRAM_REMAINING = S.n_recv << 1;                /* two 64-bit packets per value */
    start_run();
    if (S.n_recv) wfi();
    wait_tables();
}
