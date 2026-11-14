/* Recipe 3: a block copy into controller memory (spec 6.5; D5, D6, D26). */
#include "rcp.h"
#define SRC_PAR 0x20000        /* big SRAM, area 1: the parameters (chunks; 4-chunk aligned for 128-bit reads) */
#define SRC_OVL 0x21000        /* ... and the overlay image */
#define NPAR 16
#define NOVL 256
#define OVL  (REGION_PMEM + 0x6000)    /* program memory: above this program's code (proposed layout) */
static uint32_t params[NPAR] __attribute__((aligned(16)));   /* 128-bit writes land 16-byte aligned (D26) */
volatile uint32_t res, par_sum;

/* RECIPE cmem */
/* Copy n 128-bit packets (4n words) from big SRAM at chunk src to controller memory at the address dst, data memory
 * (region 0x00) or program memory (0x01): a mode-0 stream to the tile itself, advancing STREAM_DEST by 16 bytes per
 * packet (spec 6.5). src is 4-chunk aligned and dst 16-byte aligned (D26). Counted on counter 2. The code doing the
 * copy must lie outside dst's range. */
static void cmem_copy(uint32_t src, uint32_t dst, uint32_t n) {
    set_stream(0, STREAM_SRC_W(src, 0), dst, STREAM_COUNT_W(n, SIZE_128, CTR_CM, MODE_WRITE, 0));
    tile->DMA_CONFIG = DMA_CFG_RUN(RUN_STREAMS, 0, 1);
    tile->WRITE_COUNTER[CTR_CM - 1] = n;            /* 1 per packet, whatever its size */
    start_run();
    wait_all(EV_CTR(CTR_CM) | EV_RUN_DONE);         /* the last word is in memory, and the tables are free */
}
/* END */

int main(void) {
    for (;;) {
        uint32_t w = wait_for_command();
        if (CMD_TYPE(w) != 1) sim_exit(0);
        /* RECIPE cmem_use */
        cmem_copy(SRC_PAR, (uint32_t)params, NPAR / 4);     /* parameters into data memory */
        cmem_copy(SRC_OVL, OVL, NOVL / 4);                  /* an overlay into program memory */
        uint32_t (*overlay)(uint32_t) = (uint32_t (*)(uint32_t))OVL;
        res = overlay(params[0]);
        /* END */
    }
}
