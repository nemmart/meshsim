/* Recipe: a gather (spec 4.4). Command 1 (argument k, 1 or 2): n = 50 + the tile index (key, ordinal) index records at
 * IDX fetch their values (k records each) from VALS into OUT, in index order. */
#include "rcp.h"
#define VALS 0x20000           /* area 1 */
#define IDX  0x60000           /* area 3 */
#define OUT  0xA0000           /* area 5 */

/* RECIPE gather */
/* For each of the n (key, ordinal) records of the index at idx, fetch value[ordinal], k records, from vals (value j is
 * at vals + 2k j), and write the values in index order from out. One mode-1 request per index record goes to the tile
 * itself; the GATHER engine sends the k value packets as mode 2 to PTR[me]. Only the values count: n * k. */
static void gather(uint32_t idx, uint32_t vals, uint32_t out, uint32_t n, unsigned k) {
    wait_tables();
    DMA_RECORD_BASE_ADDRESS = vals;
    PTR[tile_index()] = out;
    set_stream(0, STREAM_SRC_ADDR(idx), STREAM_DEST_ENTRY(0, 0), STREAM_COUNT_ENTRY(n, MODE_GATHER, SIZE_64, 0));
    local_run(DMA_CFG_RUN(RUN_STREAMS, 0, 1) | DMA_CFG_KM1(k - 1), n * k);
}
/* END */

int main(void) {
    for (;;) {
        uint32_t w = wait_for_command();
        if (CMD_TYPE(w) != 1) return 0;
        gather(IDX, VALS, OUT, 50 + tile_index(), CMD_ARG(w));
    }
}
