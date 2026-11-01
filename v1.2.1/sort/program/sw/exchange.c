/* exchange.c -- the exchanges (worker 6; v1.2: WORKER8): the one-word exchange (xchg) and the tree reduction
 * (tree_reduce), used by XB, RED, XC, TH, SWEEP and DESC. XB, RED, K and V themselves are in transfer.c. */
#include "sort.h"

/* RECIPES 7: one 32-bit mode-0 write per destination to REPLY_WORD(me) there, the same address in every receiver, so
 * ROUTER_ADDR is set once with no-increment and each send changes only ROUTER_HDR's dst; counted on counter 3. in[s] is
 * copied out of REPLY[2 s] once every word is in (the next exchange can't start before the barrier that follows this
 * command). Every send site in the sort stores ROUTER_HDR whole, so the no-increment bit never outlives its exchange
 * (rule 10) without a clearing store here. */
void xchg(const uint32_t *out, uint32_t *in) {
    tile->WRITE_COUNTER[CTR_REPLY - 1] = 64;           /* first: no word can arrive before it (a store adds; it is 0) */
    unsigned me = tile_index();
    tile->ROUTER_ADDR = REPLY_WORD(me);
    const uint32_t hdr = ROUTER_HDR_W(0, MODE_WRITE, CTR_REPLY) | ROUTER_NOINC;
    for (int s = 0; s < 64; s++) {
        unsigned dst = (me + GROUP_OFF[s]) & 63;
        tile->ROUTER_HDR = hdr | HDR_DST(dst);
        send32(out[dst]);
    }
    wait_counters();
    for (int s = 0; s < 64; s++) in[s] = REPLY[2 * s];
}

/* RED's receive buffers, at the same address on every tile: from the right (me + 1), the left (me - 1), below
 * (me + 8), above (me - 8), and tile 0's broadcast. Up to RMAX words (RMAX/2 64-bit packets) each, 8-byte aligned
 * (D26: a 64-bit packet lands at an 8-byte-aligned address; RMAX is even, so every row is). */
#define RMAX 20
static uint32_t rb[4][RMAX] __attribute__((aligned(8)));
static uint32_t rbB[RMAX] __attribute__((aligned(8)));

static void merge(uint32_t *m, const uint32_t *v, unsigned words) {
    for (unsigned g = 0; g < words; g++) if (v[g] > m[g]) m[g] = v[g];
}

/* RECIPES 10: the vector as 64-bit mode-0 writes into the parent's data memory, counted on counter 2; ROUTER_ADDR
 * advances by 8 per packet. */
static void send_vector(unsigned dst, const uint32_t *at, const uint32_t *m, unsigned words) {
    send_to(ROUTER_HDR_W(dst, MODE_WRITE, CTR_CM), (uint32_t)at);
    for (unsigned i = 0; i < words; i += 2) send64(m[i + 1], m[i]);
}

void tree_reduce(uint32_t *vec, unsigned words, uint32_t *out, unsigned wait) {
    /* merged in place; an odd vector's last packet carries one word past its end, which nothing reads back */
    unsigned pk = (words + 1) >> 1;
    if (S.red_children) {
        if (wait) wait_counters();
        for (unsigned k = 0; k < 4; k++) if (S.red_mask >> k & 1) merge(vec, rb[k], words);
    }
    if (S.red_parent == DST_BROADCAST) tile->WRITE_COUNTER[CTR_CM - 1] = pk;   /* tile 0 receives its own broadcast too */
    send_vector(S.red_parent, S.red_parent == DST_BROADCAST ? rbB : rb[S.red_pbuf], vec, words);
    if (S.red_parent != DST_BROADCAST) tile->WRITE_COUNTER[CTR_CM - 1] = pk;   /* the broadcast can't come before this: it needs ours */
    wait_counters();
    for (unsigned g = 0; g < words; g++) out[g] = rbB[g];
}
