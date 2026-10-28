/* skew.c -- stage 2 of the skew switch and the TH path on the machine (worker 6, S2; skew-v2.md 2.3, 5, 6).
 * The planner functions are worker 5's (design_v2/sort/skew/planner); this file supplies the machine around them:
 * the exchanges, the R2 reduction, the decision, and TH's four layer phases, each start-type with its configure and
 * configure in the tail of the command before it (as K's is in RED's tail). Commands and what they do on each path:
 *   XC    PLAN: off[me][q] from each q (one-word exchange)          otherwise empty
 *   TH    PLAN: th_build, R2 (19 words), sw_stage2; TH: th_desc;    otherwise empty
 *         not TH: the direct or LB tables and K configured
 *   DESC  TH: TH's hop-1 descriptors (one-word exchange); K1.0 configured
 *   K     direct / LB: the keys all-to-all                         TH: empty
 *   K1.0  TH: keys hop 1, then K2.0 configured;   K2.0  TH: keys hop 2 (mode 4 for 128-bit values, histogram digit 1), then V1.0 configured
 *   V     direct / LB: the values all-to-all                       TH: empty
 *   V1.0  TH: values hop 1, then V2.0 configured;  V2.0  TH: values hop 2
 * XC is the kernel's (main.c). BT (stage 3) is bt_plan.c and bt_run.c (M7). This file is in the TH overlay. */
#include "sort.h"

/* TH's layer phases run every stream at R = W (one window per stream): the receivers' eject runs at ~100 % for the whole
 * phase, with no slack to drain the burst at a group boundary (offsets change, so path latencies change). At gap 0
 * the backlog grows group by group and overflows the eject FIFOs (meas: reverse sorted, 64 x 2,000: eject 16 of 16,
 * packets lost). A gap after each group drains it. Worker 5's harness has no network, so it could not see this. */
#ifndef TH_GAP
#define TH_GAP 4
#endif

void skew_th(void) {
    if (!S.plan_wanted) return;
    DMA_CM_REMAINING = S.red_children * ((NG + 3 + 1) >> 1);    /* first: R2's packets from the children */
    Tile *T = &SK;
    th_build(T);
    tree_reduce(T->r2_own, NG + 3, T->r2, 1);
    sw_stage2(T, 1);
    if (S.force_bt && T->bt_est != NONE32) T->path = PATH_BT;      /* TEST ONLY */
    S.path = T->path;
    S.bt_wanted = (T->path == PATH_BT);
    if (T->path == PATH_TH) th_desc(T);           /* direct or LB: main.c loads XFER and configures K; BT: SWEEP, DESC */
}

/* Configure one TH layer phase (the table-access rule holds: called after the previous phase's wake and
 * wait_tables). The planner writes STREAM, GROUP R and PTR; the GROUP high words, the counters and DMA_CONFIG are
 * the sort's: hop 1 with counting off; keys hop 2 with histogram digit 1. Augmenting (128-bit values) is hop 2's stream
 * mode, 4, which the planner's th_hop2 writes on the intermediate tile (meshsim M12). */
static void th_config(unsigned kind, unsigned hop) {
    Tile *T = &SK;
    unsigned co = kind ? 16 : 0, sl = kind ? 64 : 0;
    wait_tables();
    if (hop == 1) th_hop1(T, kind); else th_hop2(T, kind);
    for (int g = 0; g < NG; g++) GROUP[2 * (co + g) + 1] = GROUP_HI(sl + 4 * g, 4, TH_GAP);
    S.next_rem = T->hw.sram_remaining;
    uint32_t cfg = DMA_CFG_RUN(RUN_GROUPS, co, 16);
    if (hop == 2 && kind == KIND_K) {
        DMA_RECORD_BASE_ADDRESS = T->hw.augment_base; /* BUF_Y: received key i gets ordinal i */
        cfg |= DMA_CFG_HISTDIG(1);
    }
    DMA_CONFIG = cfg;
}

void skew_desc(void) {
    if (S.path != PATH_TH) return;
    xchg(SK.th_desc_out, SK.desc_in);              /* hop-1 descriptors: source p -> intermediate m */
    th_config(KIND_K, 1);
}

/* A TH layer phase (start-type): the counter, the start pulse (identical code from WFI on every tile), WFI, then the
 * next phase's configure. */
void th_phase(unsigned phase) {                    /* 0 K1.0, 1 K2.0, 2 V1.0, 3 V2.0 */
    DMA_SRAM_REMAINING = S.next_rem;
    start_run();
    if (S.next_rem) wfi();
    wait_tables();
    unsigned next = phase + 1;                     /* configure the next one: K2.0, V1.0 (128-bit values), V2.0 */
    if (next < 4 && (next < 2 || S.fmt == FMT_K128)) th_config(next >> 1, (next & 1) + 1);
}
