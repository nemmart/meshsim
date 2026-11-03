package act4;

import gold.Code;
import gold.Decoder;
import gold.Elf;
import gold.EmulationError;
import gold.Expander;
import gold.Instruction;
import gold.Machine;
import gold.Memory;
import gold.Op;
import gold.Row;
import gold.SecpCompute;
import gold.VInstr;

import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;

/**
 * ACT4-only test equipment, not part of gold (owner, DECISIONS-v1.3 section 17). Every ACT4 self-checking test puts
 * data inside its code (RVTEST_SIGUPD, riscv-arch-test tests/env/signature.h:21-31: two RVTEST_WORD_PTR words after
 * each check's failure jump), so no ACT4 ELF loads under gold's strict loader. This runner decodes at run time, as
 * upstream's jolt-emu does, but executes through gold's ordinary Machine and expansions: at each instruction it reads
 * the bytes at pc from memory, decodes them with gold's Decoder and expands them with gold's Expander. An instruction
 * Jolt can't prove (or a standard UNIMP) becomes one row flagged to throw if executed (jolt-emu panics there).
 *
 * <pre>
 *   Act4Runner &lt;elf&gt; [max rows] [--trace-failure]
 *       prints: rows, run hash, tohost word and verdict, or where it aborted; with --trace-failure, the first failed
 *       check's tested instruction and description (RVTEST_SIGUPD's inline words)
 * </pre>
 * Verdict (tests/arch-tests/jolt/rvmodel_macros.h at jolt 17a9459): tohost = 1 PASS, (gp &lt;&lt; 1) | 1 FAIL in case gp.
 */
public final class Act4Runner {
  /** Rows appended in execution order: each instruction reached is decoded and expanded at the end of the list. */
  static final class RuntimeCode implements Code {
    final Memory memory;
    final List<VInstr> rows = new ArrayList<>();
    long nextPc;

    RuntimeCode(Memory memory) {
      this.memory = memory;
      rows.add(new VInstr());                   // index 0, as in Bytecode
    }

    @Override public VInstr at(int index) {
      // falling through past the last expansion: the next instruction is the one after it
      while (index >= rows.size()) append(nextPc);
      return rows.get(index);
    }

    @Override public int indexOf(long pc) { return append(pc); }

    @Override public String removedAt(long pc) { return null; }

    int append(long pc) {
      byte[] bytes = new byte[4];
      for (int i = 0; i < 4; i++) bytes[i] = (byte) memory.loadByte(pc + i);
      Decoder.Decoded d = Decoder.decodeNext(bytes, 0);
      Instruction in = d.instr();
      int start = rows.size();
      List<VInstr> seq;
      if (in.opcode == Op.UNIMP || !in.isValid()) {
        VInstr v = new VInstr();
        v.unprovable = String.format("%s at %#x (decoded at run time)", in.isValid() ? "UNIMP" : "undecodable", pc);
        seq = List.of(v);
      } else {
        seq = Expander.expand(in);
      }
      for (VInstr v : seq) { v.riscPc = pc; v.srcOpcode = in.opcode; }
      seq.get(seq.size() - 1).pcIncrement = d.size();
      rows.addAll(seq);
      nextPc = pc + d.size();
      return start;
    }
  }

  public static void main(String[] args) throws Exception {
    Elf elf = new Elf(Path.of(args[0]));
    boolean traceFailure = java.util.Arrays.asList(args).contains("--trace-failure");
    long maxRows = args.length > 1 && !args[1].startsWith("--") ? Long.parseLong(args[1]) : 1L << 24;
    Memory memory = new Memory();
    for (Elf.Segment s : elf.segments) {
      if (s.type() == Elf.PT_LOAD) memory.write(s.vaddr(), elf.bytes, (int) s.offset(), (int) s.fileSize());
    }
    Long tohost = elf.symbols.get("tohost");
    Machine m = new Machine(memory, new RuntimeCode(memory), elf.entry, new SecpCompute());
    long windowHash = 0, runHash = 0;
    String end;
    java.util.Map<Long, String> handlers = new java.util.HashMap<>();
    for (var e : elf.symbols.entrySet()) {
      if (e.getKey().startsWith("failedtest_x")) handlers.put(e.getValue(), e.getKey());
    }
    try {
      while (!m.halted() && m.cycle < maxRows) {
        if (traceFailure && handlers.containsKey(m.pc)) {
          // failedtest_x<link>_x<temp>: the link register points at RVTEST_SIGUPD's two words (instruction, string)
          String h = handlers.get(m.pc);
          int link = Integer.parseInt(h.substring("failedtest_x".length(), h.indexOf('_', "failedtest_x".length())));
          long at = m.regs[link];
          long inst = memory.load(at & ~7L) >>> (8 * (at & 7)) | (at % 8 != 0 ? memory.load((at & ~7L) + 8) << (64 - 8 * (at & 7)) : 0);
          long str = memory.load((at + 8) & ~7L) >>> (8 * ((at + 8) & 7)) | ((at + 8) % 8 != 0 ? memory.load(((at + 8) & ~7L) + 8) << (64 - 8 * ((at + 8) & 7)) : 0);
          StringBuilder sb = new StringBuilder();
          for (long a = str; sb.length() < 200; a++) { int c = memory.loadByte(a); if (c == 0) break; sb.append((char) c); }
          int temp = Integer.parseInt(h.substring(h.lastIndexOf("_x") + 2));
          System.out.printf("FAILED CHECK at row %d: %s, tested instruction at %#x: %s%n  expected (x%d) %#x; registers:",
              m.cycle, h, inst, sb, temp, m.regs[temp]);
          for (int r = 1; r < 32; r++) System.out.printf(" x%d=%#x", r, m.regs[r]);
          System.out.println();
          traceFailure = false;
        }
        m.step();
        windowHash = Row.mix(windowHash, Row.hash(m.row));
        if (m.cycle % (1 << 16) == 0) { runHash = Row.mix(runHash, windowHash); windowHash = 0; }
      }
      if (m.cycle % (1 << 16) != 0) runHash = Row.mix(runHash, windowHash);
      end = m.halted() ? "halted" : "row limit";
    } catch (EmulationError e) {
      System.out.printf("ABORT at row %d: %s%n", m.cycle, e.getMessage());
      System.exit(2);
      return;
    }
    long v = tohost == null ? 0 : memory.load(tohost);
    String verdict = v == 1 ? "PASS" : v == 0 ? "NONE" : "FAIL(" + (v >>> 1) + ")";
    System.out.printf("%d rows (%s), run hash %016x, tohost %#x: %s%n", m.cycle, end, runHash, v, verdict);
    System.exit(v == 1 ? 0 : 1);
  }
}
