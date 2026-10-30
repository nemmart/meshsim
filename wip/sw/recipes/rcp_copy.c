/* Recipe 2: a stream run, a local copy (spec 4.2; D6 mode 1, D17). */
#include "rcp.h"
#define SRC 0x00000            /* area 0 (chunks) */
#define DST 0x40000            /* area 2 */

/* RECIPE copy */
/* Copy n 64-bit records from src to dst in this tile's big SRAM (chunk addresses, both even: a 64-bit access is
 * 8-byte aligned, D26): one stream, mode 1 to the tile itself (d = 0), which lands at PTR[me]. src and dst must not
 * overlap (one engine run never reads and writes one buffer, spec 4.1). Called with the tables free (D17): the last
 * thing that used them was a local_run(), which waited for DMA run done. */
static void local_copy(uint32_t src, uint32_t dst, uint32_t n) {
    tables->PTR[tile_index()] = dst;
    set_stream(0, STREAM_SRC_W(src, 0), 0, STREAM_COUNT_W(n, SIZE_64, CTR_SRAM, MODE_SRC, 0));
    local_run(DMA_CFG_RUN(RUN_STREAMS, 0, 1), n);
}
/* END */

int main(void) {
    for (;;) {
        uint32_t w = wait_for_command();
        if (CMD_TYPE(w) != 1) sim_exit(0);
        local_copy(SRC, DST, CMD_ARG(w));
    }
}
