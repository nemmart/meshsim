/* rcp.h (v1.2 draft) -- helpers shared by the recipe programs. The text between a RECIPE marker and its END is
 * quoted verbatim in sw/RECIPES.md (WORKER6/check/check_book.py compares them). Compiled against the draft header,
 * not run: there is no v1.2 simulator yet. */
#ifndef RCP_H
#define RCP_H
#include "../mesh-v1.2-draft.h"

/* RECIPE local_run */
/* A local run of the address generator into big SRAM (spec 4.2; D3, D17): DMA_CONFIG, arm counter 1 with the n packets
 * the run writes to this tile, start, then wait for both the counter and DMA run done (EVENTS [10]): the run's writes
 * have landed and its last table read is complete, so the tables are free. n = 0 (a run that writes nothing here)
 * waits for the run alone: an unarmed counter never fires. */
static inline void local_run(uint32_t config, uint32_t n) {
    tile->DMA_CONFIG = config;
    tile->WRITE_COUNTER[CTR_SRAM - 1] = n;          /* a store adds; the count is 0 here */
    start_run();                                    /* PULSE_DMA: also clears EVENTS [10] */
    wait_all(n ? EV_CTR(CTR_SRAM) | EV_RUN_DONE : EV_RUN_DONE);
}
/* END */

/* the 16-group schedule (schedule-4.md): group g's four offsets; every group is a permutation */
static const uint8_t SCHED[16][4] __attribute__((unused)) = { { 1, 29, 35, 63 }, { 2, 30, 34, 62 }, { 3, 31, 33, 61 }, { 5, 23, 41, 59 },
    { 6, 22, 42, 58 }, { 7, 21, 43, 57 }, { 8, 28, 36, 56 }, { 9, 27, 37, 55 }, { 10, 26, 38, 54 }, { 11, 25, 39, 53 },
    { 12, 24, 40, 52 }, { 13, 15, 49, 51 }, { 14, 18, 46, 50 }, { 16, 20, 44, 48 }, { 17, 19, 45, 47 }, { 0, 4, 32, 60 } };

#ifdef RCP_XCHG
/* RECIPE xchg */
/* The one-word exchange (D6, D7): out[d] goes to tile d as a 32-bit mode-0 write to REPLY_WORD(me), the same
 * address in every receiver, so ROUTER_ADDR is set once with no-increment and each send changes only ROUTER_HDR's
 * dst. Afterwards in[s] holds tile s's word to this tile, copied from REPLY[2 s]. Counted on counter 3 (replies),
 * which every receiver arms first in the command, by identical code: the fastest sender's word can arrive a few
 * cycles after GO. A tile keeps at most one reply outstanding to each receiver until it has been read. The
 * no-increment bit stays set until the program clears it (D37.5), so the exchange clears it when it is done. */
static void xchg(const uint32_t *out, volatile uint32_t *in) {
    unsigned me = tile_index();
    tile->WRITE_COUNTER[CTR_REPLY - 1] = 64;        /* including this tile's word to itself */
    tile->ROUTER_ADDR = REPLY_WORD(me);
    for (int g = 0; g < 16; g++)
        for (int i = 0; i < 4; i++) {
            unsigned d = (me + SCHED[g][i]) & 63;
            tile->ROUTER_HDR = ROUTER_HDR_W(d, MODE_WRITE, CTR_REPLY) | ROUTER_NOINC;
            send32(out[d]);
        }
    tile->ROUTER_HDR &= ~ROUTER_NOINC;              /* done with the fixed address: clear it (D37.5) */
    wait_all(EV_CTR(CTR_REPLY));                    /* every word to this tile is in */
    for (int s = 0; s < 64; s++) in[s] = REPLY[2 * s];
}
/* END */
#endif

#ifdef RCP_REDUCE
/* RECIPE reduce */
/* Max-reduce a vector of RW words (RW even) over all 64 tiles; every tile gets the result. A fixed tree: along each row
 * toward column 0 (me + 1 is a child), up column 0 toward tile 0 (me + 8 is a child), then tile 0 broadcasts. The
 * vectors travel as 64-bit mode-0 writes into the parent's data memory, counted on counter 2, so every landing buffer
 * is 8-byte aligned (D26). reduce_init() once at start-up; reduce_max() first in a command (a leaf sends at once, so a
 * parent's counter must already be armed). */
