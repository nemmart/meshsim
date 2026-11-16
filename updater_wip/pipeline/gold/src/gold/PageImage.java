package gold;

import java.io.ByteArrayOutputStream;
import java.security.MessageDigest;
import java.util.Map;
import java.util.TreeMap;

/**
 * Gold's memory as a file image (the writer's side): 4 KB pages by their byte offset in the file, offset = guest
 * address - lowest_address. Canonical form (WORKER1 NOTES section 3): every page with a non-zero byte, ascending, each
 * as u64 offset then the 4,096 bytes; the digest is its SHA-256. The Reference writes the same form from its own trees.
 */
public final class PageImage {
  /** page number (offset / 4096) -> its 512 words. */
  public final TreeMap<Long, long[]> pages = new TreeMap<>();

  /** Every non-zero word of gold's memory, at or above lowestAddress; a non-zero word below it is an error. */
  public static PageImage of(Memory m, long lowestAddress) {
    PageImage img = new PageImage();
    for (int gi = 0; gi < Memory.pageCount(); gi++) {
      long[] gp = m.pageOrNull(gi);
      if (gp == null) continue;
      long base = (long) gi << Memory.PAGE_SHIFT;
      for (int w = 0; w < gp.length; w++) {
        if (gp[w] == 0) continue;
        long addr = base + 8L * w;
        if (addr < lowestAddress) {
          throw new IllegalStateException(String.format("gold has data at %#x, below lowest_address %#x", addr, lowestAddress));
        }
        long cell = (addr - lowestAddress) >>> 3;
        img.pages.computeIfAbsent(cell >>> 9, x -> new long[Frames.PAGE_WORDS])[(int) (cell & 511)] = gp[w];
      }
    }
    return img;
  }

  /** The canonical form. */
  public byte[] canonical() {
    ByteArrayOutputStream out = new ByteArrayOutputStream();
    for (Map.Entry<Long, long[]> e : pages.entrySet()) {
      boolean zero = true;
      for (long w : e.getValue()) if (w != 0) { zero = false; break; }
      if (zero) continue;
      byte[] rec = new byte[8 + Frames.PAGE_BYTES];
      for (int i = 0; i < 8; i++) rec[i] = (byte) ((e.getKey() << 12) >>> (8 * i));
      long[] v = e.getValue();
      for (int w = 0; w < v.length; w++) for (int j = 0; j < 8; j++) rec[8 + 8 * w + j] = (byte) (v[w] >>> (8 * j));
      out.writeBytes(rec);
    }
    return out.toByteArray();
  }

  public String digest() { return sha256(canonical()); }

  /** The word at a cell, 0 where no page is. */
  public long word(long cell) {
    long[] p = pages.get(cell >>> 9);
    return p == null ? 0 : p[(int) (cell & 511)];
  }

  public static String sha256(byte[] data) {
    try {
      byte[] d = MessageDigest.getInstance("SHA-256").digest(data);
      StringBuilder b = new StringBuilder();
      for (byte x : d) b.append(String.format("%02x", x));
      return b.toString();
    } catch (java.security.NoSuchAlgorithmException e) {
      throw new IllegalStateException(e);
    }
  }
}
