/* Recipe 23: waiting for the tables by events (D17). */
#include "rcp.h"
static uint32_t expected;

/* RECIPE tables_wait */
/* The tables (STREAM, GROUP, PTR, HIST) are free when both sides are done (D17): the arrivals, which update PTR and
 * HIST, by the write counter the program armed (guarantee A: K covers the update); and this tile's own run, which reads
 * STREAM and GROUP, by DMA run done, EVENTS [7] (guarantee B: it fires after the run's last table read). Neither
 * alone is enough: arrivals can finish before the tile's own run, and the run can finish before the arrivals. */
static void a2a_finish(void) {
    wait_all(expected ? EV_CTR(CTR_SRAM) | EV_RUN_DONE : EV_RUN_DONE);
}
/* A helper that can't know what ran before (a pass that starts by checking the tables are free) may poll the level
 * STATUS.rd_running instead (D17), but only when nothing counted can still be arriving: it says nothing about PTR
 * and HIST updates from arrivals. */
static void tables_free_unknown(void) {
    wait_rd_idle();
}
/* END */

int main(void) {
    for (;;) {
        uint32_t w = wait_for_command();
        if (CMD_TYPE(w) == 1) { tile->WRITE_COUNTER[0] = expected; start_run(); a2a_finish(); }
        else if (CMD_TYPE(w) == 2) tables_free_unknown();
        else sim_exit(0);
    }
}
