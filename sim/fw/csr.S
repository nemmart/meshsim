/* csr (M1, sim/planning/DECISIONS.md D2): the machine-mode trap CSRs (Controller.csrRead/csrWrite): each one's kept bits,
 * the read-only ones (misa, mip: writes ignored), the immediate forms and the forms that don't write (rs1 = x0), an
 * unimplemented CSR; then one deliberate trap to this program's own handler, chosen by the boot command's second argument
 * (slot 2): 0 an illegal instruction (csrr of cycle, 0xC00, which the core lacks), 1 a load bus error (an unmapped address),
 * 2 a store bus error (program memory). The handler records mcause, mepc, mtval and mstatus and exits (SIM_EXIT = 1).
 * Every result goes to res[] in data memory (the trace's digest covers it). */
#include "mesh.h"
    .section .text.init
    .globl _start
_start:
    la    s0, res
    /* mtvec: direct mode only, the low two bits read 0; its old value is the boot ROM's handler */
    la    t0, handler
    ori   t1, t0, 3
    csrrw t2, mtvec, t1
    csrr  t3, mtvec
    sw    t2, 0(s0)
    sw    t3, 4(s0)
    /* mscratch: all 32 bits; csrrsi, csrrci, csrrwi; csrrs and csrrc with rs1 = x0 read without writing */
    li    t0, 0xA5A5F00F
    csrw  mscratch, t0
    csrrsi t1, mscratch, 0x10
    csrrci t2, mscratch, 0x0F
    csrrs t3, mscratch, zero
    csrrc t4, mscratch, zero
    csrrwi t5, mscratch, 21
    csrr  t6, mscratch
    sw    t1, 8(s0)
    sw    t2, 12(s0)
    sw    t3, 16(s0)
    sw    t4, 20(s0)
    sw    t5, 24(s0)
    sw    t6, 28(s0)
    /* mstatus: only MIE [3] and MPIE [7] are kept; MPP [12:11] reads 3 */
    li    t0, -1
    csrrw t1, mstatus, t0
    csrr  t2, mstatus
    csrrc t3, mstatus, t0
    csrr  t4, mstatus
    sw    t1, 32(s0)
    sw    t2, 36(s0)
    sw    t3, 40(s0)
    sw    t4, 44(s0)
    /* mie: only MEIE [11] (mstatus.MIE is 0 here, so nothing can interrupt) */
    csrrw t1, mie, t0
    csrr  t2, mie
    csrrw t3, mie, zero
    sw    t1, 48(s0)
    sw    t2, 52(s0)
    sw    t3, 56(s0)
    /* mepc: the low two bits read 0; mcause and mtval: all 32 bits */
    li    t0, 0x12345677
    csrw  mepc, t0
    csrr  t1, mepc
    li    t0, 0x8000000B
    csrw  mcause, t0
    csrr  t2, mcause
    li    t0, 0xDEADBEEF
    csrw  mtval, t0
    csrr  t3, mtval
    sw    t1, 60(s0)
    sw    t2, 64(s0)
    sw    t3, 68(s0)
    /* misa and mip: read-only here, writes ignored (WARL); mip [11] is ERRORS != 0, here 0 */
    csrr  t1, misa
    csrw  misa, zero
    csrr  t2, misa
    csrr  t3, mip
    csrrwi t4, mip, 31
    csrr  t5, mip
    sw    t1, 72(s0)
    sw    t2, 76(s0)
    sw    t3, 80(s0)
    sw    t4, 84(s0)
    sw    t5, 88(s0)
    /* the trap: slot 2 of the command block (the boot command's second argument) */
    li    t6, COMMAND_BASE
    lw    t0, 8(t6)
    li    t1, 1
    beq   t0, t1, load_fault
    li    t1, 2
    beq   t0, t1, store_fault
    csrr  a0, 0xC00                     /* cycle: not implemented, an illegal instruction */
    j     1f
load_fault:
    li    t1, 0x30000000
    lw    a0, 4(t1)                     /* region 0x30: unmapped, a load bus error */
    j     1f
store_fault:
    li    t1, 0x01000100
    sw    t0, 0(t1)                     /* program memory: a store bus error */
1:  j     1b                            /* not reached */

    .balign 4
handler:
    csrr  t1, mcause
    csrr  t2, mepc
    csrr  t3, mtval
    csrr  t4, mstatus
    sw    t1, 92(s0)
    sw    t2, 96(s0)
    sw    t3, 100(s0)
    sw    t4, 104(s0)
    lw    t5, X0REL(OFF_ERRORS)(zero)
    sw    t5, 108(s0)
    li    a0, 1
    sw    a0, X0REL(OFF_SIM_EXIT)(zero)
2:  j     2b

    .data
    .balign 4
res: .space 112
