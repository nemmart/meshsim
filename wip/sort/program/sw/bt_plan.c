/* bt_plan.c -- BT on the machine, the planning commands (worker 8, M7; skew-v2.md 2.4 and 5; the planner is worker 5's,
 * design_v2/sort/skew/planner/skew_bt.c). The BTPLAN overlay, with the planner's sweep, descriptor, held-statistics and
 * decision functions.
 *
 * SWEEP: 128 rounds of the diagonal sweep as a pipeline around the ring, one command, no C&C. In round r tile s places
 * cell (s, s + r mod 64) with column s + r's token, which tile s + 1 used in round r - 1. Worker 5's harness passed the
 * token with no flow control; on the machine an early token would overwrite one still in use, and its packets would
 * meet a counter at 0 (invariant 9). So (M7):
 *   - three landing buffers at each tile, used in turn (token for round q in buffer q mod 3), each 9 packets: the
 *     token's 16 words, then a sequence word (q), so the receiver polls one word and knows the token is complete
 *     (one sender, one kind: in order);
 *   - the cell is placed on the token where it landed (bt_sweep_cell_c, no copy), and the token goes on from there;
 *   - then the receiver sends its right neighbour (the sender) one credit packet: q, "buffer released";
 *   - a sender sends token q only when the credit says the receiver has released token q - 3 (the same buffer);
 *   - DMA_CM_REMAINING is stored once, first in the command, with every packet the sweep will bring (127 tokens of 9
 *     packets, 127 credits) plus the L1 reduction's packets from RED's tree children; nothing waits on it until the
 *     end, so it never reaches 0 early.
 * Measured before this version (two buffers, a copy into u.sw.cmask, a send loop): 82.3K on trace hot 100, 30 %, 32-bit
 * values, of which about 187 cycles per round of copying and sending on the critical path. A C&C barrier per round
 * instead of credits would cost at least the sum over rounds of the slowest tile's cell (106.8K on that set, worker 5's
 * harness) plus 128 barriers, and would put the barriers on every sort (C&C does not know the path).
 * The buffers are TH's descriptor arrays (th_desc_out, desc_in), dead on the BT path. Then bt_sweep_done and the L1
 * reduction.
 *
 * DESC (26n): every step-1 layer's exchange at once, L words from each sender to each receiver, more than one reply (64
 * bits) carries, so they are mode-6 writes into held[l][sender] (as v1.0's mode-7 words were), and after a sender's
 * last layer one more mode-6 word, 1, into desc_in[sender]: one sender's packets to one receiver arrive in order, so
 * desc_in[s] = 1 says s's descriptors are all in. desc_in is cleared at the end of SWEEP (whose packets have all
 * landed then). DMA_CM_REMAINING = 64 (L + 1) + R3's packets from the children, first; bt_desc and the sends per
 * layer, then the markers; a poll of the 64 markers; held statistics, R3 (15 words, max) on the tree (its WFI covers
 * every packet), bt_decide. The caller (main.c) loads BTRUN and configures K1.0, or configures the
 * direct or LB path. */
#include "sort.h"
#include <stddef.h>

#define ROUNDS  (2 * NT)                 /* two passes of 64 rounds */
#define TOK_W   18                       /* one landing buffer: 16 words of token, the sequence word, a pad */
#define TOK_PK  9
#ifndef NBUF
#define NBUF    3                        /* landing buffers (a sender may run NBUF - 1 rounds ahead of its receiver) */
#endif
#define TOFF    ((offsetof(Tile, th_desc_out) & 4) >> 2)    /* the landing buffers 8-byte aligned (v1.0 needed it for mode 5; kept) */
_Static_assert(offsetof(Tile, desc_in) == offsetof(Tile, th_desc_out) + 4 * NT, "th_desc_out and desc_in adjacent");
_Static_assert(TOFF + NBUF * TOK_W + 2 <= 2 * NT, "the landing buffers and the credit word fit th_desc_out + desc_in");
#define TOK(b)  (&SK.th_desc_out[TOFF + (b) * TOK_W])
#define CREDIT  (&SK.th_desc_out[TOFF + NBUF * TOK_W])

/* The protocol's waits, as functions so that the harness can measure them per tile (SortTest -Dsort.watch). */
__attribute__((noinline)) static void tok_wait(volatile uint32_t *b, uint32_t r) { while (b[2 * VL] != r) ; }
__attribute__((noinline)) static void cr_wait(volatile uint32_t *cr, uint32_t r) { while ((int32_t)(*cr - r) < 0) ; }

