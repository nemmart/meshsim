/* exchange.c -- the kernel's exchanges (worker 6; resident since M7): the one-word exchange (xchg) and the tree
 * reduction (tree_reduce), used by XB, RED, XC, TH, SWEEP and DESC from their overlays. XB, RED, K and V themselves
 * are in transfer.c (the XFER overlay). */
#include "sort.h"


/* 26n: one 32-bit reply per destination, landing by source at REPLY[2 me] there; in[s] is copied out of REPLY[2 s]
 * once every word is in (the next exchange can't start before the barrier that follows this command). */
void xchg(const uint32_t *out, uint32_t *in) {
    DMA_REPLY_REMAINING = 64;                          /* first: no word can arrive before it (a write adds; it is 0) */
    unsigned me = tile_index();
    const uint32_t hdr = ROUTER_HDR(0, MODE_REPLY, 0);
    for (int s = 0; s < 64; s++) {
        unsigned dst = (me + GROUP_OFF[s]) & 63;
        ROUTER_SET_HDR = hdr | ROUTER_HDR_DST(dst);
        ROUTER_SEND_32 = out[dst];
    }
    wfi();
    for (int s = 0; s < 64; s++) in[s] = REPLY[2 * s];
}

/* RED's receive buffers, at the same address on every tile: from the right (me + 1), the left (me - 1), below
 * (me + 8), above (me - 8), and tile 0's broadcast. Up to RMAX words (RMAX/2 packets) each. */
#define RMAX 20
/* In the shared region (.bss.plan, M7): never live during the split. */
static uint32_t rb[4][RMAX] __attribute__((section(".bss.plan"), aligned(8)));
static uint32_t rbB[RMAX] __attribute__((section(".bss.plan"), aligned(8)));

static void merge(uint32_t *m, const uint32_t *v, unsigned words) {
    for (unsigned g = 0; g < words; g++) if (v[g] > m[g]) m[g] = v[g];
}

static void send_vector(unsigned dst, const uint32_t *at, const uint32_t *m, unsigned words) {
    ROUTER_SET_HDR = ROUTER_HDR(dst, MODE_CM, CM_WORD((uint32_t)at));
    for (unsigned i = 0; i < words; i += 2) send64(m[i + 1], m[i]);   /* addr += 2 per packet */
}

void tree_reduce(uint32_t *vec, unsigned words, uint32_t *out, unsigned wait) {
    /* merged in place; an odd vector's last packet carries one word past its end, which nothing reads back */
    unsigned pk = (words + 1) >> 1;
    if (S.red_children) {
        if (wait) wfi();
        for (unsigned k = 0; k < 4; k++) if (S.red_mask >> k & 1) merge(vec, rb[k], words);
    }
    if (S.red_parent == DST_BROADCAST) DMA_CM_REMAINING = pk;   /* tile 0 receives its own broadcast too */
    send_vector(S.red_parent, S.red_parent == DST_BROADCAST ? rbB : rb[S.red_pbuf], vec, words);
    if (S.red_parent != DST_BROADCAST) DMA_CM_REMAINING = pk;   /* the broadcast can't come before this: it needs ours */
    wfi();
    for (unsigned g = 0; g < words; g++) out[g] = rbB[g];
}