#define RW 16
static uint32_t red_in[2][RW] __attribute__((aligned(8)));     /* from me + 1, from me + 8 */
static uint32_t red_out[RW] __attribute__((aligned(8)));       /* tile 0's broadcast */
static uint32_t red_kids, red_parent;
static void reduce_init(void) {
    unsigned me = tile_index(), col = me & 7, row = me >> 3;
    red_kids = (col < 7) + (col == 0 && row < 7);
    red_parent = col ? me - 1 : row ? me - 8 : DST_BROADCAST;
}
static void reduce_max(uint32_t *v, uint32_t *out) {
    unsigned me = tile_index();
    if (red_kids) {
        tile->WRITE_COUNTER[CTR_CM - 1] = red_kids * (RW / 2);    /* RW / 2 packets per child */
        wait_all(EV_CTR(CTR_CM));                                  /* the children's vectors are in */
        for (int i = 0; i < RW; i++) {
            if ((me & 7) < 7 && red_in[0][i] > v[i]) v[i] = red_in[0][i];
            if ((me & 7) == 0 && me < 56 && red_in[1][i] > v[i]) v[i] = red_in[1][i];
        }
    }
    uint32_t *at = (me & 7) ? red_in[0] : me ? red_in[1] : red_out;
    if (me == 0) tile->WRITE_COUNTER[CTR_CM - 1] = RW / 2;        /* tile 0 receives its own broadcast */
    send_to(ROUTER_HDR_W(red_parent, MODE_WRITE, CTR_CM), (uint32_t)at);
    for (int i = 0; i < RW; i += 2) send64(v[i + 1], v[i]);
    if (me != 0) tile->WRITE_COUNTER[CTR_CM - 1] = RW / 2;        /* the broadcast can't come before this: it needs this vector */
    wait_all(EV_CTR(CTR_CM));                                      /* the broadcast is in */
    for (int i = 0; i < RW; i++) out[i] = red_out[i];
}
/* END */
#endif

static inline void spin(int n) { for (volatile int i = 0; i < n; i++) ; }
/* A delay of exactly 4 n cycles on the controller (n >= 1), as v1.1: addi, nop and a taken bnez per iteration.
 * The core's timing is deterministic (4.11). */
static inline void delay4(uint32_t n) {
    __asm__ volatile ("1:\n\taddi %0, %0, -1\n\tnop\n\tbnez %0, 1b" : "+r"(n) :: "memory");
}

#ifdef RCP_RESULT
/* RECIPE request */
/* A request on slot k (0-2): the slot owns counter CTR_REQ(k) and its own reply buffer, named in the request (D25,
 * D38, D40), so up to three requests can be outstanding at once (C&C 2 allows three; C&C 1 and C&C 3 one each) and no
 * reply overwrites another's (F15). The slot's counter is armed before the request goes, for everything it brings
 * back: its reply if it has one, and its data. A request stays outstanding, and its slot busy, until all of that has
 * landed (D44, a software convention): a read, which has no reply, until its data has. w0 names the target, the op
 * and CTR_REQ(k). */
static volatile uint32_t req_reply[REQ_SLOTS][2] __attribute__((aligned(8)));
static void request_issue(unsigned k, uint32_t w0, uint32_t w2, uint32_t w3, uint32_t data_packets, int reply) {
    tile->WRITE_COUNTER[CTR_REQ(k) - 1] = data_packets + (reply ? 1 : 0);   /* nothing can come back sooner */
    request_send(w3, w2, (uint32_t)&req_reply[k][0], w0);      /* W1: the reply address (D40) */
}
static uint32_t request_wait(unsigned k) {
    wait_all(EV_CTR(CTR_REQ(k)));                   /* reply and data landed: no longer outstanding, the slot is free */
    return req_reply[k][0];
}
/* END */

/* RECIPE result_to_cc1 */
/* A result for C&C 1 (the self-test's signature): one request on slot 0. The request bus has back-pressure and never
 * drops anything, so nothing needs pacing (v1.1 paced replies to C&C through REPORT's delay loop). One request
 * outstanding to C&C 1 (D38): the tile waits for C&C 1's reply before anything else goes to it. */
static uint32_t result_to_cc1(uint32_t hi, uint32_t lo) {
    request_issue(0, REQ_CC1(REQ_OP_RESULT, CTR_REQ(0), tile_index()), lo, hi, 0, 1);   /* no data, one reply */
    return request_wait(0);                         /* C&C 1's answer */
}
/* END */
#endif
#endif
