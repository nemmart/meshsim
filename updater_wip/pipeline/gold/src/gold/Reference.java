package gold;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.List;
import java.util.TreeSet;

/**
 * The reference images: a frame file played into empty memory for one segment s, the oracle the Updater's output is
 * compared with. Written to be read, not to be fast, and not the Updater's design. It reads only the frame file
 * (FrameFile) and shares no code with the writer (FrameWriter, UpdateWords, FileTree).
 *
 * <p>Every file is a real tree in a model of LPDDR (owner, 2026-11-16): 4 KB data pages under index pages of 1,024
 * 32-bit entries. An entry is a page's address in 128-byte units with the owned bit, bit 31, above it, or the hole
 * 0xFFFF_FFFF. A file is named by its root entry. A copy of a file is its source's root entry with the owned bit clear:
 * every page is shared. A write to a page the file doesn't own takes a fresh page at each level from the top that the
 * file doesn't own: a shared index page is copied with its entries' owned bits cleared, a shared data page is copied, a
 * hole becomes an index page of holes or a data page of zeros. Each file counts the pages it takes, so a page written
 * many times, in one segment or several, is taken once per file. The root is made by the file system's firmware when
 * the file is made or copied, before the run, and is not one of the pages the run takes (owner, 2026-11-16).
 *
 * <pre>
 *   program image   frame 2, into an empty file of frame 1's length
 *   base table      the base's frame 5, into an empty file (if the file establishes one)
 *   run's table     a copy of the base table when frame 4 names one, else an empty file; then frame 5
 *   cycle-0 image   a copy of the program image; then frame 6
 *   start image     a copy of the cycle-0 image; then segments 0 to s - 1 of frame 9
 *   Tracer's copy   a copy of the start image; each page segment s writes, taken unchanged
 *   final image     a copy of the start image; then segments s to 127
 * </pre>
 */
public final class Reference {
  /** The root is not one of the pages a run takes (owner, 2026-11-16). The one place that says so on this side. */
  static final boolean ROOT_IS_A_PAGE_TAKEN = false;
  static final int HOLE = 0xFFFFFFFF, OWNED = 0x80000000;
  static final int ENTRIES = 1024, WORDS = 512;

  // ---------------------------------------------------------------------------------------------- LPDDR and the trees

  /** LPDDR: pages by number, an index page as int[1024], a data page as long[512]. */
  static final class Lpddr {
    final ArrayList<Object> pages = new ArrayList<>();

    int take(Object page) {
      pages.add(page);
      return pages.size() - 1;
    }

    /** Frees every page from this one on (the pages taken for one segment's images, before the next). */
    void truncate(int size) { while (pages.size() > size) pages.remove(pages.size() - 1); }
  }

  /** A file: its root entry, its length and depth, and the pages it has taken. */
  static final class Tree {
    final Lpddr mem;
    final long length;
    final int depth;
    int root = HOLE;
    long taken;

    Tree(Lpddr mem, long lengthPages) {
      this.mem = mem;
      this.length = lengthPages;
      int d = 1;                                       // at least one index level (C3); 1,024^d pages fit
      long capacity = ENTRIES;
      while (capacity < lengthPages) { capacity *= ENTRIES; d++; }
      this.depth = d;
    }

    /** A copy: the source's pages, all shared. */
    static Tree copyOf(Tree src) {
      Tree t = new Tree(src.mem, src.length);
      t.root = src.root == HOLE ? HOLE : src.root & ~OWNED;
      return t;
    }

    static boolean owned(int entry) { return entry != HOLE && (entry & OWNED) != 0; }

    static int pageNumber(int entry) { return (entry & ~OWNED) / 32; }   // 128-byte units: 32 to a page

    static int entryFor(int page) { return page * 32 | OWNED; }

    int[] index(int entry) { return (int[]) mem.pages.get(pageNumber(entry)); }

    long[] data(int entry) { return (long[]) mem.pages.get(pageNumber(entry)); }

