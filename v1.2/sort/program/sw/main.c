/* main.c -- the sort program's command loop (worker 6; overlays: worker 8, M7; v1.2: WORKER8). C&C sequences the phases
 * with broadcast commands (CONVENTIONS.md section 4); each command runs to its end and returns here, which pulses done
 * and waits for the next one (wait_for_command(), RECIPES 1).
 * v1.2: one image, no overlays. Program memory (32 KB) holds all the code, data memory (32 KB) all the state, so
 * v1.1's overlay loads, its staged images and the shared region (the split's state over the planner's) are gone, and
 * so is SPLIT's zero_planner(): SK is zeroed once by crt0. */
#include "sort.h"

struct sort_state S;
Tile SK __attribute__((aligned(8)));

/* The split's barrier (split.c built with -DSPLIT_BARRIER=sort_barrier): every wake from a command reads slot 0. */
__attribute__((noinline)) void sort_barrier(void) { (void)wait_for_command(); }

int main(void) {
    for (;;) {
        uint32_t w = wait_for_command();           /* pulse done, WFI, consume GO, then slot 0 (RECIPES 1) */
        unsigned arg = CMD_ARG(w);
        switch (CMD_TYPE(w)) {
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
            if (S.path == PATH_TH) { if (arg == 0) th_phase(CMD_TYPE(w) - CMD_K1); }  /* TH: layer 0 of each */
            else if (S.path == PATH_BT) bt_phase(CMD_TYPE(w) - CMD_K1, arg);
            break;
        case CMD_L2: l2(); break;
        case CMD_EXIT: return 0;                   /* crt0 stores it to the exit word (8.15) */
        default: return 0x100 | CMD_TYPE(w);      /* unknown command: stop (the exit value says why) */
        }
    }
}
