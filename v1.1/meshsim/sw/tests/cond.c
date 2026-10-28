/* M14: the conditions (spec 4.13), the done gate (4.12), the error line and REPORT (6.6), provoked from C (CondTest).
 * v1.1: the cases follow TILE_CONDITIONS' v1.1 bit numbers (spec 8.3; v1.0's in brackets); 26n modes and counters.
 * Command type = case; arguments 0, 1 = a 64-bit mask of the tiles that act (the others do nothing).
 *   0 [1] router overflow: the four neighbours of tile 9 each stream 200 records to it at once (its eject FIFOs); tile 9
 *     can't count the dropped records, so it parks and CondTest recovers it with HALT (6.1)
 *   1 [9] bad mode or size: a packet with mode 0
 *   2 [4] unexpected arrival: a reply (mode 8) to itself, counter 0
 *   3 [0] drift: a group run whose stream reads and writes one area (the writes first: the read queue refuses the AG)
 *   4 [2] write-engine input overflow: tile 8 streams 40 records to tile 9, which lands them at a chunk = 3 mod 4
 *     (every write splits in two, so the write engine takes one packet per 2 cycles); tile 9 parks for HALT, as in case 0
 *   5 (table request overflow: injected by CondTest; the engines can't overflow it in the model)
 *   6 [8] (GATHER queue overflow: injected by CondTest; a tile's own sources can't outrun its GATHER engine in the model)
 *   7 [3] read-port error: 65 reads queued at once (an overflow)
 *   8 [10] done early: done with DMA_REPLY_REMAINING = 1 (the reply, if any, comes later: case 20)
 *   9 [6] bus error: a store to TILE_STATUS (the controller halts)    10 [7] illegal instruction: div (halts)
 *  12 the GATHER probe: 64 requests, k = 8, from the send port at its full rate: the queue's peak (no overflow)
 *  20 a late arrival after done: the acting tiles send one reply to me + 1, after it is back in wait_for_command()
 *  21 paced packets to C&C: argument 2 = packets per tile, sent back to back after (me + 1) x 4 cycles
 *  22 mixed, for REPORT: tile me does case (0, 2, 1, 8, 7)[me % 5] (the first: nothing); tile 63 a bus error (9)
 *  30 record TILE_CONDITIONS in cnd       CMD_REPORT: REPORT       0x7F exit */
// build: -O2
#include "mesh.h"
/* v1.1: cond.c no longer includes the recipe header rcp.h, which the software session rebuilds against mesh.h 1.1 (its
 * local_run() uses v1.0's STREAM and DMA_PULSE). These are verbatim copies of the helpers it took from there: spin(),
 * delay4() and the REPORT recipe (RECIPES 20). */
static inline void spin(int n) { for (volatile int i = 0; i < n; i++) ; }
/* A delay of exactly 4 n cycles on the controller (n >= 1): each iteration is addi, nop and a taken bnez (2 cycles with
 * its branch penalty); the last, not taken, is 3 + the call's own. The core's timing is deterministic (4.11). */
static inline void delay4(uint32_t n) {
    __asm__ volatile ("1:\n\taddi %0, %0, -1\n\tnop\n\tbnez %0, 1b" : "+r"(n) :: "memory");
}

static void paced_to_cnc(uint32_t hi, uint32_t lo) {
    delay4(tile_index() + 1);                      /* (me + 1) x 4 cycles: the same code on every tile */
    ROUTER_SET_HDR = ROUTER_HDR(DST_CNC, MODE_REPLY, 0);         /* C&C's port ignores the mode and the address */
    send64(hi, lo);
}
static void report(void) { paced_to_cnc(tile_index(), conditions()); }
volatile uint32_t cnd, sink[256], res[80];
#define A0 0x00000             /* area 0 */
#define A1 0x20000             /* area 1 */

