package gold;

import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Random;
import java.util.TreeSet;

/**
 * Tests of the host's frames that need no workload (WORKER1):
 * <ol>
 *   <li>the page counts two ways on synthetic writes: FileTree's sets against Reference's copy-on-write trees, at depths
 *       1 to 3, with pages written in many segments (each counted once per file), holes, and an empty segment;</li>
 *   <li>the checks catch what they are for: a frame file for a guest is broken one way at a time, and the reader, the
 *       reference or --verify must fail each time;</li>
 *   <li>the fall-back from adding to a base table to a full new table, forced with a base already full.</li>
 * </ol>
 * Usage: FramesTest &lt;program dir&gt; &lt;input dir&gt; &lt;segment rows&gt; &lt;program dir&gt; &lt;input dir&gt;: the broken files
 * from the first guest (it must write memory), the fall-back on the second (its run must take jumps).
 */
public final class FramesTest {
  static int failures;

  public static void main(String[] args) throws Exception {
    counts();
    mutations(Path.of(args[0]), Path.of(args[1]), Long.parseLong(args[2]));
    fallback(Path.of(args[3]), Path.of(args[4]));
    System.out.println(failures == 0 ? "frames test: ok" : "frames test: " + failures + " FAILED");
    System.exit(failures == 0 ? 0 : 1);
  }

  static void expect(boolean ok, String what) {
    if (!ok) { failures++; System.out.println("FAIL " + what); }
  }

  // ---- 1. the counts two ways ----

  static void counts() {
    Random rnd = new Random(20261116);
    long[] lengths = {1, 700, 1024, 1025, 40_000, 1L << 20, (1L << 20) + 1, 3L << 20};
    int cases = 0;
    for (long length : lengths) {
      for (int trial = 0; trial < 6; trial++) {
        // 128 segments' written pages: a few hot pages written in many segments, scattered ones, clusters
        List<TreeSet<Long>> written = new ArrayList<>();
        long[] hot = new long[3];
        for (int i = 0; i < 3; i++) hot[i] = (long) (rnd.nextDouble() * length);
        for (int s = 0; s < 128; s++) {
          TreeSet<Long> w = new TreeSet<>();
          if (s % 17 != 5) {
            for (long h : hot) if (rnd.nextInt(3) == 0) w.add(h);
            int n = rnd.nextInt(trial + 2);
            for (int i = 0; i < n; i++) w.add((long) (rnd.nextDouble() * length));
            if (rnd.nextInt(8) == 0) {                                  // a cluster of consecutive pages
              long c = (long) (rnd.nextDouble() * Math.max(1, length - 40));
              for (long p = c; p < Math.min(length, c + 40); p++) w.add(p);
            }
          }
          written.add(w);
        }
        int depth = FileTree.depth(length);
        // the reference's trees: a source image with some pages, then start, final, Tracer's copy for a few s
        Reference.Lpddr mem = new Reference.Lpddr();
        Reference.Tree source = new Reference.Tree(mem, length);
        for (int i = 0; i < 20; i++) source.write((long) (rnd.nextDouble() * length) * 512, 1);
        expect(source.depth == depth, "depth of " + length + " pages: tree " + source.depth + ", FileTree " + depth);
        int mark = mem.pages.size();
        for (int s : new int[] {0, 1, 5, 64, 127}) {
          mem.truncate(mark);
          Reference.Tree start = Reference.Tree.copyOf(source);
          for (int k = 0; k < s; k++) for (long p : written.get(k)) start.write(p * 512 + rnd.nextInt(512), k);
          Reference.Tree tracer = Reference.Tree.copyOf(start), fin = Reference.Tree.copyOf(start);
          for (long p : written.get(s)) tracer.own(p);
          for (int k = s; k < 128; k++) for (long p : written.get(k)) fin.write(p * 512 + rnd.nextInt(512), k);
          TreeSet<Long> before = new TreeSet<>(), after = new TreeSet<>();
          for (int k = 0; k < s; k++) before.addAll(written.get(k));
          for (int k = s; k < 128; k++) after.addAll(written.get(k));
          String at = String.format("length %d, trial %d, s %d", length, trial, s);
          expect(start.taken == FileTree.pagesTaken(before, depth), at + ": start " + start.taken + " vs " + FileTree.pagesTaken(before, depth));
          expect(fin.taken == FileTree.pagesTaken(after, depth), at + ": final " + fin.taken + " vs " + FileTree.pagesTaken(after, depth));
          expect(tracer.taken == FileTree.pagesTaken(written.get(s), depth), at + ": tracer " + tracer.taken);
          cases++;
        }
      }
    }
    // a page written again in a later segment is taken once: by hand
    Reference.Lpddr mem = new Reference.Lpddr();
    Reference.Tree t = new Reference.Tree(mem, 1L << 20);
    t.write(5, 1);
    t.write(6, 2);
    t.write(512 * 1024 * 3 + 7, 3);
    t.write(5, 4);
    expect(t.taken == 2 + 2, "by hand: two second-level index pages, two data pages (the root is the firmware's) = 4, took " + t.taken);
    expect(FileTree.pagesTaken(new TreeSet<>(List.of(0L, 3072L, 0L)), 2) == 4, "by hand: FileTree 4");
    System.out.println("counts two ways: " + cases + " cases (depths 1-3, 128 segments, pages rewritten across segments)");
  }

  // ---- 2. the checks catch what they are for ----

