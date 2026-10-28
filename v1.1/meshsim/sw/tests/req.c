/* The request port from C (spec 4.18, 8.10; RequestBusTest straps target 0 to tile 63's receive queue):
 *   tile 0 sends N requests with request_send(), stalling while its request FIFO is full;
 *   tile 63 sleeps on REQUEST_PENDING ([19], live) and receives them, checking the requestor and the order;
 *   the rest exit. */
#include "mesh.h"
#define N 600
volatile uint32_t r[4];
int main(void) {
    unsigned me = tile_index();
    if (me == 0) {
        for (uint32_t i = 0; i < N; i++) request_send(i, 0x2222, 0x1111, REQ_TARGET(0) | i << 10);
        r[0] = N;
    } else if (me == 63) {
        uint32_t bad = 0;
        WAKE_MASK = WAKE_REQUEST_PENDING | WAKE_MASK_RESET;
        for (uint32_t i = 0; i < N; i++) {
            wfi();                                        /* falls through while a request waits ([19] is live) */
            uint32_t w0 = REQUEST_GET_W0, w1 = REQUEST_GET_W1, w3 = REQUEST_RECEIVE;
            if (w3 != i || (w0 & REQ_REQUESTOR_MASK) != 0 || (w0 >> 10) != i || w1 != 0x1111) bad++;
        }
        WAKE_MASK = WAKE_MASK_RESET;
        r[1] = bad; r[2] = N;
    }
    return 0;
}
