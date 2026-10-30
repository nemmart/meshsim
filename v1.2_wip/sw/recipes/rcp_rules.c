/* Recipe 15: the rules that bite. Each RECIPE region is the right form; the wrong form is described in the book. */
#include "rcp.h"
#define A  0x00000             /* area 0 */
#define B  0x40000             /* area 2 */
#define C  0x80000             /* area 4 */
volatile uint32_t rbox;
static uint32_t expect;

/* RECIPE rule_wait */
/* A phase that arms a counter waits for it before done (CONVENTIONS 5): the wait proves every arrival is in. Done
 * with an arrival outstanding is the done-early error (D18 [8]), which traps (D20). */
static void self_reply(void) {
    unsigned me = tile_index();
    tile->WRITE_COUNTER[CTR_REPLY - 1] = 1;
    send_to(ROUTER_HDR_W(me, MODE_WRITE, CTR_REPLY) | ROUTER_NOINC, REPLY_WORD(me));
    send32(7);
    tile->ROUTER_HDR &= ~ROUTER_NOINC;              /* the bit stays set until cleared (D37.5) */
    wait_all(EV_CTR(CTR_REPLY));                    /* the reply is in */
    rbox = REPLY[2 * me];
}
/* END */

/* RECIPE rule_disjoint */
/* One engine run never reads and writes the same buffer (spec 4.1: no coherence between a run's reads and its
 * writes). To shift records within a buffer, copy out and back: two runs, each between disjoint buffers. */
static void shift_up(uint32_t buf, uint32_t tmp, uint32_t n, uint32_t by) {
    tables->PTR[tile_index()] = tmp;
    set_stream(0, STREAM_SRC_W(buf, 0), 0, STREAM_COUNT_W(n, SIZE_64, CTR_SRAM, MODE_SRC, 0));
    local_run(DMA_CFG_RUN(RUN_STREAMS, 0, 1), n);       /* buf -> tmp */
    tables->PTR[tile_index()] = buf + 2 * by;
    set_stream(0, STREAM_SRC_W(tmp, 0), 0, STREAM_COUNT_W(n, SIZE_64, CTR_SRAM, MODE_SRC, 0));
    local_run(DMA_CFG_RUN(RUN_STREAMS, 0, 1), n);       /* tmp -> buf + by records */
}
/* END */

/* RECIPE rule_start */
/* From the WFI that GO ends to the start store, every tile runs the same instructions (spec 6.3): no data-dependent
 * branch or loop, no stall (a send store to a full holding register, a multi-cycle wait). Counter values may differ: a
 * store costs one cycle whatever its value. Anything else belongs in the configure command. */
static void ring_start(void) {
    tile->WRITE_COUNTER[CTR_SRAM - 1] = expect;
    start_run();
    wait_all(EV_CTR(CTR_SRAM) | EV_RUN_DONE);
}
/* END */

/* RECIPE rule_align */
/* Every 64- or 128-bit access is naturally aligned, checked in hardware (D26): a stream's source and every element
 * it reads, every PTR its packets land through, every packet address. Records are 64-bit at even chunks; a group may
 * still mix read sizes (here 64-bit records to this tile and 32-bit words to the right-hand neighbour). */
static void two_streams_one_group(uint32_t n, uint32_t n_left) {
    unsigned me = tile_index();
    tables->PTR[me] = C; tables->PTR[(me - 1) & 63] = C + 0x8000;
    set_stream(0, STREAM_SRC_W(A, 0), 0, STREAM_COUNT_W(n, SIZE_64, CTR_SRAM, MODE_SRC, 0));
    set_stream(1, STREAM_SRC_W(B + 3, 0), 0, STREAM_COUNT_W(n, SIZE_32, CTR_SRAM, MODE_SRC, 1));   /* 32-bit: any chunk */
    set_group(0, GROUP_LO_R(n > n_left ? n : n_left), GROUP_HI(0, 2, 0));
    local_run(DMA_CFG_RUN(RUN_GROUPS, 0, 1), n + n_left);
}
/* END */

int main(void) {
    unsigned me = tile_index();
    for (;;) {
        uint32_t w = wait_for_command();
        switch (CMD_TYPE(w)) {
        case 4: ring_start(); break;
        case 1: self_reply(); break;
        case 2: shift_up(A, B, 64, 8); break;
        case 3:
            tables->PTR[(me - 1) & 63] = C;
            set_stream(0, STREAM_SRC_W(A, 0), 0, STREAM_COUNT_W(20 + (me & 7), SIZE_64, CTR_SRAM, MODE_SRC, 1));
            tile->DMA_CONFIG = DMA_CFG_RUN(RUN_STREAMS, 0, 1);
            expect = 20 + ((me - 1) & 7);
            break;
        case 5: two_streams_one_group(32, 32); break;
        default: sim_exit(0);
        }
    }
}
