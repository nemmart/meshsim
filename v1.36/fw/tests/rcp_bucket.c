/* Recipe: the histogram digit, the prefix sum and bucket passes (spec 4.9, 4.10, 4.15). Command 1 (argument n, plus the
 * tile index): n (key, value) records at IN are copied to X counting digit 1 on arrival, then four bucket passes
 * X -> Y -> X -> Y -> X sort them by key (stable LSD radix), and PTR[64] is restored for C&C's staging. */
#include "rcp.h"
#define IN 0x00000             /* area 0 */
#define X  0x40000             /* area 2 */
#define Y  0x80000             /* area 4 */

/* RECIPE histogram */
/* Count-on-arrival (spec 4.9): a run with DMA_CFG_HISTDIG(d) adds 1 to HIST[digit d of the key] for every record it writes.
 * HIST is not cleared by hardware: write zeros first. Here, a local copy that counts digit 1 as it goes. */
static void copy_counting(uint32_t src, uint32_t dst, uint32_t n) {
    wait_tables();
    for (int t = 0; t < 256; t++) HIST[t] = 0;
    PTR[tile_index()] = dst;
    set_stream(0, STREAM_SRC_ADDR(src), STREAM_DEST_ENTRY(0, 0), STREAM_COUNT_ENTRY(n, MODE_SRC, SIZE_64, 0));
    local_run(DMA_CFG_RUN(RUN_STREAMS, 0, 1) | DMA_CFG_HISTDIG(1), n);
}
/* END */

/* RECIPE bucket */
/* One stable radix pass on digit d (1-4; 1 is the key's least significant byte, spec 4.15): n records from src into
 * dst, bucketed by mode 4 (each lands at PTR[its digit]). HIST holds digit d's counts on entry; the pass counts digit
 * next (0: none) for the pass after. */
static void bucket_pass(uint32_t src, uint32_t dst, uint32_t n, unsigned d, unsigned next) {
    wait_tables();
    uint32_t at = dst;
    for (int t = 0; t < 256; t++) {                /* the prefix sum: PTR[t] = dst + 2 * (records with digit < t) */
        uint32_t c = HIST[t];
        PTR[t] = at;
        at += c << 1;
        HIST[t] = 0;                               /* ready for this pass's counts of digit next */
    }
    set_stream(0, STREAM_SRC_ADDR(src), STREAM_DEST_ENTRY(0, 0), STREAM_COUNT_ENTRY(n, MODE_BUCKET, SIZE_64, 0));
    local_run(DMA_CFG_RUN(RUN_STREAMS, 0, 1) | DMA_CFG_BUCKETDIG(d) | DMA_CFG_HISTDIG(next), n);
}
/* END */

int main(void) {
    for (;;) {
        uint32_t w = wait_for_command();
        if (CMD_TYPE(w) != 1) return 0;
        uint32_t n = CMD_ARG(w) + tile_index();
        /* RECIPE radix_sort */
        copy_counting(IN, X, n);                   /* X, with digit 1 counted */
        bucket_pass(X, Y, n, 1, 2);
        bucket_pass(Y, X, n, 2, 3);
        bucket_pass(X, Y, n, 3, 4);
        bucket_pass(Y, X, n, 4, 0);                /* sorted by the 32-bit key, in X */
        wait_tables();
        PTR[64] = stage_base();                    /* mode 4 overwrote PTR[64], C&C's staging pointer (spec 4.9) */
        /* END */
    }
}
