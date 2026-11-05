package gold;

import java.util.ArrayList;
import java.util.List;

/**
 * The expanded program (jolt-cpp's VirtualBytecode::from_riscv_code, isa/virtual.cpp:1308-1388 at 821e1bd): a NOP at
 * index 0, then every source instruction's expansion in address order, UNIMP removed. A table maps each 2-byte code
 * offset to the index of the expansion starting there (isa/virtual.hpp:159-171).
 */
public final class Bytecode implements Code {
  public final long codeStart, codeEnd;
  public final VInstr[] instrs;
  private final int[] pcToIndex;
  /** Removed UNIMP instructions (as jolt-cpp removes them) by halfword offset, with why they can't be proven:
   *  reaching one throws. */
  private final java.util.Map<Long, String> removed;

  private Bytecode(long codeStart, long codeEnd, VInstr[] instrs, int[] pcToIndex, java.util.Map<Long, String> removed) {
    this.codeStart = codeStart; this.codeEnd = codeEnd; this.instrs = instrs; this.pcToIndex = pcToIndex;
    this.removed = removed;
  }

  @Override public String removedAt(long pc) { return removed.get(pc); }

  @Override public VInstr at(int index) { return instrs[index]; }

  /**
   * The one UNIMP encoding loaded as an unreachable marker: c.unimp (0x0000), which a16z skips at decode
   * (jolt-program image/elf.rs:126-134). The 32-bit unimp (0xC0001073, csrrw x0, cycle, x0) is rejected at load, as
   * a16z rejects it (UnsupportedCsr; owner's D4 follow-up, 2026-11-03).
   */
  static boolean standardUnimp(int word, int size) {
    return size == 2 && (word & 0xffff) == 0;
  }

  /** Thrown at load in strict mode. */
  public static final class UnprovableInstruction extends RuntimeException {
    private static final long serialVersionUID = 1L;
    UnprovableInstruction(String m) { super(m); }
  }

  /** The first row of the expansion of the instruction at pc (0 where none starts). */
  @Override public int indexOf(long pc) {
    long off = pc - codeStart;
    if (off < 0 || pc >= codeEnd || (off & 1) != 0) throw new EmulationError(String.format("jump target %#x is not in the bytecode map", pc));
    return pcToIndex[(int) (off >>> 1) + 1];
  }

  public int size() { return instrs.length; }

  /** One instruction's expansion at pc, as the whole program (for running a single inline). */
  public static Bytecode ofInstruction(long pc, Instruction in, int size) {
    List<VInstr> seq = Expander.expand(in);
    for (VInstr v : seq) { v.riscPc = pc; v.srcOpcode = in.opcode; }
    seq.get(seq.size() - 1).pcIncrement = size;
    VInstr[] instrs = new VInstr[seq.size() + 1];
    instrs[0] = new VInstr();
    for (int i = 0; i < seq.size(); i++) instrs[i + 1] = seq.get(i);
    int[] pcToIndex = new int[size / 2 + 1];
    pcToIndex[1] = 1;
    return new Bytecode(pc, pc + size, instrs, pcToIndex, new java.util.HashMap<>());
  }

  /**
   * Gold loads only instructions it can prove (the owner's ruling on UPSTREAM-DIFFS D4, DECISIONS-v1.3 section 17;
   * one loading mode, strict): any instruction in the load stream that Jolt can't prove throws at load, except
   * c.unimp, which loads as an unreachable marker that throws if executed. The bytecode keeps jolt-cpp's
   * structure (UNIMP removed, an undecodable instruction one row), so instruction indices, and the segment files'
   * instr_idx, are jolt-cpp's.
   */
  /** A merged range of Text sections and its bytes. */
  public record Range(long start, byte[] bytes) {}

  /** One contiguous code range (tests and the single-range callers). */
  public static Bytecode build(long codeStart, byte[] code) {
    return build(List.of(new Range(codeStart, code)));
  }

  /** The Text ranges decoded in address order (a16z's decode_text_section, elf.rs:112-148); gaps are unmapped. */
  public static Bytecode build(List<Range> ranges) {
    List<VInstr> out = new ArrayList<>();
    out.add(new VInstr());                    // index 0: NOP (all zero)
    long codeStart = ranges.get(0).start();
    Range last = ranges.get(ranges.size() - 1);
    long codeEnd = last.start() + last.bytes().length;
    int[] pcToIndex = new int[(int) ((codeEnd - codeStart) / 2) + 1];
    java.util.Map<Long, String> removed = new java.util.HashMap<>();
    List<String> rejected = new ArrayList<>();
    for (Range range : ranges) {
    byte[] code = range.bytes();
    int offset = 0;
    while (offset < code.length) {
      if (offset + 2 > code.length) throw new UnprovableInstruction("truncated instruction halfword at " + Long.toHexString(range.start() + offset));
      if ((code[offset] & 3) == 3 && offset + 4 > code.length) {
        throw new UnprovableInstruction("truncated RV64 instruction word at " + Long.toHexString(range.start() + offset));
      }
      Decoder.Decoded d = Decoder.decodeNext(code, offset);
      Instruction in = d.instr();      // ECALL is always the trap through mtvec, as a16z (owner's D3 ruling)
      long pc = range.start() + offset;
      int word = wordAt(code, offset, d.size());
      offset += d.size();
      String why = null;
      if (in.opcode == Op.UNIMP || !in.isValid()) {
        why = String.format("%s %#0" + (d.size() == 2 ? 6 : 10) + "x at %#x", in.isValid() ? "UNIMP" : "undecodable",
            word, pc);
        if (!standardUnimp(word, d.size())) rejected.add(why);
      }
      if (in.opcode == Op.UNIMP) {           // removed, as Rust does (virtual.cpp:1357-1363); reaching it throws
        removed.put(pc, why);
        continue;
      }
      List<VInstr> seq = Expander.expand(in);
      boolean lrToX0 = (in.opcode == Op.LR_D || in.opcode == Op.LR_W) && !in.immA && in.a == 0;
      for (VInstr v : seq) { v.riscPc = pc; v.srcOpcode = in.opcode; v.unprovable = why; v.lrToX0 = lrToX0; }
      seq.get(seq.size() - 1).pcIncrement = d.size();
      pcToIndex[(int) ((pc - codeStart) >>> 1) + 1] = out.size();
      out.addAll(seq);
    }
    }
    if (!rejected.isEmpty()) {
      throw new UnprovableInstruction(rejected.size() + " instruction(s) Jolt can't prove, first: "
          + String.join("; ", rejected.subList(0, Math.min(5, rejected.size()))));
    }
    return new Bytecode(codeStart, codeEnd, out.toArray(new VInstr[0]), pcToIndex, removed);
  }

  static int wordAt(byte[] code, int offset, int size) {
    int w = 0;
    for (int i = 0; i < size && offset + i < code.length; i++) w |= (code[offset + i] & 0xff) << (8 * i);
    return w;
  }
}
