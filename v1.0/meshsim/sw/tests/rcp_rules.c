/* Recipe: the rules that bite (CONVENTIONS 4, 5; spec 4.1, 4.2, 4.9, 6.3). Command type = rule, argument 0 = the right
 * form (the RECIPE text), 1 = the wrong form (below it, for the test); the test checks what goes wrong.
 *   1 the end-of-phase WFI          2 disjoint buffers per run       3 identical code from WFI to the start store
 *   4 one read size per group       5 PTR[64] restored after mode 3 (C&C's staging, below the sort scratch)
 *   6 the start of rule 3 (start command)                            0 exit */
#include "rcp.h"
#define A  0x00000             /* area 0 */
#define B  0x40000             /* area 2 */
#define C  0x80000             /* area 4 */
volatile uint32_t rbox;
static uint32_t expect, delay;

/* RECIPE rule_wfi */
/* A phase that sets a counter ends with a WFI before done (CONVENTIONS 5): the WFI proves every arrival is in. Done
 * with an arrival outstanding is a program error: the done-early condition (spec 4.13). The done gate keeps the late
 * arrival from waking wait_for_command(), but the phase has declared itself finished too soon. */
static void self_reply(void) {
    DMA_REPLY_REMAINING = 1;
    ROUTER_SET_HDR = ROUTER_HDR(tile_index(), MODE_REPLY, CM_WORD((uint32_t)&rbox));
    send32(7);
    wfi();                                         /* the local wake: the reply is in */
}
/* END */
static void self_reply_wrong(void) {
    DMA_REPLY_REMAINING = 1;
    ROUTER_SET_HDR = ROUTER_HDR(tile_index(), MODE_REPLY, CM_WORD((uint32_t)&rbox));
    send32(7);                                     /* WRONG: no WFI; done follows with the reply still on its way */
}

/* RECIPE rule_disjoint */
/* One engine run never reads and writes the same buffer (spec 4.1: no coherence between a run's reads and its
 * writes). To shift records within a buffer, copy out and back: two runs, each between disjoint buffers. */
static void shift_up(uint32_t buf, uint32_t tmp, uint32_t n, uint32_t by) {
    wait_tables(); PTR[tile_index()] = tmp;
    set_stream(0, STREAM_LO(buf, 0), STREAM_HI(n, 0, MODE_SRC, SIZE_64, 0));
    local_run(DMA_CFG_RUN(RUN_STREAMS, 0, 1), n);       /* buf -> tmp */
    wait_tables(); PTR[tile_index()] = buf + 2 * by;
    set_stream(0, STREAM_LO(tmp, 0), STREAM_HI(n, 0, MODE_SRC, SIZE_64, 0));
    local_run(DMA_CFG_RUN(RUN_STREAMS, 0, 1), n);       /* tmp -> buf + by records */
}
/* END */
static void shift_up_wrong(uint32_t buf, uint32_t n, uint32_t by) {
    wait_tables(); PTR[tile_index()] = buf + 2 * by;
    set_stream(0, STREAM_LO(buf, 0), STREAM_HI(n, 0, MODE_SRC, SIZE_64, 0));
    local_run(DMA_CFG_RUN(RUN_STREAMS, 0, 1), n);       /* WRONG: reads buf while writing it */
}

/* RECIPE rule_start */
/* From the WFI that GO ends to the start store, every tile runs the same instructions (spec 6.3): no data-dependent
 * branch or loop, no stall (a ROUTER_SEND_LO to a full holding register, a multi-cycle wait). Counter values may differ: a
 * store costs one cycle whatever its value. Anything else belongs in the configure command. */
static void ring_start(void) {
    DMA_SRAM_REMAINING = expect;
    start_run();
    wfi();
}
/* END */
static void ring_start_wrong(void) {
    DMA_SRAM_REMAINING = expect;
    spin(delay);                                   /* WRONG: a per-tile delay before the start store */
    start_run();
    wfi();
}
static uint32_t n_of(unsigned t) { return 20 + (t & 7); }
static void ring_configure(void) {                 /* this tile's n_of(me) records at A to me + 1, into C there */
    unsigned me = tile_index();
    wait_tables();
    PTR[(me - 1) & 63] = C;
    set_stream(0, STREAM_LO(A, 0), STREAM_HI(n_of(me), 1, MODE_SRC, SIZE_64, 0));
    DMA_CONFIG = DMA_CFG_RUN(RUN_STREAMS, 0, 1);
    expect = n_of((me - 1) & 63);
    delay = me & 7;
}

