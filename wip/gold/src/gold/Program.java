package gold;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;

/**
 * A guest program loaded the way jolt-cpp loads it: program.elf's PT_LOAD segments into RAM at their addresses
 * (loader.cpp:25-190), Jolt's layout around them (loader.cpp:192-300), the input files copied unparsed into their
 * regions (compressed_trace_gen.cpp, execute_fast; utils/elf_helper.cpp:161-210), and the code expanded into bytecode.
 */
public final class Program {
  public final Layout layout;
  public final Memory memory = new Memory();
  public final Bytecode bytecode;
  public final java.util.Map<String, Long> symbols;
  /** The lowest and highest initialized byte addresses of the image (a16z's memory_init), 0 if none. */
  public long imageMin, imageMax;
  /** The merged Text ranges (D1): what is code, for the stores-into-code check (DECISIONS-v1.3 section 25). */
  public final List<long[]> textRanges = new ArrayList<>();

  public Program(Path programDir, Path inputDir) throws IOException {
    Elf elf = new Elf(programDir.resolve("program.elf"));
    symbols = elf.symbols;
    if (elf.machine != Elf.EM_RISCV || elf.type != Elf.ET_EXEC) throw new IOException("not a RISC-V executable");
    if (elf.entry == 0 || elf.entry >= Memory.SIZE || (elf.entry & 1) != 0) throw new IOException("bad entry point");

    // The image by sections, as a16z's decode_elf (jolt-program image/elf.rs:40-93; owner's D1 ruling): every section at
    // or above 0x8000_0000 is copied into memory (SHT_NOBITS has no data) and extends program_end; the code is the
    // Text sections, overlapping ranges merged, decoded in address order. Program headers aren't used.
    long programEnd = Layout.RAM_START;
    List<long[]> textSections = new ArrayList<>();
    for (Elf.Section s : elf.sections) {
      if (s.addr() < Layout.RAM_START) continue;
      long end = s.addr() + s.size();
      if (end > Memory.SIZE || end < s.addr()) throw new IOException(String.format("section %#x outside RAM", s.addr()));
      programEnd = Math.max(programEnd, end);
      if (!s.nobits()) {
        if (s.offset() + s.size() > elf.bytes.length) throw new IOException(String.format("section %#x data is not readable", s.addr()));
        memory.write(s.addr(), elf.bytes, (int) s.offset(), (int) s.size());
        if (s.size() > 0) {
          if (imageMin == 0 || s.addr() < imageMin) imageMin = s.addr();
          imageMax = Math.max(imageMax, end - 1);
        }
      }
      if (s.text()) textSections.add(new long[] {s.addr(), end});
    }
    textSections.sort((x, y) -> Long.compare(x[0], y[0]));
    List<long[]> merged = new ArrayList<>();                                     // merge_ranges (elf.rs:96-109)
    for (long[] r : textSections) {
      if (!merged.isEmpty() && r[0] <= merged.get(merged.size() - 1)[1]) {
        merged.get(merged.size() - 1)[1] = Math.max(merged.get(merged.size() - 1)[1], r[1]);
      } else {
        merged.add(r.clone());
      }
    }
    if (merged.isEmpty()) throw new IOException("no Text section");
    this.textRanges.addAll(merged);
    long codeStart = merged.get(0)[0], codeEnd = merged.get(merged.size() - 1)[1];

    Map<?, ?> meta = (Map<?, ?>) Json.parse(Files.readString(programDir.resolve("program.json")));
    Map<?, ?> mc = (Map<?, ?>) meta.get("mem_conf");
    Long heap = Json.u64(mc, "heap_size");
    if (heap == null) heap = Json.u64(mc, "memory_size");
    Layout.Config config = new Layout.Config(Json.u64(mc, "max_untrusted_advice_size"),
        Json.u64(mc, "max_trusted_advice_size"), Json.u64(mc, "max_input_size"), Json.u64(mc, "max_output_size"),
        Json.u64(mc, "stack_size"), heap);
    layout = Layout.compute(config, Layout.RAM_START, programEnd, codeStart, codeEnd, elf.entry);

    // inputs, copied unparsed (an absent file is empty)
    copyInput(inputDir.resolve("input.bin"), layout.inputStart, layout.inputEnd);
    copyInput(inputDir.resolve("trusted_advice.bin"), layout.trustedAdviceStart, layout.trustedAdviceEnd);
    copyInput(inputDir.resolve("untrusted_advice.bin"), layout.untrustedAdviceStart, layout.untrustedAdviceEnd);

    List<Bytecode.Range> ranges = new ArrayList<>();
    for (long[] r : merged) ranges.add(new Bytecode.Range(r[0], memory.read(r[0], (int) (r[1] - r[0]))));
    bytecode = Bytecode.build(ranges);   // no host I/O rewriting (owner's D3 ruling)
  }

  private void copyInput(Path file, long start, long end) throws IOException {
    if (!Files.exists(file)) return;
    byte[] data = Files.readAllBytes(file);
    if (data.length > end - start) throw new IOException(file + ": " + data.length + " bytes, region holds " + (end - start));
    memory.write(start, data, 0, data.length);
  }
}
