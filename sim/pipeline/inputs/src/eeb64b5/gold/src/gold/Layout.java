package gold;

/**
 * Jolt's memory layout for a loaded program (jolt-cpp loader.cpp:192-300 at 821e1bd):
 *
 * <pre>
 *   [larger advice][smaller advice][input][output][panic][termination] ... 0x8000_0000 [program][canary][stack][heap]
 * </pre>
 *
 * The I/O region sits just below the program, sized to a power of two in words; the stack starts 128 bytes (the
 * canary) after the program's end and the heap follows it.
 */
public final class Layout {
  public static final long RAM_START = 0x8000_0000L;
  static final long FLAG_BYTES = 8;
  static final long STACK_CANARY = 128;

  public long untrustedAdviceStart, untrustedAdviceEnd, trustedAdviceStart, trustedAdviceEnd;
  public long inputStart, inputEnd, outputStart, outputEnd, panic, termination, ioEnd;
  public long programStart, programEnd, stackStart, stackEnd, heapStart, heapEnd;
  public long codeStart, codeEnd, entryPoint;

  /** The memory configuration (program.json's mem_conf; heap_size or memory_size is the heap, elf_helper.cpp:99). */
  public record Config(long maxUntrustedAdvice, long maxTrustedAdvice, long maxInput, long maxOutput, long stack,
                       long heap) {}

  static long align8(long x) { return (x + 7) & ~7L; }

  static long nextPowerOfTwo(long x) { return x <= 1 ? 1 : Long.highestOneBit(x - 1) << 1; }   // std::bit_ceil

  public long lowestAddress() { return Math.min(trustedAdviceStart, untrustedAdviceStart); }

  public static Layout compute(Config c, long programStart, long programEnd, long codeStart, long codeEnd, long entry) {
    if (programStart != RAM_START) {
      throw new IllegalArgumentException(String.format("the program must load at %#x, not %#x", RAM_START, programStart));
    }
    long untrusted = align8(c.maxUntrustedAdvice()), trusted = align8(c.maxTrustedAdvice());
    long input = align8(c.maxInput()), output = align8(c.maxOutput());
    long stack = align8(c.stack()), heap = align8(c.heap());
    if (untrusted != 0 && Long.bitCount(untrusted) != 1) throw new IllegalArgumentException("untrusted advice size must be a power of 2");
    if (trusted != 0 && Long.bitCount(trusted) != 1) throw new IllegalArgumentException("trusted advice size must be a power of 2");

    long ioRegionBytes = untrusted + trusted + input + output + 2 * FLAG_BYTES;
    long ioBytes = nextPowerOfTwo(ioRegionBytes / 8) * 8;

    Layout l = new Layout();
    if (trusted >= untrusted) {          // the larger advice region goes first
      l.trustedAdviceStart = programStart - ioBytes;
      l.trustedAdviceEnd = l.trustedAdviceStart + trusted;
      l.untrustedAdviceStart = l.trustedAdviceEnd;
      l.untrustedAdviceEnd = l.untrustedAdviceStart + untrusted;
    } else {
      l.untrustedAdviceStart = programStart - ioBytes;
      l.untrustedAdviceEnd = l.untrustedAdviceStart + untrusted;
      l.trustedAdviceStart = l.untrustedAdviceEnd;
      l.trustedAdviceEnd = l.trustedAdviceStart + trusted;
    }
    l.inputStart = Math.max(l.untrustedAdviceEnd, l.trustedAdviceEnd);
    l.inputEnd = l.inputStart + input;
    l.outputStart = l.inputEnd;
    l.outputEnd = l.outputStart + output;
    l.panic = l.outputEnd;
    l.termination = l.panic + FLAG_BYTES;
    l.ioEnd = l.termination + FLAG_BYTES;
    l.programStart = programStart;
    l.programEnd = programEnd;
    l.stackStart = programEnd + STACK_CANARY;
    l.stackEnd = l.stackStart + stack;
    l.heapStart = l.stackEnd;
    l.heapEnd = l.heapStart + heap;
    l.codeStart = codeStart;
    l.codeEnd = codeEnd;
    l.entryPoint = entry;
    return l;
  }

  /** The same fields, in row_dump's JSON order, for comparing with jolt-cpp's. */
  public long[] fields() {
    return new long[] {untrustedAdviceStart, untrustedAdviceEnd, trustedAdviceStart, trustedAdviceEnd, inputStart,
        inputEnd, outputStart, outputEnd, panic, termination, ioEnd, programStart, programEnd, stackStart, stackEnd,
        heapStart, heapEnd, codeStart, codeEnd, entryPoint};
  }

  public static final String[] FIELD_NAMES = {"untrusted_advice_start", "untrusted_advice_end", "trusted_advice_start",
      "trusted_advice_end", "input_start", "input_end", "output_start", "output_end", "panic", "termination", "io_end",
      "program_start", "program_end", "stack_start", "stack_end", "heap_start", "heap_end", "code_start", "code_end",
      "entry_point"};
}
