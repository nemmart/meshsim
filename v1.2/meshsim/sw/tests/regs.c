/* Register test (spec 8, 4.10-4.12; mesh.h 1.2): write-only registers read 0 and the configuration registers read back;
 * the counters' additive writes and 20-bit wrap; the table window (HIST read back here; PTR, the STREAM arrays and GROUP
 * are write-only, so test/CtrlTest reads them through the simulator); the send port (ROUTER_SET_W0-W2 below the sending
 * word, the send register supplying the top word, words beyond the size 0, W0-W2 cleared at every send, ROUTER_ADDR
 * + 16, 4, 8 at mode 0 and fixed under no-increment); self-sends in modes 0 and 1 with counters done; the events'
 * registers at reset; STATUS. */
#include "mesh.h"
volatile uint32_t r[64];
volatile uint32_t buf[8] __attribute__((aligned(16)));
int main(void) {
    unsigned me = tile_index(), n = 0;
    /* write-only registers read 0; the configuration registers read back what was stored (8.1) */
    tile->DMA_CONFIG = 0x00BCDEF; tile->DMA_RECORD_BASE_ADDRESS = 0x54321; tile->ROUTER_HDR = ROUTER_HDR_W(me, MODE_SRC, 1) | HDR_SIZE(3) | 0x1FFFE;
    tile->ROUTER_SET_W0 = 0x70; tile->ROUTER_SET_W1 = 0x71; tile->ROUTER_SET_W2 = 0x72; tile->ROUTER_ADDR = 0x12345678;
    r[n++] = tile->DMA_CONFIG; r[n++] = tile->PULSE_DMA; r[n++] = tile->DMA_RECORD_BASE_ADDRESS; r[n++] = tile->SRAM_RD_DEST_ADDR;
    r[n++] = tile->ROUTER_HDR; r[n++] = tile->ROUTER_SET_W0; r[n++] = tile->ROUTER_SET_W1; r[n++] = tile->ROUTER_SET_W2;
    r[n++] = tile->ROUTER_SEND_32; r[n++] = tile->ROUTER_ADDR; r[n++] = tile->PULSE_DONE;   /* r[0..10]: 0xBCDEF, 0, 0x54321, 0, hdr without size and spare, 0, 0, 0, 0, 0x12345678, 0 */
    /* the write counters: 20 bits; a write adds (4.12); a sum past 2^20 - 1 wraps (meshsim flags it: CtrlTest expects 3) */
    tile->WRITE_COUNTER[0] = 0x12345; tile->WRITE_COUNTER[1] = 0xFFFFFFFF; tile->WRITE_COUNTER[2] = 0x3; tile->WRITE_COUNTER[2] = 0;
    r[n++] = tile->WRITE_COUNTER[0]; r[n++] = tile->WRITE_COUNTER[1]; r[n++] = tile->WRITE_COUNTER[2];   /* r[11..13]: 0x12345, 0xFFFFF, 3 */
    tile->WRITE_COUNTER[0] = 0x100000 - 0x12345; tile->WRITE_COUNTER[1] = 1; tile->WRITE_COUNTER[2] = 0xFFFFD;   /* each wraps to 0 */
    /* the table window: HIST 17 bits (read back); PTR 21 bits, the STREAM arrays and GROUP 32 bits a word (write-only) */
    tables->HIST[5] = 0xFFFFFFFF; tables->PTR[me ^ 1] = 0xFFFFFFFF; set_stream(3, 0xDEADBEEF, 0x12345678, 0x0BADBEEF); set_group(63, 0x0BADF00D, 0xFEEDFACE);
    r[n++] = tables->HIST[5];                                                              /* r[14] */
    tables->HIST[5] = 0;
    /* a 64-bit reply to itself lands at REPLY[2 me], [2 me + 1]; then a 32-bit one writes word 0 only (no-increment) */
    tile->WRITE_COUNTER[CTR_REPLY - 1] = 1;
    send_to(ROUTER_HDR_W(me, MODE_WRITE, CTR_REPLY) | ROUTER_NOINC, REPLY_WORD(me));
    send64(0xBBBB0000 + me, 0xAAAA0000 + me);
    wfi();
    r[n++] = REPLY[2 * me]; r[n++] = REPLY[2 * me + 1];                                    /* r[15..16] */
    tile->WRITE_COUNTER[CTR_REPLY - 1] = 1;
    send32(0xCCCC0000 + me);
    wfi();
    r[n++] = REPLY[2 * me]; r[n++] = REPLY[2 * me + 1];                                    /* r[17..18]: CCCC, BBBB */
    r[n++] = tile->ROUTER_ADDR;                                                            /* r[19]: REPLY_WORD(me), no-increment */
    /* mode 0 to itself at 128, 32 and 64 bits: ROUTER_ADDR + 16, + 4, + 8; W0-W2 cleared by each send; words beyond the
     * size 0 (the 64-bit write is at buf[6], 8-byte aligned) */
    buf[6] = 0x6666; buf[7] = 0x7777;
    tile->WRITE_COUNTER[CTR_CM - 1] = 3;
    send_to(ROUTER_HDR_W(me, MODE_WRITE, CTR_CM), (uint32_t)&buf[0]);
    send128(0xCCCC0000 + me, 0xBBBB0000 + me, 0xAAAA0000 + me, 0x11110000 + me);      /* buf[0..3] */
    tile->ROUTER_SET_W0 = 0xDDDD0000 + me; tile->ROUTER_SEND_32 = 0x22220000 + me;         /* buf[4]: W0 not sent, then cleared */
    tile->ROUTER_ADDR = (uint32_t)&buf[6];
    tile->ROUTER_SEND_64 = 0x33330000 + me;                                                 /* buf[6] = 0 (W0 cleared), buf[7] = 3333 */
    wfi();
    for (int i = 0; i < 8; i++) r[n++] = buf[i];                                           /* r[20..27]; buf[5] untouched */
    r[n++] = tile->ROUTER_ADDR;                                                            /* r[28]: &buf[8] */
    /* mode 1 to itself, at PTR[me]: 128, 64 and 32 bits land at their sizes, PTR += 4, 2, 1; ROUTER_ADDR unchanged */
    tables->PTR[me] = 0x100;
    tile->WRITE_COUNTER[CTR_SRAM - 1] = 3;
    tile->ROUTER_HDR = ROUTER_HDR_W(me, MODE_SRC, CTR_SRAM);
    send128(0xD3D30000 + me, 0xD2D20000 + me, 0xD1D10000 + me, 0xD0D00000 + me);
    send64(0xB0B00000 + me, 0xA0A00000 + me);
    send32(0xC0C00000 + me);
    wfi();
    r[n++] = tile->STATUS;                                                                 /* r[29] (CtrlTest reads PTR[me] = 0x107) */
    r[n++] = tile->EVENTS_MASK; r[n++] = tile->EVENTS_REQUIRED; r[n++] = tile->EVENTS_PEEK;  /* r[30..32]: [8] | [7], 0, nothing pending */
    r[n++] = tile->ROUTER_ADDR;                                                            /* r[33]: still &buf[8] (modes 1-5 leave it) */
    tile->PULSE_DONE = 1;
    r[n++] = tile->STATUS;                                                                 /* r[34]: done set */
    r[n] = n;                                                                              /* r[35] = 35 */
    return 0;
}
