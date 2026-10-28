/* The controller read port (spec 4.3, 8.8): the setup register SRAM_RD_DEST_ADDR (destination word, size); 40 32-bit
 * results land at destination + i (each carries its own word, so the order they land in doesn't matter), counting
 * DMA_CM_REMAINING; 8 64-bit and 4 128-bit reads; the free count; counters done; tile 6 only: a misaligned 128-bit read is
 * dropped with the read-port error; tile 7 only: an overflow: with a local copy holding the SRAM read queue (the address
 * generator outranks the read port), the 65th store into the full queue is dropped. */
#include "mesh.h"
volatile uint32_t res[64], wide[32], r[8];
int main(void) {
    unsigned me = tile_index();
    r[0] = SRAM_RD_ADDR_QUEUE;                                   /* 64 free */
    DMA_CM_REMAINING = 40 + 8 + 4;
    SRAM_RD_DEST_ADDR = SRAM_RD_DEST(CM_WORD((uint32_t)&res[0]), SRAM_RD_SIZE_32);
    for (unsigned i = 0; i < 40; i++) SRAM_RD_ADDR_QUEUE = 0x1000 + (i * 37 & 0xFFF);
    SRAM_RD_DEST_ADDR = SRAM_RD_DEST(CM_WORD((uint32_t)&wide[0]), SRAM_RD_SIZE_64);
    for (unsigned i = 0; i < 8; i++) SRAM_RD_ADDR_QUEUE = 0x1100 + 2 * i;       /* wide[0..15] */
    SRAM_RD_DEST_ADDR = SRAM_RD_DEST(CM_WORD((uint32_t)&wide[16]), SRAM_RD_SIZE_128);
    for (unsigned i = 0; i < 4; i++) SRAM_RD_ADDR_QUEUE = 0x1200 + 4 * i;       /* wide[16..31] */
    r[1] = SRAM_RD_ADDR_QUEUE;                                   /* fewer than 64: reads still outstanding */
    wfi();
    r[2] = SRAM_RD_ADDR_QUEUE;                                   /* 64 again: every result has landed */
    r[3] = DMA_CM_REMAINING;
    uint32_t sum = 0;
    for (unsigned i = 0; i < 40; i++) sum += res[i];             /* read the results before reusing res[] (invariant 8) */
    r[5] = sum;
    if (me == 6) {                                               /* a 128-bit read at chunk 2: misaligned, dropped (4.3) */
        SRAM_RD_DEST_ADDR = SRAM_RD_DEST(CM_WORD((uint32_t)&wide[16]), SRAM_RD_SIZE_128);
        SRAM_RD_ADDR_QUEUE = 0x1202;
        for (int i = 0; i < 4; i++) (void)status();
        r[4] = conditions();
    }
    if (me == 7) {
        set_stream(0, STREAM_SRC_ADDR(0x20000), STREAM_DEST_ENTRY(0, 0), STREAM_COUNT_ENTRY(2048, MODE_SRC, SIZE_64, 0));
        PTR[me] = 0x80000;
        DMA_CONFIG = DMA_CFG_RUN(RUN_STREAMS, 0, 1);
        DMA_SRAM_REMAINING = 2048;
        DMA_CM_REMAINING = 64;
        SRAM_RD_DEST_ADDR = SRAM_RD_DEST(CM_WORD((uint32_t)&res[0]), SRAM_RD_SIZE_32);
        TILE_PULSE = TILE_PULSE_START(1);
        while (!(status() & TILE_ST_RD_RUNNING(1))) ;
        for (volatile int d = 0; d < 4; d++) ;                     /* the address generator now issues every cycle */
        for (unsigned i = 0; i < 65; i++) SRAM_RD_ADDR_QUEUE = 0x2000 + i;   /* the 65th is dropped */
        wfi();
        r[4] = conditions();
    }
    return 0;
}
