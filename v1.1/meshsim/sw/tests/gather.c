/* Gather 1 (spec 4.4; the walk-through's L1 gather) as a one-command phase: the sorted (key, ordinal) index at 0x60000
 * (area 3), values of k = 2 records at DMA_RECORD_BASE_ADDRESS = 0x20000 (areas 1-2), output at PTR[me] = 0xA0000 (areas 5-6).
 *   CMD 1 (argument: n values): set up, start, WFI for the local wake
 *   CMD 2 exit */
#include "mesh.h"
int main(void) {
    unsigned me = tile_index();
    for (;;) {
        uint32_t w = wait_for_command();
        if (CMD_TYPE(w) != 1) return 0;
        uint32_t n = CMD_ARG(w);
        DMA_RECORD_BASE_ADDRESS = 0x20000;
        PTR[me] = 0xA0000;
        set_stream(195, STREAM_SRC_ADDR(0x60000), STREAM_DEST_ENTRY(0, 0), STREAM_COUNT_ENTRY(n, MODE_GATHER, SIZE_64, 0));
        DMA_CONFIG = DMA_CFG_RUN(RUN_STREAMS, 195, 1) | DMA_CFG_KM1(2 - 1);
        DMA_SRAM_REMAINING = 2 * n;
        TILE_PULSE = TILE_PULSE_START(1);
        wfi();
    }
}
