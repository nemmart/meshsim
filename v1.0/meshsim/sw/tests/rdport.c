/* The controller read port (spec 4.3): results land in issue order at SRAM_RD_DEST_ADDR + i, counting DMA_REPLY_REMAINING;
 * the free count; the local wake; and (tile 7 only) an overflow: with a local copy holding the SRAM read queue (the address
 * generator outranks the read port), the 65th store into the full queue is dropped. */
#include "mesh.h"
volatile uint32_t res[64], r[8];
int main(void) {
    unsigned me = tile_index();
    r[0] = SRAM_RD_ADDR_QUEUE;                                   /* 64 free */
    DMA_REPLY_REMAINING = 40;
    SRAM_RD_DEST_ADDR = CM_WORD((uint32_t)&res[0]);
    for (unsigned i = 0; i < 40; i++) SRAM_RD_ADDR_QUEUE = 0x1000 + (i * 37 & 0xFFF);
    r[1] = SRAM_RD_ADDR_QUEUE;                                   /* fewer than 64: reads still outstanding */
    wfi();
    r[2] = SRAM_RD_ADDR_QUEUE;                                   /* 64 again: every result has landed */
    r[3] = DMA_REPLY_REMAINING;
    uint32_t sum = 0;
    for (unsigned i = 0; i < 40; i++) sum += res[i];             /* read the results before reusing res[] (invariant 8) */
    r[5] = sum;
    if (me == 7) {
        set_stream(0, STREAM_LO(0x20000, 0), STREAM_HI(2048, 0, MODE_SRC, SIZE_64, 0));
        PTR[me] = 0x80000;
        DMA_CONFIG = DMA_CFG_RUN(RUN_STREAMS, 0, 1);
        DMA_SRAM_REMAINING = 2048;
        DMA_REPLY_REMAINING = 64;
        SRAM_RD_DEST_ADDR = CM_WORD((uint32_t)&res[0]);
        DMA_PULSE = DMA_PULSE_START(1);
        while (!(status() & TILE_ST_RD_RUNNING(1))) ;
        for (volatile int d = 0; d < 4; d++) ;                     /* the address generator now issues every cycle */
        for (unsigned i = 0; i < 65; i++) SRAM_RD_ADDR_QUEUE = 0x2000 + i;   /* the 65th is dropped */
        wfi();
        r[4] = conditions();                          /* M14: was status() (the sticky bits) */
    }
    return 0;
}
