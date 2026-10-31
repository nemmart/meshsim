/* Recipe: an all-to-all on the 16-group schedule (spec 4.2, 6.3; schedule-4.md). Each tile's send buffer at SRC holds
 * cnt[d] records for each destination d, sorted by destination; cnt[d] = (me * 7 + d * 3) & 7. Commands:
 *   1 counts: the one-word exchange of the counts (inc[s] = records from s), then this tile's group maxima
 *   2 configure: R by a max-reduction of the group maxima, then STREAM, GROUP, PTR and DMA_CONFIG
 *   3 start: the counter, the start, WFI for the arrivals
 *   0 exit */
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
 * order. The tables stay as they are until the run ends; the start command loads the run. */
static void a2a_configure(void) {
    unsigned me = tile_index();
    uint32_t first[64], f = 0;
    for (int d = 0; d < 64; d++) { first[d] = f; f += cnt[d]; }
    wait_tables();
    for (int g = 0; g < 16; g++) {
        for (int i = 0; i < 4; i++) {
            unsigned off = SCHED[g][i], d = (me + off) & 63;
            set_stream(4 * g + i, STREAM_SRC_ADDR(SRC + 2 * first[d]), STREAM_DEST_ENTRY(0, off), STREAM_COUNT_ENTRY(cnt[d], MODE_SRC, SIZE_64, 0));
        }
        set_group(g, GROUP_LO_R(R[g]), GROUP_HI(4 * g, 4, 0));     /* gap 0 (spec 4.2) */
    }
    uint32_t at = INBOX;
    expected = 0;
    for (int s = 0; s < 64; s++) { PTR[s] = at; at += 2 * inc[s]; expected += inc[s]; }
    DMA_CONFIG = DMA_CFG_RUN(RUN_GROUPS, 0, 16);
}
/* Start (the next command): from waking to the start store, the same code on every tile (spec 6.3). */
static void a2a_start(void) {
    DMA_SRAM_REMAINING = expected;                     /* the counter first, */
    start_run();                                   /* then the start; */
    if (expected) wfi();                           /* a count of 0 makes no edge */
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
        default: return 0;
        }
    }
}
