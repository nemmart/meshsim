// build: -O2
/* Recipes: write a block, fill with pseudo-random data, hash, and the self-test that combines them with an all-to-all
 * and REPORT (spec 4.3, 4.9, 4.11, 6.6; selftest.h; meshsim M14). Commands:
 *   1 fill from C&C's seed: arguments seed_lo, seed_hi, n (one unicast command per tile); n records at SRC
 *   2 fill from the tile index: arguments base_lo, base_hi, n (broadcast); n records at SRC
 *   3 configure the all-to-all: n / 64 records from SRC to each tile, landing at DST in source order
 *   4 start it: counter, start, WFI
 *   5 hash: n records at the chunk in argument 0 (argument 1 = n); sig = the 64-bit hash
 *   6 signature: sig to C&C, paced (paced_to_cnc)
 *   7 write a block: n = argument 0 records from data memory (blk[]) to big SRAM at WB
 *   8 (for REPORT's test) the tiles in the 64-bit mask (arguments lo, hi) provoke an unexpected arrival
 *   CMD_REPORT: REPORT;   0 exit */
#define RCP_REPORT
#include "rcp.h"
#include "selftest.h"
#define SRC 0x00000            /* area 0: the filled block */
#define DST 0x40000            /* area 2: the all-to-all's receive buffer */
#define WB  0x80000            /* area 4: the block written from data memory */
#define BLK 256                /* records in blk[] */
static uint32_t n_rec, m_per, expected;
volatile uint32_t sig[2], state[2];
uint32_t blk[2 * BLK];

/* RECIPE write_block */
/* n 64-bit records from data memory (w[2i] the low word, w[2i + 1] the high) into big SRAM at chunk dst: mode 2 from the
 * send port to the tile itself, two stores per record (ROUTER_SET_HI, then ROUTER_SEND_LO, which sends). They land at PTR[me], which
 * advances by 2 per packet (spec 4.9, 4.11). PTR[me] is this tile's own landing pointer: set it only when no run uses it. */
static void write_block(uint32_t dst, const uint32_t *w, uint32_t n) {
    unsigned me = tile_index();
    wait_tables();
    PTR[me] = dst;
    DMA_SRAM_REMAINING = n;
    ROUTER_SET_HDR = ROUTER_HDR(me, MODE_SRC, 0);
    for (uint32_t i = 0; i < n; i++) send64(w[2 * i + 1], w[2 * i]);
    if (n) wfi();                                  /* every record written: DMA_SRAM_REMAINING = 0 and mem_idle */
}
/* END */

/* RECIPE fill */
/* n pseudo-random 64-bit records into big SRAM at chunk dst: selftest.h's xorshift64, generated in registers and sent
 * like write_block(), with no data memory in between. (hi, lo) is the generator's state, seeded by st_seed() or
 * st_tile_seed(); it is left where the block ended, so a second call continues the sequence. */
static void fill(uint32_t dst, uint32_t n, uint32_t *hi, uint32_t *lo) {
    unsigned me = tile_index();
    uint32_t h = *hi, l = *lo;
    wait_tables();
    PTR[me] = dst;
    DMA_SRAM_REMAINING = n;
    ROUTER_SET_HDR = ROUTER_HDR(me, MODE_SRC, 0);
    for (uint32_t i = 0; i < n; i++) { st_next(&h, &l); send64(h, l); }
    *hi = h; *lo = l;
    if (n) wfi();
}
/* END */

/* RECIPE seeds */
/* The two ways to seed. From C&C: one unicast command per tile, the 64-bit seed as two arguments. From the tile index:
 * one broadcast command whose two arguments are a base seed (0 for none), mixed with the index by st_tile_seed(). */
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
 * is folded the next is in flight. Each batch counts in DMA_REPLY_REMAINING; the store for the next batch comes after the
 * WFI that consumed the last one. A step of its own: no packets may arrive for this tile meanwhile (CONVENTIONS 9). */
static void hash_block(uint32_t src, uint32_t n, uint32_t *hi, uint32_t *lo) {
    static volatile uint32_t buf[2][32];
    uint32_t h0 = ST_HASH_H0, h1 = ST_HASH_H1, words = 2 * n, issued = 0, folded = 0;
    unsigned k = 0;
    while (folded < words) {
        uint32_t cur = issued - folded;            /* the batch in flight to buf[k] (0 the first time) */
        if (cur) wfi();                            /* it has landed */
        if (issued < words) {                      /* the next batch, to the other buffer */
            uint32_t m = words - issued < 32 ? words - issued : 32;
            DMA_REPLY_REMAINING = m;
            SRAM_RD_DEST_ADDR = CM_WORD((uint32_t)&buf[cur ? k ^ 1 : k][0]);
            for (uint32_t i = 0; i < m; i++) SRAM_RD_ADDR_QUEUE = src + issued + i;
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
 * signature to C&C (command 6), which compares it with the simulator's. The all-to-all's configure and start: */
static void a2a_uniform_configure(uint32_t n) {
    unsigned me = tile_index();
    m_per = n >> 6;
    wait_tables();
    for (int g = 0; g < 16; g++) {
        for (int i = 0; i < 4; i++) {
            unsigned off = SCHED[g][i], d = (me + off) & 63;
            set_stream(4 * g + i, STREAM_LO(SRC + 2 * m_per * d, 0), STREAM_HI(m_per, off, MODE_SRC, SIZE_64, 0));
        }
        set_group(g, GROUP_LO_R(m_per), GROUP_HI(4 * g, 4, 0));   /* every count is m_per: R needs no reduction */
    }
    for (int s = 0; s < 64; s++) PTR[s] = DST + 2 * m_per * s;   /* source s's block lands at DST + s blocks */
    expected = 64 * m_per;
    DMA_CONFIG = DMA_CFG_RUN(RUN_GROUPS, 0, 16);
}
static void a2a_uniform_start(void) {
    DMA_SRAM_REMAINING = expected;                     /* identical code from waking to the start store (6.3) */
    start_run();
    if (expected) wfi();
}
/* END */

int main(void) {
    unsigned me = tile_index();
    for (int i = 0; i < BLK; i++) { blk[2 * i] = me << 16 | i; blk[2 * i + 1] = 0xB10C0000u | i; }
    for (;;) {
        uint32_t w = wait_for_command();
        volatile uint32_t *arg = CMD_ARGS(w);
        uint32_t hi, lo;
        switch (CMD_TYPE(w)) {
        case 4: a2a_uniform_start(); break;        /* first: the shortest path to the start store */
        case 6: paced_to_cnc(sig[1], sig[0]); break;
        case CMD_REPORT: report(); break;
        case 1: seed_from_cnc(arg, &hi, &lo); n_rec = arg[2]; fill(SRC, n_rec, &hi, &lo); state[1] = hi; state[0] = lo; break;
        case 2: seed_from_index(arg, &hi, &lo); n_rec = arg[2]; fill(SRC, n_rec, &hi, &lo); state[1] = hi; state[0] = lo; break;
        case 3: a2a_uniform_configure(n_rec); break;
        case 5: hash_block(arg[0], arg[1], &hi, &lo); sig[1] = hi; sig[0] = lo; break;
        case 7: write_block(WB, blk, arg[0]); break;
        case 8:
            if ((me < 32 ? arg[0] >> me : arg[1] >> (me - 32)) & 1) {
                ROUTER_SET_HDR = ROUTER_HDR(me, MODE_REPLY, CM_WORD((uint32_t)&state[0]));   /* DMA_REPLY_REMAINING is 0: unexpected */
                send32(1);
                spin(10);
            }
            break;
        default: return 0;
        }
    }
}
