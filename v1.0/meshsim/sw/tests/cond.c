/* M14: the conditions (spec 4.13), the done gate (4.12), the error line and REPORT (6.6), provoked from C (CondTest).
 * Command type = case; arguments 0, 1 = a 64-bit mask of the tiles that act (the others do nothing).
 *   0 drift: a group run whose stream reads and writes one area (the writes first: the read queue refuses the AG)
 *   1 router overflow: the four neighbours of tile 9 each stream 200 records to it at once (its eject FIFOs)
 *   2 write-engine input overflow: tile 8 streams 40 records to tile 9, which lands them at a chunk = 3 mod 4
 *     (every write splits in two, so the write engine takes one packet per 2 cycles)
 *   3 read-port overflow: 65 reads queued at once      4 unexpected arrival: a mode-7 word to itself, counter 0
 *   5 (table request overflow: injected by CondTest; the engines can't overflow it in the model)
 *   6 bus error: a store to TILE_STATUS (the controller halts)    7 illegal instruction: div (halts)
 *   8 (GATHER queue overflow: injected by CondTest; a tile's own sources can't outrun its GATHER engine in the model)
 *   9 bad mode: a packet with mode 0       10 done early: done with a counter non-zero
 *  12 the GATHER probe: 64 requests, k = 8, from the send port at its full rate: the queue's peak (no overflow)
 *  11 done early: done with DMA_REPLY_REMAINING = 1 (the word, if any, comes later: case 20)
 *  20 a late arrival after done: the acting tiles send one mode-7 word to me + 1, after it is back in wait_for_command()
 *  21 paced packets to C&C: argument 2 = packets per tile, sent back to back after (me + 1) x 4 cycles
 *  22 mixed, for REPORT: tile me does case (0, 4, 9, 10, 11, 3)[me % 6] (0 = nothing); tile 63 a bus error
 *  30 record TILE_CONDITIONS in cnd       CMD_REPORT: REPORT       0x7F exit */
// build: -O2
#define RCP_REPORT
#include "rcp.h"
volatile uint32_t cnd, sink[256], res[80];
#define A0 0x00000             /* area 0 */
#define A1 0x20000             /* area 1 */

static void abandon_sram(void) {               /* traffic was dropped: wait until it has all landed, then give up */
    spin(1500);
    while (status() & TILE_ST_GATHER_BUSY(1)) ;
    while (!(status() & TILE_ST_MEM_IDLE(1))) ;
    DMA_SRAM_REMAINING = 0;
}
static void act(unsigned c, unsigned me, volatile uint32_t *arg) {
    switch (c) {
    case 0:                                    /* drift: 64 records from A0 to A0 + 0x1000 (one area), one group */
        wait_tables();
        PTR[me] = A0 + 0x1000;
        set_stream(0, STREAM_LO(A0, 0), STREAM_HI(64, 0, MODE_SRC, SIZE_64, 0));
        set_group(0, GROUP_LO_R(64), GROUP_HI(0, 1, 0));
        DMA_SRAM_REMAINING = 64;
        DMA_CONFIG = DMA_CFG_RUN(RUN_GROUPS, 0, 1);
        DMA_PULSE = DMA_PULSE_START(1);
        wfi();
        break;
    case 1:
        if (me == 9) {                         /* the receiver: 4 x 200 records, one area per source; some dropped */
            wait_tables();
            PTR[8] = A0; PTR[10] = A1; PTR[1] = 2 * A1; PTR[17] = 3 * A1;
            DMA_SRAM_REMAINING = 0xFFFFF;
            abandon_sram();
        } else {                               /* 8, 10, 1, 17: a 200-record stream run to tile 9, one packet a cycle */
            spin(40);
            wait_tables();
            set_stream(0, STREAM_LO(A0, 0), STREAM_HI(200, (9 - me) & 63, MODE_SRC, SIZE_64, 0));
            DMA_CONFIG = DMA_CFG_RUN(RUN_STREAMS, 0, 1);
            DMA_PULSE = DMA_PULSE_START(1);
            while (!(status() & TILE_ST_RD_DONE(1))) ;
        }
        break;
    case 2:
        if (me == 9) {
            wait_tables();
            PTR[8] = A1 + 3;                   /* = 3 mod 4: each 64-bit write spans two rows */
            DMA_SRAM_REMAINING = 0xFFFFF;
            abandon_sram();
        } else {                               /* tile 8 */
            spin(40);
            wait_tables();
            set_stream(0, STREAM_LO(A0, 0), STREAM_HI(40, 1, MODE_SRC, SIZE_64, 0));
            DMA_CONFIG = DMA_CFG_RUN(RUN_STREAMS, 0, 1);
            DMA_PULSE = DMA_PULSE_START(1);
            while (!(status() & TILE_ST_RD_DONE(1))) ;
        }
        break;
    case 3:                                    /* a local copy keeps the SRAM read queue busy (the AG outranks the
                                                  read port), so 65 reads queued meanwhile overflow its 64 entries */
        wait_tables();
        PTR[me] = A1;
        set_stream(0, STREAM_LO(A0, 0), STREAM_HI(1024, 0, MODE_SRC, SIZE_64, 0));
        DMA_CONFIG = DMA_CFG_RUN(RUN_STREAMS, 0, 1);
        DMA_SRAM_REMAINING = 1024;
        DMA_REPLY_REMAINING = 64;
        SRAM_RD_DEST_ADDR = CM_WORD((uint32_t)&res[0]);
        DMA_PULSE = DMA_PULSE_START(1);
        while (!(status() & TILE_ST_RD_RUNNING(1))) ;
        spin(4);
        for (int i = 0; i < 65; i++) SRAM_RD_ADDR_QUEUE = A0 + 0x8000 + i;   /* the 65th finds the queue full: dropped */
        wfi();
        break;
    case 4:
        ROUTER_SET_HDR = ROUTER_HDR(me, MODE_REPLY, CM_WORD((uint32_t)&sink[0]));
        send32(1);
        spin(10);
        break;
    case 6: TILE_STATUS = 1; break;
    case 7: __asm__ volatile (".word 0x02c5c533"); break;          /* div a0, a1, a2 */
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
    case 9:
        ROUTER_SET_HDR = ROUTER_HDR(me, 0, 0);
        send32(1);
        spin(10);
        break;
    case 10:
        DMA_REPLY_REMAINING = 1;                   /* a word is expected (case 20 may send it later) ... */
        break;                                 /* ... and straight to done: done early */
    case 20:
        spin(200);                             /* well after tile me + 1 is back in wait_for_command() */
        ROUTER_SET_HDR = ROUTER_HDR((me + 1) & 63, MODE_REPLY, CM_WORD((uint32_t)&sink[2]));
        send32(0x1A7E);
        break;
    }
}
int main(void) {
    static const uint8_t mixed[5] = { 0, 4, 9, 10, 3 };
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
            if (me == 63) act(6, me, arg);
            else {
                unsigned q = me, r = 0;                 /* me % 5, no divider */
                while (q >= 5) q -= 5;
                r = mixed[q];
                if (r) act(r, me, arg);
            }
        } else act(k, me, arg);
    }
}
