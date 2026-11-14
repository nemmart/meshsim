/* wfi_go (M1, sim/planning/DECISIONS.md D2): WFI woken by GO under the done gate (wait_for_command: PULSE_DONE, then WFI;
 * from done until GO only GO wakes the core, TileControl.wakeTerm). A: GO stored by the test bench while the core sleeps
 * (no READY needed: wfi_go.tb's "go"); B: GO arrives while the core is busy, and the next wait_for_command falls straight
 * through (GO is sticky); C: the realistic phase: done, then a command lands (READY rises), C&C sees READY and stores GO.
 * EVENTS, STATUS and mcycle go to res[]; SIM_EXIT = 4. */
#include "mesh.h"
    .section .text.init
    .globl _start
_start:
    la    s0, res
    li    t6, COMMAND_BASE
    li    t0, 1
    /* A */
    csrr  s1, mcycle
    sw    t0, X0REL(OFF_PULSE_DONE)(zero)
    wfi
    csrr  s2, mcycle
    lw    t1, X0REL(OFF_EVENTS)(zero)       /* consumes GO (the reset mask) */
    lw    t2, X0REL(OFF_STATUS)(zero)
    sw    s1, 0(s0)
    sw    s2, 4(s0)
    sw    t1, 8(s0)
    sw    t2, 12(s0)
    /* B: GO lands during the busy loop */
    li    t4, 300
1:  addi  t4, t4, -1
    bnez  t4, 1b
    lw    t2, X0REL(OFF_EVENTS_PEEK)(zero)
    csrr  s1, mcycle
    sw    t0, X0REL(OFF_PULSE_DONE)(zero)
    wfi
    csrr  s2, mcycle
    lw    t1, X0REL(OFF_EVENTS)(zero)
    sw    t2, 16(s0)
    sw    s1, 20(s0)
    sw    s2, 24(s0)
    sw    t1, 28(s0)
    /* C: done; a command lands (READY); GO */
    csrr  s1, mcycle
    sw    t0, X0REL(OFF_PULSE_DONE)(zero)
    wfi
    csrr  s2, mcycle
    lw    t1, X0REL(OFF_EVENTS)(zero)
    lw    t2, X0REL(OFF_STATUS)(zero)
    lw    t3, 0(t6)
    lw    t4, 32(t6)
    sw    s1, 32(s0)
    sw    s2, 36(s0)
    sw    t1, 40(s0)
    sw    t2, 44(s0)
    sw    t3, 48(s0)
    sw    t4, 52(s0)
    li    a0, 4
    sw    a0, X0REL(OFF_SIM_EXIT)(zero)
2:  j     2b

    .data
    .balign 4
res: .space 64