    /** Takes a fresh page for an entry the file doesn't own: a copy of what it points at, or empty for a hole. */
    int takeFor(int entry, boolean isIndex, boolean isRoot) {
      Object page;
      if (isIndex) {
        int[] p = new int[ENTRIES];
        if (entry == HOLE) {
          java.util.Arrays.fill(p, HOLE);
        } else {
          int[] src = index(entry);
          for (int i = 0; i < ENTRIES; i++) p[i] = src[i] == HOLE ? HOLE : src[i] & ~OWNED;   // the children stay shared
        }
        page = p;
      } else {
        page = entry == HOLE ? new long[WORDS] : data(entry).clone();
      }
      if (!isRoot || ROOT_IS_A_PAGE_TAKEN) taken++;
      return entryFor(mem.take(page));
    }

    /** The data page, owned by this file, taking pages down the path as needed. */
    long[] own(long page) {
      if (page < 0 || page >= length) throw new IllegalArgumentException("page " + page + " beyond the file's " + length + " pages");
      if (!owned(root)) root = takeFor(root, true, true);
      int[] node = index(root);
      for (int level = 1; level <= depth; level++) {
        int i = (int) ((page >>> (10 * (depth - level))) & (ENTRIES - 1));
        boolean leaf = level == depth;
        if (!owned(node[i])) node[i] = takeFor(node[i], !leaf, false);
        if (leaf) return data(node[i]);
        node = index(node[i]);
      }
      throw new IllegalStateException("depth " + depth);
    }

    void write(long cell, long value) { own(cell >>> 9)[(int) (cell & 511)] = value; }

    /** The data page's words, or null for a hole anywhere on the path. */
    long[] read(long page) {
      int e = root;
      for (int level = 1; level <= depth; level++) {
        if (e == HOLE) return null;
        e = index(e)[(int) ((page >>> (10 * (depth - level))) & (ENTRIES - 1))];
      }
      return e == HOLE ? null : data(e);
    }

    /** The image in the canonical form (NOTES section 3): every page with a non-zero byte, ascending, u64 offset and
     *  the 4,096 bytes. */
    byte[] image() {
      java.io.ByteArrayOutputStream out = new java.io.ByteArrayOutputStream();
      walk(root, 1, 0, out);
      return out.toByteArray();
    }

    private void walk(int entry, int level, long prefix, java.io.ByteArrayOutputStream out) {
      if (entry == HOLE) return;
      if (level > depth) {
        long[] w = data(entry);
        boolean zero = true;
        for (long x : w) if (x != 0) { zero = false; break; }
        if (zero) return;
        out.writeBytes(le(prefix * 4096));
        for (long x : w) out.writeBytes(le(x));
        return;
      }
      int[] node = index(entry);
      for (int i = 0; i < ENTRIES; i++) walk(node[i], level + 1, prefix * ENTRIES + i, out);
    }
  }

  static byte[] le(long v) {
    byte[] b = new byte[8];
    for (int i = 0; i < 8; i++) b[i] = (byte) (v >>> (8 * i));
    return b;
  }

  // --------------------------------------------------------------------------------------------------- update words

  /** Raw update words: count words of 8 bytes; the rest of the frame is the padding. */
  public static long[] decodeRaw(byte[] data, long count) {
    if (8 * count > data.length) throw new IllegalArgumentException(count + " raw words in " + data.length + " bytes");
    long[] w = new long[(int) count];
    for (int i = 0; i < w.length; i++) for (int j = 0; j < 8; j++) w[i] |= (data[8 * i + j] & 0xffL) << (8 * j);
    for (long i = 8 * count; i < data.length; i++) if (data[(int) i] != 0) throw new IllegalArgumentException("raw: padding isn't zero");
    return w;
  }

  /**
   * LRU codes for count words, least significant bit first within each byte, against a 32-entry move-to-front table
   * that starts all zeros: 1 and a 5-bit index is a hit (the entry moves to the front); 0, 0 and 32 bits, or 0, 1 and
   * 64 bits, is a miss (the value goes in at the front and the last entry drops out). Returns the words; bitsUsed[0]
   * gets the bits read.
   */
  public static long[] decodeLru(byte[] data, long count, long[] bitsUsed) {
    long[] table = new long[32];
    long[] out = new long[(int) count];
    long bit = 0;
    for (int n = 0; n < out.length; n++) {
      long v;
      if (bits(data, bit, 1) == 1) {
        int i = (int) bits(data, bit + 1, 5);
        bit += 6;
        v = table[i];
        System.arraycopy(table, 0, table, 1, i);
      } else if (bits(data, bit + 1, 1) == 0) {
        v = bits(data, bit + 2, 32);
        bit += 34;
        System.arraycopy(table, 0, table, 1, 31);
      } else {
        v = bits(data, bit + 2, 64);
        bit += 66;
        System.arraycopy(table, 0, table, 1, 31);
      }
      table[0] = v;
      out[n] = v;
    }
    if (bitsUsed != null) bitsUsed[0] = bit;
    return out;
  }

