package gold;

/**
 * The proof's parameters as a16z's Jolt computes them (DECISIONS-v1.3 sections 14, 16, 25; a16z 7189216): K, the RAM
 * size in 8-byte cells, by its prover's rule (crates/jolt-prover/src/config.rs:53-99, derive_from_dimensions),
 *
 *   K = next_pow2(max(remap(highest address touched), remap(min image address) + image words + 1)),
 *   remap(a) = (a - lowest_address) / 8,
 *
 * with the image's extent as its RAMPreprocessing (crates/jolt-program/src/preprocess/ram.rs:39-60: the lowest
 * initialized byte; the highest + 3; words = ceil(max / 8) - min / 8 + 1); lowest_address = min(trusted, untrusted
 * advice start); and a16z's MemoryLayout (common/src/jolt_device.rs:302-325) in its declaration order. The checks
 * (section 25): every access (address - lowest_address) < 8K, unsigned on the full 64-bit address, as the Tracer's
 * comparator will check it; and K <= compute_max_ram_k (preprocess/ram.rs:118-130), which a16z's verifier requires.
 */
public final class ProofParams {
  public final int log2K;
  public final long k, lowestAddress, maxRamK;
  /** a16z's MemoryLayout, in declaration order (FIELD_NAMES). */
  public final long[] memoryLayout;

  public static final String[] FIELD_NAMES = {"program_size", "max_trusted_advice_size", "trusted_advice_start",
      "trusted_advice_end", "max_untrusted_advice_size", "untrusted_advice_start", "untrusted_advice_end",
      "max_input_size", "max_output_size", "input_start", "input_end", "output_start", "output_end", "stack_size",
      "stack_end", "heap_size", "heap_end", "panic", "termination", "io_end"};

  /** A run that can't be proven at these parameters: the Tracer's comparator, or a16z's verifier, would reject it. */
  public static final class Unprovable extends RuntimeException {
    private static final long serialVersionUID = 1L;
    Unprovable(String m) { super(m); }
  }

  /** checked_next_power_of_two (0 and 1 give 1). */
  static long nextPow2(long x) { return x <= 1 ? 1 : Long.highestOneBit(x - 1) << 1; }

  /** From the program and the run's memory rows (Machine's lowest and highest word address touched). */
  public ProofParams(Program p, Machine m) {
    Layout l = p.layout;
    lowestAddress = l.lowestAddress();
    memoryLayout = new long[] {l.programEnd - Layout.RAM_START, l.trustedAdviceEnd - l.trustedAdviceStart,
        l.trustedAdviceStart, l.trustedAdviceEnd, l.untrustedAdviceEnd - l.untrustedAdviceStart, l.untrustedAdviceStart,
        l.untrustedAdviceEnd, l.inputEnd - l.inputStart, l.outputEnd - l.outputStart, l.inputStart, l.inputEnd,
        l.outputStart, l.outputEnd, l.stackEnd - l.stackStart, l.programEnd /* a16z's stack_end: the stack's low end */,
        l.heapEnd - l.heapStart, l.heapEnd, l.panic, l.termination, l.ioEnd};
    long imageEnd = 0;
    if (p.imageMin != 0) {
      long min = p.imageMin, max = p.imageMax + 3;
      long words = (max + 7) / 8 - min / 8 + 1;
      imageEnd = remap(min) + words + 1;
    }
    if (m.minAddress != 0 && Long.compareUnsigned(m.minAddress, lowestAddress) < 0) {
      throw new Unprovable(String.format("access at row %d to %#x is outside (address - lowest_address) < 8K: below "
          + "lowest_address %#x (a16z's prover: RAM address precedes the memory layout)", m.minAddressRow, m.minAddress,
          lowestAddress));
    }
    long touched = m.maxAddress == 0 ? 0 : remap(m.maxAddress);
    k = nextPow2(Math.max(touched, imageEnd));
    log2K = Long.numberOfTrailingZeros(k);
    maxRamK = nextPow2((l.heapEnd - lowestAddress) / 8);
  }

  long remap(long address) { return (address - lowestAddress) / 8; }

  /** The Tracer's comparator on an access's word address. */
  public boolean inRange(long address) {
    return Long.compareUnsigned(address - lowestAddress, 8 * k) < 0;
  }

  /** The run's accesses against the range, and K against a16z's verifier bound; throws Unprovable. */
  public void check(Machine m) {
    if (m.maxAddress != 0 && !inRange(m.maxAddress)) {
      throw new Unprovable(String.format("access at row %d to %#x is outside (address - lowest_address) < 8K "
          + "(lowest_address %#x, K 2^%d): a16z's K rule doesn't cover its highest cell", m.maxAddressRow, m.maxAddress,
          lowestAddress, log2K));
    }
    if (k > maxRamK) {
      throw new Unprovable(String.format("K 2^%d exceeds a16z's max_ram_k 2^%d (heap_end %#x): the access at row %d to "
          + "%#x is above the layout's top", log2K, Long.numberOfTrailingZeros(maxRamK), memoryLayout[16],
          m.maxAddressRow, m.maxAddress));
    }
  }

  /** Frame 7's data (DECISIONS-v1.3 section 16): u32 version 1, u32 n, then n u64 fields: log2 K, lowest_address, then
   *  a16z's MemoryLayout in declaration order; fields are only ever added at the end. */
  public byte[] frame7() {
    int n = 2 + memoryLayout.length;
    byte[] b = new byte[8 + 8 * n];
    SegmentWriter.putInt(b, 0, 1);
    SegmentWriter.putInt(b, 4, n);
    long[] fields = new long[n];
    fields[0] = log2K;
    fields[1] = lowestAddress;
    System.arraycopy(memoryLayout, 0, fields, 2, memoryLayout.length);
    for (int i = 0; i < n; i++) for (int j = 0; j < 8; j++) b[8 + 8 * i + j] = (byte) (fields[i] >>> (8 * j));
    return b;
  }
}
