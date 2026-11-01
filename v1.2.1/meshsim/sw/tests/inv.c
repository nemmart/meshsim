/* Programming errors the simulator checks (spec 9): tile i does case i mod 4.
 *   0: done while a counter is still expecting arrivals (invariant 4; done early, ERRORS [8])
 *   1: a second reply from the same source before the program read the first (invariant 8)
 *   2: a STREAM window store while the address generator is running (its lead-in included; 4.10)
 *   3: an unexpected arrival (ERRORS [2]); a load of a reserved register-block word reads 0 and is flagged (8.1) */
#include "mesh.h"
volatile uint32_t r[4];
int main(void) {
    unsigned me = tile_index();
    switch (me & 3) {
    case 0: tile->WRITE_COUNTER[CTR_SRAM - 1] = 1; tile->PULSE_DONE = 1; break;
    case 1:
        tile->WRITE_COUNTER[CTR_REPLY - 1] = 2;
        send_to(ROUTER_HDR_W(me, MODE_WRITE, CTR_REPLY) | ROUTER_NOINC, REPLY_WORD(me));
        send32(1); send32(2);                                    /* both land at REPLY[2 me] */
        wfi(); break;
    case 2:
        set_stream(0, STREAM_SRC_W(0, 0), 0, STREAM_COUNT_W(1, SIZE_64, CTR_SRAM, MODE_SRC, 0));
        tables->PTR[me] = 0x20000; tile->WRITE_COUNTER[CTR_SRAM - 1] = 1;
        tile->DMA_CONFIG = DMA_CFG_RUN(RUN_STREAMS, 0, 1); start_run();
        tables->STREAM_SRC[1] = 0;                               /* in the lead-in: the run is running */
        wfi(); break;
    default:
        send_to(ROUTER_HDR_W(me, MODE_WRITE, CTR_REPLY), REPLY_WORD(me)); send32(5);
        for (int i = 0; i < 20; i++) r[0] = tile->ERRORS;
        r[1] = ((volatile uint32_t *)tile)[40];                  /* reserved word 0x8A0: reads 0, flagged */
        r[2] = tile->ERRORS;
        break;
    }
    return 0;
}
