/* Recipe 8: a vector exchange with the neighbours (D6, D26). */
#include "rcp.h"
#define W 6
static uint32_t nb[4][W] __attribute__((aligned(8)));
static uint32_t v[W];

/* RECIPE xvec */
/* Send vec[0..W-1] (W even) to the four neighbours, as 64-bit mode-0 writes into their data memory: the vector lands
 * in the neighbour's nb[k], k naming this tile as the neighbour sees it: 0 from its left (me - 1), 1 from its right,
 * 2 from above (me - 8), 3 from below. nb is 8-byte aligned and W even, so every packet is aligned (D26). Counted on
 * counter 2. Call it first in a command. */
static void xvec(const uint32_t *vec) {
    static const int8_t to[4] = { 1, -1, 8, -8 };  /* to me + 1, this tile is its left neighbour: nb[0] */
    unsigned me = tile_index();
    tile->WRITE_COUNTER[CTR_CM - 1] = 4 * (W / 2);  /* four vectors of W / 2 packets will arrive */
    for (int k = 0; k < 4; k++) {
        send_to(ROUTER_HDR_W((me + to[k]) & 63, MODE_WRITE, CTR_CM), (uint32_t)&nb[k][0]);
        for (int i = 0; i < W; i += 2) send64(vec[i + 1], vec[i]);
    }
    wait_all(EV_CTR(CTR_CM));
}
/* END */

int main(void) {
    for (int i = 0; i < W; i++) v[i] = tile_index() * 16 + i;
    for (;;) {
        uint32_t w = wait_for_command();
        if (CMD_TYPE(w) != 1) sim_exit(0);
        xvec(v);
    }
}
