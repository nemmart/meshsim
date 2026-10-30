/* Recipe 11: reading big SRAM through the read port (spec 4.3; D13, D26, D36). */
#include "rcp.h"
#define KEYS 0x60000           /* area 3 */
#define NQ 7
volatile uint32_t first, lb[NQ];

/* RECIPE read_one */
/* One 32-bit chunk of big SRAM into data memory: set up the read port (the result's byte offset in data memory, the
 * size and the counter, D13), queue the read, and its result comes back through the loopback as a mode-0 write,
 * counted on counter 2. Not while packets are arriving for this tile (CONVENTIONS 9). */
static uint32_t sram_read32(uint32_t chunk) {
    static volatile uint32_t result;
    tile->WRITE_COUNTER[CTR_CM - 1] = 1;
    tile->SRAM_RD_DEST_ADDR = RD_DEST((uint32_t)&result, SIZE_32, CTR_CM);
    tile->SRAM_RD_ADDR_QUEUE = chunk;
    wait_all(EV_CTR(CTR_CM));                       /* the result has landed */
    return result;
}
/* END */

/* RECIPE search */
/* Lower bounds (the first i with keys[i] >= q) of nq <= 32 queries in n sorted 32-bit keys at chunk keys: every active
 * search issues one read per step, then one wait for all the results (spec 4.3's loop; at most 63 reads in a step).
 * Results may land in any order: each lands at its own word, res[a] for the a-th read of the step.
 * A step of its own: no packets arrive for this tile meanwhile, and every result has landed before the next exchange. */
static void search(uint32_t keys, uint32_t n, const uint32_t *q, volatile uint32_t *lo, unsigned nq) {
    static volatile uint32_t res[32];
    uint32_t l[32], h[32];
    unsigned act[32], na = 0;
    for (unsigned i = 0; i < nq; i++) { l[i] = 0; h[i] = n; if (n) act[na++] = i; }
    while (tile->SRAM_RD_ADDR_QUEUE < READ_PORT_DEPTH) ;   /* once: never waits, since earlier phases waited for their reads */
    while (na) {
        tile->WRITE_COUNTER[CTR_CM - 1] = na;
        tile->SRAM_RD_DEST_ADDR = RD_DEST((uint32_t)&res[0], SIZE_32, CTR_CM);   /* result a lands in res[a] */
        for (unsigned a = 0; a < na; a++) { unsigned i = act[a]; tile->SRAM_RD_ADDR_QUEUE = keys + ((l[i] + h[i]) >> 1); }
        wait_all(EV_CTR(CTR_CM));
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
        uint32_t q[NQ];
        switch (CMD_TYPE(w)) {
        case 1: first = sram_read32(KEYS); break;
        case 2: for (int i = 0; i < NQ; i++) q[i] = arg[i]; search(KEYS, CMD_ARG(w), q, lb, NQ); break;
        default: sim_exit(0);
        }
    }
}
