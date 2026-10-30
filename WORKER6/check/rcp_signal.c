/* Recipe 28: count-only packets (D43): the fence (in rcp_cc1.c) and a signal between tiles. */
#include "rcp.h"
#define PW 8
static uint32_t vecs[4][PW], gots[4][PW];

/* RECIPE signal */
/* A signal between tiles: one count-only packet (mode 5, D43) on a counter the receiver armed. It decrements the
 * counter and writes nothing, so "done" or "your data is all sent" costs one packet and no landing word, where a
 * reply would write a word nobody reads. The address is ignored and ROUTER_ADDR doesn't move (it advances for mode 0
 * only, D42). */
static void signal(unsigned dst, unsigned ctr) {
    tile->ROUTER_HDR = ROUTER_HDR_W(dst, MODE_COUNT, ctr);
    send32(0);
}
/* A ring pipeline: each step every tile sends a PW-word vector into its right-hand neighbour's inbox, then frees its
 * own inbox for its left-hand neighbour with a signal once it has copied the data out. The data counts on counter 2,
 * the release on counter 3. First in a command (step 0's data can arrive a few cycles after GO). */
static uint32_t inbox[PW] __attribute__((aligned(8)));
static void ring_pipe(uint32_t (*vec)[PW], uint32_t (*got)[PW], unsigned steps) {
    unsigned me = tile_index(), right = (me + 1) & 63, left = (me - 1) & 63;
    tile->WRITE_COUNTER[CTR_CM - 1] = PW / 2;       /* step 0's data from the left */
    for (unsigned s = 0; s < steps; s++) {
        if (s) wait_all(EV_CTR(CTR_REPLY));         /* the right-hand neighbour has freed its inbox */
        tile->WRITE_COUNTER[CTR_REPLY - 1] = 1;     /* before the data goes: its release can only follow it */
        send_to(ROUTER_HDR_W(right, MODE_WRITE, CTR_CM), (uint32_t)inbox);
        for (int i = 0; i < PW; i += 2) send64(vec[s][i + 1], vec[s][i]);
        wait_all(EV_CTR(CTR_CM));                   /* this step's data from the left is in */
        for (int i = 0; i < PW; i++) got[s][i] = inbox[i];
        if (s + 1 < steps) tile->WRITE_COUNTER[CTR_CM - 1] = PW / 2;   /* the next step's data, before the release */
        signal(left, CTR_REPLY);                    /* the left-hand neighbour may send again */
    }
    wait_all(EV_CTR(CTR_REPLY));                    /* the last release: no counter is left armed at done */
}
/* END */

int main(void) {
    for (;;) {
        uint32_t w = wait_for_command();
        if (CMD_TYPE(w) != 1) sim_exit(0);
        ring_pipe(vecs, gots, 4);
    }
}
