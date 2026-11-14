/* cc_req.c (test_cc_req): C&C 1's program for the request bus's end-to-end test (a tile's REQUEST_SEND
 * through the bus into a controller's REQUEST_RECEIVE). Booted through the host port: starts tile 5 (its program,
 * tile_req, is in its program memory; CMD_JUMP to TILE_ENTRY, then GO from C&C 1's peripheral: the boot ROM's tile branch
 * jumps on GO once a command has landed), waits WAIT cycles without draining (its queue fills, the
 * root runs out of credit, tile 5's FIFO fills), then receives the N requests: it polls EVENTS_PEEK's request pending
 * (EVENTS [12], live), reads REQUEST_GET_W0-W2 and REQUEST_RECEIVE (the pop), and logs the four words and mcycle;
 * then one REQUEST_RECEIVE on the empty queue (receive underflow, ERRORS [14]) and SIM_EXIT. */
#include "mesh.h"
#define N 300
#define WAIT 6000u
#define TILE_ENTRY 0x01000000u
volatile unsigned words[4 * N];
volatile unsigned stamp[N];
volatile unsigned extra[2];
static inline unsigned cyc(void) { unsigned x; __asm__ volatile("csrr %0, mcycle" : "=r"(x)); return x; }
int main(void) {
    tile->ROUTER_HDR = ROUTER_HDR_W(5, MODE_WRITE, CTR_NONE) | HDR_SIZE(SIZE_128);
    tile->ROUTER_ADDR = COMMAND_BASE;
    send128(0, 0, TILE_ENTRY, CMD_WORD(CMD_JUMP, 0, 0));
    while (tile->STATUS & ST_SEND_BUSY) ;
    for (int i = 0; i < 100; i++) __asm__ volatile("nop");   /* the command lands before GO */
    cc1->PULSE_GO = 1;
    const unsigned t0 = cyc();
    while (cyc() - t0 < WAIT) ;
    for (unsigned i = 0; i < N; i++) {
        while (!(tile->EVENTS_PEEK & EV_REQ_PENDING)) ;
        words[4 * i] = tile->REQUEST_GET_W0;
        words[4 * i + 1] = tile->REQUEST_GET_W1;
        words[4 * i + 2] = tile->REQUEST_GET_W2;
        words[4 * i + 3] = tile->REQUEST_RECEIVE;
        stamp[i] = cyc();
    }
    extra[0] = tile->REQUEST_RECEIVE;          /* the queue is empty: 0, and receive underflow */
    extra[1] = tile->ERRORS;
    for (int i = 0; i < 50; i++) __asm__ volatile("nop");
    return 0x5EE;
}
