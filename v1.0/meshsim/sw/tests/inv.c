/* Programming errors the simulator checks (spec 9): tile i does case i mod 4.
 *   0: done while a counter is still expecting arrivals (invariant 4)
 *   1: a second mode-7 reply to a word the program has not read (invariant 8)
 *   2: a STREAM window store while the address generator is running (its lead-in included; 4.10)
 *   3: an unexpected arrival (invariant 9) sets its condition; a store of 1 to DMA_PULSE[3] (reserved) is ignored and
 *      flagged: the condition stays */
#include "mesh.h"
volatile uint32_t rep, buf[2], r[4];
int main(void) {
    unsigned me = tile_index();
    switch (me & 3) {
    case 0: DMA_SRAM_REMAINING = 1; DMA_PULSE = DMA_PULSE_DONE(1); DMA_SRAM_REMAINING = 0; break;
    case 1:
        DMA_REPLY_REMAINING = 2; ROUTER_SET_HDR = ROUTER_HDR(me, MODE_REPLY, CM_WORD((uint32_t)&rep));
        send32(1); ROUTER_SET_HDR = ROUTER_HDR(me, MODE_REPLY, CM_WORD((uint32_t)&rep)); send32(2);
        wfi(); break;
    case 2:
        set_stream(0, STREAM_LO(0, 0), STREAM_HI(1, 0, MODE_SRC, SIZE_64, 0));
        PTR[me] = 0x20000; DMA_SRAM_REMAINING = 1;
        DMA_CONFIG = DMA_CFG_RUN(RUN_STREAMS, 0, 1); DMA_PULSE = DMA_PULSE_START(1);
        set_stream(1, 0, 0);                                      /* in the lead-in: the run is running */
        wfi(); break;
    default:
        ROUTER_SET_HDR = ROUTER_HDR(me, MODE_REPLY, CM_WORD((uint32_t)&buf[0])); send32(5);
        for (int i = 0; i < 20; i++) r[0] = conditions();
        DMA_PULSE = 1u << 3;                                      /* reserved: ignored (meshsim flags it) */
        r[1] = conditions();
        break;
    }
    return 0;
}
