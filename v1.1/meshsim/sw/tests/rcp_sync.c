/* Recipe: a synchronised start by identical code (spec 6.3). Command 1 (set-up): each tile copies its parameters
 * (par.n records to send, par.m to receive) from big SRAM and configures a stream run that sends its par.n records at
 * SRC to its right-hand neighbour (landing at DST there); set-up lengths differ (a spin of 2 * me iterations stands for
 * per-tile work). Command 2 (start): every tile leaves WFI on GO's cycle and runs the same instructions to its start
 * store, so all runs start on the same cycle (+ each tile's 0/+1 skew). Command 0: exit. */
#include "rcp.h"
#define SRC    0x00000         /* area 0 */
#define PARAMS 0x20000         /* area 1: { n, m } */
#define DST    0x40000         /* area 2 */
struct par { uint32_t n, m; };
static struct par par;

int main(void) {
    unsigned me = tile_index();
    uint32_t m = 0;
    for (;;) {
        /* RECIPE sync */
        uint32_t w = wait_for_command();          /* every tile leaves WFI on GO's cycle (+ its 0/+1 skew) */
        if (CMD_TYPE(w) == 2) {                    /* the start: identical code from WFI on every tile */
            DMA_SRAM_REMAINING = m;                /* the counter first, */
            start_run();                           /* then the start store */
            if (m) wfi();                          /* the arrivals (a count of 0 makes no edge) */
            continue;
        }
        if (CMD_TYPE(w) != 1) return 0;
        /* the set-up: any length, per tile; it ends in wait_for_command() */
        spin(2 * me);
        wait_tables();
        set_stream(1, STREAM_SRC_ADDR(PARAMS), STREAM_DEST_ENTRY(CM_WORD((uint32_t)&par), 0), STREAM_COUNT_ENTRY(1, MODE_CM, SIZE_64, 0));
        DMA_CM_REMAINING = 1;
        DMA_CONFIG = DMA_CFG_RUN(RUN_STREAMS, 1, 1);
        start_run();
        wfi();                                     /* the parameters are in */
        wait_tables();
        PTR[(me - 1) & 63] = DST;
        set_stream(0, STREAM_SRC_ADDR(SRC), STREAM_DEST_ENTRY(0, 1), STREAM_COUNT_ENTRY(par.n, MODE_SRC, SIZE_64, 0));
        DMA_CONFIG = DMA_CFG_RUN(RUN_STREAMS, 0, 1);
        m = par.m;
        /* END */
    }
}
