/* Recipe: augmented transfers (mode 4; spec 4.9). Commands:
 *   1 (argument n, plus the tile index): the local form, the sort's first pass: n raw 32-bit keys at KEYS -> X as
 *     (key, j), j = 0, 1, 2, ...
 *   2 configure, 3 start: the remote form: each tile sends its n_of(me) records at SRC to both ring neighbours in mode 4,
 *     and receives its left neighbour's then its right neighbour's records into one buffer, R
 *   0 exit */
#include "rcp.h"
#define KEYS 0x00000           /* area 0: raw keys, one chunk each */
#define X    0x40000           /* area 2 */
#define SRC  0x20000           /* area 1: (key, anything) records */
#define R    0x80000           /* area 4 */
static uint32_t n_of(unsigned t) { return 20 + ((t * 3) & 15); }
static uint32_t expect;

/* RECIPE aug_local */
/* The sort's first pass (spec 4.9): n raw 32-bit keys at keys, one per packet (read size 32, the high half 0), sent to
 * the tile itself in mode 4. Each lands at dst as the record (key, j), where j = 0, 1, 2, ... is its position in dst. */
static void augment_local(uint32_t keys, uint32_t dst, uint32_t n) {
    wait_tables();
    PTR[tile_index()] = dst;
    DMA_RECORD_BASE_ADDRESS = dst;                    /* ordinal = (landing chunk - base) / 2 */
    set_stream(0, STREAM_LO(keys, 0), STREAM_HI(n, 0, MODE_AUG, SIZE_32, 0));
    local_run(DMA_CFG_RUN(RUN_STREAMS, 0, 1), n);
}
/* END */

/* RECIPE aug_remote */
/* Configure (one command): receive the left neighbour's n_left records and then the right neighbour's into buf, in
 * mode 4: every record's high word becomes its position in buf, whichever sender it came from. Send this tile's n
 * records at src to both neighbours (two streams, d = 1 and d = 63); the start command loads the run. */
static void aug_configure(uint32_t src, uint32_t n, uint32_t buf, uint32_t n_left, uint32_t n_right) {
    unsigned me = tile_index();
    wait_tables();
    PTR[(me - 1) & 63] = buf;
    PTR[(me + 1) & 63] = buf + 2 * n_left;
    DMA_RECORD_BASE_ADDRESS = buf;
    set_stream(0, STREAM_LO(src, 0), STREAM_HI(n, 1, MODE_AUG, SIZE_64, 0));
    set_stream(1, STREAM_LO(src, 0), STREAM_HI(n, 63, MODE_AUG, SIZE_64, 0));
    DMA_CONFIG = DMA_CFG_RUN(RUN_STREAMS, 0, 2);
    expect = n_left + n_right;
}
/* Start (the next command, straight after waking: the same code on every tile up to the start store, spec 6.3). */
static void aug_start(void) {
    DMA_SRAM_REMAINING = expect;                       /* the counter first, */
    start_run();                                   /* then the start */
    wfi();                                         /* both neighbours' records are in */
}
/* END */

int main(void) {
    for (;;) {
        uint32_t w = wait_for_command();
        if (CMD_TYPE(w) == 3) { aug_start(); continue; }   /* first: the start path (6.3) */
        unsigned me = tile_index();
        switch (CMD_TYPE(w)) {
        case 1: augment_local(KEYS, X, CMD_ARG(w) + me); break;
        case 2: aug_configure(SRC, n_of(me), R, n_of((me - 1) & 63), n_of((me + 1) & 63)); break;
        default: return 0;
        }
    }
}
