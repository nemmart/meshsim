package jumpmap;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;

/**
 * The Tracer's jump-target map (DECISIONS-v1.3 section 18; spec 4.21.4): 2^k lines of 16 entries of 8 bytes, each the
 * key o = pc - lowest_address (u32) then the base instr_idx (u32), least significant byte first; a free entry is all
 * ones. The home line is the top k bits of (o * p1) mod 2^32; on a full line the probe steps by p3 (mod 2^k) and reads at
 * most 3 lines. Keys are placed by the same rule, in ascending key order, each in the first of its 3 lines with a free
 * entry, filling a line's entries in order; nothing is ever removed, so a lookup that meets a free entry has missed.
 */
public final class HashTable {
  public static final int ENTRIES = 16, LINE_BYTES = 128, PROBE_LINES = 3;
  public static final int FREE = 0xFFFFFFFF;
  /** The golden-ratio multiplier, floor(2^32 / phi) made odd: the first p1 tried. */
  public static final int GOLDEN = 0x9E3779B1;

  public final int k, p1, p3;
  public final int lines;
  final int[] keys, bases;        // lines * ENTRIES, FREE where free
  final int[] fill;               // entries used per line
  /** Keys placed at 0, 1 or 2 steps from home. */
  public final long[] placedAt = new long[PROBE_LINES];
  public int entries;

  public HashTable(int k, int p1, int p3) {
    if (k < 0 || k > 24) throw new IllegalArgumentException("k " + k);
    if ((p1 & 1) == 0) throw new IllegalArgumentException("p1 must be odd");
    if (p3 <= 0 || p3 >= 1 << 24 || !isPrime(p3)) throw new IllegalArgumentException("p3 must be a 24-bit prime");
    this.k = k; this.p1 = p1; this.p3 = p3;
    lines = 1 << k;
    keys = new int[lines * ENTRIES];
    bases = new int[lines * ENTRIES];
    java.util.Arrays.fill(keys, FREE);
    java.util.Arrays.fill(bases, FREE);
    fill = new int[lines];
  }

  /** h = ((key * p1) mod 2^32) >> (32 - k); k = 0 gives line 0 (spec 4.21.4). */
  public static int home(int key, int p1, int k) {
    return k == 0 ? 0 : (int) (((key * p1) & 0xffffffffL) >>> (32 - k));
  }

  /** The line after h in the probe: (h + p3) mod 2^k. */
  public static int step(int h, int p3, int k) {
    return (int) ((h + (long) p3) & ((1L << k) - 1));
  }

  /** Places a key; returns the steps from home (0-2), or -1 when its 3 lines are full. */
  public int insert(int key, int base) {
    if (key == FREE) throw new IllegalArgumentException("key 0xFFFFFFFF is the free entry");
    int h = home(key, p1, k);
    for (int s = 0; s < PROBE_LINES; s++) {
      if (fill[h] < ENTRIES) {
        int at = h * ENTRIES + fill[h]++;
        keys[at] = key;
        bases[at] = base;
        placedAt[s]++;
        entries++;
        return s;
      }
      h = step(h, p3, k);
    }
    return -1;
  }

  public int fullestLine() {
    int m = 0;
    for (int f : fill) m = Math.max(m, f);
    return m;
  }

  public double load() { return entries / (double) (lines * ENTRIES); }

  /** The file image: line h at byte 128 h. */
  public byte[] image() {
    ByteBuffer b = ByteBuffer.allocate(lines * LINE_BYTES).order(ByteOrder.LITTLE_ENDIAN);
    for (int i = 0; i < keys.length; i++) { b.putInt(keys[i]); b.putInt(bases[i]); }
    return b.array();
  }

  /** The smallest k with n <= 8 * 2^k: at most half full. */
  public static int minLines(int n) {
    int k = 0;
    while ((long) n > 8L << k) k++;
    return k;
  }

  /** p3: the first prime at or above the golden ratio's top 24 bits (0x9E3779), so the steps land far from home. */
  public static int defaultStep() {
    int p = 0x9E3779;
    while (!isPrime(p)) p++;
    return p;
  }

  static boolean isPrime(int n) {
    if (n < 2) return false;
    if (n % 2 == 0) return n == 2;
    for (int d = 3; (long) d * d <= n; d += 2) if (n % d == 0) return false;
    return true;
  }

  /**
   * Builds the table for sorted distinct keys: k the smallest at most half full; p1 the golden ratio if it puts every
   * key in its home line, else the best of it and 31 more odd multipliers (fewest keys away from home, then the
   * smallest fullest line) among those placing every key within 3 lines; k + 1 if none does.
   */
  public static HashTable choose(int[] sortedKeys, int[] bases) {
    int p3 = defaultStep();
    for (int k = minLines(sortedKeys.length); k <= 24; k++) {
      HashTable best = null;
      long seed = 0x243F6A8885A308D3L;                         // pi's fraction: a fixed candidate sequence
      for (int c = 0; c < 32; c++) {
        int p1;
        if (c == 0) {
          p1 = GOLDEN;
        } else {
          seed += 0x9E3779B97F4A7C15L;                           // splitmix64
          long z = seed;
          z = (z ^ (z >>> 30)) * 0xBF58476D1CE4E5B9L;
          z = (z ^ (z >>> 27)) * 0x94D049BB133111EBL;
          z ^= z >>> 31;
          p1 = (int) z | 1;
        }
        HashTable t = new HashTable(k, p1, p3);
        boolean ok = true;
        for (int i = 0; i < sortedKeys.length && ok; i++) ok = t.insert(sortedKeys[i], bases[i]) >= 0;
        if (!ok) continue;
        if (c == 0 && t.placedAt[0] == t.entries) return t;
        if (best == null || t.entries - t.placedAt[0] < best.entries - best.placedAt[0]
            || (t.entries - t.placedAt[0] == best.entries - best.placedAt[0] && t.fullestLine() < best.fullestLine())) {
          best = t;
        }
      }
      if (best != null) return best;
    }
    throw new IllegalStateException("no table found");
  }

  /** A lookup's result: the base, or MISS, and the lines read. */
  public static final int MISS = -1;

  /**
   * The Tracer's probe (spec 4.21.4) over a file image: read line h, compare its 16 entries; a match gives the base; a
   * free entry in the line, or a third full line without a match, is the map error. Returns the base (u32 as a long) in
   * the low 32 bits and the lines read in bits 32-33, or -(lines read) on a miss.
   */
  public static long probe(ByteBuffer image, int k, int p1, int p3, int key) {
    int h = home(key, p1, k);
    for (int s = 1; s <= PROBE_LINES; s++) {
      int at = h * LINE_BYTES;
      boolean free = false;
      for (int j = 0; j < ENTRIES; j++) {
        int kk = image.getInt(at + 8 * j);
        if (kk == key) return (image.getInt(at + 8 * j + 4) & 0xffffffffL) | ((long) s << 32);
        if (kk == FREE) free = true;
      }
      if (free) return -s;
      h = step(h, p3, k);
    }
    return -PROBE_LINES;
  }

  /** The lines a lookup of a present key reads (home first), into out; returns how many. */
  public int linesRead(int key, int[] out) {
    int h = home(key, p1, k);
    for (int s = 0; s < PROBE_LINES; s++) {
      out[s] = h;
      int base = h * ENTRIES;
      for (int j = 0; j < fill[h]; j++) if (keys[base + j] == key) return s + 1;
      if (fill[h] < ENTRIES) return -(s + 1);
      h = step(h, p3, k);
    }
    return -PROBE_LINES;
  }
}
