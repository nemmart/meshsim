/* main.c -- the sort program's command loop (worker 6; overlays: worker 8, M7; v1.2: WORKER8). C&C sequences the phases
 * with broadcast commands (CONVENTIONS.md section 4); each command runs to its end and returns here, which pulses done
 * and waits for the next one (wait_for_command(), RECIPES 1).
 * v1.2: one image, no overlays. Program memory (32 KB) holds all the code, data memory (32 KB) all the state, so
 * v1.1's overlay loads, its staged images and the shared region (the split's state over the planner's) are gone, and
 * so is SPLIT's zero_planner(): SK is zeroed once by crt0.
 * Snarky One port (../PORTING.md): there are no commands (rtl/tile/base/spec.md 6.5). The command loop is a fixed
 * schedule in the image: after the start-up's barrier, the phases in C&C's order for the run's format, ARGS[ARG_FORMAT]
 * (SortTest.sortRun's), each run as its command was, then the barrier, as wait_for_command() ended each command; the split's own barriers are
 * plain barriers. main returns 0 after L2's, to the master tile (port.c).
 */
#include "sort.h"

struct sort_state S;
Tile SK __attribute__((aligned(8)));

/* The barrier where every phase ends, and the split's (split.c built with -DSPLIT_BARRIER=sort_barrier): one copy of
 * its WFI, which every tile leaves alike. */
__attribute__((noinline)) void sort_barrier(void) { arrive_and_wait(); }

/* One phase, the command loop's case for its command, then the barrier. */
static void phase(uint32_t type, unsigned arg) {
    switch (type) {
    case CMD_L1: l1(arg); break;
    case CMD_SPLIT:
        S.shaped = arg;
        split_get_cuts(BUF_X, S.n_in, S.shaped, S.cut, S.n);
        break;
    case CMD_XB: xb(); break;
    case CMD_RED: red(); break;
    /* skew handling's fixed sequence: a tile finishes a command that is empty for its path at once */
    case CMD_XC: if (S.plan_wanted) xchg(SK.inpos, SK.offo); break;              /* q sends p off[p][q] */
    case CMD_TH:
        S.force_bt = arg & 1;
        if (S.plan_wanted) {
            skew_th();
            if (S.path <= PATH_LB) config_direct_lb();                            /* K's configure, K and V */
        }
        break;
    case CMD_SWEEP: if (S.path == PATH_BT) bt_sweep(); break;
    case CMD_DESC:
        if (S.path == PATH_TH) skew_desc();
        else if (S.path == PATH_BT) {
            bt_descs();
            if (S.path == PATH_BT) bt_first();
            else config_direct_lb();                          /* R3's exact cost lost to direct or LB */
        }
        break;
    case CMD_K: if (S.path <= PATH_LB) k_start(); break;
    case CMD_V: if (S.path <= PATH_LB) v_start(); break;
    case CMD_K1: case CMD_K2: case CMD_V1: case CMD_V2:
        if (S.path == PATH_TH) { if (arg == 0) th_phase(type - CMD_K1); }  /* TH: layer 0 of each */
        else if (S.path == PATH_BT) bt_phase(type - CMD_K1, arg);
        break;
    case CMD_L2: l2(); break;
    }
    sort_barrier();
}

/* Two-hop layer phases (SortTest: K1.0-4, K2.0-4, V1.0-4, V2.0-4). */
#define LAYERS 5

int main(void) {
    const uint32_t fmt = ARGS[ARG_FORMAT];
    sort_barrier();                                /* the start-up's end (crt0): every tile starts L1 together */
    phase(CMD_L1, fmt);
    phase(CMD_SPLIT, 0);
    phase(CMD_XB, 0);
    phase(CMD_RED, 0);
    phase(CMD_XC, 0);
    phase(CMD_TH, 0);
    phase(CMD_SWEEP, 0);
    phase(CMD_DESC, 0);
    phase(CMD_K, 0);
    for (unsigned layer = 0; layer < LAYERS; layer++) phase(CMD_K1, layer);
    for (unsigned layer = 0; layer < LAYERS; layer++) phase(CMD_K2, layer);
    if (fmt == FMT_K128) {
        phase(CMD_V, 0);
        for (unsigned layer = 0; layer < LAYERS; layer++) phase(CMD_V1, layer);
        for (unsigned layer = 0; layer < LAYERS; layer++) phase(CMD_V2, layer);
    }
    phase(CMD_L2, 0);
    return 0;                                      /* crt0 sends it to the master tile (port.c) */
}
