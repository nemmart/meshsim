# irq_wfi: the machine external interrupt and WFI. MEIP is the test memory's barrier line: a store to the barrier
# register lowers it, and the test raises it DELAY cycles after it sees a trigger store (events.h).
#   1  WFI with mstatus.MIE clear, the release 0 to 13 and 40 cycles after the trigger: the WFI falls through or waits
#      (the long one reaches the canonical wait), and execution resumes after it with no trap
#   2  the same with mstatus.MIE set: the interrupt is taken, mepc the instruction after the WFI
#   3  the release 0 to 23 cycles after the trigger while loads, stores, multiplies, branches, calls and CSR
#      instructions run: one interrupt each
#   4  MEIP pending with mstatus.MIE clear, then a CSR write that sets it: taken at the very next instruction
# a0 holds the failing check's number at `fail`.
#include "events.h"

    .text
    .option norelax
    .globl _start
_start:
    la      t0, handler
    csrw    mtvec, t0
    li      s0, BARRIER
    li      s1, TRIGGER
    li      s2, 0                       # interrupts taken
    la      s6, data
    li      t0, MEIE
    csrw    mie, t0                     # MEIE alone; mstatus.MIE is clear

    # ---- 1: WFI, MIE clear
    li      s3, 0
wait_loop:
    li      t0, 1
    sw      t0, 0(s0)                   # arm: the line is low
    sw      s3, 0(s1)                   # the release, s3 cycles after
    wfi
    lw      t1, 0(s0)                   # the line is high, and no interrupt was taken
    andi    t1, t1, 1
    bnez    t1, 1f
    fail_with 1
1:  beqz    s2, 2f
    fail_with 2
2:  addi    s3, s3, 1
    li      t0, 14
    blt     s3, t0, wait_loop
    li      t0, 41
    beq     s3, t0, wait_done
    li      s3, 40                      # once more, with a long wait
    j       wait_loop
wait_done:
    sw      zero, 0(s0)                 # the line low

    # ---- 2: WFI, MIE set: the interrupt is taken, mepc after the WFI
    csrsi   mstatus, 8
    li      s3, 0
taken_loop:
    li      t0, 1
    sw      t0, 0(s0)
    sw      s3, 0(s1)
    mv      s4, s2
    wfi
after_wfi:
    addi    s4, s4, 1
    beq     s4, s2, 1f                  # exactly one interrupt
    fail_with 3
1:  la      t0, after_wfi
    beq     s5, t0, 2f                  # its mepc
    fail_with 4
2:  addi    s3, s3, 1
    li      t0, 14
    blt     s3, t0, taken_loop

    # ---- 3: an interrupt in running code, the release 0 to 23 cycles after the trigger
    li      s3, 0
run_loop:
    li      t0, 1
    sw      t0, 0(s0)
    mv      s4, s2
    sw      s3, 0(s1)
    lw      t1, 0(s6)                   # a load, and its use
    addi    t1, t1, 3
    sw      t1, 4(s6)
    lw      t2, 4(s6)
    mul     t3, t1, t2                  # a multiply of a result just made, and its use
    add     t3, t3, t1
    mulhu   t4, t3, t3
    beq     t1, t2, 1f                  # a taken branch
    fail_with 5
1:  jal     ra, leaf                    # a call and its return
    csrr    t0, mscratch                # a CSR read, a CSR write and the instruction after it
    csrw    mscratch, t3
    xor     t0, t0, t3
    c.addi  t0, 1
    c.mv    t1, t0
    lbu     t2, 1(s6)
    sh      t2, 8(s6)
    lh      t2, 8(s6)
    bne     t2, t2, 2f                  # a branch not taken
    czero.eqz t1, t1, t2
2:  nop
    nop
    nop
    nop
    addi    s4, s4, 1
    beq     s4, s2, 3f
    fail_with 6
3:  addi    s3, s3, 1
    li      t0, 24
    blt     s3, t0, run_loop

    # ---- 4: MEIP pending with MIE clear, then set
    csrci   mstatus, 8
    li      t0, 1
    sw      t0, 0(s0)
    sw      zero, 0(s1)                 # the release, at once
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    mv      s4, s2
    csrsi   mstatus, 8
enabled:
    addi    s4, s4, 1
    beq     s4, s2, 1f
    fail_with 7
1:  la      t0, enabled
    beq     s5, t0, 2f
    fail_with 8
2:  li      t0, 14 + 24 + 1
    beq     s2, t0, pass
    fail_with 9

pass:
    li      t0, RESULT_PASS
    sw      zero, 0(t0)
1:  j       1b
fail:
    li      t0, RESULT_FAIL
    sw      a0, 0(t0)
1:  j       1b

leaf:
    addi    t5, t5, 1
    ret

    .balign 4
handler:
    csrr    a1, mcause
    li      a2, 0x8000000b
    beq     a1, a2, 1f
    fail_with 10
1:  sw      zero, 0(s0)                 # the line low
    addi    s2, s2, 1
    csrr    s5, mepc
    mret

    .data
    .balign 4
data:
    .word   0x01020304, 0, 0
