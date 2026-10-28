/* Recipe: reading big SRAM through the controller read port (spec 4.3; CONVENTIONS 9). Each tile holds n = 300 + me
 * sorted 32-bit keys at KEYS. Commands:
 *   1 (argument c): one read: first = the key at chunk KEYS + c
 *   2 (arguments q[0..NQ-1] in the block): lower bounds of the NQ queries, all searches advancing together
 *   0 exit */
#include "rcp.h"
#define KEYS 0x60000           /* area 3 */
#define NQ 8
volatile uint32_t first, lb[NQ];

/* RECIPE read_one */
/* One 32-bit chunk of big SRAM into data memory: queue the read, and its result comes back as a mode-7 word through the
 * loopback, counting in DMA_REPLY_REMAINING (spec 4.3). Not while packets are arriving for this tile (CONVENTIONS 9). */
static uint32_t sram_read32(uint32_t chunk) {
    static volatile uint32_t result;
    DMA_REPLY_REMAINING = 1;
    SRAM_RD_DEST_ADDR = CM_WORD((uint32_t)&result);
    SRAM_RD_ADDR_QUEUE = chunk;
    wfi();                                         /* the result has landed (and the latch is consumed) */
    return result;
}
/* END */

/* RECIPE search */
/* Lower bounds (the first i with keys[i] >= q) of nq <= 32 queries in n sorted 32-bit keys at chunk keys: every active
 * search issues one read per step, then one WFI for all the results (spec 4.3's loop; at most 63 reads in a step).
 * A step of its own: no packets arrive for this tile meanwhile, and every result has landed before the next exchange.
 * The sort's split does this faster: 8 searches in registers with a shared step, about 12 cycles a step. */
static void search(uint32_t keys, uint32_t n, const uint32_t *q, volatile uint32_t *lo, unsigned nq) {
    static volatile uint32_t res[32];
    uint32_t l[32], h[32];
    unsigned act[32], na = 0;
    for (unsigned i = 0; i < nq; i++) { l[i] = 0; h[i] = n; if (n) act[na++] = i; }
    while (SRAM_RD_ADDR_QUEUE < READ_PORT_DEPTH) ;  /* once: never waits, since earlier phases ended with a WFI */
    while (na) {
        DMA_REPLY_REMAINING = na;
        SRAM_RD_DEST_ADDR = CM_WORD((uint32_t)&res[0]);       /* result a lands in res[a] */
        for (unsigned a = 0; a < na; a++) { unsigned i = act[a]; SRAM_RD_ADDR_QUEUE = keys + ((l[i] + h[i]) >> 1); }
        wfi();
        unsigned nn = 0;
        for (unsigned a = 0; a < na; a++) {
            unsigned i = act[a], mid = (l[i] + h[i]) >> 1;
            if (res[a] < q[i]) l[i] = mid + 1; else h[i] = mid;
            if (l[i] < h[i]) act[nn++] = i;
        }
        na = nn;
    }
    for (unsigned i = 0; i < nq; i++) lo[i] = l[i];
}
/* END */

int main(void) {
    for (;;) {
        uint32_t w = wait_for_command();
        volatile uint32_t *arg = CMD_ARGS(w);
        if (CMD_TYPE(w) == 1) first = sram_read32(KEYS + CMD_ARG(w));
        else if (CMD_TYPE(w) == 2) {
            uint32_t q[NQ];
            for (int i = 0; i < NQ; i++) q[i] = arg[i];
            search(KEYS, 300 + tile_index(), q, lb, NQ);
        } else return 0;
    }
}
