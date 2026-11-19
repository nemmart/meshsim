# nmi_wfi: the NMI in a WFI's wait (events.h: the test raises the core's nmi input DELAY cycles after it sees the
# trigger store, and holds it). mie and mstatus.MIE are clear: the NMI wakes the core and is taken, mcause
# 0x8000_0000, mepc the instruction after the WFI, mtval 0; after it, with the input still high, no NMI is taken
# again, and the machine external interrupt works as before.
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
    li      t0, NMI | 12
    sw      t0, 0(s1)
    wfi
after_wfi:
    li      t0, 1
    beq     s2, t0, 1f                  # the NMI was taken, once
    fail_with 1
1:  li      t0, 0x80000000
    beq     s3, t0, 2f                  # its mcause
    fail_with 2
2:  la      t0, after_wfi
    beq     s4, t0, 3f                  # its mepc
    fail_with 3
3:  beqz    s5, 4f                      # its mtval
    fail_with 4
4:  csrr    t0, mstatus                 # MPIE and MIE as the handler's MRET left them: MPIE set, MIE clear
    li      t1, 0x1880
    beq     t0, t1, 5f
    fail_with 5
5:  nop
    nop
    nop
    nop
    # the machine external interrupt, after the NMI
    li      t0, MEIE
    csrw    mie, t0
    csrsi   mstatus, 8
    li      t0, 1
    sw      t0, 0(s0)
    li      t0, RELEASE | 6
    sw      t0, 0(s1)
    wfi
    li      t0, 2
    beq     s2, t0, 6f
    fail_with 6
6:  li      t0, 0x8000000b
    beq     s3, t0, pass
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
    csrr    s4, mepc
    csrr    s5, mtval
    sw      zero, 0(s0)                 # the barrier line low (it is MEIP's; the NMI stays)
    mret
