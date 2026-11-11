/* Block copy into controller memory (spec 6.5, v1.2): 1,024 words (256 128-bit packets: a row-aligned source at read
 * size 128) from big SRAM chunk 0x1000 into program memory at 0x01004000 by a mode-0 stream run to the tile itself on
 * counter 2, waiting with WFI for counters done; then call the copied code. */
#include "mesh.h"
#define NWORDS 1024
#define DST (REGION_PMEM + 0x4000)
volatile uint32_t res;
int main(void) {
    set_stream(0, STREAM_SRC_W(0x1000, 0), DST, STREAM_COUNT_W(NWORDS / 4, SIZE_128, CTR_CM, MODE_WRITE, 0));
    tile->WRITE_COUNTER[CTR_CM - 1] = NWORDS / 4;
    tile->DMA_CONFIG = DMA_CFG_RUN(RUN_STREAMS, 0, 1);
    start_run();
    wfi();
    res = ((uint32_t (*)(uint32_t))DST)(tile_index());
    return 0;
}
