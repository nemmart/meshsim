/* bt_run.c -- BT on the machine, the layer phases (worker 8, M7; skew-v2.md 2.4, 4 and 5). The BTRUN overlay, with the
 * planner's bt_hop1_src, bt_hop1_m, bt_hop2_m and bt_hop2_d.
 *
 * Phases, in the fixed sequence's order: K1.0 .. K1.(L1-1), K2.0 .. K2.(L2-1), and for 128-bit values V1.*, V2.*.
 * Each is start-type (as K and TH's): DMA_SRAM_REMAINING, then the start pulse by identical code on every tile, WFI, then
 * the next phase's configure in the tail. Step-1 layer l runs bank l mod 4 (GROUP 16(l mod 4) .., 16
 * groups); step 2 runs bank 0. Gap 4 after each group (lead: phases that run every stream at the full window).
 * A step-2 layer's configure needs the half-word exchange m -> d. Its counter must be stored before any word can
 * arrive, so it is stored in the start path of the phase before (DMA_REPLY_REMAINING = 64, before the start pulse); the
 * words are built (bt_hop2_m) and sent once this tile's address generator is done with its tables, and land in
 * desc_in (TH's, dead on the BT path), from which in2 is filled for bt_hop2_d. */
#include "sort.h"

#ifndef BT_GAP
#define BT_GAP 4
#endif

static unsigned layers(unsigned kh) { return (kh & 1) ? S.bt_L2 : S.bt_L1; }

/* the phase after (kh, l), or 0 at the end */
static int next_phase(unsigned kh, unsigned l, unsigned *nkh, unsigned *nl) {
    if (l + 1 < layers(kh)) { *nkh = kh; *nl = l + 1; return 1; }
    *nl = 0;
    if (kh == 0 || kh == 2) { *nkh = kh + 1; return 1; }
    if (kh == 1 && S.fmt == FMT_K128) { *nkh = 2; return 1; }
    return 0;
}

/* Configure phase (kh, l), the table-access rule holding (after wait_tables). A step-2 phase's half-words
 * have landed in desc_in. */
static void bt_config(unsigned kh, unsigned l) {
    Tile *T = &SK;
    unsigned kind = kh >> 1;
    uint32_t cfg;
    if (!(kh & 1)) {
        if (l % NBANK == 0) bt_hop1_src(T, l, kind);         /* the streams of up to four layers, banks 0-3 */
        bt_hop1_m(T, l, kind);
        cfg = DMA_CFG_RUN(RUN_GROUPS, 16 * (l % NBANK), 16);      /* hop 1: mode 2, counting off */
    } else {
        for (int m = 0; m < NT; m++) T->u.h2.in2[m] = (uint16_t)T->desc_in[m];
        bt_hop2_d(T, l, kind);
        cfg = DMA_CFG_RUN(RUN_GROUPS, 0, 16);
        if (kind == KIND_K) {                                /* keys hop 2: histogram digit 1; m's streams augment (mode 4, 128-bit values) */
            DMA_RECORD_BASE_ADDRESS = T->hw.augment_base;
            cfg |= DMA_CFG_HISTDIG(1);
        }
    }
    S.next_rem = T->hw.sram_remaining;
    DMA_CONFIG = cfg;
}

/* DESC's tail on the BT path: the GROUP high words of all four banks, then K1.0. */
void bt_first(void) {
    wait_tables();
    for (int b = 0; b < NBANK; b++)
        for (int g = 0; g < NG; g++) GROUP[2 * (16 * b + g) + 1] = GROUP_HI(64 * b + 4 * g, 4, BT_GAP);
    bt_config(0, 0);
}

void bt_phase(unsigned kh, unsigned l) {
    if (l >= layers(kh)) return;                             /* a layer this sort does not have: empty */
    unsigned nkh = 0, nl = 0, me = SK.me;
    int more = next_phase(kh, l, &nkh, &nl), x2 = more && (nkh & 1);
    DMA_SRAM_REMAINING = S.next_rem;                             /* the counters first, */
    if (x2) DMA_REPLY_REMAINING = NT;                            /* (the next step-2 layer's half-words) */
    start_run();                                             /* then the start pulse: identical code on every tile */
    if (x2) {
        wait_tables();
        bt_hop2_m(&SK, nl, nkh >> 1);                        /* bank 0's streams and out2 */
        uint32_t hdr = ROUTER_HDR(0, MODE_REPLY, CM_WORD((uint32_t)&SK.desc_in[0]) + me);
        for (int s = 0; s < 64; s++) {
            unsigned dst = (me + GROUP_OFF[s]) & 63;
            ROUTER_SET_HDR = hdr | ROUTER_HDR_DST(dst);
            ROUTER_SEND_LO = SK.u.h2.out2[dst];
        }
    }
    if (S.next_rem || x2) wfi();                             /* the transfer's arrivals (and the half-words) */
    wait_tables();
    if (more) bt_config(nkh, nl);
}