static void abandon_sram(void) {               /* traffic was dropped: its counter can never reach 0. A counter is never
                                                  overwritten (4.12); the recovery is HALT, which clears it (6.1). Wait for
                                                  the traffic to land, record the conditions and park: CondTest halts the
                                                  tile and checks it comes back clean */
    spin(1500);
    while (status() & TILE_ST_GATHER_BUSY(1)) ;
    while (!(status() & TILE_ST_MEM_IDLE(1))) ;
    cnd = conditions();
    WAKE_MASK = 0;                             /* nothing can wake it: HALT is the way out */
    for (;;) wfi();
}
static void act(unsigned c, unsigned me, volatile uint32_t *arg) {
    switch (c) {
    case 3:                                    /* drift: 64 records from A0 to A0 + 0x1000 (one area), one group */
        wait_tables();
        PTR[me] = A0 + 0x1000;
        set_stream(0, STREAM_SRC_ADDR(A0), STREAM_DEST_ENTRY(0, 0), STREAM_COUNT_ENTRY(64, MODE_SRC, SIZE_64, 0));
        set_group(0, GROUP_LO_R(64), GROUP_HI(0, 1, 0));
        DMA_SRAM_REMAINING = 64;
        DMA_CONFIG = DMA_CFG_RUN(RUN_GROUPS, 0, 1);
        TILE_PULSE = TILE_PULSE_START(1);
        wfi();
        break;
    case 0:
        if (me == 9) {                         /* the receiver: 4 x 200 records, one area per source; some dropped */
            wait_tables();
            PTR[8] = A0; PTR[10] = A1; PTR[1] = 2 * A1; PTR[17] = 3 * A1;
            DMA_SRAM_REMAINING = 0xFFFFF;
            abandon_sram();
        } else {                               /* 8, 10, 1, 17: a 200-record stream run to tile 9, one packet a cycle */
            spin(40);
            wait_tables();
            set_stream(0, STREAM_SRC_ADDR(A0), STREAM_DEST_ENTRY(0, (9 - me) & 63), STREAM_COUNT_ENTRY(200, MODE_SRC, SIZE_64, 0));
            DMA_CONFIG = DMA_CFG_RUN(RUN_STREAMS, 0, 1);
            TILE_PULSE = TILE_PULSE_START(1);
            while (!(status() & TILE_ST_RD_DONE(1))) ;
        }
        break;
    case 4:
        if (me == 9) {
            wait_tables();
            PTR[8] = A1 + 3;                   /* = 3 mod 4: each 64-bit write spans two rows */
            DMA_SRAM_REMAINING = 0xFFFFF;
            abandon_sram();
        } else {                               /* tile 8 */
            spin(40);
            wait_tables();
            set_stream(0, STREAM_SRC_ADDR(A0), STREAM_DEST_ENTRY(0, 1), STREAM_COUNT_ENTRY(40, MODE_SRC, SIZE_64, 0));
            DMA_CONFIG = DMA_CFG_RUN(RUN_STREAMS, 0, 1);
            TILE_PULSE = TILE_PULSE_START(1);
            while (!(status() & TILE_ST_RD_DONE(1))) ;
        }
        break;
    case 7:                                    /* a local copy keeps the SRAM read queue busy (the AG outranks the
                                                  read port), so 65 reads queued meanwhile overflow its 64 entries */
        wait_tables();
        PTR[me] = A1;
        set_stream(0, STREAM_SRC_ADDR(A0), STREAM_DEST_ENTRY(0, 0), STREAM_COUNT_ENTRY(1024, MODE_SRC, SIZE_64, 0));
        DMA_CONFIG = DMA_CFG_RUN(RUN_STREAMS, 0, 1);
        DMA_SRAM_REMAINING = 1024;
        DMA_CM_REMAINING = 64;
        SRAM_RD_DEST_ADDR = SRAM_RD_DEST(CM_WORD((uint32_t)&res[0]), SRAM_RD_SIZE_32);
        TILE_PULSE = TILE_PULSE_START(1);
        while (!(status() & TILE_ST_RD_RUNNING(1))) ;
        spin(4);
        for (int i = 0; i < 65; i++) SRAM_RD_ADDR_QUEUE = A0 + 0x8000 + i;   /* the 65th finds the queue full: dropped */
        wfi();
        break;
    case 2:
        ROUTER_SET_HDR = ROUTER_HDR(me, MODE_REPLY, 0);
        send32(1);
        spin(10);
        break;
    case 9: TILE_STATUS = 1; break;
    case 10: __asm__ volatile (".word 0x02c5c533"); break;          /* div a0, a1, a2 */
    case 12:                                   /* the GATHER probe: 64 requests (ordinal 0), k = 8, from the send port
                                                  as fast as it goes; the queue's peak is measured (no condition) */
        wait_tables();
        DMA_RECORD_BASE_ADDRESS = A1;
        PTR[me] = A0 + 0x10000;
        DMA_CONFIG = DMA_CFG_KM1(7);
        DMA_SRAM_REMAINING = 64 * 8;
        ROUTER_SET_HDR = ROUTER_HDR(me, MODE_GATHER, 0);
        for (int i = 0; i < 64; i++) send64(0, i);
        wfi();
        break;
    case 1:
        ROUTER_SET_HDR = ROUTER_HDR(me, 0, 0);
        send32(1);
        spin(10);
        break;
    case 8:
        DMA_REPLY_REMAINING = 1;                   /* a word is expected (case 20 may send it later) ... */
        break;                                 /* ... and straight to done: done early */
    case 20:
        spin(200);                             /* well after tile me + 1 is back in wait_for_command() */
        ROUTER_SET_HDR = ROUTER_HDR((me + 1) & 63, MODE_REPLY, 0);
        send32(0x1A7E);
        break;
    }
}
int main(void) {
    static const uint8_t mixed[5] = { 0, 2, 1, 8, 7 };   /* nothing, unexpected, bad mode or size, done early, read-port error */
    unsigned me = tile_index();
    for (;;) {
        uint32_t w = wait_for_command(), k = CMD_TYPE(w);
        volatile uint32_t *arg = CMD_ARGS(w);
        if (k == CMD_REPORT) { report(); continue; }
        if (k == 21) {
            uint32_t n = arg[2];
            delay4(me + 1);                    /* the replies paced 4 cycles apart, in tile order */
            ROUTER_SET_HDR = ROUTER_HDR(DST_CNC, MODE_REPLY, 0);
            for (uint32_t i = 0; i < n; i++) send64(me, i);
            continue;
        }
        if (k == 30) { cnd = conditions(); continue; }
        if (k == 0x7F) return 0;
        if (!((me < 32 ? arg[0] >> me : arg[1] >> (me - 32)) & 1)) continue;
        if (k == 22) {
            if (me == 63) act(9, me, arg);                  /* bus error */
            else {
                unsigned q = me, r = 0;                 /* me % 5, no divider */
                while (q >= 5) q -= 5;
                r = mixed[q];
                if (r) act(r, me, arg);
            }
        } else act(k, me, arg);
    }
}
