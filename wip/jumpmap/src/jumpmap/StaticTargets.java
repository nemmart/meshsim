package jumpmap;

import gold.Bytecode;
import gold.Decoder;
import gold.Elf;
import gold.Instruction;
import gold.Layout;
import gold.Op;
import gold.Program;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/**
 * The program's static jump targets (section 18), from the ELF and gold's bytecode:
 * <ul>
 *   <li><b>direct</b>: every branch's and JAL's target (pc + offset; a JAL with offset 0 is the program's end, no target);</li>
 *   <li><b>return</b>: the instruction after every call, a JAL or JALR whose rd is x1 or x5 (ra, t0: the RISC-V link
 *       registers), compressed forms included (gold decodes them to the base forms);</li>
 *   <li><b>auipc call</b>: an AUIPC rX followed at once by a JALR through rX (the far `call` and `tail`): pc + hi + lo;</li>
 *   <li><b>auipc address</b>: an AUIPC rX followed at once by an ADDI from rX (`la` of a code address, a function
 *       pointer made in code) that is an instruction start;</li>
 *   <li><b>data</b>: every 8-byte-aligned u64 in a non-executable section at or above 0x8000_0000 with data that equals
 *       an instruction start.</li>
 * </ul>
 * An instruction start is a pc where gold's bytecode starts an expansion (index not 0, not a removed UNIMP). The text is
 * decoded exactly as gold's Program does (the Text sections, overlapping ranges merged, in address order; D1).
 */
public final class StaticTargets {
  public static final int DIRECT = 1, RETURN = 2, DATA = 4, RUN = 8, AUIPC_CALL = 16, AUIPC_ADDR = 32;
  /** The kinds the static table is built from. */
  public static final int STATIC = DIRECT | RETURN | DATA | AUIPC_CALL | AUIPC_ADDR;

  public final Program program;
  public final Bytecode bc;
  public final long lowest;
  /** key o -> kinds (bit mask). */
  public final Map<Integer, Integer> kinds = new HashMap<>();
  /** Every instruction start's key, ascending. */
  public final int[] allStarts;
  public long directRefs, returnRefs, dataWords, dataInCodeNotStart, badDirect, badReturn;
  public long auipcCallRefs, auipcAddrRefs, badAuipcCall;
  public long instructions;