  static FrameWriter.Options options(long segmentRows, int asics) {
    FrameWriter.Options o = new FrameWriter.Options();
    o.asics = asics;
    o.cuts = SegmentWriter.defaultCuts(segmentRows, asics);
    o.stem = "/programs/test";
    o.workdir = "/work/test";
    o.basePath = "/programs/test.base";
    o.snapshots = true;
    return o;
  }

  static void mutations(Path program, Path input, long segmentRows) throws Exception {
    FrameWriter.Options o = options(segmentRows, 64);
    o.compression = FrameWriter.Compression.NONE;
    FrameWriter w = new FrameWriter(program, input, o);
    byte[] good = w.write();
    expect(Verify.run(w, good), "the good file verifies");
    FrameFile f = FrameFile.parse(good);
    expect(f.n == 64, "N inferred from frame 7: " + f.n);
    expect(FrameReader.check(f), "the good file checks");

    int n = f.n;
    int f1pages = (int) f.get(1).offset + 16 + str(f.programStem) + 12;
    int f3 = (int) f.get(3).offset + 16;
    int strings = 4 + str(f.stem) + str(f.workdir);
    int params = 4 + 8 * f.params.length;
    int dataPages = f3 + strings + params + 8;
    int perAsic = dataPages + 4 + 4 * n + 4 + 4 * n;
    int f4pages = (int) f.get(4).offset + 16 + str(f.table.path) + str(f.table.base) + 20;

    mutated(good, "frame 1's pages + 1", b -> add32(b, f1pages, 1), true, false);
    mutated(good, "a per-ASIC count (s 0, final image) + 1", b -> add32(b, perAsic + 4, 1), true, false);
    mutated(good, "the Tracer's copy count of the last segment - 1", b -> add32(b, perAsic + 12 * (n - 1) + 8, -1), true, false);
    mutated(good, "frame 3's data image pages + 1", b -> add32(b, dataPages, 1), true, false);
    mutated(good, "frame 4's pages + 1", b -> add32(b, f4pages, 1), true, false);
    mutated(good, "a reserved header bit", b -> b[3] = 1, true, true);
    mutated(good, "compression on frame 3", b -> b[(int) f.get(3).offset] = 1, true, true);
    mutated(good, "frame 7's advice field against frame 8", b -> b[(int) f.get(7).offset + 16 + 16 + 12] ^= (byte) 0x40, true, false);
    long total = 0;
    for (long x : f.segWords) total += x;
    expect(total > 1, "the broken-file guest writes memory");
    int v = (int) f.get(9).offset + 16 + 8 + 7;                   // frame 9's first value (raw), its high byte
    mutated(good, "frame 9's first value", b -> b[v] ^= 1, false, false, w);
  }

  static void fallback(Path program, Path input) throws Exception {
    // the fall-back (HOST-FRAMES, Open): a base that can't take the run's targets, a one-line table already full
    HashTable full = new HashTable(0, 0x6c442cb7, 10368899);
    for (int i = 0; i < 16; i++) full.insert(0x7000_0000 + i, i);
    FrameWriter.Options ob = options(4, 128);
    ob.baseOverride = full;
    FrameWriter fb = new FrameWriter(program, input, ob);
    byte[] fallback = fb.write();
    FrameFile ff = FrameFile.parse(fallback);
    boolean targets = !fb.run.jumps.runTargets().isEmpty();
    expect(targets, "the fall-back test needs a guest whose run takes jumps");
    expect(fb.tableMode.equals("fallback"), "the run's table falls back: " + fb.tableMode);
    expect(ff.baseHeader != null && ff.baseHeader.k == 0 && ff.table.base.isEmpty() && ff.table.k == fb.run.jumps.table().k,
        "the base still established; the run's frame 4 names none, with the full table's k");
    expect(Verify.run(fb, fallback), "the fall-back verifies (every jump found in the full new table)");
    expect(FrameReader.check(ff), "the fall-back checks");
    System.out.println("the fall-back to a full new table: " + fb.tableMode + ", " + fb.run.jumps.runTargets().size()
        + " run targets, the full table's k " + fb.table.k);
  }

  static int str(String s) { return 4 + (s.getBytes(java.nio.charset.StandardCharsets.UTF_8).length + 3) / 4 * 4; }

  static void add32(byte[] b, int at, int d) {
    long v = 0;
    for (int i = 0; i < 4; i++) v |= (b[at + i] & 0xffL) << (8 * i);
    v += d;
    for (int i = 0; i < 4; i++) b[at + i] = (byte) (v >>> (8 * i));
  }

  interface Mutation { void apply(byte[] b); }

  static void mutated(byte[] good, String what, Mutation m, boolean byCheck, boolean byParse) {
    mutated(good, what, m, byCheck, byParse, null);
  }

  /** The check must fail: the parse (byParse), the reader's --check (byCheck), or --verify against gold (w). */
  static void mutated(byte[] good, String what, Mutation m, boolean byCheck, boolean byParse, FrameWriter w) {
    byte[] b = Arrays.copyOf(good, good.length);
    m.apply(b);
    boolean caught;
    try {
      FrameFile f = FrameFile.parse(b);
      caught = !byParse && (byCheck ? !FrameReader.check(f) : !Verify.run(w, b));
    } catch (IllegalArgumentException e) {
      caught = byParse || byCheck;
    }
    System.out.println((caught ? "caught: " : "MISSED: ") + what);
    expect(caught, "the checks miss: " + what);
  }
}
