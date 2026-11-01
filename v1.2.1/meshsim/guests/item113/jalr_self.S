/* item 113, case 4: a JALR that overwrites its base register and jumps to itself.
 *   self: jalr t0, 0(t0)  with t0 = self. The target is self (a self-jump); the link self + 4 goes into t0, so a
 *   second execution would jump to self + 4, where the program ends.
 * jolt-cpp (trace_owner.cpp:122-125, the self-jump rule): the JALR row (rd t0, rd_prev = self, rd = self + 4) is
 *   sequential: pc += 4, instr_idx + 1, no log word. The next row is the `j .` at self + 4 (JAL v40, 0 after the
 *   remap), which ends the program. Rows: MARK, JALR, JAL; then padding.
 *   Caveat: this is the interpreter's view. Whether jolt-cpp's JIT run itself executes the JALR once or twice
 *   isn't established; if twice, its run log and its interpreted rows may disagree. That is part of what to look at.
 * 735355e (source reading, unconfirmed): the tracer stops when a step starts where the previous one did, so the
 *   trace ends after the first JALR row: MARK, JALR, then padding.
 * a16z HEAD (source reading): the JALR isn't at its fixed point (its rerun targets self + 4), so it runs again:
 *   MARK, JALR, JALR (pc self, same instr_idx), JAL; the second JALR's rd_prev = self + 4, rd = self + 4. */
#include "common.inc"
    .option norvc
    la    t0, self
    MARK(0x111)
self:
    jalr  t0, 0(t0)
    j     .
