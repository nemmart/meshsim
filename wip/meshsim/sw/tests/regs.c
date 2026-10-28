/* Register test (spec 8, 4.10-4.12; mesh.h 1.1 at 26n): write-only reads, the counters' additive writes and 20-bit wrap,
 * the table window (HIST read back here; PTR, the STREAM arrays and GROUP are write-only, so test/CtrlTest reads them
 * through the simulator), the send port (ROUTER_SET_W0-W2 below the sending word, ROUTER_SEND_32/_64/_128 supplying the
 * top word, words beyond the size 0, W0-W2 cleared at every send, addr +1/+2/+4 for mode 6 and +1 for mode 7),
 * self-sends in modes 2, 6, 7 and 8 with counters done, the wake registers' resets, status bits. */
#include "mesh.h"
volatile uint32_t r[64];
volatile uint32_t buf[8];
int main(void) {
    unsigned me = tile_index(), n = 0;
    /* write-only registers read 0 */
    DMA_CONFIG = 0x00BCDEF; DMA_RECORD_BASE_ADDRESS = 0x54321; ROUTER_SET_HDR = ROUTER_HDR(me, MODE_SRC, 0);
    ROUTER_SET_W0 = 0x70; ROUTER_SET_W1 = 0x71; ROUTER_SET_W2 = 0x72;
    r[n++] = DMA_CONFIG; r[n++] = TILE_PULSE; r[n++] = DMA_RECORD_BASE_ADDRESS; r[n++] = SRAM_RD_DEST_ADDR;
    r[n++] = ROUTER_SET_HDR; r[n++] = ROUTER_SET_W0; r[n++] = ROUTER_SET_W1; r[n++] = ROUTER_SET_W2;
    r[n++] = ROUTER_SEND_32; r[n++] = ROUTER_SEND_64; r[n++] = ROUTER_SEND_128;                               /* r[0..10] = 0 */
    /* the down-counters: 20 bits; a write adds (4.12); a sum past 2^20 - 1 wraps (meshsim flags it: CtrlTest expects 3) */
    DMA_SRAM_REMAINING = 0x12345; DMA_CM_REMAINING = 0xFFFFFFFF; DMA_REPLY_REMAINING = 0x3; DMA_REPLY_REMAINING = 0;
    r[n++] = DMA_SRAM_REMAINING; r[n++] = DMA_CM_REMAINING; r[n++] = DMA_REPLY_REMAINING;              /* r[11..13]: 0x12345, 0xFFFFF, 3 */
    DMA_SRAM_REMAINING = 0x100000 - 0x12345; DMA_CM_REMAINING = 1; DMA_REPLY_REMAINING = 0xFFFFD;      /* each wraps to 0 */
    /* the table window: HIST 17 bits (read back); PTR 21 bits, the STREAM arrays and GROUP 32 bits a word (write-only:
     * CtrlTest reads them) */
    HIST[5] = 0xFFFFFFFF; PTR[me ^ 1] = 0xFFFFFFFF; set_stream(3, 0xDEADBEEF, 0x12345678, 0x0BADBEEF); set_group(63, 0x0BADF00D, 0xFEEDFACE);
    r[n++] = HIST[5];                                                                      /* r[14] */
    HIST[5] = 0;
    /* mode 8 to itself: a 64-bit reply lands at REPLY[2 me], [2 me + 1]; then a 32-bit one writes word 0 only */
    DMA_REPLY_REMAINING = 1;
    ROUTER_SET_HDR = ROUTER_HDR(me, MODE_REPLY, 0);
    send64(0xBBBB0000 + me, 0xAAAA0000 + me);
    wfi();
    r[n++] = REPLY[2 * me]; r[n++] = REPLY[2 * me + 1];                                    /* r[15..16] */
    DMA_REPLY_REMAINING = 1;
    send32(0xCCCC0000 + me);
    wfi();
    r[n++] = REPLY[2 * me]; r[n++] = REPLY[2 * me + 1];                                    /* r[17..18]: CCCC, BBBB */
    /* mode 6 to itself at 128, 32 and 64 bits: addr +4, +1, +2; W0-W2 cleared by each send; words beyond the size 0 */
    buf[6] = 0x6666; buf[7] = 0x7777;
    DMA_CM_REMAINING = 3;
    ROUTER_SET_HDR = ROUTER_HDR(me, MODE_CM, CM_WORD((uint32_t)&buf[0]));
    send128(0xCCCC0000 + me, 0xBBBB0000 + me, 0xAAAA0000 + me, 0x11110000 + me);      /* buf[0..3] */
    ROUTER_SET_W0 = 0xDDDD0000 + me; ROUTER_SEND_32 = 0x22220000 + me;                     /* buf[4]: W0 not sent, then cleared */
    ROUTER_SEND_64 = 0x33330000 + me;                                                       /* buf[5] = 0 (W0 cleared), buf[6] = 3333 */
    wfi();
    for (int i = 0; i < 8; i++) r[n++] = buf[i];                                           /* r[19..26]; buf[7] untouched */
    /* mode 7 to itself: command slots 14, 15 (+1 each); never counted, so poll cm_idle */
    ROUTER_SET_HDR = ROUTER_HDR(me, MODE_COMMAND, 14);
    send32(0x55500000 + me); send32(0x55510000 + me);
    for (int i = 0; i < 20; i++) (void)status();
    while (!(status() & TILE_ST_CM_IDLE(1))) ;
    r[n++] = COMMAND[14]; r[n++] = COMMAND[15];                                            /* r[27..28] */
    /* mode 2 to itself, at PTR[me]: 128, 64 and 32 bits land at their sizes, PTR += 4, 2, 1 */
    PTR[me] = 0x100;
    DMA_SRAM_REMAINING = 3;
    ROUTER_SET_HDR = ROUTER_HDR(me, MODE_SRC, 0);
    send128(0xD3D30000 + me, 0xD2D20000 + me, 0xD1D10000 + me, 0xD0D00000 + me);
    send64(0xB0B00000 + me, 0xA0A00000 + me);
    send32(0xC0C00000 + me);
    wfi();
    uint32_t s = status();
    r[n++] = s;                                                                            /* r[29] (CtrlTest reads PTR[me] = 0x107) */
    r[n++] = WAKE_MASK; r[n++] = WAKE_REQUIRED; r[n++] = WAKE_PEEK & ~WAKE_UART_TX;        /* r[30..32]: [8] | [9], 0, nothing pending */
    r[n++] = DMA_PERIPH_REMAINING;                                                         /* r[33]: 0 */
    TILE_PULSE = TILE_PULSE_DONE(1);
    r[n++] = status();                                                                     /* r[34]: done set */
    r[n] = n;                                                                              /* r[35] = 35 */
    return 0;
}
