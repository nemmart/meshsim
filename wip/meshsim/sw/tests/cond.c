/* The errors (spec 4.13, 6.1; v1.2), provoked from C (CondTest). The image enables interrupts at start-up (boot part 2's
 * job, 6.2), leaving mtvec at the boot ROM's handler, so an error raises the hard interrupt and the handler sends its
 * 24 reports to C&C 1 and parks; a bus error or an illegal instruction traps at once. Command type = the case; arguments
 * 0, 1 = a 64-bit mask of the tiles that act (the others go back to wait_for_command()).
 *   0 router overflow: the four neighbours of tile 9 each stream 200 records to it at once (its eject FIFOs)
 *   1 bad packet: a 64-bit mode-0 packet to itself at a misaligned address
 *   2 unexpected arrival: a count-only packet to itself on counter 3, which is at 0
 *   3 drift: a group run whose stream reads and writes one area (the writes first: the read queue refuses the AG)
 *   7 read-port error: 65 reads queued while a local copy holds the SRAM read queue
 *   8 done early: counter 3 armed, then straight to done
 *   9 bus error: a store to STATUS              10 illegal instruction: div
 *  14 receive underflow: a load of REQUEST_RECEIVE (a tile has no receive queue)
 *  0x7F exit */
// build: -O2
#include "mesh.h"
static inline void spin(int n) { for (volatile int i = 0; i < n; i++) ; }
volatile uint32_t res[80] __attribute__((aligned(16)));
#define A0 0x00000             /* area 0 */
#define A1 0x20000             /* area 1 */

static void act(unsigned c, unsigned me) {
    switch (c) {
    case 0:
        if (me == 9) {                         /* the receiver: 4 x 200 records, one area per source; some dropped */
            wait_rd_idle();
            tables->PTR[8] = A0; tables->PTR[10] = A1; tables->PTR[1] = 2 * A1; tables->PTR[17] = 3 * A1;
            tile->WRITE_COUNTER[CTR_SRAM - 1] = 800;
            wfi();                             /* the overflow's error interrupts it here */
        } else {                               /* 8, 10, 1, 17: a 200-record stream run to tile 9, one packet a cycle */
            spin(40);
            wait_rd_idle();
            set_stream(0, STREAM_SRC_W(A0, 0), 0, STREAM_COUNT_W(200, SIZE_64, CTR_SRAM, MODE_SRC, (9 - me) & 63));
            tile->DMA_CONFIG = DMA_CFG_RUN(RUN_STREAMS, 0, 1);
            start_run();
            wait_rd_idle();
        }
        break;
    case 1:
        send_to(ROUTER_HDR_W(me, MODE_WRITE, CTR_CM), (uint32_t)&res[1]);   /* 8-byte packet at a 4-byte boundary */
        send64(1, 2);
        spin(40);
        break;
    case 2:
        send_to(ROUTER_HDR_W(me, MODE_COUNT, 3), 0);
        send32(0);
        spin(40);
        break;
    case 3:                                    /* drift: 64 records from A0 to A0 + 0x1000 (one area), one group */
        wait_rd_idle();
        tables->PTR[me] = A0 + 0x1000;
        set_stream(0, STREAM_SRC_W(A0, 0), 0, STREAM_COUNT_W(64, SIZE_64, CTR_SRAM, MODE_SRC, 0));
        set_group(0, GROUP_LO_R(64), GROUP_HI(0, 1, 0));
        tile->WRITE_COUNTER[CTR_SRAM - 1] = 64;
        tile->DMA_CONFIG = DMA_CFG_RUN(RUN_GROUPS, 0, 1);
        start_run();
        wfi();
        break;
    case 7:                                    /* a local copy keeps the SRAM read queue busy (the AG outranks the read port) */
        wait_rd_idle();
        tables->PTR[me] = A1;
        set_stream(0, STREAM_SRC_W(A0, 0), 0, STREAM_COUNT_W(1024, SIZE_64, CTR_SRAM, MODE_SRC, 0));
        tile->DMA_CONFIG = DMA_CFG_RUN(RUN_STREAMS, 0, 1);
        tile->WRITE_COUNTER[CTR_SRAM - 1] = 1024;
        tile->WRITE_COUNTER[CTR_CM - 1] = 64;
        tile->SRAM_RD_DEST_ADDR = RD_DEST((uint32_t)&res[0], SIZE_32, CTR_CM);
        start_run();
        while (!(tile->STATUS & ST_RD_RUNNING)) ;
        spin(4);
        for (int i = 0; i < 65; i++) tile->SRAM_RD_ADDR_QUEUE = A0 + 0x8000 + i;   /* the 65th: dropped */
        wfi();
        break;
    case 8: tile->WRITE_COUNTER[CTR_REPLY - 1] = 1; break;          /* then wait_for_command()'s done: done early */
    case 9: *(volatile uint32_t *)&tile->STATUS = 1; break;
    case 10: __asm__ volatile (".word 0x02c5c533"); break;           /* div a0, a1, a2 */
    case 14: (void)tile->REQUEST_RECEIVE; spin(40); break;
    }
}
int main(void) {
    unsigned me = tile_index();
    __asm__ volatile ("csrs mie, %0" :: "r"(1u << 11));             /* mie.MEIE */
    __asm__ volatile ("csrsi mstatus, 8");                           /* mstatus.MIE: from here an error traps (mtvec: the ROM's handler) */
    for (;;) {
        uint32_t w = wait_for_command(), k = CMD_TYPE(w);
        volatile uint32_t *arg = CMD_ARGS(w);
        if (k == 0x7F) return 0;
        if (!((me < 32 ? arg[0] >> me : arg[1] >> (me - 32)) & 1)) continue;
        act(k, me);
    }
}
