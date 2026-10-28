/* WFI and the local wake (spec 4.12, 6.3), in four steps; the test checks the controller's WFI log.
 *   A: the latch is set before WFI (a self copy finishes first): WFI falls straight through
 *   B: a counter store clears the latch, so wait_for_command() waits for GO; a count of 0 makes no edge
 *   C: GO is not latched: done, then busy while GO arrives, then WFI waits for the next (forced) GO
 *   D: the command lands before done: READY fires at done */
#include "mesh.h"
volatile uint32_t buf[4], rep, r[4], cmds[4];
static void spin(int n) { for (volatile int i = 0; i < n; i++) ; }
int main(void) {
    unsigned me = tile_index();
    DMA_CM_REMAINING = 2;
    ROUTER_SET_HDR = ROUTER_HDR(me, MODE_CMEM, CM_WORD((uint32_t)&buf[0]));
    send64(1, 2); send64(3, 4);
    spin(20);
    r[0] = status();
    wfi();                                   /* A */
    DMA_REPLY_REMAINING = 1;
    ROUTER_SET_HDR = ROUTER_HDR(me, MODE_REPLY, CM_WORD((uint32_t)&rep));
    send32(9);
    spin(20);
    DMA_REPLY_REMAINING = 0;                     /* clears the latch the reply set */
    DMA_CM_REMAINING = 0;                        /* a count of 0: no edge */
    cmds[0] = wait_for_command();            /* B; it reads the command word on waking */
    DMA_PULSE = DMA_PULSE_DONE(1);
    spin(60);                                /* GO arrives here, while not in WFI */
    wfi();                                   /* C */
    cmds[1] = MBOX[0];
    spin(60);                                /* the next command lands here */
    cmds[2] = wait_for_command();            /* D */
    r[1] = buf[0] + buf[1] + buf[2] + buf[3] + rep;
    return 0;
}
