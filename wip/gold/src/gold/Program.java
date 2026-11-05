package gold;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
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

  public Program(Path programDir, Path inputDir) throws IOException {
    Elf elf = new Elf(programDir.resolve("program.elf"));
    symbols = elf.symbols;
    if (elf.machine != Elf.EM_RISCV || elf.type != Elf.ET_EXEC) throw new IOException("not a RISC-V executable");
    if (elf.entry == 0 || elf.entry >= Memory.SIZE || (elf.entry & 1) != 0) throw new IOException("bad entry point");

    // segments (loader.cpp:120-170): one executable segment, which is the code
    long codeStart = -1, codeSize = 0, minAddr = Long.MAX_VALUE, maxAddr = 0;
    for (Elf.Segment s : elf.segments) {
      if (s.type() != Elf.PT_LOAD) continue;
      if (s.align() != 4096) throw new IOException(String.format("segment %#x: alignment must be 4096", s.vaddr()));
      if (s.memSize() == 0 || s.memSize() < s.fileSize()) throw new IOException(String.format("segment %#x: bad size", s.vaddr()));
      if (s.vaddr() < 0 || s.vaddr() + s.memSize() > Memory.SIZE) throw new IOException("segment outside RAM");
      memory.write(s.vaddr(), elf.bytes, (int) s.offset(), (int) s.fileSize());   // the rest stays zero
      if (s.executable()) {
        if (codeSize != 0) throw new IOException("multiple executable segments");
        codeStart = s.vaddr();
        codeSize = s.memSize();
      }
      minAddr = Math.min(minAddr, s.vaddr());
      maxAddr = Math.max(maxAddr, s.vaddr() + s.memSize());
    }
    if (codeSize == 0) throw new IOException("no executable segment");

    Map<?, ?> meta = (Map<?, ?>) Json.parse(Files.readString(programDir.resolve("program.json")));
    Map<?, ?> mc = (Map<?, ?>) meta.get("mem_conf");
    Long heap = Json.u64(mc, "heap_size");
    if (heap == null) heap = Json.u64(mc, "memory_size");
    Layout.Config config = new Layout.Config(Json.u64(mc, "max_untrusted_advice_size"),
        Json.u64(mc, "max_trusted_advice_size"), Json.u64(mc, "max_input_size"), Json.u64(mc, "max_output_size"),
        Json.u64(mc, "stack_size"), heap);
    layout = Layout.compute(config, minAddr, maxAddr, codeStart, codeStart + codeSize, elf.entry);

    // inputs, copied unparsed (an absent file is empty)
    copyInput(inputDir.resolve("input.bin"), layout.inputStart, layout.inputEnd);
    copyInput(inputDir.resolve("trusted_advice.bin"), layout.trustedAdviceStart, layout.trustedAdviceEnd);
    copyInput(inputDir.resolve("untrusted_advice.bin"), layout.untrustedAdviceStart, layout.untrustedAdviceEnd);

    byte[] code = memory.read(layout.codeStart, (int) codeSize);
    bytecode = Bytecode.build(layout.codeStart, code);   // no host I/O rewriting (owner's D3 ruling)
  }

  private void copyInput(Path file, long start, long end) throws IOException {
    if (!Files.exists(file)) return;
    byte[] data = Files.readAllBytes(file);
    if (data.length > end - start) throw new IOException(file + ": " + data.length + " bytes, region holds " + (end - start));
    memory.write(start, data, 0, data.length);
  }
}
