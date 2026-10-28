/* Recipe: exchanges from the send port (spec 4.11). Commands:
 *   1: the one-word exchange: tile s sends tile d the word s << 8 | d; in[s] receives it
 *   2: four words to the right-hand neighbour, one ROUTER_SET_HDR and four sends (the address auto-increments)
 *   0: exit */
#define RCP_XCHG
#include "rcp.h"
volatile uint32_t in[64], in4[4];
static uint32_t out[64], my4[4];

/* RECIPE send_words */
/* n words to one tile: one ROUTER_SET_HDR, then one send per word. The address advances by 1 per send in mode 7 (by 2 per
 * 64-bit packet in mode 5), so the words land at at[0..n-1]. The receiver counts n in DMA_REPLY_REMAINING. */
static void send_words(unsigned dst, volatile uint32_t *at, const uint32_t *w, unsigned n) {
    ROUTER_SET_HDR = ROUTER_HDR(dst, MODE_REPLY, CM_WORD((uint32_t)at));
    for (unsigned i = 0; i < n; i++) send32(w[i]);
}
/* END */

int main(void) {
    unsigned me = tile_index();
    for (unsigned d = 0; d < 64; d++) out[d] = me << 8 | d;
    for (unsigned i = 0; i < 4; i++) my4[i] = me << 8 | 0xF0 | i;
    for (;;) {
        uint32_t w = wait_for_command();
        if (CMD_TYPE(w) == 1) xchg(out, in);
        else if (CMD_TYPE(w) == 2) {
            DMA_REPLY_REMAINING = 4;                   /* first in the command */
            send_words((me + 1) & 63, in4, my4, 4);
            wfi();
        } else return 0;
    }
}
