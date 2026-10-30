/* The controller read port (spec 4.3, 8.8; v1.2): the setup register SRAM_RD_DEST_ADDR (a data-memory byte offset, the
 * size, the counter); 40 32-bit results land at destination + 4 i (each carries its own word, so the order they land in
 * doesn't matter), counting on counter 2; 8 64-bit and 4 128-bit reads; the free count; counters done; tile 6 only: a
 * misaligned 128-bit read is not performed, its result lands as 0 and ERRORS [1] is set; tile 7 only: an overflow: with
 * a local copy holding the SRAM read queue (the address generator outranks the read port), the 65th store into the full
 * queue is dropped (ERRORS [7]). */
#include "mesh.h"
volatile uint32_t res[64], wide[32] __attribute__((aligned(16))), r[8];
int main(void) {
    unsigned me = tile_index();
    r[0] = tile->SRAM_RD_ADDR_QUEUE;                             /* 64 free */
    tile->WRITE_COUNTER[CTR_CM - 1] = 40 + 8 + 4;
    tile->SRAM_RD_DEST_ADDR = RD_DEST((uint32_t)&res[0], SIZE_32, CTR_CM);
    for (unsigned i = 0; i < 40; i++) tile->SRAM_RD_ADDR_QUEUE = 0x1000 + (i * 37 & 0xFFF);
    tile->SRAM_RD_DEST_ADDR = RD_DEST((uint32_t)&wide[0], SIZE_64, CTR_CM);
    for (unsigned i = 0; i < 8; i++) tile->SRAM_RD_ADDR_QUEUE = 0x1100 + 2 * i;       /* wide[0..15] */
    tile->SRAM_RD_DEST_ADDR = RD_DEST((uint32_t)&wide[16], SIZE_128, CTR_CM);
    for (unsigned i = 0; i < 4; i++) tile->SRAM_RD_ADDR_QUEUE = 0x1200 + 4 * i;       /* wide[16..31] */
    r[1] = tile->SRAM_RD_ADDR_QUEUE;                             /* fewer than 64: reads still outstanding */
    r[6] = tile->SRAM_RD_DEST_ADDR;                              /* the destination, advanced by 4 x 16 bytes */
    wfi();
    r[2] = tile->SRAM_RD_ADDR_QUEUE;                             /* 64 again: every result has landed */
    r[3] = tile->WRITE_COUNTER[CTR_CM - 1];
    uint32_t sum = 0;
    for (unsigned i = 0; i < 40; i++) sum += res[i];             /* read the results before reusing res[] (invariant 8) */
    r[5] = sum;
    if (me == 6) {                                               /* a 128-bit read at chunk 2: misaligned, not performed (4.3) */
        wide[16] = 0x5A5A5A5A;
        tile->WRITE_COUNTER[CTR_CM - 1] = 1;
        tile->SRAM_RD_DEST_ADDR = RD_DEST((uint32_t)&wide[16], SIZE_128, CTR_CM);
        tile->SRAM_RD_ADDR_QUEUE = 0x1202;
        wfi();
        r[4] = tile->ERRORS;
        r[7] = wide[16];                                         /* 0: the result's payload */
    }
    if (me == 7) {
        set_stream(0, STREAM_SRC_W(0x20000, 0), 0, STREAM_COUNT_W(2048, SIZE_64, CTR_SRAM, MODE_SRC, 0));
        tables->PTR[me] = 0x80000;
        tile->DMA_CONFIG = DMA_CFG_RUN(RUN_STREAMS, 0, 1);
        tile->WRITE_COUNTER[CTR_SRAM - 1] = 2048;
        tile->WRITE_COUNTER[CTR_CM - 1] = 64;
        tile->SRAM_RD_DEST_ADDR = RD_DEST((uint32_t)&res[0], SIZE_32, CTR_CM);
        start_run();
        while (!(tile->STATUS & ST_RD_RUNNING)) ;
        for (volatile int d = 0; d < 4; d++) ;                     /* the address generator now issues every cycle */
        for (unsigned i = 0; i < 65; i++) tile->SRAM_RD_ADDR_QUEUE = 0x2000 + i;   /* the 65th is dropped */
        wfi();
        r[4] = tile->ERRORS;
    }
    return 0;
}
