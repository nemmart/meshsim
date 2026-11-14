/* Recipe: a block copy into controller memory (spec 6.5, 4.11; CONVENTIONS 8). Command 1 copies NPAR parameter words
 * into data memory and an NOVL-word overlay into instruction memory at OVL, then calls the overlay with params[0]. */
#include "rcp.h"
#define SRC_PAR 0x20000        /* big SRAM, area 1: the parameters */
#define SRC_OVL 0x21000        /* ... and the overlay image */
#define NPAR 16
#define NOVL 256
#define OVL  0x1800            /* instruction memory: above this program's code, below the loader */
static uint32_t params[NPAR];                 /* mode 6 writes 1, 2 or 4 consecutive words at any word address */
volatile uint32_t res, par_sum;

/* RECIPE cmem */
/* Copy n 64-bit packets (2n words) from big SRAM at src to controller memory at the byte address dst, instruction or
 * data memory: a mode-6 stream to the tile itself (spec 6.5). The code doing the copy must lie outside dst's range. */
static void cmem_copy(uint32_t src, uint32_t dst, uint32_t n) {
    wait_tables();
    set_stream(0, STREAM_SRC_ADDR(src), STREAM_DEST_ENTRY(CM_WORD(dst), 0), STREAM_COUNT_ENTRY(n, MODE_CM, SIZE_64, 0));
    DMA_CONFIG = DMA_CFG_RUN(RUN_STREAMS, 0, 1);
    DMA_CM_REMAINING = n;                              /* mode 6 counts here: 1 per packet, whatever its size */
    TILE_PULSE = TILE_PULSE_START(1);
    wfi();                                         /* wakes when the last word is in memory */
}
/* END */

int main(void) {
    for (;;) {
        uint32_t w = wait_for_command();
        if (CMD_TYPE(w) != 1) return 0;
        /* RECIPE cmem_use */
        cmem_copy(SRC_PAR, (uint32_t)params, NPAR / 2);    /* parameters into data memory */
        cmem_copy(SRC_OVL, OVL, NOVL / 2);                  /* an overlay into instruction memory */
        uint32_t (*overlay)(uint32_t) = (uint32_t (*)(uint32_t))OVL;
        res = overlay(params[0]);
        /* END */
        uint32_t s = 0;
        for (int i = 0; i < NPAR; i++) s += params[i];
        par_sum = s;
    }
}
