/* Recipe 6: a gather (spec 4.4; D6 mode 4). */
#include "rcp.h"
#define IDX  0x00000
#define VALS 0x40000
#define OUT  0x80000

/* RECIPE gather */
/* For each of the n (key, ordinal) records of the index at idx, fetch value[ordinal], k records, from vals (value j is
 * at vals + 2k j), and write the values in index order from out. One mode-4 request per index record goes to the tile
 * itself; its counter field (D6) is the counter the values count on: the GATHER engine sends the k value packets as
 * mode 1 to PTR[me], counted on counter 1. The requests themselves never count: n * k in all. */
static void gather(uint32_t idx, uint32_t vals, uint32_t out, uint32_t n, unsigned k) {
    tile->DMA_RECORD_BASE_ADDRESS = vals;
    tables->PTR[tile_index()] = out;
    set_stream(0, STREAM_SRC_W(idx, 0), 0, STREAM_COUNT_W(n, SIZE_64, CTR_SRAM, MODE_GATHER, 0));
    local_run(DMA_CFG_RUN(RUN_STREAMS, 0, 1) | DMA_CFG_KM1(k - 1), n * k);
}
/* END */

int main(void) {
    for (;;) {
        uint32_t w = wait_for_command();
        if (CMD_TYPE(w) != 1) sim_exit(0);
        gather(IDX, VALS, OUT, 50 + (tile_index() & 63), CMD_ARG(w));
    }
}
