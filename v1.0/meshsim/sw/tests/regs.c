/* Register test (spec 8, 4.10-4.12): write-only reads, counter widths, the table window (HIST read back here; PTR,
 * STREAM and GROUP are write-only, so test/CtrlTest reads them through the simulator), the send port's auto-increment
 * and ROUTER_SET_HI clearing, self-sends in modes 2, 5, 6 and 7 with the local wake, status bits. */
#include "mesh.h"
volatile uint32_t r[64];
volatile uint32_t replies[4];
volatile uint32_t buf[8];
int main(void) {
    unsigned me = tile_index(), n = 0;
    /* write-only registers read 0 */
    DMA_CONFIG = 0x00BCDEF; DMA_RECORD_BASE_ADDRESS = 0x54321; ROUTER_SET_HDR = ROUTER_HDR(me, MODE_SRC, 0); ROUTER_SET_HI = 0x77;
    r[n++] = DMA_CONFIG; r[n++] = DMA_PULSE; r[n++] = DMA_RECORD_BASE_ADDRESS; r[n++] = SRAM_RD_DEST_ADDR;
    r[n++] = ROUTER_SET_HDR; r[n++] = ROUTER_SET_HI; r[n++] = ROUTER_SEND_LO;                                  /* r[0..6] = 0 */
    /* the down-counters: 20 bits, read/write */
    DMA_SRAM_REMAINING = 0x12345; DMA_CM_REMAINING = 0xFFFFFFFF; DMA_REPLY_REMAINING = 0x3;
    r[n++] = DMA_SRAM_REMAINING; r[n++] = DMA_CM_REMAINING; r[n++] = DMA_REPLY_REMAINING;              /* r[7..9] */
    DMA_SRAM_REMAINING = 0; DMA_CM_REMAINING = 0; DMA_REPLY_REMAINING = 0;
    /* the table window: HIST 17 bits (read back); PTR 20 bits, STREAM and GROUP 64 bits (write-only: CtrlTest reads them) */
    HIST[5] = 0xFFFFFFFF; PTR[me ^ 1] = 0xFFFFFFFF; set_stream(3, 0xDEADBEEF, 0x12345678); set_group(63, 0x0BADF00D, 0xFEEDFACE);
    r[n++] = HIST[5]; n += 5;                                                              /* r[10]; r[11..15] unused */
    HIST[5] = 0;
    /* mode 7 to itself: three replies, auto-increment +1, then the local wake */
    DMA_REPLY_REMAINING = 3;
    ROUTER_SET_HDR = ROUTER_HDR(me, MODE_REPLY, CM_WORD((uint32_t)&replies[0]));
    send32(100 + me); send32(200 + me); send32(300 + me);
    wfi();
    r[n++] = replies[0]; r[n++] = replies[1]; r[n++] = replies[2]; r[n++] = DMA_REPLY_REMAINING;      /* r[16..19] */
    /* mode 5 to itself: auto-increment +2; ROUTER_SET_HI is cleared by each send */
    DMA_CM_REMAINING = 2;
    ROUTER_SET_HDR = ROUTER_HDR(me, MODE_CMEM, CM_WORD((uint32_t)&buf[0]));
    send64(0xAAAA0000 + me, 0x11110000 + me);
    send32(0x22220000 + me);
    wfi();
    r[n++] = buf[0]; r[n++] = buf[1]; r[n++] = buf[2]; r[n++] = buf[3];                    /* r[20..23] */
    /* mode 6 to itself: mailbox slots 64, 65 (+1 each); no counter, so poll cm_idle */
    ROUTER_SET_HDR = ROUTER_HDR(me, MODE_MBOX, 64);
    send32(0x55500000 + me); send32(0x55510000 + me);
    for (int i = 0; i < 20; i++) (void)status();
    while (!(status() & TILE_ST_CM_IDLE(1))) ;
    r[n++] = MBOX[64]; r[n++] = MBOX[65];                                                  /* r[24..25] */
    /* mode 2 to itself, at PTR[me] */
    PTR[me] = 0x100;
    DMA_SRAM_REMAINING = 2;
    ROUTER_SET_HDR = ROUTER_HDR(me, MODE_SRC, 0);
    send64(0xB0B00000 + me, 0xA0A00000 + me);
    send64(0xD0D00000 + me, 0xC0C00000 + me);
    wfi();
    n++;                                                                                   /* r[26] unused: CtrlTest reads PTR[me] = 0x104 */
    uint32_t s = status();
    r[n++] = s;                                                                            /* r[27] */
    DMA_PULSE = DMA_PULSE_DONE(1);
    r[n++] = status();                                                                     /* r[28]: done set */
    r[n] = n;
    return 0;
}
