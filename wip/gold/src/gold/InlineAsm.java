package gold;

import static gold.Op.*;

import java.util.TreeSet;

/**
 * What every inline is written with: upstream Jolt's InlineExpansionBuilder (crates/jolt-program/src/expand/inline.rs and
 * allocator.rs at f8fc214, the commit jolt-cpp 821e1bd pins). Inline registers are handed out from 48 up, lowest free
 * first (inline.rs:127-146); at the end, every register ever allocated is zeroed by an `ADDI r, x0, 0` reset row, in
 * register order (allocator.rs:174-186, take_registers_for_reset). The emit forms are virtual.cpp's (:174-218).
 */
final class InlineAsm {
  static final int FIRST_INLINE_REG = 48;

  final Expander e;
  /** The source instruction's operands: rs1, rs2, and rs3 = rd (the output address for most inlines). */
  final int rs1, rs2, rs3;
  private final boolean[] live = new boolean[Isa.NUM_REGS - FIRST_INLINE_REG];
  private final TreeSet<Integer> touched = new TreeSet<>();

  InlineAsm(Expander e, int rd, int rs1, int rs2) {
    this.e = e; this.rs3 = rd; this.rs1 = rs1; this.rs2 = rs2;
  }

  int alloc() {
    for (int i = 0; i < live.length; i++) {
      if (!live[i]) {
        live[i] = true;
        touched.add(FIRST_INLINE_REG + i);
        return FIRST_INLINE_REG + i;
      }
    }
    throw new IllegalStateException("inline registers exhausted");
  }

  int[] alloc(int n) {
    int[] r = new int[n];
    for (int i = 0; i < n; i++) r[i] = alloc();
    return r;
  }

  void release(int... regs) { for (int r : regs) live[r - FIRST_INLINE_REG] = false; }

  /** The reset rows for every register allocated, ascending. */
  void finish() {
    for (int i = 0; i < live.length; i++) if (live[i]) throw new IllegalStateException("inline register leaked");
    for (int r : touched) li(r, 0);
  }

  // ---- emit forms ----

  void r(int op, int rd, int a, int b) { e.emitR(op, rd, a, b); }
  void i(int op, int rd, int a, long imm) { e.emitI(op, rd, a, imm); }
  void ld(int rd, int base, long offset) { e.emitLd(LD, rd, base, offset); }
  void sd(int base, int value, long offset) { e.emitS(SD, base, value, offset); }
  void advice(int rd) { e.emitAdvice(rd); }
  /** LUI rd, imm: a full 64-bit constant (emit_u). */
  void lui(int rd, long imm) { e.emitU(LUI, rd, imm); }
  /** ADDI rd, x0, imm (emit_u with ADDI). */
  void li(int rd, long imm) { e.emitU(ADDI, rd, imm); }
  void assertEq(int a, int b) { e.emitB(VirtualAssertEQ, 0, a, b); }
  void assertLte(int a, int b) { e.emitB(VirtualAssertLTE, 0, a, b); }
}
