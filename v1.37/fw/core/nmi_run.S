# nmi_run: the NMI in running code, then an exception. The NMI is raised (and held) 7 cycles after the trigger while
# loads, a multiply and a branch run, with mstatus.MIE and mie clear: it is taken once, at an instruction boundary
# (the interrupted instruction runs after the handler). The ECALL after it traps as usual, and no NMI follows.
#include "events.h"

    .text
    .option norelax
    .globl _start
_start:
    la      t0, handler
    csrw    mtvec, t0
    li      s1, TRIGGER
    li      s2, 0                       # traps taken
    la      s6, data
    li      t0, NMI | 7
    sw      t0, 0(s1)
    lw      t1, 0(s6)
    addi    t1, t1, 3
    sw      t1, 4(s6)
    lw      t2, 4(s6)
    mul     t3, t1, t2
    add     t3, t3, t1
    beq     t1, t2, 1f
    fail_with 1
1:  li      t4, 0x01020307
    bne     t1, t4, 2f                  # the code ran whole, around the NMI
    mul     t4, t4, t4
    add     t4, t4, t1
    beq     t3, t4, 3f
2:  fail_with 2
3:  li      t0, 1
    beq     s2, t0, 4f                  # one trap so far: the NMI
    fail_with 3
4:  li      t0, 0x80000000
    beq     s3, t0, 5f
    fail_with 4
5:  ecall
    li      t0, 2
    beq     s2, t0, 6f
    fail_with 5
6:  li      t0, 11
    beq     s3, t0, 7f
    fail_with 6
7:  nop
    nop
    nop
    nop
    nop
    nop
    li      t0, 2
    beq     s2, t0, pass                # and no further NMI
    fail_with 7

pass:
    li      t0, RESULT_PASS
    sw      zero, 0(t0)
1:  j       1b
fail:
    li      t0, RESULT_FAIL
    sw      a0, 0(t0)
1:  j       1b

    .balign 4
handler:
    addi    s2, s2, 1
    csrr    s3, mcause
    bltz    s3, 1f
    csrr    t0, mepc                    # an exception: resume after the ECALL
    addi    t0, t0, 4
    csrw    mepc, t0
1:  mret

    .data
    .balign 4
data:
    .word   0x01020304, 0, 0
