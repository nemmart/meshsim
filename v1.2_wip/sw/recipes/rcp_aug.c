/* Recipe 5: augmented transfers (spec 4.9; D6 mode 3). */
#include "rcp.h"
#define KEYS 0x00000           /* area 0: raw keys, one chunk each */
#define X    0x40000           /* area 2 */
#define SRC  0x20000           /* area 1: (key, anything) records */
#define BUF  0x80000           /* area 4 */
static uint32_t n_of(unsigned t) { return 20 + ((t * 3) & 15); }
static uint32_t expect;

/* RECIPE aug_local */
/* The sort's first pass (spec 4.9): n raw 32-bit keys at keys, one per packet (read size 32, the high half 0), sent to
 * the tile itself in mode 3. Each lands at dst as the 64-bit record (key, j), where j = 0, 1, 2, ... is its position
 * in dst. PTR[me] and the base are record-aligned (even chunks, D32). */
static void augment_local(uint32_t keys, uint32_t dst, uint32_t n) {
    tables->PTR[tile_index()] = dst;
    tile->DMA_RECORD_BASE_ADDRESS = dst;           /* ordinal = (landing chunk - base) / 2 */
    set_stream(0, STREAM_SRC_W(keys, 0), 0, STREAM_COUNT_W(n, SIZE_32, CTR_SRAM, MODE_AUGMENT, 0));
    local_run(DMA_CFG_RUN(RUN_STREAMS, 0, 1), n);
}
/* END */

/* RECIPE aug_remote */
/* Configure (one command): receive the left neighbour's n_left records and then the right neighbour's into buf, in
 * mode 3: every record's high word becomes its position in buf, whichever sender it came from. Send this tile's n
 * records at src to both neighbours (two streams, d = 1 and d = 63); the start command starts the run. */
static void aug_configure(uint32_t src, uint32_t n, uint32_t buf, uint32_t n_left, uint32_t n_right) {
    unsigned me = tile_index();
    tables->PTR[(me - 1) & 63] = buf;
    tables->PTR[(me + 1) & 63] = buf + 2 * n_left;
    tile->DMA_RECORD_BASE_ADDRESS = buf;
    set_stream(0, STREAM_SRC_W(src, 0), 0, STREAM_COUNT_W(n, SIZE_64, CTR_SRAM, MODE_AUGMENT, 1));
    set_stream(1, STREAM_SRC_W(src, 0), 0, STREAM_COUNT_W(n, SIZE_64, CTR_SRAM, MODE_AUGMENT, 63));
    tile->DMA_CONFIG = DMA_CFG_RUN(RUN_STREAMS, 0, 2);
    expect = n_left + n_right;
}
/* Start (the next command, straight after waking: the same code on every tile up to the start store, spec 6.3). */
static void aug_start(void) {
    tile->WRITE_COUNTER[CTR_SRAM - 1] = expect;     /* the counter first, */
    start_run();                                    /* then the start */
    wait_all(EV_CTR(CTR_SRAM) | EV_RUN_DONE);       /* both neighbours' records are in; this tile's run is over */
}
/* END */

int main(void) {
    unsigned me = tile_index();
    for (;;) {
        uint32_t w = wait_for_command();
        switch (CMD_TYPE(w)) {
        case 3: aug_start(); break;                /* first: the shortest path to the start store */
        case 1: augment_local(KEYS, X, 64); break;
        case 2: aug_configure(SRC, n_of(me), BUF, n_of((me - 1) & 63), n_of((me + 1) & 63)); break;
        default: sim_exit(0);
        }
    }
}
