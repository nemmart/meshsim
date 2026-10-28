/* Block copy into controller memory (spec 6.5, v1.1): 1,024 words (256 128-bit packets: a row-aligned source at read
 * size 128) from big SRAM chunk 0x1000 into instruction memory at 0x0800 by a mode-5 stream run to the tile itself,
 * waiting with WFI for the local wake; then call the copied code. Spec 6.5's sequence (CONVENTIONS.md section 8 still
 * gives v1.0's 64-bit form until the software session updates it). */
#include "mesh.h"
#define NWORDS 1024
#define DST 0x0800
volatile uint32_t res;
int main(void) {
    set_stream(0, STREAM_SRC_ADDR(0x1000), STREAM_DEST_ENTRY(CM_WORD(DST), 0), STREAM_COUNT_ENTRY(NWORDS / 4, MODE_CM, SIZE_128, 0));
    DMA_CM_REMAINING = NWORDS / 4;
    DMA_CONFIG = DMA_CFG_RUN(RUN_STREAMS, 0, 1);
    TILE_PULSE = TILE_PULSE_START(1);
    wfi();
    res = ((uint32_t (*)(uint32_t))DST)(tile_index());
    return 0;
}
