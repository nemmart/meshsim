/* rcp.h -- helpers shared by the recipe programs (sw/tests/rcp_*.c; RECIPES.md). The text between a RECIPE marker and
 * END is quoted in RECIPES.md, and RecipeTest checks the two agree. */
#ifndef RCP_H
#define RCP_H
#include "mesh.h"

/* wait_tables() is in mesh.h from v9 (M14; it was recipe 0 here). */

/* RECIPE local_run */
/* A local run into big SRAM (spec 4.2, 4.12): DMA_CONFIG, the counter, then the start store; WFI wakes
 * when the n packets have been written (DMA_SRAM_REMAINING = 0 and mem_idle). n > 0: a count of 0 makes no edge. */
static inline void local_run(uint32_t config, uint32_t n) {
    DMA_CONFIG = config;
    DMA_SRAM_REMAINING = n;
    TILE_PULSE = TILE_PULSE_START(1);
    wfi();
}
/* END */

/* the 16-group schedule (design_v2/spec/schedule-4.md): group g's four offsets; every group is a permutation */
static const uint8_t SCHED[16][4] __attribute__((unused)) = { { 1, 29, 35, 63 }, { 2, 30, 34, 62 }, { 3, 31, 33, 61 }, { 5, 23, 41, 59 },
    { 6, 22, 42, 58 }, { 7, 21, 43, 57 }, { 8, 28, 36, 56 }, { 9, 27, 37, 55 }, { 10, 26, 38, 54 }, { 11, 25, 39, 53 },
    { 12, 24, 40, 52 }, { 13, 15, 49, 51 }, { 14, 18, 46, 50 }, { 16, 20, 44, 48 }, { 17, 19, 45, 47 }, { 0, 4, 32, 60 } };

#ifdef RCP_XCHG
/* RECIPE xchg */
/* The one-word exchange (spec 4.11, 4.12): out[d] goes to tile d as a 32-bit reply, which lands by source in d's reply
 * block, so afterwards in[s] holds tile s's word to this tile (copied out of REPLY[2 s]). One reply per destination,
 * from the send port, one destination per step in the 16-group schedule's order, so every step is a permutation of
 * the tiles. Call it first in a command, by identical code on every tile: DMA_REPLY_REMAINING must be written (it adds;
 * it is 0 here) before any reply can arrive. A tile sends at most one reply to each receiver until it has been read. */
static void xchg(const uint32_t *out, volatile uint32_t *in) {
    unsigned me = tile_index();
    DMA_REPLY_REMAINING = 64;                          /* including this tile's word to itself */
    for (int g = 0; g < 16; g++)
        for (int i = 0; i < 4; i++) {
            unsigned d = (me + SCHED[g][i]) & 63;
            ROUTER_SET_HDR = ROUTER_HDR(d, MODE_REPLY, 0);   /* addr unused: a reply lands at word 2 src */
            send32(out[d]);
        }
    wfi();                                         /* every word to this tile is in */
    for (int s = 0; s < 64; s++) in[s] = REPLY[2 * s];
}
/* END */
#endif

#ifdef RCP_REDUCE
/* RECIPE reduce */
/* Max-reduce a vector of RW words (RW even) over all 64 tiles; every tile gets the result. A fixed tree: along each row
 * toward column 0 (me + 1 is a child), up column 0 toward tile 0 (me + 8 is a child), then tile 0 broadcasts. The
 * vectors travel as mode-6 packets (controller-memory writes) into the parent's data memory. reduce_init() once at start-up; reduce_max() first
 * in a command (a leaf sends at once, so a parent's DMA_CM_REMAINING must already be stored). */
#define RW 16
static uint32_t red_in[2][RW];                 /* from me + 1, from me + 8 */
static uint32_t red_out[RW];                   /* tile 0's broadcast */
static uint32_t red_kids, red_parent;
static void reduce_init(void) {
    unsigned me = tile_index(), col = me & 7, row = me >> 3;
    red_kids = (col < 7) + (col == 0 && row < 7);
    red_parent = col ? me - 1 : row ? me - 8 : DST_BROADCAST;
}
static void reduce_max(uint32_t *v, uint32_t *out) {
    unsigned me = tile_index();
    if (red_kids) {
        DMA_CM_REMAINING = red_kids * (RW / 2);        /* RW / 2 packets per child */
        wfi();                                     /* the children's vectors are in */
        for (int i = 0; i < RW; i++) {
            if ((me & 7) < 7 && red_in[0][i] > v[i]) v[i] = red_in[0][i];
            if ((me & 7) == 0 && me < 56 && red_in[1][i] > v[i]) v[i] = red_in[1][i];
        }
    }
    uint32_t *at = (me & 7) ? red_in[0] : me ? red_in[1] : red_out;
    if (me == 0) DMA_CM_REMAINING = RW / 2;            /* tile 0 receives its own broadcast */
    ROUTER_SET_HDR = ROUTER_HDR(red_parent, MODE_CM, CM_WORD((uint32_t)at));
    for (int i = 0; i < RW; i += 2) send64(v[i + 1], v[i]);
    if (me != 0) DMA_CM_REMAINING = RW / 2;            /* the broadcast can't come before this: it needs this vector */
    wfi();                                         /* the broadcast is in */
    for (int i = 0; i < RW; i++) out[i] = red_out[i];
}
/* END */
#endif

static inline void spin(int n) { for (volatile int i = 0; i < n; i++) ; }
/* A delay of exactly 4 n cycles on the controller (n >= 1): each iteration is addi, nop and a taken bnez (2 cycles with
 * its branch penalty); the last, not taken, is 3 + the call's own. The core's timing is deterministic (4.11). */
static inline void delay4(uint32_t n) {
    __asm__ volatile ("1:\n\taddi %0, %0, -1\n\tnop\n\tbnez %0, 1b" : "+r"(n) :: "memory");
}

#ifdef RCP_REPORT
/* RECIPE report */
/* REPORT (spec 6.6): C&C's debugging command. When its error bit is set, C&C stops issuing GOs, waits until every tile
 * is in wait_for_command() with its conditions intact, then broadcasts CMD_REPORT. Each tile answers with one packet to
 * C&C: its tile index and TILE_CONDITIONS. A delay loop of (me + 1) x 4 cycles paces the answers, one every 4 cycles in
 * tile order, so nothing fans in: the core's timing is deterministic and every tile reaches the loop by identical code.
 * Call it first in the command; paced_to_cnc() also serves other results. */
static void paced_to_cnc(uint32_t hi, uint32_t lo) {
    delay4(tile_index() + 1);                      /* (me + 1) x 4 cycles: the same code on every tile */
    ROUTER_SET_HDR = ROUTER_HDR(DST_CNC, MODE_REPLY, 0);         /* C&C's port ignores the mode and the address */
    send64(hi, lo);
}
static void report(void) { paced_to_cnc(tile_index(), conditions()); }
/* END */
#endif
#endif
