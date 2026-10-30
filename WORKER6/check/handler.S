/* Recipe 24: the error handler (D20, D21). Report the fixed set to C&C 1 over the request bus, then park.
 * It never writes data memory and needs no stack, so the boot ROM can hold the same code (D22, D36). Every value is
 * sent before the register that held it is reused: x1-x3 are free once their own report has gone. */
#include "../../wip/sw/mesh-v1.2-draft.h"
#define W0   X0REL(OFF_REQUEST_SET_W0)
#define W1   X0REL(OFF_REQUEST_SET_W1)
#define W2   X0REL(OFF_REQUEST_SET_W2)
#define SEND X0REL(OFF_REQUEST_SEND)
#define HDR(seq) ((1 << 10) | ((seq) << 18))      /* REQ_CC1(REQ_OP_REPORT, seq): target 0 (C&C 1), op 1; placeholder */
/* report seq carrying three registers ra, rb, rc; x1 is free once the first report's words are in the port */
#define REPORT_REGS(seq, ra, rb, rc)  sw ra, W1(zero); sw rb, W2(zero); li x1, HDR(seq); sw x1, W0(zero); sw rc, SEND(zero)
/* report seq carrying three words already in x1-x3 (x3 last: its store sends), with the header built in x4 */
#define REPORT_123(seq)  sw x1, W1(zero); sw x2, W2(zero); li x4, HDR(seq); sw x4, W0(zero); sw x3, SEND(zero)

    .section .text.handler, "ax"
    .balign 4
    .globl trap_handler
/* RECIPE handler */
trap_handler:                           /* mtvec points here; MIE is now 0 and is never set again (D20) */
    sw   x1, W1(zero)                   /* report 0: x1, x2, x3. The main code may have left the port's words */
    sw   x2, W2(zero)                   /* half written (D20, W1-04): every report writes all four */
    li   x1, HDR(0)
    sw   x1, W0(zero)
    sw   x3, SEND(zero)                 /* stalls only while the request FIFO is full (4.18) */
    REPORT_REGS(1,  x4,  x5,  x6)
    REPORT_REGS(2,  x7,  x8,  x9)
    REPORT_REGS(3,  x10, x11, x12)
    REPORT_REGS(4,  x13, x14, x15)
    REPORT_REGS(5,  x16, x17, x18)
    REPORT_REGS(6,  x19, x20, x21)
    REPORT_REGS(7,  x22, x23, x24)
    REPORT_REGS(8,  x25, x26, x27)
    REPORT_REGS(9,  x28, x29, x30)
    mv   x1, x31                        /* report 10: x31, mepc, mcause */
    csrr x2, mepc
    csrr x3, mcause
    REPORT_123(10)
    csrr x1, mtval                      /* report 11: mtval, PARAMS, STATUS */
    lw   x2, X0REL(OFF_PARAMS)(zero)
    lw   x3, X0REL(OFF_STATUS)(zero)
    REPORT_123(11)
    lw   x1, X0REL(OFF_ERRORS)(zero)    /* report 12: ERRORS, EVENTS_PEEK (never EVENTS: it consumes), counter 1 */
    lw   x2, X0REL(OFF_EVENTS_PEEK)(zero)
    lw   x3, X0REL(OFF_WRITE_COUNTER_1)(zero)
    REPORT_123(12)
    lw   x1, X0REL(OFF_WRITE_COUNTER_1 + 4)(zero)    /* report 13: counters 2-4 */
    lw   x2, X0REL(OFF_WRITE_COUNTER_1 + 8)(zero)
    lw   x3, X0REL(OFF_WRITE_COUNTER_1 + 12)(zero)
    REPORT_123(13)
    /* reports 14-22: three per peripheral, present or not, so the count is fixed (D21; FINDINGS F3). An absent
     * peripheral's window is a bus error (D10), so PARAMS [2:0] decides, and its reports carry zeros. */
    lw   x5, X0REL(OFF_PARAMS)(zero)    /* x5: the present mask; x6: the window's x0-relative base; x7: seq */
    li   x6, X0REL(0x200)
    li   x7, 14
1:  andi x8, x5, 1
    li   x1, 0
    li   x2, 0
    li   x3, 0
    beqz x8, 2f
    add  x9, x6, zero                   /* the window: PARAMS, STATUS, ERRORS (words 0-2) */
    lw   x1, 0(x9)
    lw   x2, 4(x9)
    lw   x3, 8(x9)
2:  slli x4, x7, 18                     /* header for seq x7 */
    ori  x4, x4, 1 << 10
    sw   x1, W1(zero); sw x2, W2(zero); sw x4, W0(zero); sw x3, SEND(zero)
    addi x7, x7, 1
    li   x1, 0
    li   x2, 0
    li   x3, 0
    beqz x8, 3f
    lw   x1, 24(x9)                     /* EVENTS_PEEK (word 6), EVENTS_MASK (7), DMA_HDR (8) */
    lw   x2, 28(x9)
    lw   x3, 32(x9)
3:  slli x4, x7, 18
    ori  x4, x4, 1 << 10
    sw   x1, W1(zero); sw x2, W2(zero); sw x4, W0(zero); sw x3, SEND(zero)
    addi x7, x7, 1
    li   x1, 0
    beqz x8, 4f
    lw   x1, 36(x9)                     /* DMA_ADDR (word 9); two words spare */
4:  slli x4, x7, 18
    ori  x4, x4, 1 << 10
    sw   x1, W1(zero); sw zero, W2(zero); sw x4, W0(zero); sw zero, SEND(zero)
    addi x7, x7, 1
    srli x5, x5, 1                      /* the next peripheral */
    addi x6, x6, 0x200
    li   x8, 23
    bltu x7, x8, 1b
park:
    j    park                           /* terminal: only the reset restarts the controller (D20) */
/* END */
