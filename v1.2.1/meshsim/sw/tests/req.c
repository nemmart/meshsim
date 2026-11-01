/* The request port from C (spec 3.3, 4.18, 8.10; v1.2): tile 0 sends N requests to C&C 2 (target 1) with
 * request_send(), stalling while its request FIFO is full and waking on EVENTS [11] (request FIFO no longer full) when
 * it wants to; the test drains C&C 2's receive queue (256 deep, in the tree) from Java and checks the requestor and the
 * order. Every other tile exits. */
#include "mesh.h"
#define N 600
volatile uint32_t r[4];
int main(void) {
    unsigned me = tile_index();
    if (me == 0) {
        for (uint32_t i = 0; i < N; i++) request_send(i, 0x2222, 0x1111, REQ_TARGET(TARGET_CC2) | i << 10);
        r[0] = N;
        r[1] = tile->ERRORS;
    }
    return 0;
}
