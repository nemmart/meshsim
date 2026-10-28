/* Recipe: a stream run, as a local copy (spec 4.2, 4.9, 4.12). Command 1 copies n records from SRC to DST and back
 * from DST to DST2 (two runs, the tables re-written between them); n = the command's argument + the tile index. */
#include "rcp.h"
#define SRC  0x00000           /* area 0 */
#define DST  0x40000           /* area 2 */
#define DST2 0x80000           /* area 4 */

/* RECIPE copy */
/* Copy n 64-bit records from src to dst in this tile's big SRAM: one stream, mode 2 to the tile itself (d = 0), which
 * lands at PTR[me]. src and dst must not overlap (one engine run never reads and writes one buffer, spec 4.1). */
static void local_copy(uint32_t src, uint32_t dst, uint32_t n) {
    wait_tables();
    PTR[tile_index()] = dst;
    set_stream(0, STREAM_LO(src, 0), STREAM_HI(n, 0, MODE_SRC, SIZE_64, 0));
    local_run(DMA_CFG_RUN(RUN_STREAMS, 0, 1), n);
}
/* END */

int main(void) {
    for (;;) {
        uint32_t w = wait_for_command();
        if (CMD_TYPE(w) != 1) return 0;
        uint32_t n = CMD_ARG(w) + tile_index();
        local_copy(SRC, DST, n);
        local_copy(DST, DST2, n);
    }
}
