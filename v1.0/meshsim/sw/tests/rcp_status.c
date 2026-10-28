/* Recipe: status and conditions (spec 8.3, 8.3a, 4.13; M14). Commands (each tile records ev[k] = the conditions case k
 * set, and cond[k] = TILE_CONDITIONS after it):
 *   1 nothing wrong
 *   2 an unexpected arrival: a mode-7 word to itself with DMA_REPLY_REMAINING at 0
 *   3 a bad mode: a packet sent with mode 0
 *   0 exit; info[] = tile index, big-SRAM chunks, the live bits after case 1 */
#include "rcp.h"
volatile uint32_t ev[8], cond[8], info[4], sink[4];

/* RECIPE status */
/* TILE_STATUS (spec 8.3) is what a program steers by: the tile index, the big-SRAM size and the live bits (engines,
 * idle bits, done). TILE_CONDITIONS (8.3a, 4.13) holds the errors: 0 in a correct program, read-only, cleared only by
 * entering HALT, and any bit raises the tile's error line to C&C. A phase that wants to know what it caused reads the
 * conditions at its start and compares at its end. */
static uint32_t at_start;
static inline void phase_start(void) { at_start = conditions(); }
static inline uint32_t phase_conditions(void) { return conditions() & ~at_start; }   /* COND_* bits set in this phase */
/* END */

int main(void) {
    unsigned me = tile_index();
    for (;;) {
        uint32_t w = wait_for_command(), k = CMD_TYPE(w);
        phase_start();
        switch (k) {
        case 1:
            info[0] = tile_index(); info[1] = sram_chunks(); info[2] = status() & 0x3FF;
            break;
        case 2:
            ROUTER_SET_HDR = ROUTER_HDR(me, MODE_REPLY, CM_WORD((uint32_t)&sink[0]));
            send32(1);
            spin(10);
            break;
        case 3:
            ROUTER_SET_HDR = ROUTER_HDR(me, 0, 0);
            send32(1);
            spin(10);
            break;
        default:
            return 0;
        }
        ev[k & 7] = phase_conditions();
        cond[k & 7] = conditions();
    }
}
