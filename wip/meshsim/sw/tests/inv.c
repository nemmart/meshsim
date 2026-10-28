/* Programming errors the simulator checks (spec 9): tile i does case i mod 4.
 *   0: done while a counter is still expecting arrivals (invariant 4)
 *   1: a second reply from the same source before the program read the first (invariant 8)
 *   2: a STREAM window store while the address generator is running (its lead-in included; 4.10)
 *   3: an unexpected arrival (invariant 9) sets its condition; a store of 1 to TILE_PULSE[3] (GO, unwired on a mesh
 *      tile) is ignored and flagged: the condition stays */
#include "mesh.h"
volatile uint32_t r[4];
int main(void) {
    unsigned me = tile_index();
    switch (me & 3) {
    case 0: DMA_SRAM_REMAINING = 1; TILE_PULSE = TILE_PULSE_DONE(1); DMA_SRAM_REMAINING = 0; break;
    case 1:
        DMA_REPLY_REMAINING = 2; ROUTER_SET_HDR = ROUTER_HDR(me, MODE_REPLY, 0);
        send32(1); send32(2);                                    /* both land at REPLY[2 me] */
        wfi(); break;
    case 2:
        set_stream(0, STREAM_SRC_ADDR(0), STREAM_DEST_ENTRY(0, 0), STREAM_COUNT_ENTRY(1, MODE_SRC, SIZE_64, 0));
        PTR[me] = 0x20000; DMA_SRAM_REMAINING = 1;
        DMA_CONFIG = DMA_CFG_RUN(RUN_STREAMS, 0, 1); TILE_PULSE = TILE_PULSE_START(1);
        set_stream(1, 0, 0, 0);                                   /* in the lead-in: the run is running */
        wfi(); break;
    default:
        ROUTER_SET_HDR = ROUTER_HDR(me, MODE_REPLY, 0); send32(5);
        for (int i = 0; i < 20; i++) r[0] = conditions();
        TILE_PULSE = TILE_PULSE_GO(1);                            /* GO, unwired here: ignored (meshsim flags it) */
        r[1] = conditions();
        break;
    }
    return 0;
}
