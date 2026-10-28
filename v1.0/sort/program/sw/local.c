/* local.c -- the local sorts (worker 6, P1): L1 = augmenting (or copy) pass + 4 bucket passes; L2 = 4 bucket passes.
 * Every pass is a one-stream loopback run (spec 4.2) with count-on-arrival of the next digit (4.9), waited for by the
 * local wake (4.12). Stable LSD radix on bytes 1..4 (1 = least significant, 4.16); in-order loopback keeps ties in
 * arrival order. For k = 2 each ends with a gather (4.4): the sorted index's ordinals fetch the 128-bit values. */
#include "sort.h"

/* One local run of one stream: DMA_CONFIG, the counter, then the start store (6.5's order), then WFI for
 * the local wake (DMA_SRAM_REMAINING = 0 and mem_idle). n > 0, or the wake never comes. */
static void run_pass(unsigned stream, uint32_t cfg, uint32_t n) {
    DMA_CONFIG = cfg | DMA_CFG_RUN(RUN_STREAMS, stream, 1);
    DMA_SRAM_REMAINING = n;
    DMA_PULSE = DMA_PULSE_START(1);
    wfi();
}

/* PTR[t] = base + 2 * (records with digit < t), and HIST[t] = 0 for the next pass's counts. */
static void prefix_to_ptr(uint32_t base) {
    volatile uint32_t *c = HIST, *p = PTR;
    for (int t = 0; t < 256; t += 4) {
        uint32_t c0 = c[t], c1 = c[t + 1], c2 = c[t + 2], c3 = c[t + 3];
        p[t] = base;     base += c0 << 1;
        p[t + 1] = base; base += c1 << 1;
        p[t + 2] = base; base += c2 << 1;
        p[t + 3] = base; base += c3 << 1;
        c[t] = 0; c[t + 1] = 0; c[t + 2] = 0; c[t + 3] = 0;
    }
}

/* A gather (4.4): the index of n (key, ordinal) records at idx, values of k = 2 records (128 bits) at vals, out to
 * BUF_VS in index order. One mode-1 request per record; DMA_SRAM_REMAINING counts the 2n value packets. */
__attribute__((noinline)) static void gather(unsigned stream, uint32_t idx, uint32_t vals, uint32_t n) {
    wait_tables();
    set_stream(stream, STREAM_LO(idx, 0), STREAM_HI(n, 0, MODE_GATHER, SIZE_64, 0));
    DMA_RECORD_BASE_ADDRESS = vals;
    PTR[tile_index()] = BUF_VS;
    run_pass(stream, DMA_CFG_KM1(2 - 1), n << 1);
}

static void clear_hist(void) {
    volatile uint32_t *c = HIST;
    for (int t = 0; t < 256; t += 4) { c[t] = 0; c[t + 1] = 0; c[t + 2] = 0; c[t + 3] = 0; }
}

/* C&C's staging pointer (spec 6.2, 4.9 / I-10): mode-3 passes overwrite PTR[64]. */
void restore_stage_ptr(void) {
    wait_tables();
    PTR[64] = stage_base();
}

/* Four bucket passes over n records, ping-pong between BUF_X and BUF_Y, starting from 'from' (STR_FROM_X or STR_FROM_Y).
 * HIST must hold digit 1's counts on entry; each pass counts the next digit; HIST is clear on exit. */
static void bucket_passes(unsigned from, uint32_t n) {
    for (unsigned d = 1; d <= 4; d++) {
        unsigned to_y = (from == STR_FROM_X);
        wait_tables();
        prefix_to_ptr(to_y ? BUF_Y : BUF_X);
        run_pass(from, DMA_CFG_BUCKETDIG(d) | DMA_CFG_HISTDIG(d < 4 ? d + 1 : 0), n);
        from = to_y ? STR_FROM_Y : STR_FROM_X;
    }
}

static void set_bucket_streams(uint32_t n) {
    set_stream(STR_FROM_X, STREAM_LO(BUF_X, 0), STREAM_HI(n, 0, MODE_BUCKET, SIZE_64, 0));
    set_stream(STR_FROM_Y, STREAM_LO(BUF_Y, 0), STREAM_HI(n, 0, MODE_BUCKET, SIZE_64, 0));
}

/* L1 (walk-through 5.1; 6 for 32-bit values): the input into BUF_X with digit 1 counted on arrival, then sort 1, then
 * (k = 2) gather 1: the values, read from BUF_VIN by the ordinals j, into BUF_VS in sorted-key order.
 * k = 2: raw keys, one per packet (read size 32), in mode 4 (augmented): BUF_X gets (key, j), j = 0, 1, 2, ...
 * 32-bit values: (key, value) records copied as they are (read size 64), in mode 2.
 * Ends with sorted records in BUF_X (four passes X->Y->X->Y->X) and HIST clear. */
void l1(uint32_t fmt) {
    uint32_t n = S.n_in, me = tile_index();
    S.fmt = fmt;
    wait_tables();
    if (n) {
        unsigned k128 = (fmt == FMT_K128);
        clear_hist();
        set_stream(STR_IN, STREAM_LO(BUF_IN, 0), STREAM_HI(n, 0, KEY_MODE(k128), k128 ? SIZE_32 : SIZE_64, 0));
        set_bucket_streams(n);
        PTR[me] = BUF_X;
        DMA_RECORD_BASE_ADDRESS = BUF_X;
        run_pass(STR_IN, DMA_CFG_HISTDIG(1), n);
        bucket_passes(STR_FROM_X, n);
        if (k128) gather(STR_G1, BUF_X, BUF_VIN, n);          /* gather 1: values into sorted-key order */
    }
    restore_stage_ptr();
}

/* L2 (walk-through 5.7): sort 2 of the n_recv records received in BUF_Y, whose digit 1 the keys all-to-all counted
 * on arrival. Ends with the final keys in BUF_Y (Y->X->Y->X->Y) and HIST clear; for k = 2, gather 2 then fetches the
 * received values from BUF_VR by the ordinals K's augmenting stamped and writes the final values to BUF_VS. */
void l2(void) {
    uint32_t n = S.n_recv;
    wait_tables();
    if (n) {
        set_bucket_streams(n);
        bucket_passes(STR_FROM_Y, n);
        if (S.fmt == FMT_K128) gather(STR_G2, BUF_Y, BUF_VR, n);   /* gather 2: the final values */
    }
    restore_stage_ptr();
}
