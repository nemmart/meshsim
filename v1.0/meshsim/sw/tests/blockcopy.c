/* Block copy into controller memory (spec 6.5): 1,024 words (512 packets) from big SRAM chunk 0x1000 into instruction
 * memory at 0x0800 by a mode-5 stream run to the tile itself, waiting with WFI for the local wake; then call the copied
 * code. The recipe exactly as CONVENTIONS.md section 8 gives it. */
#include "mesh.h"
#define NWORDS 1024
#define DST 0x0800
volatile uint32_t res;
int main(void) {
    set_stream(0, STREAM_LO(0x1000, CM_WORD(DST)), STREAM_HI(NWORDS / 2, 0, MODE_CMEM, SIZE_64, 0));
    DMA_CM_REMAINING = NWORDS / 2;
    DMA_CONFIG = DMA_CFG_RUN(RUN_STREAMS, 0, 1);
    DMA_PULSE = DMA_PULSE_START(1);
    wfi();
    res = ((uint32_t (*)(uint32_t))DST)(tile_index());
    return 0;
}
