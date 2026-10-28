/* Recipe: a vector exchange with the four neighbours (mode 5; spec 4.8, 4.11). Command 1: each tile sends v[i] =
 * me << 8 | i to me + 1, me - 1, me + 8 and me - 8. Command 0: exit. */
#include "rcp.h"
#define W 6                    /* words per vector, even */
uint32_t nb[4][W];
static uint32_t v[W];

/* RECIPE xvec */
/* Send v[0..W-1] (W even) to the four neighbours, as mode-5 packets into their data memory: the vector lands in the
 * neighbour's nb[k], k naming this tile as the neighbour sees it: 0 from its left (me - 1), 1 from its right, 2 from
 * above (me - 8), 3 from below. The address advances by 2 per 64-bit packet. Call it first in a command. */
static void xvec(const uint32_t *vec) {
    static const int8_t to[4] = { 1, -1, 8, -8 };  /* to me + 1, this tile is its left neighbour: nb[0] */
    unsigned me = tile_index();
    DMA_CM_REMAINING = 4 * (W / 2);                    /* four vectors of W / 2 packets will arrive */
    for (int k = 0; k < 4; k++) {
        ROUTER_SET_HDR = ROUTER_HDR((me + to[k]) & 63, MODE_CMEM, CM_WORD((uint32_t)&nb[k][0]));
        for (int i = 0; i < W; i += 2) send64(vec[i + 1], vec[i]);
    }
    wfi();
}
/* END */

int main(void) {
    unsigned me = tile_index();
    for (int i = 0; i < W; i++) v[i] = me << 8 | i;
    for (;;) {
        uint32_t w = wait_for_command();
        if (CMD_TYPE(w) != 1) return 0;
        xvec(v);
    }
}
