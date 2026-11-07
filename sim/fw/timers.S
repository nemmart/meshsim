/* timers (M1, sim/planning/DECISIONS.md D2): mcycle, mcycleh, mtime and mtimeh (mtime = mcycle / ref_cycle, 40) read
 * around loops of known length, each loop exercising one of the core's timings: a taken branch (+1), a multiply (3 in
 * all), a register-block load (2, sampling in the last); then mcycle and mcycleh written, a carry out of mcycle into
 * mcycleh, and mtime following the written mcycle. mtime and mtimeh are CSRs 0xC01 and 0xC81 (Controller.MTIME,
 * MTIMEH). Every reading goes to res[]; SIM_EXIT = 2. */
#include "mesh.h"
    .section .text.init
    .globl _start
_start:
    la    s0, res
    csrr  t0, mcycle
    csrr  t1, mcycleh
    csrr  t2, 0xC01
    csrr  t3, 0xC81
    sw    t0, 0(s0)
    sw    t1, 4(s0)
    sw    t2, 8(s0)
    sw    t3, 12(s0)
    /* 100 iterations of addi and a taken bnez (the last not taken) */
    li    t4, 100
    csrr  s1, mcycle
1:  addi  t4, t4, -1
    bnez  t4, 1b
    csrr  s2, mcycle
    /* 10 multiplies, back to back with their loop */
    li    t4, 10
2:  mul   t5, t4, t4
    mulhu t6, t5, t4
    addi  t4, t4, -1
    bnez  t4, 2b
    csrr  s3, mcycle
    /* 10 register-block loads (PARAMS, STATUS) and a data-memory load */
    li    t4, 10
3:  lw    t5, X0REL(OFF_PARAMS)(zero)
    lw    t6, X0REL(OFF_STATUS)(zero)
    lw    a1, 0(s0)
    addi  t4, t4, -1
    bnez  t4, 3b
    csrr  s4, mcycle
    sw    s1, 16(s0)
    sw    s2, 20(s0)
    sw    s3, 24(s0)
    sw    s4, 28(s0)
    /* mtime across about 600 cycles: 15 ticks of ref_cycle */
    csrr  s5, 0xC01
    li    t4, 200
4:  addi  t4, t4, -1
    bnez  t4, 4b
    csrr  s6, 0xC01
    sw    s5, 32(s0)
    sw    s6, 36(s0)
    /* mcycleh, then mcycle 16 below the carry; read low, high, low until the carry has happened */
    li    t0, 5
    csrw  mcycleh, t0
    li    t0, -16
    csrw  mcycle, t0
    csrr  a2, mcycle
    csrr  a3, mcycleh
5:  csrr  a4, mcycleh
    csrr  a5, mcycle
    beq   a4, a3, 5b
    csrr  a6, 0xC01
    csrr  a7, 0xC81
    sw    a2, 40(s0)
    sw    a3, 44(s0)
    sw    a4, 48(s0)
    sw    a5, 52(s0)
    sw    a6, 56(s0)
    sw    a7, 60(s0)
    /* a write through the immediate form: mcycle = 0 */
    csrrwi a2, mcycle, 0
    csrr  a3, mcycle
    csrr  a4, mcycleh
    sw    a2, 64(s0)
    sw    a3, 68(s0)
    sw    a4, 72(s0)
    li    a0, 2
    sw    a0, X0REL(OFF_SIM_EXIT)(zero)
6:  j     6b

    .data
    .balign 4
res: .space 80
