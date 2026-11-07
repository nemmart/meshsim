package gold;

import java.util.Arrays;
import java.util.HashMap;
import java.util.Map;

/**
 * Frame 6, the proof's jump-target table (DECISIONS-v1.3 sections 16, 18): the static targets (StaticTargets) and the
 * run's own targets, merged into one HashTable, built after the run. The run's targets are as WORKER26's jumpmap takes
 * them (wip/jumpmap Main.transfer): every control transfer's new pc (a taken branch, a JAL other than the end, a JALR;
 * a transfer to its own pc is sequential) with gold's next instr_idx; keys o = pc - lowest_address. Frame 6's data:
 * u32 k, u32 p1, u32 p3, then the 2^k lines of 128 B.
 */
public final class JumpTable {
  private final Map<Integer, Integer> runTargets = new HashMap<>();
  private final StaticTargets st;
  private HashTable table;

  public JumpTable(Program p) { st = new StaticTargets(p, p.elf); }

  static final long SEQUENTIAL = -1, END = -2;

  /** The transfer rule (jumpmap Main.transfer): row is the step's record, v and pc the row before it ran. */
  static long transfer(VInstr v, int op, long pc, long[] row) {
    long a = v.rs1Imm ? v.rs1 : row[4], b = v.rs2Imm ? v.rs2 : row[5];
    switch (op) {
      case Op.BEQ: return a == b ? pc + v.rd : SEQUENTIAL;
      case Op.BNE: return a != b ? pc + v.rd : SEQUENTIAL;
      case Op.BLT: return a < b ? pc + v.rd : SEQUENTIAL;
      case Op.BGE: return a >= b ? pc + v.rd : SEQUENTIAL;
      case Op.BLTU: return Long.compareUnsigned(a, b) < 0 ? pc + v.rd : SEQUENTIAL;
      case Op.BGEU: return Long.compareUnsigned(a, b) >= 0 ? pc + v.rd : SEQUENTIAL;
      case Op.JAL: return a == 0 ? END : pc + a;
      case Op.JALR: return (a + b) & ~1L;
      default: return SEQUENTIAL;
    }
  }

  /** After each step: v and pc as before it, m after it. */
  public void observe(VInstr v, long pc, Machine m) {
    long target = transfer(v, v.opcode, pc, m.row);
    if (target == SEQUENTIAL || target == END || target == pc) return;
    int key = st.key(target);
    Integer prev = runTargets.put(key, m.index);
    if (prev != null && prev != m.index) throw new IllegalStateException(String.format("target o=%#x has two bases", key));
  }

  /** The merged table (jumpmap Main: the static targets' bases, then the run's). */
  public HashTable table() {
    if (table != null) return table;
    Map<Integer, Integer> merged = new HashMap<>();
    for (int key : st.kinds.keySet()) merged.put(key, st.base(st.pc(key)));
    merged.putAll(runTargets);
    int[] keys = new int[merged.size()];
    int i = 0;
    for (int k : merged.keySet()) keys[i++] = k;
    long[] l = new long[keys.length];
    for (i = 0; i < keys.length; i++) l[i] = keys[i] & 0xffffffffL;
    Arrays.sort(l);
    int[] bases = new int[keys.length];
    for (i = 0; i < keys.length; i++) { keys[i] = (int) l[i]; bases[i] = merged.get(keys[i]); }
    return table = HashTable.choose(keys, bases);
  }

  /** Frame 6's data: u32 k, u32 p1, u32 p3, then the lines. */
  public byte[] frame6() {
    HashTable t = table();
    byte[] lines = t.image();
    byte[] b = new byte[12 + lines.length];
    SegmentWriter.putInt(b, 0, t.k);
    SegmentWriter.putInt(b, 4, t.p1);
    SegmentWriter.putInt(b, 8, t.p3);
    System.arraycopy(lines, 0, b, 12, lines.length);
    return b;
  }
}
