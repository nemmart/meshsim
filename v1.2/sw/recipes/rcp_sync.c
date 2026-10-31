/* Recipe 13: a synchronised start by identical code (spec 6.3, 6.4). */
#include "rcp.h"
#define SRC    0x00000         /* area 0 */
#define PRM    0x20000         /* area 1: { n, m }, 4-chunk aligned */
#define DST    0x40000         /* area 2 */
struct par { uint32_t n, m, _pad[2]; };
static struct par par __attribute__((aligned(16)));     /* lands as one 128-bit packet (D26) */
static uint32_t m;

int main(void) {
    unsigned me = tile_index();
    for (;;) {
        /* RECIPE sync */
        uint32_t w = wait_for_command();          /* every tile leaves WFI on GO's cycle (+ its 0/+1 skew) */
        if (CMD_TYPE(w) == 2) {                    /* the start: identical code from WFI on every tile */
            tile->WRITE_COUNTER[CTR_SRAM - 1] = m; /* the counter first, */
            start_run();                           /* then the start store */
            wait_all(m ? EV_CTR(CTR_SRAM) | EV_RUN_DONE : EV_RUN_DONE);
            continue;
        }
        if (CMD_TYPE(w) != 1) sim_exit(0);
        /* the set-up: any length, per tile; it ends in wait_for_command() */
        spin(2 * me);
        set_stream(1, STREAM_SRC_W(PRM, 0), (uint32_t)&par, STREAM_COUNT_W(1, SIZE_128, CTR_CM, MODE_WRITE, 0));
        tile->WRITE_COUNTER[CTR_CM - 1] = 1;
        tile->DMA_CONFIG = DMA_CFG_RUN(RUN_STREAMS, 1, 1);
        start_run();
        wait_all(EV_CTR(CTR_CM) | EV_RUN_DONE);    /* the parameters are in; the tables are free */
        tables->PTR[(me - 1) & 63] = DST;
        set_stream(0, STREAM_SRC_W(SRC, 0), 0, STREAM_COUNT_W(par.n, SIZE_64, CTR_SRAM, MODE_SRC, 1));
        tile->DMA_CONFIG = DMA_CFG_RUN(RUN_STREAMS, 0, 1);
        m = par.m;
        /* END */
    }
}