  /** n bits (n <= 64) from bit position at, least significant first. */
  static long bits(byte[] data, long at, int n) {
    if (at + n > 8L * data.length) throw new IllegalArgumentException("LRU codes run past the frame's data, bit " + at);
    long v = 0;
    for (int i = 0; i < n; i++) {
      long p = at + i;
      v |= (long) ((data[(int) (p >>> 3)] >>> (p & 7)) & 1) << i;
    }
    return v;
  }

  /** A frame's update words in the form its header names. */
  static long[] decode(FrameFile.Frame fr, long count) {
    return fr.lru() ? decodeLru(fr.data, count, null) : decodeRaw(fr.data, count);
  }

  /** The bits the LRU form of these words takes (to compare the forms' sizes; counted here, not by the writer). */
  public static long lruBits(long[] words) {
    long[] table = new long[32];
    long bits = 0;
    for (long v : words) {
      int hit = -1;
      for (int i = 0; i < 32; i++) if (table[i] == v) { hit = i; break; }
      if (hit >= 0) {
        bits += 6;
        System.arraycopy(table, 0, table, 1, hit);
      } else {
        bits += (v >>> 32) == 0 ? 34 : 66;
        System.arraycopy(table, 0, table, 1, 31);
      }
      table[0] = v;
    }
    return bits;
  }

  /** Applies update words to a file; checks the runs and, if pagesOnly, that every run is whole pages. */
  static void apply(Tree t, long[] u, boolean pagesOnly, String what) {
    int i = 0;
    while (i < u.length) {
      long start = u[i] & 0xffffffffL, n = u[i] >>> 32;
      if (pagesOnly && (start % 512 != 0 || n % 512 != 0)) {
        throw new IllegalArgumentException(what + ": a run at word " + start + " of " + n + " words is not whole pages");
      }
      if (i + 1 + n > u.length) throw new IllegalArgumentException(what + ": a run of " + n + " words runs past the end");
      for (long j = 0; j < n; j++) t.write(start + j, u[(int) (i + 1 + j)]);
      i += 1 + (int) n;
    }
  }

  /** The 4 KB pages a list of update words writes. */
  static TreeSet<Long> pagesOf(long[] u) {
    TreeSet<Long> s = new TreeSet<>();
    for (int i = 0; i < u.length; ) {
      long start = u[i] & 0xffffffffL, n = u[i] >>> 32;
      for (long c = start; c < start + n; c++) s.add(c >>> 9);
      i += 1 + (int) n;
    }
    return s;
  }

  // ------------------------------------------------------------------------------------------------- the reference

  public final FrameFile f;
  final Lpddr mem = new Lpddr();
  public Tree program, base, table, cycle0, start, tracer, fin;
  public long[] programWords, baseWords, tableWords, dataWords;
  public final long[][] segments;
  int mark;

