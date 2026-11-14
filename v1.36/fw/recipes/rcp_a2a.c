/* Recipe 9: an all-to-all on the 16-group schedule (spec 4.2, 6.3; schedule-4.md). cnt[d] = (me * 7 + d * 3) & 7.
 *   1 counts: the one-word exchange of the counts (inc[s] = records from s), then this tile's group maxima
 *   2 configure: R by a max-reduction of the group maxima, then STREAM, GROUP, PTR and DMA_CONFIG
 *   3 start: the counter, the start, the wait */
#define RCP_XCHG
#define RCP_REDUCE
#include "rcp.h"
#define SRC   0x00000          /* area 0: the send buffer */
#define INBOX 0x40000          /* area 2: the receive buffer, one run per source in source order */
static uint32_t cnt[64];
volatile uint32_t inc[64];
static uint32_t gmax[RW], R[RW], expected;

/* RECIPE a2a */
/* Configure (after the counts exchange, inc[s] = records from tile s, and the reduction of the group maxima, R[g] =
 * the largest count any tile sends in group g): stream 4g + i sends this tile's run for offset SCHED[g][i]; group g
 * is a round-robin over its four streams for R[g] rotations; the receive pointers put each source's run in source
 * order. The tables stay as they are until the run ends; the start command starts the run. */
static void a2a_configure(void) {
    unsigned me = tile_index();
    uint32_t first[64], f = 0;
    for (int d = 0; d < 64; d++) { first[d] = f; f += cnt[d]; }
    for (int g = 0; g < 16; g++) {
        for (int i = 0; i < 4; i++) {
            unsigned off = SCHED[g][i], d = (me + off) & 63;
            set_stream(4 * g + i, STREAM_SRC_W(SRC + 2 * first[d], 0), 0, STREAM_COUNT_W(cnt[d], SIZE_64, CTR_SRAM, MODE_SRC, off));
        }
        set_group(g, GROUP_LO_R(R[g]), GROUP_HI(4 * g, 4, 0));     /* gap 0 (spec 4.2) */
    }
    uint32_t at = INBOX;
    expected = 0;
    for (int s = 0; s < 64; s++) { tables->PTR[s] = at; at += 2 * inc[s]; expected += inc[s]; }
    tile->DMA_CONFIG = DMA_CFG_RUN(RUN_GROUPS, 0, 16);
}
/* Start (the next command): from waking to the start store, the same code on every tile (spec 6.3). */
static void a2a_start(void) {
    tile->WRITE_COUNTER[CTR_SRAM - 1] = expected;   /* the counter first (a store of 0 does nothing), */
    start_run();                                    /* then the start; */
    wait_all(expected ? EV_CTR(CTR_SRAM) | EV_RUN_DONE : EV_RUN_DONE);   /* an unarmed counter never fires */
}
/* END */

int main(void) {
    unsigned me = tile_index();
    reduce_init();
    for (int d = 0; d < 64; d++) cnt[d] = (me * 7 + d * 3) & 7;
    for (;;) {
        uint32_t w = wait_for_command();
        switch (CMD_TYPE(w)) {
        case 3: a2a_start(); break;                /* first: the shortest path to the start store */
        case 1:
            /* RECIPE a2a_R */
            xchg(cnt, inc);                        /* first in the command: inc[s] = records from tile s */
            for (int g = 0; g < 16; g++) {         /* this tile's largest count in each group, for R */
                uint32_t m = 0;
                for (int i = 0; i < 4; i++) { uint32_t c = cnt[(me + SCHED[g][i]) & 63]; if (c > m) m = c; }
                gmax[g] = m;
            }
            break;
        case 2:
            reduce_max(gmax, R);                   /* first in the command: R[g], the largest over all tiles */
            a2a_configure();
            break;
            /* END */
        default: sim_exit(0);
        }
    }
}
