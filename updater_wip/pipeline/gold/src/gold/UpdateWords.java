package gold;

import java.util.Arrays;
import java.util.TreeSet;

/**
 * Update words (HOST-FRAMES.md, "Update words"), the writer's side: runs in address order, each a header word (start, a
 * 64-bit-word index in the target file, in [31:0]; the count in [63:32]) and that many values; sent raw, or LRU-coded
 * against a 32-entry move-to-front table that is all zeros at the start of the frame and, in frame 9, carries on across
 * segments. Unlike v1.3's frame 5 there is no count in front: the counts are in frames 3 and 4.
 */
public final class UpdateWords {
  public static final int LRU_ENTRIES = 32;

  private UpdateWords() {}

  /**
   * The LRU codes, least significant bit first within each byte: a hit is 1 and the entry's 5-bit index; a miss is 0,
   * then 0 and the low 32 bits (the value is zero-extended), or 1 and the 64-bit value. A hit moves its entry to the
   * front; a miss pushes the value at the front, dropping the last.
   */
  public static byte[] lru(long[] words) {
    long[] table = new long[LRU_ENTRIES];
    SegmentWriter.BitWriter bits = new SegmentWriter.BitWriter();
    for (long w : words) {
      int hit = -1;
      for (int i = 0; i < LRU_ENTRIES; i++) if (table[i] == w) { hit = i; break; }
      if (hit >= 0) {
        bits.put(1, 1);
        bits.put(hit, 5);
        System.arraycopy(table, 0, table, 1, hit);
      } else {
        bits.put(0, 1);
        if ((w >>> 32) != 0) { bits.put(1, 1); bits.put(w, 64); } else { bits.put(0, 1); bits.put(w, 32); }
        System.arraycopy(table, 0, table, 1, LRU_ENTRIES - 1);
      }
      table[0] = w;
    }
    return bits.toByteArray();
  }

  /** Maximal runs over a set of cells (64-bit-word indices) with their values. */
  public static long[] runs(long[] sortedCells, long[] values) {
    SegmentWriter.LongList out = new SegmentWriter.LongList();
    for (int a = 0; a < sortedCells.length; ) {
      int b = a + 1;
      while (b < sortedCells.length && sortedCells[b] == sortedCells[b - 1] + 1) b++;
      out.add(header(sortedCells[a], b - a));
      for (int j = a; j < b; j++) out.add(values[j]);
      a = b;
    }
    return out.toArray();
  }

  public static long header(long start, long count) {
    Frames.u32(start, "a run's start");
    Frames.u32(count, "a run's count");
    return start | count << 32;
  }

  /** The 4 KB pages (cell >> 9) a list of update words writes. */
  public static TreeSet<Long> pages(long[] u) {
    TreeSet<Long> s = new TreeSet<>();
    int i = 0;
    while (i < u.length) {
      long start = u[i] & 0xffffffffL, n = u[i] >>> 32;
      for (long p = start >>> 9; n > 0 && p <= (start + n - 1) >>> 9; p++) s.add(p);
      i += 1 + (int) n;
    }
    return s;
  }

  /** The words of several lists, one after another. */
  public static long[] concat(long[][] lists) {
    int n = 0;
    for (long[] l : lists) n += l.length;
    long[] out = new long[n];
    int at = 0;
    for (long[] l : lists) { System.arraycopy(l, 0, out, at, l.length); at += l.length; }
    return out;
  }

  static long[] sorted(TreeSet<Long> s) { return s.stream().mapToLong(Long::longValue).toArray(); }

  static long[] copyOf(long[] a) { return Arrays.copyOf(a, a.length); }
}
