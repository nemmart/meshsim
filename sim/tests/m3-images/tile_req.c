/* tile_req.c (WORKER14, test_cc_req): a tile's program for the request bus's end-to-end test. Run on tile 5, started by
 * C&C 1's CMD_JUMP. It sends N requests to C&C 1 (target 0) back to back: REQUEST_SET_W0-W2, then REQUEST_SEND, which
 * stalls while its port's 128-entry FIFO is full (C&C 1 doesn't drain at first, so the queue fills, the root runs out of
 * credit and the FIFO fills: canPush with a head freed the same cycle). stamp[i]: mcycle after send i completed. */
#include "mesh.h"
#define N 300
volatile unsigned stamp[N];
static inline unsigned cyc(void) { unsigned x; __asm__ volatile("csrr %0, mcycle" : "=r"(x)); return x; }
int main(void) {
    for (unsigned i = 0; i < N; i++) {
        tile->REQUEST_SET_W0 = (i << 10) | (0u << 7);            /* target 0: C&C 1; [6:0] stamped by the bus */
        tile->REQUEST_SET_W1 = 0xA5A50000u | i;
        tile->REQUEST_SET_W2 = i * 0x01010101u;
        tile->REQUEST_SEND = 0xC0DE0000u | i;
        stamp[i] = cyc();
    }
    for (;;) __asm__ volatile("wfi");
}
