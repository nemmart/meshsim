# trap_nmi: an exception first, then the NMI input: a core that has taken any trap takes no NMI. The ECALL traps;
# then the NMI is raised and held while code runs and through a WFI's wait, which it does not end: the machine
# external interrupt's release ends it (mie.MEIE set, mstatus.MIE clear: no trap).
#include "events.h"

    .text
    .option norelax
    .globl _start
_start:
    la      t0, handler
    csrw    mtvec, t0
    li      s0, BARRIER
    li      s1, TRIGGER
    li      s2, 0                       # traps taken
    ecall
    li      t0, NMI | 2
    sw      t0, 0(s1)
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    li      t0, MEIE
    csrw    mie, t0
    li      t0, 1
    sw      t0, 0(s0)
    li      t0, RELEASE | 30
    sw      t0, 0(s1)
    wfi                                 # the NMI input is high: it must not end the wait
    lw      t1, 0(s0)
    andi    t1, t1, 1
    bnez    t1, 1f                      # the release did
    fail_with 1
1:  li      t0, 1
    beq     s2, t0, pass                # the ECALL's trap alone
    fail_with 2

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
    csrr    t0, mcause
    li      t1, 11
    beq     t0, t1, 1f
    fail_with 3
1:  csrr    t0, mepc
    addi    t0, t0, 4
    csrw    mepc, t0
    mret
