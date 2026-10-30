/* Recipes 17-19 and 21: write a block, fill, hash and the self-test. */
#define RCP_RESULT
#include "rcp.h"
#include "selftest.h"
#define SRC 0x00000            /* area 0: the filled block */
#define DST 0x40000            /* area 2: the all-to-all's receive buffer */
#define WB  0x80000            /* area 4: the block written from data memory */
#define BLK 256
static uint32_t n_rec, m_per, expected;
volatile uint32_t sig[2], state[2];
static uint32_t blk[2 * BLK];

/* RECIPE write_block */
/* n 64-bit records from data memory (w[2i] the low word, w[2i + 1] the high) into big SRAM at chunk dst (even): mode 0
 * to the tile's own region 0x03 at an explicit address, ROUTER_ADDR advancing by 8 per packet (D5, D7). No PTR, so
 * no table is touched. Counted on counter 1. */
static void write_block(uint32_t dst, const uint32_t *w, uint32_t n) {
    tile->WRITE_COUNTER[CTR_SRAM - 1] = n;
    send_to(ROUTER_HDR_W(tile_index(), MODE_WRITE, CTR_SRAM), SRAM_ADDR(dst));
    for (uint32_t i = 0; i < n; i++) send64(w[2 * i + 1], w[2 * i]);
    if (n) wait_all(EV_CTR(CTR_SRAM));              /* every record written */
}
/* END */

/* RECIPE fill */
/* n pseudo-random 64-bit records into big SRAM at chunk dst: selftest.h's xorshift64, generated in registers and sent
 * like write_block(), with no data memory in between. (hi, lo) is the generator's state, seeded by st_seed() or
 * st_tile_seed(); it is left where the block ended, so a second call continues the sequence. */
static void fill(uint32_t dst, uint32_t n, uint32_t *hi, uint32_t *lo) {
    uint32_t h = *hi, l = *lo;
    tile->WRITE_COUNTER[CTR_SRAM - 1] = n;
    send_to(ROUTER_HDR_W(tile_index(), MODE_WRITE, CTR_SRAM), SRAM_ADDR(dst));
    for (uint32_t i = 0; i < n; i++) { st_next(&h, &l); send64(h, l); }
    *hi = h; *lo = l;
    if (n) wait_all(EV_CTR(CTR_SRAM));
}
/* END */

/* RECIPE seeds */
/* The two ways to seed. From C&C 1: one unicast command per tile, the 64-bit seed as two arguments. From the tile
 * index: one broadcast command whose two arguments are a base seed (0 for none), mixed with the index by st_tile_seed(). */
static void seed_from_cnc(volatile uint32_t *arg, uint32_t *hi, uint32_t *lo) {
    *lo = arg[0]; *hi = arg[1];
    st_seed(hi, lo);
}
static void seed_from_index(volatile uint32_t *arg, uint32_t *hi, uint32_t *lo) {
    st_tile_seed(tile_index(), arg[1], arg[0], hi, lo);
}
/* END */

/* RECIPE hash */
/* The 64-bit hash of n records of big SRAM at chunk src (selftest.h: two 32-bit lanes, the low and the high words,
 * h = rotl((h ^ w) * M, R)), read through the read port (spec 4.3) in batches of 32 words, two buffers: while one batch
 * is folded the next is in flight. Each batch is counted on counter 2; the next batch is armed after the wait that
 * consumed the last one's done. A step of its own: no packets may arrive for this tile meanwhile (CONVENTIONS 9). */
static void hash_block(uint32_t src, uint32_t n, uint32_t *hi, uint32_t *lo) {
    static volatile uint32_t buf[2][32];
    uint32_t h0 = ST_HASH_H0, h1 = ST_HASH_H1, words = 2 * n, issued = 0, folded = 0;
    unsigned k = 0;
    while (folded < words) {
        uint32_t cur = issued - folded;            /* the batch in flight to buf[k] (0 the first time) */
        if (cur) wait_all(EV_CTR(CTR_CM));         /* it has landed */
        if (issued < words) {                      /* the next batch, to the other buffer */
            uint32_t m = words - issued < 32 ? words - issued : 32;
            tile->WRITE_COUNTER[CTR_CM - 1] = m;
            tile->SRAM_RD_DEST_ADDR = RD_DEST((uint32_t)&buf[cur ? k ^ 1 : k][0], SIZE_32, CTR_CM);
            for (uint32_t i = 0; i < m; i++) tile->SRAM_RD_ADDR_QUEUE = src + issued + i;
            issued += m;
        }
        if (!cur) continue;
        for (uint32_t i = 0; i < cur; i += 2) {    /* fold: the low word into lane 0, the high into lane 1 */
            h0 = st_mix(h0, buf[k][i], ST_HASH_M0);
            h1 = st_mix(h1, buf[k][i + 1], ST_HASH_M1);
        }
        folded += cur; k ^= 1;
    }
    *hi = h1; *lo = h0;
}
/* END */

/* RECIPE selftest */
/* The self-test: seed and fill a block (command 1 or 2), run an operation on it (here the uniform all-to-all on the
 * 16-group schedule: n / 64 records to each tile, configure then start), hash what arrived (command 5), and send the
 * signature to C&C 1 (command 6) as a request, which C&C 1 compares with the simulator's. The all-to-all: */
static void a2a_uniform_configure(uint32_t n) {
    unsigned me = tile_index();
    m_per = n >> 6;
    for (int g = 0; g < 16; g++) {
        for (int i = 0; i < 4; i++) {
            unsigned off = SCHED[g][i], d = (me + off) & 63;
            set_stream(4 * g + i, STREAM_SRC_W(SRC + 2 * m_per * d, 0), 0, STREAM_COUNT_W(m_per, SIZE_64, CTR_SRAM, MODE_SRC, off));
        }
        set_group(g, GROUP_LO_R(m_per), GROUP_HI(4 * g, 4, 0));   /* every count is m_per: R needs no reduction */
    }
    for (int s = 0; s < 64; s++) tables->PTR[s] = DST + 2 * m_per * s;   /* source s's block lands at DST + s blocks */
    expected = 64 * m_per;
    tile->DMA_CONFIG = DMA_CFG_RUN(RUN_GROUPS, 0, 16);
}
static void a2a_uniform_start(void) {
    tile->WRITE_COUNTER[CTR_SRAM - 1] = expected;   /* identical code from waking to the start store (6.3) */
    start_run();
    wait_all(expected ? EV_CTR(CTR_SRAM) | EV_RUN_DONE : EV_RUN_DONE);
}
/* END */

int main(void) {
    uint32_t hi = 0, lo = 0;
    for (;;) {
        uint32_t w = wait_for_command();
        volatile uint32_t *arg = CMD_ARGS(w);
        switch (CMD_TYPE(w)) {
        case 4: a2a_uniform_start(); break;
        case 1: seed_from_cnc(arg, &hi, &lo); n_rec = arg[2]; fill(SRC, n_rec, &hi, &lo); break;
        case 2: seed_from_index(arg, &hi, &lo); n_rec = arg[2]; fill(SRC, n_rec, &hi, &lo); break;
        case 3: a2a_uniform_configure(n_rec); break;
        case 5: hash_block(arg[0], arg[1], &hi, &lo); sig[0] = lo; sig[1] = hi; break;
        case 6: result_to_cc1(sig[1], sig[0]); break;
        case 7: write_block(WB, blk, BLK); break;
        default: sim_exit(0);
        }
    }
}
