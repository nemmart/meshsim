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

  // ---- values: a register or a known constant, folded where both operands are constants (inline.rs:290-400) ----

  /** An operand: a register, or an immediate the builder knows at expansion time. */
  record Value(boolean isImm, long v) {
    static Value reg(int r) { return new Value(false, r); }
    static Value imm(long x) { return new Value(true, x); }
    int reg() { return (int) v; }
  }

  interface Fold { long apply(long x, long y); }

  /** bin: reg op reg (R form), reg op imm (I form; an immediate first operand is swapped), imm op imm folded. */
  Value bin(int regOp, int immOp, Value a, Value b, int rd, Fold fold) {
    if (!a.isImm() && !b.isImm()) { r(regOp, rd, a.reg(), b.reg()); return Value.reg(rd); }
    if (!a.isImm()) { i(immOp, rd, a.reg(), b.v()); return Value.reg(rd); }
    if (!b.isImm()) return bin(regOp, immOp, b, a, rd, fold);
    return Value.imm(fold.apply(a.v(), b.v()));
  }

  Value add(Value a, Value b, int rd) { return bin(ADD, ADDI, a, b, rd, (x, y) -> x + y); }
  Value xor(Value a, Value b, int rd) { return bin(XOR, XORI, a, b, rd, (x, y) -> x ^ y); }
  Value and(Value a, Value b, int rd) { return bin(AND, ANDI, a, b, rd, (x, y) -> x & y); }

  /** 32-bit rotate right: VirtualROTRIW with a mask of the shift (a shift of 0 or 32 is an XOR with 0). */
  Value rotri32(Value a, int shamt, int rd) {
    if (shamt == 0 || shamt == 32) return xor(a, Value.imm(0), rd);
    if (a.isImm()) return Value.imm(Integer.rotateRight((int) a.v(), shamt) & 0xffffffffL);
    long mask = ((1L << (32 - shamt)) - 1) << shamt;
    i(VirtualROTRIW, rd, a.reg(), mask);
    return Value.reg(rd);
  }

  /** 32-bit logical right shift: the source SRLIW, which expands to VirtualSRLIW with its bitmask. */
  Value srli32(Value a, int shamt, int rd) {
    if (shamt == 0) return xor(a, Value.imm(0), rd);
    if (a.isImm()) return Value.imm(((int) a.v() >>> shamt) & 0xffffffffL);
    i(VirtualSRLIW, rd, a.reg(), (1L << 32) - (1L << (shamt & 0x1f)));
    return Value.reg(rd);
  }

  /** 64-bit rotate right by a mask's trailing zeros: VirtualROTRI. */
  Value rotri(Value a, long mask, int rd) {
    if (a.isImm()) return Value.imm(Long.rotateRight(a.v(), Long.numberOfTrailingZeros(mask)));
    i(VirtualROTRI, rd, a.reg(), mask);
    return Value.reg(rd);
  }

  /** 64-bit rotate right by an amount (inline.rs:393-406); 0 or 64 is an XOR with 0. */
  Value rotr64(Value a, int amount, int rd) {
    if (amount == 0 || amount == 64) return xor(a, Value.imm(0), rd);
    if (a.isImm()) return Value.imm(Long.rotateRight(a.v(), amount));
    return rotri(a, Bits.maskTrailingZeros(amount), rd);
  }
}