/* RECIPE rule_size */
/* All the streams of one group have the same read size (spec 4.2): it sets the group's period, one visit per cycle,
 * or two for 128 bits. A 128-bit stream and a 64-bit one belong in different groups. */
static void two_streams_one_group(uint32_t n) {
    wait_tables();
    PTR[tile_index()] = C;
    set_stream(0, STREAM_LO(A, 0), STREAM_HI(n, 0, MODE_SRC, SIZE_64, 0));
    set_stream(1, STREAM_LO(B, 0), STREAM_HI(n, 0, MODE_SRC, SIZE_64, 0));    /* the same size as stream 0 */
    set_group(0, GROUP_LO_R(n), GROUP_HI(0, 2, 0));
    local_run(DMA_CFG_RUN(RUN_GROUPS, 0, 1), 2 * n);
}
/* END */
static void two_streams_one_group_wrong(uint32_t n) {
    wait_tables();
    PTR[tile_index()] = C;
    set_stream(0, STREAM_LO(A, 0), STREAM_HI(n, 0, MODE_SRC, SIZE_64, 0));
    set_stream(1, STREAM_LO(B, 0), STREAM_HI(n, 0, MODE_SRC, SIZE_32, 0));    /* WRONG: another size */
    set_group(0, GROUP_LO_R(n), GROUP_HI(0, 2, 0));
    local_run(DMA_CFG_RUN(RUN_GROUPS, 0, 1), 2 * n);
}

/* one mode-3 pass of n records A -> B on digit 1, counted by a copy B -> C first (as rcp_bucket.c) */
static void one_bucket_pass(uint32_t n) {
    unsigned me = tile_index();
    wait_tables();
    for (int t = 0; t < 256; t++) HIST[t] = 0;
    PTR[me] = C;
    set_stream(0, STREAM_LO(A, 0), STREAM_HI(n, 0, MODE_SRC, SIZE_64, 0));
    local_run(DMA_CFG_RUN(RUN_STREAMS, 0, 1) | DMA_CFG_HISTDIG(1), n);
    wait_tables();
    uint32_t at = B;
    for (int t = 0; t < 256; t++) { uint32_t c = HIST[t]; PTR[t] = at; at += c << 1; }
    set_stream(0, STREAM_LO(A, 0), STREAM_HI(n, 0, MODE_BUCKET, SIZE_64, 0));
    local_run(DMA_CFG_RUN(RUN_STREAMS, 0, 1) | DMA_CFG_BUCKETDIG(1), n);
}
/* RECIPE rule_stage */
/* C&C stages data (mode 2 from src 64) at PTR[64], which the loader points at this tile's staging area: the 64 KB
 * below the top 128 KB of sort scratch (stage_base(), spec 6.2). A program keeps its buffers out of both, and a phase
 * with mode 3 uses all 256 PTR entries, PTR[64] among them: restore it before the phase ends. */
static void bucket_phase(uint32_t n) {
    one_bucket_pass(n);
    wait_tables();
    PTR[64] = stage_base();
}
/* END */

int main(void) {
    for (;;) {
        uint32_t w = wait_for_command();
        unsigned wrong = CMD_ARG(w) & 1, me = tile_index();
        switch (CMD_TYPE(w)) {
        case 6: if (wrong) ring_start_wrong(); else ring_start(); break;
        case 1: if (wrong) self_reply_wrong(); else self_reply(); break;
        case 2: if (wrong) shift_up_wrong(A, 40, 8); else shift_up(A, B, 40, 8); break;
        case 3: ring_configure(); break;
        case 4: if (wrong) two_streams_one_group_wrong(16 + me); else two_streams_one_group(16 + me); break;
        case 5: if (wrong) one_bucket_pass(30 + me); else bucket_phase(30 + me); break;
        default: return 0;
        }
    }
}
