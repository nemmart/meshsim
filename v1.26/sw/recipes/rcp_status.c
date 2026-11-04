/* Recipe 12: parameters, status and errors (D18, D20). */
#include "rcp.h"
volatile uint32_t me_out, chunks_out, present_out, live_out;

/* RECIPE status */
/* PARAMS is constant: the endpoint id, the big-SRAM size and which peripherals are present. STATUS is live and
 * diagnostic: no program needs to poll it (D17, D18). ERRORS is not something a program checks: any bit raises the
 * hard interrupt and the trap is terminal (D20), so a program that is running has ERRORS = 0. */
static void describe(void) {
    uint32_t p = tile->PARAMS;
    me_out = PARAMS_ID(p);                          /* tile_index() */
    chunks_out = (PARAMS_SIZE(p) + 1) << 16;        /* sram_chunks(): 256 KB = 0x10000 chunks */
    present_out = PARAMS_PERIPH_PRESENT(p);         /* bit n - 1: peripheral n; its window exists only then */
    live_out = tile->STATUS;                        /* rd_running, rd_done, GATHER busy, send_busy, done, landed, request_busy */
}
/* END */

int main(void) {
    for (;;) {
        uint32_t w = wait_for_command();
        if (CMD_TYPE(w) != 1) sim_exit(0);
        describe();
    }
}
