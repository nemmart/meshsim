/* WFI and the wake events (spec 4.12, 6.3), in four steps; the test checks the controller's WFI log.
 *   A: counters done is set before WFI (a self copy finishes first): WFI falls straight through, and wfi()'s load
 *      consumes it
 *   B: a reply sets counters done again; wait_for_command()'s done pulse clears it (and closes the done gate), so the
 *      WFI waits for GO
 *   C: GO is a sticky event: done, then busy while GO arrives, then a WFI falls straight through on the pending GO
 *   D: the command lands before done: READY fires at done */
#include "mesh.h"
volatile uint32_t buf[4], r[4], cmds[4];
static void spin(int n) { for (volatile int i = 0; i < n; i++) ; }
int main(void) {
    unsigned me = tile_index();
    DMA_CM_REMAINING = 2;
    ROUTER_SET_HDR = ROUTER_HDR(me, MODE_CM, CM_WORD((uint32_t)&buf[0]));
    send64(1, 2); send64(3, 4);
    spin(20);
    r[0] = status();
    r[2] = wfi();                            /* A: returns WAKE_EVENTS, counters done ([9]) and DMA_CM_REMAINING done ([1]) */
    DMA_REPLY_REMAINING = 1;
    ROUTER_SET_HDR = ROUTER_HDR(me, MODE_REPLY, 0);
    send32(9);
    spin(20);                                /* the reply lands: counters done is pending */
    cmds[0] = wait_for_command();            /* B: done clears it; WFI waits for GO */
    TILE_PULSE = TILE_PULSE_DONE(1);
    spin(60);                                /* GO arrives here, while not in WFI */
    wfi();                                   /* C: falls through on the pending GO and consumes it */
    cmds[1] = COMMAND[0];
    spin(60);                                /* the next command lands here */
    cmds[2] = wait_for_command();            /* D */
    r[1] = buf[0] + buf[1] + buf[2] + buf[3] + REPLY[2 * me];
    return 0;
}
