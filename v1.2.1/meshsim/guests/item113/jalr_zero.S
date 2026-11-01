/* item 113, case 5: a JALR whose target is 0 (spec 4.19.7's pc table, F3, N6).
 * jolt-cpp (trace_owner.cpp:115-117): `jalr x0, 0(t0)` with t0 = 0 is remapped to JALR v40, t0, 0 (rd = x0, a
 *   side effect); its row records rd_reg 40, rd = pc + 4. New pc 0 ends the program: pc = 0, instr_idx = 0, and
 *   every later row is padding. The `j .` after it is never reached. The replayer pops one read-log word, 0.
 * 735355e (source reading, unconfirmed; item 113 lists this as not yet determined): the JALR sets pc = 0, and the
 *   next step fetches at 0 below DRAM_BASE: the jolt device's zero padding returns 0, the reserved compressed
 *   halfword 0 doesn't decode, and the tracer panics. If so, no trace exists to compare, and the case can't occur
 *   in a valid trace. */
#include "common.inc"
    .option norvc
    li    t0, 0
    MARK(0x111)
    jalr  x0, 0(t0)
    j     .
