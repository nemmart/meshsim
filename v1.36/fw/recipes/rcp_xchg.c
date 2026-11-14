/* Recipe 7: the one-word exchange, and several words to one tile (D6, D7). */
#define RCP_XCHG
#include "rcp.h"
static uint32_t out[64];
volatile uint32_t in[64];
static volatile uint32_t box[8] __attribute__((aligned(8)));

/* RECIPE send_words */
/* n words to one tile's data memory: ROUTER_HDR without no-increment, ROUTER_ADDR at the first word, then one 32-bit
 * send per word; ROUTER_ADDR advances by 4 per 32-bit send. The receiver arms counter 2 for n packets. Mixing sizes
 * in one run needs care: a 64-bit packet must start 8-byte aligned (D26, W3-29). */
static void send_words(unsigned dst, volatile uint32_t *at, const uint32_t *w, unsigned n) {
    send_to(ROUTER_HDR_W(dst, MODE_WRITE, CTR_CM), (uint32_t)at);
    for (unsigned i = 0; i < n; i++) send32(w[i]);
}
/* END */

int main(void) {
    unsigned me = tile_index();
    for (int d = 0; d < 64; d++) out[d] = me * 64 + d;
    for (;;) {
        uint32_t w = wait_for_command();
        switch (CMD_TYPE(w)) {
        case 1: xchg(out, in); break;
        case 2:                                    /* tile 1 sends four words to tile 0 */
            if (me == 0) { tile->WRITE_COUNTER[CTR_CM - 1] = 4; wait_all(EV_CTR(CTR_CM)); }
            if (me == 1) send_words(0, box, out, 4);
            break;
        default: sim_exit(0);
        }
    }
}
