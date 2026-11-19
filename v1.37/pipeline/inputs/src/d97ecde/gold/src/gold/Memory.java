package gold;

/**
 * The guest's memory: jolt-cpp reserves a flat 4 GiB indexed by the guest address (utils/mem.hpp:26, vm.cpp), zero
 * where nothing was written. Kept here as 16 KB pages of 8-byte words (DECISIONS-v1.3 section 12's page), allocated on
 * first write.
 */
public final class Memory {
  public static final int PAGE_SHIFT = 14;                  // 16 KB
  static final int WORDS_PER_PAGE = 1 << (PAGE_SHIFT - 3);
  public static final long SIZE = 1L << 32;

  private final long[][] pages = new long[(int) (SIZE >>> PAGE_SHIFT)][];

  static void check(long addr) {
    if (addr < 0 || addr >= SIZE) throw new EmulationError(String.format("address %#x outside the 4 GiB RAM", addr));
  }

  /** The word at an 8-byte-aligned address. */
  public long load(long addr) {
    check(addr);
    long[] p = pages[(int) (addr >>> PAGE_SHIFT)];
    return p == null ? 0 : p[(int) (addr >>> 3) & (WORDS_PER_PAGE - 1)];
  }

  public void store(long addr, long value) {
    check(addr);
    int pi = (int) (addr >>> PAGE_SHIFT);
    long[] p = pages[pi];
    if (p == null) {
      if (value == 0) return;
      p = pages[pi] = new long[WORDS_PER_PAGE];
    }
    p[(int) (addr >>> 3) & (WORDS_PER_PAGE - 1)] = value;
  }

  public int loadByte(long addr) {
    return (int) (load(addr & ~7L) >>> (8 * (addr & 7))) & 0xff;
  }

  public void storeByte(long addr, int b) {
    long a = addr & ~7L;
    int shift = (int) (8 * (addr & 7));
    store(a, (load(a) & ~(0xffL << shift)) | ((long) (b & 0xff) << shift));
  }

  public void write(long addr, byte[] data, int from, int length) {
    for (int i = 0; i < length; i++) storeByte(addr + i, data[from + i]);
  }

  public byte[] read(long addr, int length) {
    byte[] out = new byte[length];
    for (int i = 0; i < length; i++) out[i] = (byte) loadByte(addr + i);
    return out;
  }
}