void bt_sweep(void) {
    Tile *T = &SK;
    unsigned me = T->me, left = (me - 1) & 63, right = (me + 1) & 63;
    DMA_CM_REMAINING = (ROUNDS - 1) * (TOK_PK + 1) + S.red_children;     /* first (see above) */
    volatile uint32_t *cr = CREDIT;
    uint32_t hTok[NBUF];
    for (int b = 0; b < NBUF; b++) hTok[b] = ROUTER_HDR(left, MODE_CM, CM_WORD((uint32_t)TOK(b)));
    const uint32_t hCr = ROUTER_HDR(right, MODE_CM, CM_WORD((uint32_t)CREDIT));
    bt_sweep_init(T);
    uint32_t *C = &T->u.sw.cmask[0][0];
    for (int i = 0; i < 2 * VL; i++) C[i] = 0xFFFFFFFFu;               /* round 0: every column at count 0 */
    unsigned bin = 0, bout = 1;                                         /* r mod NBUF, (r + 1) mod NBUF */
    for (uint32_t r = 0; r < ROUNDS; r++) {
        if (r) { C = (uint32_t *)TOK(bin); tok_wait(C, r); }            /* token r complete, in buffer r mod 3 */
        bt_sweep_cell_c(T, r >> 6, r & 63, (uint32_t (*)[2])C);
        if (r + 1 < ROUNDS) {
            if (r + 1 >= NBUF) cr_wait(cr, r + 1 - NBUF);               /* the left neighbour released that buffer's token */
            ROUTER_SET_HDR = hTok[bout];                                       /* token r + 1 into buffer (r + 1) mod NBUF */
#pragma GCC unroll 8
            for (int v = 0; v < VL; v++) send64(C[2 * v + 1], C[2 * v]);
            send64(0, r + 1);
        }
        if (r) { ROUTER_SET_HDR = hCr; send64(0, r); }                         /* credit: token r's buffer released */
        bin = bin == NBUF - 1 ? 0 : bin + 1; bout = bout == NBUF - 1 ? 0 : bout + 1;
    }
    bt_sweep_done(T);
    for (int s = 0; s < NT; s++) T->desc_in[s] = 0;     /* DESC's markers (the landing buffers are dead now) */
    if (!S.red_children) wfi();         /* the sweep's last packets (a tile with children waits in tree_reduce) */
    uint32_t v[2] = { T->L1own, 0 }, g[2];
    tree_reduce(v, 1, g, 1);
    S.bt_L1 = g[0];
}

void bt_descs(void) {
    Tile *T = &SK;
    unsigned me = T->me, L = S.bt_L1 < LMAX ? S.bt_L1 : LMAX;
    DMA_CM_REMAINING = NT * (L + 1) + S.red_children * ((3 + 2 * (LMAX + 1) + 1) >> 1);   /* first: every layer's
                                                                   descriptors, the markers, and R3's packets from the children */
    for (unsigned l = L; l < LMAX; l++) for (int s = 0; s < NT; s++) T->held[l][s] = NONE32;
    for (unsigned l = 0; l < L; l++) {
        bt_desc(T, l);
        uint32_t hdr = ROUTER_HDR(0, MODE_CM, CM_WORD((uint32_t)&T->held[l][0]) + me);
        for (int s = 0; s < 64; s++) {
            unsigned dst = (me + GROUP_OFF[s]) & 63;
            ROUTER_SET_HDR = hdr | ROUTER_HDR_DST(dst);
            ROUTER_SEND_32 = T->u.desc_out[dst];
        }
    }
    {                                                       /* the markers: after the last layer, in the same order */
        uint32_t hdr = ROUTER_HDR(0, MODE_CM, CM_WORD((uint32_t)&T->desc_in[0]) + me);
        for (int s = 0; s < 64; s++) {
            unsigned dst = (me + GROUP_OFF[s]) & 63;
            ROUTER_SET_HDR = hdr | ROUTER_HDR_DST(dst);
            ROUTER_SEND_32 = 1;
        }
    }
    /* The descriptors alone, by polling: a WFI would also wait for the children's R3 packets, which they send only
     * after their own held statistics, so bt_held_stats (7-8K) would run one tree level at a time (measured in v1.0:
     * DESC 86K instead of about 40K on trace hot 100, 30 %). Every sender's marker in: every descriptor is in held[][]
     * (one sender's packets arrive in order). With children, the counters-done edge comes when the last packet of all
     * (descriptors, markers, R3) lands and tree_reduce's WFI consumes it; without children the descriptors and markers
     * are the only count, and this tile WFIs for them. */
    if (S.red_children) {
        volatile uint32_t *mk = T->desc_in;
        for (int s = 0; s < NT; s++) while (mk[s] != 1) ;
    } else wfi();
    bt_held_stats(T);
    uint32_t v[3 + 2 * (LMAX + 1) + 1];
    v[0] = T->L1own; v[1] = T->L2own; v[2] = T->err ? NONE32 : T->stage_own;
    for (int l = 0; l <= LMAX; l++) { v[3 + l] = T->R1own[l]; v[3 + LMAX + 1 + l] = T->R2own[l]; }
    tree_reduce(v, 3 + 2 * (LMAX + 1), T->r3, 1);           /* WFI for the children's R3 packets */
    bt_decide(T);
    if (S.force_bt && T->costBT != NONE32) T->path = PATH_BT;      /* TEST ONLY */
    S.path = T->path;
    S.bt_L1 = T->r3[0]; S.bt_L2 = T->r3[1];
}
