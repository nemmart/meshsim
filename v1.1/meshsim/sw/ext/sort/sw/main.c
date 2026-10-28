/* main.c -- the sort program's command loop and overlay loader (worker 6; overlays: worker 8, M7). Resident (the
 * kernel). C&C sequences the phases with broadcast commands (CONVENTIONS.md section 4); each command runs code in the
 * kernel or in the overlay image this loop has loaded for it. An image is loaded in the tail of the command before
 * the one that needs it (after its work, before done), or at the start of a command that only works locally (L2), so
 * no command's "counters first" store is ever delayed by a load. */
#include "sort.h"

struct sort_state S;
Tile SK __attribute__((section(".bss.plan"), aligned(8)));

/* ---- overlays: the linker's OVERLAY (sort.ld) gives every image the same VMA (_ov_base) and consecutive load
 * addresses from STAGE_LMA, each a multiple of 8 bytes; C&C stages them in that order at stage_base() at boot. */
#define STAGE_LMA 0x10000000u
extern char _ov_base[];
extern char __load_start_ov_local[], __load_stop_ov_local[], __load_start_ov_split[], __load_stop_ov_split[],
    __load_start_ov_xfer[], __load_stop_ov_xfer[], __load_start_ov_th[], __load_stop_ov_th[],
    __load_start_ov_btplan[], __load_stop_ov_btplan[], __load_start_ov_btrun[], __load_stop_ov_btrun[];
static char *const OVT[OV_N][2] = {
    { __load_start_ov_local, __load_stop_ov_local }, { __load_start_ov_split, __load_stop_ov_split },
    { __load_start_ov_xfer, __load_stop_ov_xfer }, { __load_start_ov_th, __load_stop_ov_th },
    { __load_start_ov_btplan, __load_stop_ov_btplan }, { __load_start_ov_btrun, __load_stop_ov_btrun } };
static uint32_t ov_cur;          /* the image in the region: OV_LOCAL (0) from boot */

/* Spec 6.5's block copy: one mode-6 loopback stream from the staging area into the region, 64 bits per packet; wfi()
 * returns on counters done when DMA_CM_REMAINING reaches 0 and port B is empty. Called with every counter at 0 and the address
 * generator idle. */
__attribute__((noinline)) void ov_load(unsigned id) {
    if (ov_cur == id) return;
    uint32_t off = ((uint32_t)OVT[id][0] - STAGE_LMA) >> 2, pk = ((uint32_t)OVT[id][1] - (uint32_t)OVT[id][0]) >> 3;
    wait_tables();
    set_stream(STR_OV, STREAM_SRC_ADDR(stage_base() + off), STREAM_DEST_ENTRY(CM_WORD((uint32_t)_ov_base), 0), STREAM_COUNT_ENTRY(pk, MODE_CM, SIZE_64, 0));
    DMA_CM_REMAINING = pk;
    DMA_CONFIG = DMA_CFG_RUN(RUN_STREAMS, STR_OV, 1);
    TILE_PULSE = TILE_PULSE_START(1);
    wfi();
    ov_cur = id;
}

/* The split's barrier (split.c built with -DSPLIT_BARRIER=sort_barrier): every wake from a command reads slot 0. */
__attribute__((noinline)) void sort_barrier(void) { (void)wait_for_command(); }

/* The planner's region is dirty after the split (the shared region, sort.ld): zero the Tile once, in SPLIT's tail. */
void zero_planner(void) {
    volatile uint32_t *p = (volatile uint32_t *)&SK;
    const unsigned n = sizeof(Tile) / 4;          /* 1,338 words: not a multiple of 4 (M7 fix: the unrolled loop wrote
                                                     two words past the Tile, which S now follows) */
    unsigned i = 0;
    for (; i + 4 <= n; i += 4) { p[i] = 0; p[i + 1] = 0; p[i + 2] = 0; p[i + 3] = 0; }
    for (; i < n; i++) p[i] = 0;
}

int main(void) {
    for (;;) {
        uint32_t w = wait_for_command();           /* pulse done, WFI, then slot 0 (mesh.h v5) */
        unsigned arg = CMD_ARG(w);
        switch (CMD_TYPE(w)) {
        case CMD_L1: l1(arg); ov_load(OV_SPLIT); break;                              /* LOCAL; then SPLIT */
        case CMD_SPLIT:                                                              /* SPLIT; then XFER */
            S.shaped = arg;
            split_get_cuts(BUF_X, S.n_in, S.shaped, S.cut, S.n);
            zero_planner();                        /* the shared region: the split's state is dead from here */
            ov_load(OV_XFER);
            break;
        case CMD_XB: xb(); break;                                                    /* XFER */
        case CMD_RED: red(); if (S.plan_wanted) ov_load(OV_TH); break;               /* XFER; stage 2: then TH */
        /* skew handling's fixed sequence: a tile finishes a command that is empty for its path at once */
        case CMD_XC: if (S.plan_wanted) xchg(SK.inpos, SK.offo); break;              /* q sends p off[p][q] */
        case CMD_TH:                                                                 /* TH; BT: then BTPLAN */
            S.force_bt = arg & 1;
            if (S.plan_wanted) {
                skew_th();
                if (S.path == PATH_BT) ov_load(OV_BTPLAN);
                else if (S.path <= PATH_LB) { ov_load(OV_XFER); config_direct_lb(); }   /* XFER: K's configure, K and V */
            }
            break;
        case CMD_SWEEP: if (S.path == PATH_BT) bt_sweep(); break;                    /* BTPLAN */
        case CMD_DESC:
            if (S.path == PATH_TH) skew_desc();                                      /* TH */
            else if (S.path == PATH_BT) {                                            /* BTPLAN; then BTRUN */
                bt_descs();
                if (S.path == PATH_BT) { ov_load(OV_BTRUN); bt_first(); }
                else { ov_load(OV_XFER); config_direct_lb(); }   /* R3's exact cost lost to direct or LB */
            }
            break;
        case CMD_K: if (S.path <= PATH_LB) k_start(); break;                         /* XFER (always loaded on these paths) */
        case CMD_V: if (S.path <= PATH_LB) v_start(); break;
        case CMD_K1: case CMD_K2: case CMD_V1: case CMD_V2:
            if (S.path == PATH_TH) { if (arg == 0) th_phase(CMD_TYPE(w) - CMD_K1); }  /* TH: layer 0 of each */
            else if (S.path == PATH_BT) bt_phase(CMD_TYPE(w) - CMD_K1, arg);           /* BTRUN */
            break;
        case CMD_L2: ov_load(OV_LOCAL); l2(); break;                                 /* LOCAL */
        case CMD_STAGE:                            /* C&C stages the overlay images (6.2): its mode-2 packets are counted, */
            DMA_SRAM_REMAINING = arg;              /* so it says how many first (26n: every mode-2 packet counts, C&C's too) */
            if (arg) wfi();
            break;
        case CMD_EXIT: return 0;
        default: return 0x100 | CMD_TYPE(w);      /* unknown command: stop (simulator exit, a0 says why) */
        }
    }
}
