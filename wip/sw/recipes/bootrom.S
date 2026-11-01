/* Recipe 22: the boot ROM, as D36 writes it with D37.1's change. One ROM for every unit, at 0x02000000, the reset vector.
 * It never writes data memory; it reads only the command block, after the command has landed (D31). v1.25
 * (DECISIONS-v1.25 s8d, spec 4.13): the ROM is 256 bytes (64 words); its handler sends a two-report dump (sequence 24
 * and 25) and parks, and the full
 * 24-report set is the image's handler (recipe 24, in program memory, installed by boot part 2). */
#include "../mesh-v1.2-draft.h"
#define W0   X0REL(OFF_REQUEST_SET_W0)
#define W1   X0REL(OFF_REQUEST_SET_W1)
#define W2   X0REL(OFF_REQUEST_SET_W2)
#define SEND X0REL(OFF_REQUEST_SEND)
#define HDR(seq) ((1 << 10) | ((seq) << 21))      /* REQ_CC1(REQ_OP_REPORT, 0, seq): target 0, op 1, no reply; placeholder
                                                     header. Spec 4.13: the ROM's two reports are sequence 24 and 25, so
                                                     C&C 1 tells the set from a unit's first report (0: 24 reports; 24: two) */
    .section .bootrom, "ax"
    .globl _reset
/* RECIPE bootrom */
_reset:
    la   t0, trap_handler               /* 1. mtvec: the ROM's two-report handler, below */
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
/* END */

/* RECIPE rom_handler */
    .balign 4
    .globl trap_handler
trap_handler:                           /* mtvec points here; MIE is now 0 and is never set again (D20) */
    lw   x2, X0REL(OFF_ERRORS)(zero)    /* report 24: ERRORS, mcause, mepc. x1 (ra) is kept for report 25 */
    csrr x3, mcause
    csrr x4, mepc
    sw   x2, W1(zero)                   /* the main code may have left the port's words half written (D20, W1-04): */
    sw   x3, W2(zero)                   /* every report writes all four */
    li   x5, HDR(24)
    sw   x5, W0(zero)
    sw   x4, SEND(zero)                 /* stalls only while the request FIFO is full (4.18) */
    csrr x2, mtval                      /* report 25: mtval, STATUS, ra (x1) */
    lw   x3, X0REL(OFF_STATUS)(zero)
    sw   x2, W1(zero)
    sw   x3, W2(zero)
    li   x5, HDR(25)
    sw   x5, W0(zero)
    sw   x1, SEND(zero)
park:
    j    park                           /* terminal: only the reset restarts the controller (D20) */
/* END */