  public StaticTargets(Program program, Elf elf) {
    this.program = program;
    this.bc = program.bytecode;
    this.lowest = program.layout.lowestAddress();

    // the Text ranges, merged (as Program; a16z's decode_elf)
    List<long[]> text = new ArrayList<>();
    for (Elf.Section s : elf.sections) {
      if (s.addr() >= Layout.RAM_START && s.text() && s.size() > 0) text.add(new long[] {s.addr(), s.addr() + s.size()});
    }
    text.sort((x, y) -> Long.compare(x[0], y[0]));
    List<long[]> merged = new ArrayList<>();
    for (long[] r : text) {
      if (!merged.isEmpty() && r[0] <= merged.get(merged.size() - 1)[1]) {
        merged.get(merged.size() - 1)[1] = Math.max(merged.get(merged.size() - 1)[1], r[1]);
      } else {
        merged.add(r.clone());
      }
    }

    IntList starts = new IntList();
    for (long[] r : merged) {
      byte[] code = program.memory.read(r[0], (int) (r[1] - r[0]));
      int off = 0;
      int auipcRd = -1;                             // the previous instruction was AUIPC auipcRd: its value
      long auipcValue = 0;
      while (off < code.length) {
        Decoder.Decoded d = Decoder.decodeNext(code, off);
        Instruction in = d.instr();
        long pc = r[0] + off;
        off += d.size();
        int prevRd = auipcRd;
        long prevValue = auipcValue;
        auipcRd = -1;
        if (!isStart(pc)) continue;                 // a removed UNIMP
        instructions++;
        if (in.opcode == Op.AUIPC && in.a != 0) { auipcRd = in.a; auipcValue = pc + in.b; }
        if (prevRd > 0 && in.opcode == Op.JALR && in.b == prevRd) {
          auipcCallRefs++;
          if (!add((prevValue + in.c) & ~1L, AUIPC_CALL)) badAuipcCall++;
        }
        if (prevRd > 0 && in.opcode == Op.ADDI && in.b == prevRd) {
          long v = prevValue + in.c;
          if (v >= bc.codeStart && v < bc.codeEnd && isStart(v)) { auipcAddrRefs++; add(v, AUIPC_ADDR); }
        }
        starts.add(key(pc));
        switch (in.opcode) {
          case Op.BEQ: case Op.BNE: case Op.BLT: case Op.BGE: case Op.BLTU: case Op.BGEU:
            directRefs++;
            if (!add(pc + in.a, DIRECT)) badDirect++;
            break;
          case Op.JAL:
            if (in.b != 0) {
              directRefs++;
              if (!add(pc + in.b, DIRECT)) badDirect++;
              if (in.a == 1 || in.a == 5) { returnRefs++; if (!add(pc + d.size(), RETURN)) badReturn++; }
            }
            break;
          case Op.JALR:
            if (in.a == 1 || in.a == 5) { returnRefs++; if (!add(pc + d.size(), RETURN)) badReturn++; }
            break;
          default:
            break;
        }
      }
    }
    allStarts = starts.sortedUnsigned();

    // code pointers in data: aligned u64s in the non-executable sections with data
    ByteBuffer eb = ByteBuffer.wrap(elf.bytes).order(ByteOrder.LITTLE_ENDIAN);
    for (Elf.Section s : elf.sections) {
      if (s.addr() < Layout.RAM_START || s.text() || s.nobits() || s.size() < 8) continue;
      for (long a = (s.addr() + 7) & ~7L; a + 8 <= s.addr() + s.size(); a += 8) {
        long v = eb.getLong((int) (s.offset() + (a - s.addr())));
        dataWords++;
        if (v >= bc.codeStart && v < bc.codeEnd) {
          if (!add(v, DATA)) dataInCodeNotStart++;
        }
      }
    }
  }

  public int key(long pc) { return (int) (pc - lowest); }

  public long pc(int key) { return lowest + (key & 0xffffffffL); }

  /** An expansion starts at pc. */
  public boolean isStart(long pc) {
    if (pc < bc.codeStart || pc >= bc.codeEnd || (pc & 1) != 0) return false;
    if (bc.removedAt(pc) != null) return false;
    return bc.indexOf(pc) != 0;
  }

  public int base(long pc) { return bc.indexOf(pc); }

  private boolean add(long pc, int kind) {
    if (!isStart(pc)) return false;
    kinds.merge(key(pc), kind, (x, y) -> x | y);
    return true;
  }

  /** Counts of keys by exact kind mask. */
  public long count(int mask) {
    long n = 0;
    for (int v : kinds.values()) if ((v & mask) != 0) n++;
    return n;
  }

  /** Keys whose only static kind is the given one. */
  public long onlyKind(int kind) {
    long n = 0;
    for (int v : kinds.values()) if ((v & STATIC) == kind) n++;
    return n;
  }

  /** A growable int list. */
  public static final class IntList {
    int[] a = new int[1024];
    int n;
    public void add(int v) { if (n == a.length) a = java.util.Arrays.copyOf(a, 2 * n); a[n++] = v; }
    public int size() { return n; }
    public int get(int i) { return a[i]; }
    public int[] toArray() { return java.util.Arrays.copyOf(a, n); }
    /** Sorted as unsigned 32-bit values, duplicates removed. */
    public int[] sortedUnsigned() {
      long[] l = new long[n];
      for (int i = 0; i < n; i++) l[i] = a[i] & 0xffffffffL;
      java.util.Arrays.sort(l);
      int m = 0;
      int[] out = new int[n];
      for (int i = 0; i < n; i++) if (i == 0 || l[i] != l[i - 1]) out[m++] = (int) l[i];
      return java.util.Arrays.copyOf(out, m);
    }
  }
}
