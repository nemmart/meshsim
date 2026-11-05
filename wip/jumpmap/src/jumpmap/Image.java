package jumpmap;

import gold.Memory;

import java.io.IOException;
import java.io.OutputStream;
import java.lang.reflect.Field;
import java.security.MessageDigest;
import java.util.Arrays;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;

/**
 * A segment's start image (WORKER26/PROMPT.md item 3b): the guest's memory as 4 KB pages of `.pages`, page n at offset
 * 4096 n, that is at guest address lowest_address + 4096 n. Built from gold's cycle-0 image, then each segment's update
 * words applied in order; checked against gold's own memory at the next segment's first row.
 */
public final class Image {
  public static final int PAGE_SHIFT = 12, PAGE_WORDS = 512;

  final long lowest;
  /** page number -> its 512 words. */
  final TreeMap<Integer, long[]> pages = new TreeMap<>();

  public Image(long lowest) {
    if ((lowest & 0xfff) != 0) throw new IllegalArgumentException(String.format("lowest_address %#x not 4 KB aligned", lowest));
    this.lowest = lowest;
  }

  public int pageOf(long addr) { return (int) ((addr - lowest) >>> PAGE_SHIFT); }

  long[] page(int n) { return pages.computeIfAbsent(n, x -> new long[PAGE_WORDS]); }

  /** Lists every page of [start, end) (guest addresses), zero where nothing is written. */
  public void cover(long start, long end) {
    if (end <= start) return;
    if (start < lowest) throw new IllegalArgumentException(String.format("%#x below lowest_address", start));
    for (int n = pageOf(start); n <= pageOf(end - 1); n++) page(n);
  }

  /** Gold's memory pages (16 KB of 8-byte words), read by reflection: Memory has no page iterator. */
  static long[][] goldPages(Memory m) {
    try {
      Field f = Memory.class.getDeclaredField("pages");
      f.setAccessible(true);
      return (long[][]) f.get(m);
    } catch (ReflectiveOperationException e) {
      throw new IllegalStateException("gold.Memory's pages", e);
    }
  }

  static final int GOLD_SUBPAGES = 1 << (Memory.PAGE_SHIFT - PAGE_SHIFT);

  /** Copies every 4 KB page of gold's memory holding a non-zero word (gold's cycle-0 image). */
  public void copyNonZero(Memory m) {
    long[][] gp = goldPages(m);
    for (int gi = 0; gi < gp.length; gi++) {
      if (gp[gi] == null) continue;
      for (int s = 0; s < GOLD_SUBPAGES; s++) {
        int from = s * PAGE_WORDS;
        if (allZero(gp[gi], from)) continue;
        long addr = ((long) gi << Memory.PAGE_SHIFT) + ((long) s << PAGE_SHIFT);
        if (addr < lowest) throw new IllegalStateException(String.format("data at %#x below lowest_address", addr));
        System.arraycopy(gp[gi], from, page(pageOf(addr)), 0, PAGE_WORDS);
      }
    }
  }

  private static boolean allZero(long[] a, int from) {
    for (int i = from; i < from + PAGE_WORDS; i++) if (a[i] != 0) return false;
    return true;
  }

  /** Applies update words (section 16: a header, start cell low 32 and count high 32, then the values). */
  public void apply(long[] u) {
    int i = 0;
    while (i < u.length) {
      long cell = u[i] & 0xffffffffL, n = u[i] >>> 32;
      i++;
      for (long j = 0; j < n; j++, i++) {
        long c = cell + j;
        page((int) (c >>> 9))[(int) (c & 511)] = u[i];
      }
    }
  }

  /** The pages an update list writes. */
  public static Set<Integer> written(long[] u) {
    Set<Integer> s = new HashSet<>();
    int i = 0;
    while (i < u.length) {
      long cell = u[i] & 0xffffffffL, n = u[i] >>> 32;
      if (n > 0) for (long p = cell >>> 9; p <= (cell + n - 1) >>> 9; p++) s.add((int) p);
      i += 1 + (int) n;
    }
    return s;
  }

  /** null if this image equals gold's memory word for word (every page; an unlisted page must be all zero there). */
  public String check(Memory m) {
    long[][] gp = goldPages(m);
    for (int gi = 0; gi < gp.length; gi++) {
      if (gp[gi] == null) continue;
      for (int s = 0; s < GOLD_SUBPAGES; s++) {
        long addr = ((long) gi << Memory.PAGE_SHIFT) + ((long) s << PAGE_SHIFT);
        int from = s * PAGE_WORDS;
        long[] mine = addr >= lowest ? pages.get(pageOf(addr)) : null;
        if (mine == null) {
          if (!allZero(gp[gi], from)) return String.format("gold has data at %#x, a page not in the image", addr);
          continue;
        }
        for (int w = 0; w < PAGE_WORDS; w++) {
          if (mine[w] != gp[gi][from + w]) {
            return String.format("word %#x: image %#x, gold %#x", addr + 8L * w, mine[w], gp[gi][from + w]);
          }
        }
      }
    }
    for (Map.Entry<Integer, long[]> e : pages.entrySet()) {
      long addr = lowest + ((long) e.getKey() << PAGE_SHIFT);
      long[] g = gp[(int) (addr >>> Memory.PAGE_SHIFT)];
      if (g == null) {
        for (int w = 0; w < PAGE_WORDS; w++) {
          if (e.getValue()[w] != 0) return String.format("image has data at %#x, gold has none", addr + 8L * w);
        }
      }
    }
    return null;
  }

  /** The page list (WORKER22's format): per page u64 offset, u32 flags (bit 0 owned), u32 0, 4096 bytes; ascending. */
  public String write(Set<Integer> owned, OutputStream out) throws IOException {
    for (int n : owned) page(n);
    MessageDigest md;
    try { md = MessageDigest.getInstance("SHA-256"); } catch (Exception e) { throw new IllegalStateException(e); }
    byte[] rec = new byte[16 + 8 * PAGE_WORDS];
    for (Map.Entry<Integer, long[]> e : pages.entrySet()) {
      Arrays.fill(rec, (byte) 0);
      putLong(rec, 0, (e.getKey() & 0xffffffffL) << PAGE_SHIFT);
      putLong(rec, 8, owned.contains(e.getKey()) ? 1 : 0);
      long[] w = e.getValue();
      for (int i = 0; i < PAGE_WORDS; i++) putLong(rec, 16 + 8 * i, w[i]);
      md.update(rec);
      if (out != null) out.write(rec);
    }
    return Main.hex(md.digest());
  }

  public int size() { return pages.size(); }

  static void putLong(byte[] b, int at, long v) {
    for (int i = 0; i < 8; i++) b[at + i] = (byte) (v >>> (8 * i));
  }
}
