/* Recipe 22: the boot ROM, as D36 writes it with D37.1's change. One ROM for every unit, at 0x02000000, the reset vector.
 * It never writes data memory; it reads only the command block, after the command has landed (D31). */
#include "../../wip/sw/mesh-v1.2-draft.h"
    .section .bootrom, "ax"
    .globl _reset
/* RECIPE bootrom */
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
    lw   t4, X0REL(OFF_EVENTS)(zero)    /* consumes GO (the reset mask holds it), and returns [6] unconsumed */
    andi t5, t4, EV_GO
    beqz t5, 1b
    andi t5, t4, EV_COMMAND             /* D37.1: jump only if a command has landed. The event, not STATUS [5]: */
    beqz t5, 3b                         /* GO cleared the status bit. A GO for other tiles: done again, wait */
    j    jump
controller:                             /* 4. C&C 1-3 (64-66): wait for "command landed" itself */
    li   t3, EV_COMMAND
    sw   t3, X0REL(OFF_EVENTS_MASK)(zero)
2:  wfi
    lw   t4, X0REL(OFF_EVENTS)(zero)    /* consumes [6] */
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
