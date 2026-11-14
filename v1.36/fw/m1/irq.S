/* irq (M1, sim/planning/DECISIONS.md D2): the external interrupt (Controller.interruptPending: mstatus.MIE, mie.MEIE and
 * ERRORS != 0, read in the same cycle, TileControl.errorsAt). A store to STATUS (read-only) is a bus error: a synchronous
 * trap (mcause 7) to h1, and ERRORS [9] from the end of that cycle. h1 points mtvec at h2 and sets MIE again (the trap
 * cleared it): the interrupt is taken in the next cycle (mcause 0x8000000B, mepc the instruction it preempted). mip,
 * ERRORS, the CSRs and mcycle at each step go to res[]; SIM_EXIT = 5. */
#include "mesh.h"
    .section .text.init
    .globl _start
_start:
    la    s0, res
    la    t0, h1
    csrw  mtvec, t0
    li    t0, 0x800
    csrw  mie, t0                           /* MEIE */
    csrsi mstatus, 8                        /* MIE: ERRORS is 0, nothing pending */
    csrr  t1, mip
    csrr  t2, mstatus
    sw    t1, 0(s0)
    sw    t2, 4(s0)
    li    t0, 1
    csrr  s1, mcycle
    sw    t0, X0REL(OFF_STATUS)(zero)       /* a store to a read-only register: bus error, trap to h1 */
    j     .

    .balign 4
h1:
    csrr  s2, mcycle
    csrr  t1, mcause
    csrr  t2, mepc
    csrr  t3, mtval
    csrr  t4, mstatus
    csrr  t5, mip
    sw    s1, 8(s0)
    sw    s2, 12(s0)
    sw    t1, 16(s0)
    sw    t2, 20(s0)
    sw    t3, 24(s0)
    sw    t4, 28(s0)
    sw    t5, 32(s0)
    la    t0, h2
    csrw  mtvec, t0
    csrr  s3, mcycle
    csrsi mstatus, 8                        /* MIE again: ERRORS [9] is set, the interrupt is taken next */
    addi  a1, zero, 1                       /* preempted: never executes */
    addi  a1, a1, 1
    j     .

    .balign 4
h2:
    csrr  s4, mcycle
    csrr  t1, mcause
    csrr  t2, mepc
    csrr  t3, mstatus
    lw    t4, X0REL(OFF_ERRORS)(zero)
    sw    s3, 36(s0)
    sw    s4, 40(s0)
    sw    t1, 44(s0)
    sw    t2, 48(s0)
    sw    t3, 52(s0)
    sw    t4, 56(s0)
    sw    a1, 60(s0)
    li    a0, 5
    sw    a0, X0REL(OFF_SIM_EXIT)(zero)
1:  j     1b

    .data
    .balign 4
res: .space 64
