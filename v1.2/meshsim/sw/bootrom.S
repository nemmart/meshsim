/* The boot ROM (spec 6.2, 4.13; v1.2), exactly as specified: one ROM for every unit at 0x02000000, the reset vector,
 * branching on PARAMS' endpoint id; then the ROM's report-and-park handler (the fixed set of 24 reports to C&C 1, no
 * stack, no data memory). The code is the recipes' (wip/sw/recipes/bootrom.S, recipe 22, and handler.S, recipe 24),
 * assembled against meshsim's generated mesh.h. sw/build.sh assembles it into sw/bootrom.hex (one 32-bit word per line,
 * from 0x02000000), which meshsim loads into every controller's ROM (Controller.bootRom). */
#include "mesh.h"
#define W0   X0REL(OFF_REQUEST_SET_W0)
#define W1   X0REL(OFF_REQUEST_SET_W1)
#define W2   X0REL(OFF_REQUEST_SET_W2)
#define SEND X0REL(OFF_REQUEST_SEND)
#define HDR(seq) ((1 << 10) | ((seq) << 21))      /* REQ_CC1(REQ_OP_REPORT, 0, seq): target 0, op 1, no reply; placeholder */
/* report seq carrying three registers ra, rb, rc; x1 is free once the first report's words are in the port */
#define REPORT_REGS(seq, ra, rb, rc)  sw ra, W1(zero); sw rb, W2(zero); li x1, HDR(seq); sw x1, W0(zero); sw rc, SEND(zero)
/* report seq carrying three words already in x1-x3 (x3 last: its store sends), with the header built in x4 */
#define REPORT_123(seq)  sw x1, W1(zero); sw x2, W2(zero); li x4, HDR(seq); sw x4, W0(zero); sw x3, SEND(zero)

    .section .text.handler, "ax"
    .balign 4
    .globl trap_handler

    .section .bootrom, "ax"
    .globl _reset
_reset:
    la   t0, trap_handler               /* 1. mtvec: the ROM's report-and-park handler (recipe 24) */
    csrw mtvec, t0
    lw   t1, X0REL(OFF_PARAMS)(zero)    /* 2. the endpoint id, PARAMS [26:20] */
    srli t1, t1, 20
    andi t1, t1, 0x7F
    li   t2, 64
    bgeu t1, t2, controller
    li   t3, 1                          /* 3. a tile: pulse done; "command landed" then raises READY */
3:  sw   t3, X0REL(OFF_PULSE_DONE)(zero)
1:  wfi                                 /* the done gate: only GO wakes it */
    lw   t4, X0REL(OFF_EVENTS)(zero)    /* consumes GO (the reset mask holds it), and returns [9] unconsumed */
    andi t5, t4, EV_GO
    beqz t5, 1b
    andi t5, t4, EV_COMMAND             /* D37.1: jump only if a command has landed. The event, not STATUS [5]: */
    beqz t5, 3b                         /* GO cleared the status bit. A GO for other tiles: done again, wait.
                                           The tile branch never consumes [9] (D41): its mask is the reset's */
    j    jump
controller:                             /* 4. C&C 1-3 (64-66): wait for "command landed" itself */
    li   t3, EV_COMMAND
    sw   t3, X0REL(OFF_EVENTS_MASK)(zero)
2:  wfi
    lw   t4, X0REL(OFF_EVENTS)(zero)    /* consumes [9] (the mask holds only it) */
    andi t5, t4, EV_COMMAND
    beqz t5, 2b
    li   t3, EV_RESET_MASK
    sw   t3, X0REL(OFF_EVENTS_MASK)(zero)
jump:                                   /* 5. slot 0, then slot 1 (the first command is even: D36), and jump */
    li   t6, COMMAND_BASE
    lw   t3, 0(t6)
    lw   t3, 4(t6)
    jr   t3

    .balign 4
    .globl trap_handler
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
    lw   x1, X0REL(OFF_WRITE_COUNTER_1 + 16)(zero)   /* report 14: counters 5-7 (D40) */
    lw   x2, X0REL(OFF_WRITE_COUNTER_1 + 20)(zero)
    lw   x3, X0REL(OFF_WRITE_COUNTER_1 + 24)(zero)
    REPORT_123(14)
    /* reports 15-23: three per peripheral, present or not, so the count is fixed (D21; FINDINGS F3). An absent
     * peripheral's window is a bus error (D10), so PARAMS [2:0] decides, and its reports carry zeros (D37.3). */
    lw   x5, X0REL(OFF_PARAMS)(zero)    /* x5: the present mask; x6: the window's x0-relative base; x7: seq */
    li   x6, X0REL(0x200)
    li   x7, 15
1:  andi x8, x5, 1
    li   x1, 0
    li   x2, 0
    li   x3, 0
    beqz x8, 2f
    add  x9, x6, zero                   /* the window: PARAMS, STATUS, ERRORS (words 0-2) */
    lw   x1, 0(x9)
    lw   x2, 4(x9)
    lw   x3, 8(x9)
2:  slli x4, x7, 21                     /* header for seq x7 */
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
3:  slli x4, x7, 21
    ori  x4, x4, 1 << 10
    sw   x1, W1(zero); sw x2, W2(zero); sw x4, W0(zero); sw x3, SEND(zero)
    addi x7, x7, 1
    li   x1, 0
    beqz x8, 4f
    lw   x1, 36(x9)                     /* DMA_ADDR (word 9); two words spare */
4:  slli x4, x7, 21
    ori  x4, x4, 1 << 10
    sw   x1, W1(zero); sw zero, W2(zero); sw x4, W0(zero); sw zero, SEND(zero)
    addi x7, x7, 1
    srli x5, x5, 1                      /* the next peripheral */
    addi x6, x6, 0x200
    li   x8, 24
    bltu x7, x8, 1b
park:
    j    park                           /* terminal: only the reset restarts the controller (D20) */
