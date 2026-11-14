/* wfi_cmd (M1, sim/planning/DECISIONS.md D2): WFI woken by a command landing (TileControl.wakeTerm with the done gate open:
 * EVENTS [9] under EVENTS_MASK). The boot command's [9] is still pending (the boot ROM's tile branch never consumes it):
 * with the mask holding only [9], one EVENTS load consumes it. A: WFI sleeps until the test bench's second command lands
 * (wfi_cmd.tb); B: the third command lands while the core is busy, and the next WFI falls straight through ([9] is sticky).
 * EVENTS, EVENTS_PEEK, STATUS, mcycle and the commands' words go to res[]; SIM_EXIT = 3. */
#include "mesh.h"
    .section .text.init
    .globl _start
_start:
    la    s0, res
    li    t0, EV_COMMAND
    sw    t0, X0REL(OFF_EVENTS_MASK)(zero)
    lw    t1, X0REL(OFF_EVENTS)(zero)       /* consumes the boot command's [9] */
    lw    t2, X0REL(OFF_EVENTS_PEEK)(zero)
    sw    t1, 0(s0)
    sw    t2, 4(s0)
    /* A: sleep until a command lands */
    csrr  s1, mcycle
    wfi
    csrr  s2, mcycle
    lw    t1, X0REL(OFF_EVENTS)(zero)
    lw    t2, X0REL(OFF_STATUS)(zero)
    li    t6, COMMAND_BASE
    lw    t3, 0(t6)                         /* the second command: odd parity, its arguments in slots 8-15 */
    lw    t4, 32(t6)
    sw    s1, 8(s0)
    sw    s2, 12(s0)
    sw    t1, 16(s0)
    sw    t2, 20(s0)
    sw    t3, 24(s0)
    sw    t4, 28(s0)
    /* B: busy while the third command lands, then WFI */
    li    t4, 300
1:  addi  t4, t4, -1
    bnez  t4, 1b
    lw    t2, X0REL(OFF_EVENTS_PEEK)(zero)
    csrr  s1, mcycle
    wfi
    csrr  s2, mcycle
    lw    t1, X0REL(OFF_EVENTS)(zero)
    lw    t3, 0(t6)                         /* the third command: even parity, slots 1-7 */
    lw    t4, 4(t6)
    sw    t2, 32(s0)
    sw    s1, 36(s0)
    sw    s2, 40(s0)
    sw    t1, 44(s0)
    sw    t3, 48(s0)
    sw    t4, 52(s0)
    li    t0, EV_RESET_MASK
    sw    t0, X0REL(OFF_EVENTS_MASK)(zero)
    li    a0, 3
    sw    a0, X0REL(OFF_SIM_EXIT)(zero)
2:  j     2b

    .data
    .balign 4
res: .space 64