  /** Decodes the streams and builds the program image, the base table, the run's table and the cycle-0 image. */
  public Reference(FrameFile f) {
    this.f = f;
    segments = new long[f.n][];
    programWords = decode(f.programFrame, f.programWords);
    if (f.baseHeader != null) baseWords = decode(f.baseDataFrame, f.baseHeader.words);
    tableWords = decode(f.get(5), f.table.words);
    dataWords = decode(f.get(6), f.dataWords);
    long total = 0;
    for (long n : f.segWords) total += n;
    long[] all = decode(f.get(9), total);
    // the stream split by frame 3's counts: each segment must be whole runs
    int at = 0;
    for (int s = 0; s < f.n; s++) {
      segments[s] = java.util.Arrays.copyOfRange(all, at, at + (int) f.segWords[s]);
      at += (int) f.segWords[s];
      for (int i = 0; i < segments[s].length; i += 1 + (int) (segments[s][i] >>> 32)) {
        if (i + 1 + (segments[s][i] >>> 32) > segments[s].length) {
          throw new IllegalArgumentException("segment " + s + ": a run crosses into the next segment");
        }
      }
    }

    program = new Tree(mem, f.fileLength);
    apply(program, programWords, true, "frame 2");
    if (f.baseHeader != null) {
      FrameFile.TableHeader b = f.baseHeader;
      if (b.path.isEmpty()) throw new IllegalArgumentException("the base table's frame 4 has no path");
      if (!b.base.isEmpty()) throw new IllegalArgumentException("the base table's frame 4 names a base of its own (" + b.base + ")");
      base = new Tree(mem, tableLength(b.k));
      apply(base, baseWords, true, "the base table's frame 5");
    }
    FrameFile.TableHeader t = f.table;
    if (!t.path.isEmpty()) throw new IllegalArgumentException("the run's frame 4 has a path (" + t.path + "): a run's table has none");
    if (!t.base.isEmpty()) {
      if (base == null || !t.base.equals(f.baseHeader.path)) {
        throw new IllegalArgumentException("frame 4 names a base table (" + t.base + ") the file doesn't establish");
      }
      FrameFile.TableHeader b = f.baseHeader;
      if (t.k != b.k || t.p1 != b.p1 || t.p3 != b.p3) throw new IllegalArgumentException("frame 4's k, p1, p3 aren't the base's");
      table = Tree.copyOf(base);
    } else {
      table = new Tree(mem, tableLength(t.k));
    }
    apply(table, tableWords, true, "frame 5");
    cycle0 = Tree.copyOf(program);
    apply(cycle0, dataWords, false, "frame 6");
    mark = mem.pages.size();
  }

  /** A jump table's length: 2^k lines of 128 bytes, in pages, at least 1. */
  static long tableLength(int k) { return Math.max(1, (128L << k) / 4096); }

  /** The images for segment s: the start image, the Tracer's copy and the final image. */
  public void forSegment(int s) {
    mem.truncate(mark);
    start = Tree.copyOf(cycle0);
    for (int k = 0; k < s; k++) apply(start, segments[k], false, "segment " + k);
    tracer = Tree.copyOf(start);
    fin = Tree.copyOf(start);
    for (long p : pagesOf(segments[s])) tracer.own(p);       // the page as it was, the Tracer's own
    for (int k = s; k < f.n; k++) apply(fin, segments[k], false, "segment " + k);
  }

  /** The run's table as the Tracer reads it: the file's bytes, line h at 128 h. */
  public byte[] tableBytes() {
    long lines = 1L << f.table.k;
    byte[] b = new byte[(int) (lines * 128)];
    for (long pg = 0; pg * 4096 < b.length; pg++) {
      long[] w = table.read(pg);
      for (int i = 0; i < WORDS && 8 * (pg * 512 + i) < b.length; i++) {
        long v = w == null ? 0 : w[i];
        System.arraycopy(le(v), 0, b, (int) (8 * (pg * 512 + i)), 8);
      }
    }
    return b;
  }

  /** The Tracer's probe (spec 4.21.4): home line ((key p1) mod 2^32) >> (32 - k), then steps of p3, at most three
   *  lines; a match gives the base, a free entry or three full lines is a miss (-1). */
  public static long probe(byte[] t, int k, int p1, int p3, int key) {
    long h = k == 0 ? 0 : ((key * (long) p1) & 0xffffffffL) >>> (32 - k);
    for (int line = 0; line < 3; line++) {
      boolean free = false;
      for (int j = 0; j < 16; j++) {
        int at = (int) (h * 128 + 8 * j);
        long e = 0;
        for (int b = 0; b < 8; b++) e |= (t[at + b] & 0xffL) << (8 * b);
        if ((int) e == key) return e >>> 32;
        if ((int) e == HOLE) free = true;
      }
      if (free) return -1;
      h = (h + (p3 & 0xffffffffL)) & ((1L << k) - 1);
    }
    return -1;
  }

