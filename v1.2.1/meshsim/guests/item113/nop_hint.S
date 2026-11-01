/* item 113, case 3: the shared NOP row's virtual flag, for rd = x0 instructions without side effects.
 * jolt-cpp (isa/virtual.cpp:183, :307-311): each becomes one row ADDI x0, x0, 0 (opcode 2, rd_reg 0, rs1_reg 0,
 *   rs2 an immediate 0, seq_remaining 0), with is_virtual = is_first = 1 when the source isn't ADDI:
 *     add  x0, a1, a2   -> virtual, first
 *     slli x0, a1, 3    -> virtual, first
 *     mul  x0, a1, a2   -> virtual, first
 *     addi x0, x0, 0    -> not virtual, not first (the source is ADDI: our rd0 = 3)
 *     addi x0, a1, 5    -> not virtual, not first (also ADDI)
 * 735355e (source reading, unconfirmed): noop_for gives the same ADDI row with no sequence for every source, so
 *   none of the five is virtual or first. Every row's pc is its own instruction's address; is_compressed 0
 *   (.option norvc keeps them 32-bit). */
#include "common.inc"
    .option norvc
    li    a1, 7
    li    a2, 9
    MARK(0x111)
    add   x0, a1, a2
    slli  x0, a1, 3
    mul   x0, a1, a2
    addi  x0, x0, 0
    addi  x0, a1, 5
    MARK(0x222)
    j     .
