package gold;

import java.util.ArrayList;
import java.util.List;

/**
 * The expanded program (jolt-cpp's VirtualBytecode::from_riscv_code, isa/virtual.cpp:1308-1388 at 821e1bd): a NOP at
 * index 0, then every source instruction's expansion in address order, UNIMP removed. A table maps each 2-byte code
 * offset to the index of the expansion starting there (isa/virtual.hpp:159-171).
 */
public final class Bytecode {
  public final long codeStart, codeEnd;
  public final VInstr[] instrs;
  private final int[] pcToIndex;

  private Bytecode(long codeStart, long codeEnd, VInstr[] instrs, int[] pcToIndex) {
    this.codeStart = codeStart; this.codeEnd = codeEnd; this.instrs = instrs; this.pcToIndex = pcToIndex;
  }

  /** The first row of the expansion of the instruction at pc (0 where none starts). */
  public int indexOf(long pc) {
    long off = pc - codeStart;
    if (off < 0 || pc >= codeEnd || (off & 1) != 0) throw new EmulationError(String.format("pc %#x outside the code", pc));
    return pcToIndex[(int) (off >>> 1) + 1];
  }

  public int size() { return instrs.length; }

  public static Bytecode build(long codeStart, byte[] code, HostIo.Encoding encoding) {
    List<VInstr> out = new ArrayList<>();
    out.add(new VInstr());                    // index 0: NOP (all zero)
    int[] pcToIndex = new int[code.length / 2 + 1];
    int offset = 0;
    while (offset < code.length) {
      Decoder.Decoded d = Decoder.decodeNext(code, offset);
      Instruction in = HostIo.apply(d.instr(), encoding);
      long pc = codeStart + offset;
      offset += d.size();
      if (in.opcode == Op.UNIMP) continue;   // removed, as Rust does (virtual.cpp:1357-1363)
      List<VInstr> seq = Expander.expand(in);
      for (VInstr v : seq) { v.riscPc = pc; v.srcOpcode = in.opcode; }
      seq.get(seq.size() - 1).pcIncrement = d.size();
      pcToIndex[(int) ((pc - codeStart) >>> 1) + 1] = out.size();
      out.addAll(seq);
    }
    return new Bytecode(codeStart, codeStart + code.length, out.toArray(new VInstr[0]), pcToIndex);
  }
}