  /** Every page count of frames 1, 3 and 4 against what the trees took: the program image's, the base table's, the
   *  run's table's and the data image's (s 0 only), the per-ASIC counts for s (after forSegment(s)), and segment s's
   *  pages written. Returns the differences. */
  public List<String> checkCounts(int s) {
    List<String> bad = new ArrayList<>();
    if (s == 0) bad.addAll(fileCounts());
    if (pagesOf(segments[s]).size() != f.pagesWritten[s]) {
      bad.add("segment " + s + " writes " + pagesOf(segments[s]).size() + " pages, frame 3 says " + f.pagesWritten[s]);
    }
    long[] mine = {start.taken, fin.taken, tracer.taken};
    String[] names = {"start image", "final image", "Tracer's copy"};
    for (int j = 0; j < 3; j++) {
      if (mine[j] != f.perAsic[s][j]) bad.add("s " + s + ", " + names[j] + ": took " + mine[j] + " pages, frame 3 says " + f.perAsic[s][j]);
    }
    return bad;
  }

  /** The program image's, the base table's, the run's table's and the data image's page counts. */
  public List<String> fileCounts() {
    List<String> bad = new ArrayList<>();
    if (program.taken != f.programPages) bad.add("program image: took " + program.taken + " pages, frame 1 says " + f.programPages);
    if (base != null && base.taken != f.baseHeader.pages) bad.add("base table: took " + base.taken + " pages, its frame 4 says " + f.baseHeader.pages);
    if (table.taken != f.table.pages) bad.add("jump table: took " + table.taken + " pages, frame 4 says " + f.table.pages);
    if (cycle0.taken != f.dataPages) bad.add("data image: took " + cycle0.taken + " pages, frame 3 says " + f.dataPages);
    return bad;
  }

  public static String sha256(byte[] data) {
    try {
      StringBuilder b = new StringBuilder();
      for (byte x : MessageDigest.getInstance("SHA-256").digest(data)) b.append(String.format("%02x", x));
      return b.toString();
    } catch (java.security.NoSuchAlgorithmException e) {
      throw new IllegalStateException(e);
    }
  }

  // ------------------------------------------------------------------------------------------------- frames-images

  /** frames-images &lt;frame file&gt; &lt;s&gt; [--out=&lt;prefix&gt;] */
  static boolean main(String[] args) throws IOException {
    FrameFile f = FrameFile.parse(Files.readAllBytes(Path.of(args[0])));
    int s = Integer.parseInt(args[1]);
    if (s < 0 || s >= f.n) throw new IllegalArgumentException("s must be 0 to N - 1 = " + (f.n - 1));
    String out = null;
    for (int i = 2; i < args.length; i++) {
      if (args[i].startsWith("--out=")) out = args[i].substring(6);
      else throw new IllegalArgumentException("unknown option " + args[i]);
    }
    Reference r = new Reference(f);
    r.forSegment(s);
    byte[] st = r.start.image(), tr = r.tracer.image();
    boolean same = java.util.Arrays.equals(st, tr);
    String[][] images = {{"program", null}, {"cycle0", null}, {"start-" + s, null}, {"final", null}, {"table", null}};
    byte[][] bytes = {r.program.image(), r.cycle0.image(), st, r.fin.image(), r.table.image()};
    long[] taken = {-1, r.cycle0.taken, r.start.taken, r.fin.taken, r.table.taken};
    for (int i = 0; i < images.length; i++) {
      System.out.printf("%-10s %s  %6d pages%s%n", images[i][0], sha256(bytes[i]), bytes[i].length / 4104,
          taken[i] < 0 ? "" : String.format(", %d taken", taken[i]));
      if (out != null) Files.write(Path.of(out + "." + images[i][0] + ".img"), bytes[i]);
    }
    System.out.printf("tracer     %s  (the start image's %s), %d taken%n", sha256(tr), same ? "copy: equal" : "copy: DIFFERS",
        r.tracer.taken);
    List<String> bad = r.checkCounts(s);
    for (String b : bad) System.out.println("COUNT " + b);
    System.out.println(same && bad.isEmpty() ? "ok" : "FAILED");
    return same && bad.isEmpty();
  }
}
