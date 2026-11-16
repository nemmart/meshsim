package gold;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;
import java.util.TreeSet;
import java.util.function.LongFunction;

/**
 * The host's frames for one program and one proof (v1.4_wip/planning/HOST-FRAMES.md, as ruled on 2026-11-16), in its
 * order: 1 and 2 the program image (header, data); then, if the run's table adds to a base table, that base table as
 * frames 4 and 5 on their own; then 3 the proof header, 4 and 5 the run's jump table, 6 the data image, 7 the register
 * states, 8 the advice logs, 9 the memory segments. N segments, N the number of ASICs. Gold runs once, through
 * SegmentWriter (whose own file is not written); the per-segment data is v1.3's, repacked. The choices where
 * HOST-FRAMES is open are WORKER1's (updater_wip/planning/sessions/WORKER1/NOTES.md, "Choices").
 */
public final class FrameWriter {
  /** Which form the update-word frames (2, 5, 6, 9) are sent in: the smaller (the rule), or forced, for the tests. */
  public enum Compression { AUTO, NONE, LRU }

  /** What to write. */
  public static final class Options {
    public long[] cuts;
    /** N: 64, 128 or 256. */
    public int asics = Frames.DEFAULT_ASICS;
    /** --base=static: the static targets' table is established as a base (frames 4 and 5 on their own), and the run's
     *  table adds its targets to it (C10). */
    public boolean baseStatic;
    /** For the tests: this table as the base instead of the static targets' (forces the fall-back). */
    public HashTable baseOverride;
    public Compression compression = Compression.AUTO;
    public String stem, workdir, basePath;
    /** Take gold's memory at cycle 0, at every cut and at the end, for --verify. */
    public boolean snapshots;
  }

  /** A stream of update words in both forms; the words may differ between the forms only for frame 6 (C7). */
  public static final class Stream {
    public final long[] rawWords, lruWords;
    public final byte[] raw, lru;
    public final int compression;

    Stream(long[] rawWords, long[] lruWords, Compression c) {
      this.rawWords = rawWords;
      this.lruWords = lruWords;
      this.raw = Frames.bytes(rawWords);
      this.lru = UpdateWords.lru(lruWords);
      this.compression = switch (c) {
        case NONE -> Frames.NONE;
        case LRU -> Frames.LRU;
        case AUTO -> Frames.words128(lru.length) < Frames.words128(raw.length) ? Frames.LRU : Frames.NONE;   // C2
      };
    }

    Stream(long[] words, Compression c) { this(words, words, c); }

    public byte[] sent() { return compression == Frames.LRU ? lru : raw; }

    /** The words sent. */
    public long[] words() { return compression == Frames.LRU ? lruWords : rawWords; }
  }

  private final Program program, programOnly;
  private final Path inputDir;
  private final Options opt;
  public final int n;

  // ---- results ----
  public byte[] file;
  public SegmentWriter run;
  public ProofParams params;
  public long imagePages, programPagesTaken, dataImagePages, tablePages, tablePagesTaken, basePagesTaken;
  public int imageDepth, programPages;
  /** "full", "added" (to the base), or "fallback" (a full table: the run's targets didn't fit the base). */
  public String tableMode;
  public HashTable table, base;
  public Stream programData, baseData, tableData, dataImage, segmentsStream;
  public int dataImageThreshold;
  public final long[] segmentWords, pagesWritten;
  public final long[][] perAsic;
  public final Map<String, Long> frameBytes = new LinkedHashMap<>();
  /** Gold's memory, as image digests: at each segment's cut (start of s), at cycle 0, at the end. */
  public final String[] startDigest;
  public String cycle0Digest, finalDigest;
  public double seconds;

  public Compression compression() { return opt.compression; }

  public FrameWriter(Path programDir, Path inputDir, Options opt) throws IOException {
    this.program = new Program(programDir, inputDir);
    this.programOnly = new Program(programDir, null);
    this.inputDir = inputDir;
    this.opt = opt;
    this.n = opt.asics;
    if (Arrays.stream(Frames.ASIC_COUNTS).noneMatch(x -> x == n)) throw new IllegalArgumentException("N is 64, 128 or 256, not " + n);
    if (opt.cuts.length > n) throw new IllegalArgumentException(opt.cuts.length + " cuts: the frames carry exactly N = " + n + " segments (C9)");
    segmentWords = new long[n];
    pagesWritten = new long[n];
    perAsic = new long[n][3];
    startDigest = new String[n];
  }

  public byte[] write() throws IOException {
    long t0 = System.nanoTime();
    final long lowest = program.layout.lowestAddress();
    run = new SegmentWriter(program, opt.cuts, SegmentWriter.Format.V1_3);
    final long[][] finalRegs = new long[1][];
    run.listener = new SegmentWriter.Listener() {
      public void segmentStart(int k, Machine m) {
        if (opt.snapshots) startDigest[k] = PageImage.of(m.memory, lowest).digest();
      }
      public void step(VInstr v, long pc, Machine m) {}
      public void end(Machine m) {
        finalRegs[0] = m.regs.clone();
        if (opt.snapshots) finalDigest = PageImage.of(m.memory, lowest).digest();
      }
    };
    run.write();                                                 // throws SegmentWriter.Rejected
    params = run.params;
    if (opt.snapshots) {
      cycle0Digest = startDigest[0];                             // the first cut is row 0: before any step
      for (int s = opt.cuts.length; s < n; s++) startDigest[s] = finalDigest;
    }

    // ---- the N segments (C9: past the run's end, as SegmentWriter's own past-the-end segments) ----
    List<SegmentWriter.Segment> segs = new ArrayList<>(run.segments);
    while (segs.size() < n) {
      SegmentWriter.Segment sg = new SegmentWriter.Segment();
      for (int r = 1; r < Isa.NUM_REGS; r++) sg.state[3 + r] = finalRegs[0][r];
      segs.add(sg);
    }

    // ---- the image's length (C3, ruled): every image of the proof has the program image's ----
    imagePages = FileTree.imagePages(program.layout);
    if (imagePages != Math.max(1, params.maxRamK * 8 / Frames.PAGE_BYTES)) throw new IllegalStateException("max_ram_k");
    imageDepth = FileTree.depth(imagePages);

    // ---- frames 1 and 2: the program image, every page with a non-zero byte, as update words in whole pages ----
    PageImage prog = PageImage.of(programOnly.memory, lowest);
    TreeSet<Long> progPages = new TreeSet<>();
    for (Map.Entry<Long, long[]> e : prog.pages.entrySet()) {
      if (Arrays.stream(e.getValue()).anyMatch(x -> x != 0)) progPages.add(e.getKey());
    }
    programPages = progPages.size();
    checkWithin(progPages.isEmpty() ? -1 : progPages.last(), imagePages, "the program image");
    programData = new Stream(pageRuns(progPages, pg -> prog.pages.get(pg)), opt.compression);
    programPagesTaken = FileTree.pagesTaken(progPages, imageDepth);
    Frames.Bytes f1 = new Frames.Bytes().string(opt.stem).u32(Frames.u32(imagePages, "the image's length"))
        .u32(Frames.u32(programData.words().length, "frame 2's 64-bit words"))
        .u32(Frames.u32(Frames.words128(programData.sent().length), "frame 2's 128-bit words"))
        .u32(Frames.u32(programPagesTaken, "the program image's pages"));

    // ---- the jump tables (C8, C10) ----
    HashTable full = run.jumps.table();
    table = full;
    tableMode = "full";
    if (opt.baseStatic || opt.baseOverride != null) {
      base = opt.baseOverride != null ? opt.baseOverride : run.jumps.staticTable();
      HashTable added = addTo(base, run.jumps.runTargets());
      if (added != null) { table = added; tableMode = "added"; } else { tableMode = "fallback"; }
    }
    boolean withBase = tableMode.equals("added");
    tablePages = FileTree.tablePages(table.k);
    long[] tableWords = tableWords(table, tablePages);
    long[] baseWords = withBase ? tableWords(base, tablePages) : null;
    TreeSet<Long> sentPages = new TreeSet<>();
    for (long pg = 0; pg < tablePages; pg++) {
      if (!withBase || !Arrays.equals(tableWords, (int) (pg * 512), (int) (pg * 512 + 512), baseWords, (int) (pg * 512),
          (int) (pg * 512 + 512))) sentPages.add(pg);
    }
    tableData = new Stream(pageRuns(sentPages, pg -> Arrays.copyOfRange(tableWords, (int) (pg * 512), (int) (pg * 512 + 512))),
        opt.compression);
    tablePagesTaken = FileTree.pagesTaken(UpdateWords.pages(tableData.words()), FileTree.depth(tablePages));
    Frames.Bytes f4base = null;
    if (base != null) {                                          // the base, established on its own: a full new table
      long bp = FileTree.tablePages(base.k);
      long[] bw = tableWords(base, bp);
      TreeSet<Long> all = new TreeSet<>();
      for (long pg = 0; pg < bp; pg++) all.add(pg);
      baseData = new Stream(pageRuns(all, pg -> Arrays.copyOfRange(bw, (int) (pg * 512), (int) (pg * 512 + 512))), opt.compression);
      basePagesTaken = FileTree.pagesTaken(all, FileTree.depth(bp));
      f4base = tableHeader(opt.basePath, "", base, baseData, basePagesTaken);
    }

    // ---- frame 6: the data image (C7) ----
    dataImage = dataImage(lowest, prog);
    TreeSet<Long> dataPages = UpdateWords.pages(dataImage.words());
    checkWithin(dataPages.isEmpty() ? -1 : dataPages.last(), imagePages, "the data image");
    dataImagePages = FileTree.pagesTaken(dataPages, imageDepth);

    // ---- frame 9 and the page counts ----
    long[][] lists = new long[n][];
    List<TreeSet<Long>> written = new ArrayList<>();
    for (int k = 0; k < n; k++) {
      lists[k] = segs.get(k).updates.toArray();
      segmentWords[k] = Frames.u32(lists[k].length, "segment " + k + "'s update words");
      TreeSet<Long> w = UpdateWords.pages(lists[k]);
      checkWithin(w.isEmpty() ? -1 : w.last(), imagePages, "segment " + k);
      written.add(w);
      pagesWritten[k] = w.size();
      if (k < run.segments.size() && w.size() != segs.get(k).pages4k) throw new IllegalStateException("pages written " + k);
    }
    segmentsStream = new Stream(UpdateWords.concat(lists), opt.compression);
    for (int s = 0; s < n; s++) {                              // per ASIC: start, final, Tracer's copy
      TreeSet<Long> before = new TreeSet<>(), after = new TreeSet<>();
      for (int k = 0; k < s; k++) before.addAll(written.get(k));
      for (int k = s; k < n; k++) after.addAll(written.get(k));
      perAsic[s][0] = FileTree.pagesTaken(before, imageDepth);
      perAsic[s][1] = FileTree.pagesTaken(after, imageDepth);
      perAsic[s][2] = FileTree.pagesTaken(written.get(s), imageDepth);
    }

    // ---- frame 3 ----
    Frames.Bytes f3 = new Frames.Bytes();
    f3.u32(Frames.FORMAT_VERSION).string(opt.stem).string(opt.workdir);
    byte[] f7 = params.frame7();                                  // u32 version, u32 n, n x u64: take n and the fields
    f3.raw(Arrays.copyOfRange(f7, 4, f7.length));
    f3.u32(Frames.u32(dataImage.words().length, "frame 6's 64-bit words"))
        .u32(Frames.u32(Frames.words128(dataImage.sent().length), "frame 6's 128-bit words"))
        .u32(Frames.u32(dataImagePages, "the data image's pages"));
    for (int k = 0; k < n; k++) f3.u32(segmentWords[k]);
    f3.u32(Frames.u32(Frames.words128(segmentsStream.sent().length), "frame 9's 128-bit words"));
    for (int k = 0; k < n; k++) f3.u32(Frames.u32(pagesWritten[k], "pages written"));
    for (int s = 0; s < n; s++) for (int j = 0; j < 3; j++) f3.u32(Frames.u32(perAsic[s][j], "pages"));

    // ---- frame 4: the run's table ----
    Frames.Bytes f4 = tableHeader("", withBase ? opt.basePath : "", table, tableData, tablePagesTaken);

    // ---- frames 7 and 8 ----
    Frames.Bytes f7b = new Frames.Bytes(), f8 = new Frames.Bytes();
    for (SegmentWriter.Segment sg : segs) f7b.raw(run.stateRecord(sg));
    for (SegmentWriter.Segment sg : segs) f8.u32(Frames.u32(sg.advice.size(), "advice values"));
    for (SegmentWriter.Segment sg : segs) {
      long[] a = sg.advice.toArray();
      f8.raw(Frames.bytes(a));
      if (a.length % 2 == 1) f8.u64(0);
    }

    // ---- the file: 1, 2, (4, 5 the base), 3, 4, 5, 6, 7, 8, 9 ----
    ByteArrayOutputStream out = new ByteArrayOutputStream();
    frameBytes.put("1", Frames.frame(out, Frames.PROGRAM_HEADER, Frames.NONE, f1.toArray()));
    frameBytes.put("2", Frames.frame(out, Frames.PROGRAM_DATA, programData.compression, programData.sent()));
    if (base != null) {
      frameBytes.put("4 base", Frames.frame(out, Frames.TABLE_HEADER, Frames.NONE, f4base.toArray()));
      frameBytes.put("5 base", Frames.frame(out, Frames.TABLE_DATA, baseData.compression, baseData.sent()));
    }
    frameBytes.put("3", Frames.frame(out, Frames.PROOF_HEADER, Frames.NONE, f3.toArray()));
    frameBytes.put("4", Frames.frame(out, Frames.TABLE_HEADER, Frames.NONE, f4.toArray()));
    frameBytes.put("5", Frames.frame(out, Frames.TABLE_DATA, tableData.compression, tableData.sent()));
    frameBytes.put("6", Frames.frame(out, Frames.DATA_IMAGE, dataImage.compression, dataImage.sent()));
    frameBytes.put("7", Frames.frame(out, Frames.REGISTERS, Frames.NONE, f7b.toArray()));
    frameBytes.put("8", Frames.frame(out, Frames.ADVICE, Frames.NONE, f8.toArray()));
    frameBytes.put("9", Frames.frame(out, Frames.SEGMENTS, segmentsStream.compression, segmentsStream.sent()));
    file = out.toByteArray();
    seconds = (System.nanoTime() - t0) / 1e9;
    return file;
  }

  /** Frame 4: path (a base table on its own; empty for a run's), base (empty: a full new table), k, p1, p3, counts. */
  static Frames.Bytes tableHeader(String path, String baseName, HashTable t, Stream data, long pages) {
    return new Frames.Bytes().string(path).string(baseName).u32(t.k).u32(t.p1).u32(t.p3)
        .u32(Frames.u32(data.words().length, "frame 5's 64-bit words"))
        .u32(Frames.u32(Frames.words128(data.sent().length), "frame 5's 128-bit words"))
        .u32(Frames.u32(pages, "the jump table's pages"));
  }

  /** The base with the run's targets not already in it inserted in ascending key order; null if one doesn't fit within
   *  the probe's three lines (the fall-back to a full new table). */
  static HashTable addTo(HashTable base, Map<Integer, Integer> runTargets) {
    HashTable added = base.copy();
    long[] keys = runTargets.keySet().stream().mapToLong(k -> k & 0xffffffffL).sorted().toArray();
    for (long key : keys) {
      int idx = runTargets.get((int) key);
      long have = added.find((int) key);
      if (have >= 0) {
        if (have != (idx & 0xffffffffL)) {
          throw new IllegalStateException(String.format("target o=%#x: base %d in the base table, %d in the run", key, have, idx));
        }
        continue;
      }
      if (added.insert((int) key, idx) < 0) return null;
    }
    return added;
  }

  /** A table's file as 64-bit words (an entry: key [31:0], base [63:32]), padded to whole pages with free entries. */
  static long[] tableWords(HashTable t, long pages) {
    byte[] img = t.image();
    long[] w = new long[(int) (pages * Frames.PAGE_WORDS)];
    Arrays.fill(w, -1L);
    for (int i = 0; i < img.length / 8; i++) {
      long v = 0;
      for (int j = 0; j < 8; j++) v |= (img[8 * i + j] & 0xffL) << (8 * j);
      w[i] = v;
    }
    return w;
  }

  /** Maximal runs of consecutive whole pages (C8), each page's 512 words from words(page). */
  static long[] pageRuns(TreeSet<Long> pages, LongFunction<long[]> words) {
    SegmentWriter.LongList out = new SegmentWriter.LongList();
    long[] p = UpdateWords.sorted(pages);
    for (int a = 0; a < p.length; ) {
      int b = a + 1;
      while (b < p.length && p[b] == p[b - 1] + 1) b++;
      out.add(UpdateWords.header(p[a] * 512, (long) (b - a) * 512));
      for (int i = a; i < b; i++) for (long x : words.apply(p[i])) out.add(x);
      a = b;
    }
    return out.toArray();
  }

  /** The thresholds tried for frame 6: a stretch of at least this many zero words starts a new run. */
  static final int[] THRESHOLDS = {2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64, 128, 256, Integer.MAX_VALUE};

  /**
   * Frame 6 (C7, ruled: the least data). The three input files, each as its words (its length rounded up to 8 bytes);
   * leading and trailing zero words dropped, and a stretch of at least T zero words skipped by ending the run and
   * starting another, for the T that makes the form smallest: for the raw form T = 2 (a skipped word saves 8 bytes,
   * a new run costs one header word), for LRU the best of THRESHOLDS by encoding each. A word is skipped only if the
   * program image is zero there (the cycle-0 image is a copy of it).
   */
  Stream dataImage(long lowest, PageImage prog) throws IOException {
    Layout l = program.layout;
    TreeMap<Long, long[]> files = new TreeMap<>();               // start cell -> the file's words
    String[] names = {"input.bin", "trusted_advice.bin", "untrusted_advice.bin"};
    long[] starts = {l.inputStart, l.trustedAdviceStart, l.untrustedAdviceStart};
    for (int i = 0; i < 3; i++) {
      Path f = inputDir.resolve(names[i]);
      if (!Files.exists(f)) continue;
      byte[] b = Files.readAllBytes(f);
      if (b.length == 0) continue;
      long[] w = new long[(b.length + 7) / 8];
      for (int j = 0; j < b.length; j++) w[j / 8] |= (b[j] & 0xffL) << (8 * (j % 8));
      files.put((starts[i] - lowest) / 8, w);
    }
    long[] rawWords = dataImageWords(files, 2, prog);
    long[] bestLru = null;
    int bestT = 0;
    long bestBytes = Long.MAX_VALUE;
    for (int t : THRESHOLDS) {
      long[] w = dataImageWords(files, t, prog);
      long bytes = UpdateWords.lru(w).length;
      if (bytes <= bestBytes) { bestBytes = bytes; bestLru = w; bestT = t; }   // ties: the larger T, fewer runs
    }
    dataImageThreshold = bestT;
    return new Stream(rawWords, bestLru, opt.compression);
  }

  static long[] dataImageWords(TreeMap<Long, long[]> files, int threshold, PageImage prog) {
    SegmentWriter.LongList out = new SegmentWriter.LongList();
    for (Map.Entry<Long, long[]> e : files.entrySet()) {
      long c0 = e.getKey();
      long[] w = e.getValue();
      // a word may be left out only if it is zero and the program image is zero there
      boolean[] needed = new boolean[w.length];
      for (int i = 0; i < w.length; i++) needed[i] = w[i] != 0 || prog.word(c0 + i) != 0;
      int i = 0;
      while (i < w.length) {
        while (i < w.length && !needed[i]) i++;                   // leading zeros
        if (i == w.length) break;
        int a = i, last = i;                                       // a run from a to last, ending at a needed word
        int j = i + 1;
        while (j < w.length) {
          if (needed[j]) { last = j; j++; continue; }
          int z = j;
          while (z < w.length && !needed[z]) z++;
          if (z == w.length || z - j >= threshold) break;          // trailing zeros, or a stretch worth skipping
          j = z;
        }
        out.add(UpdateWords.header(c0 + a, last - a + 1));
        for (int x = a; x <= last; x++) out.add(w[x]);
        i = last + 1;
      }
    }
    return out.toArray();
  }

  static void checkWithin(long lastPage, long lengthPages, String what) {
    if (lastPage >= lengthPages) {
      throw new IllegalStateException(what + " writes page " + lastPage + ", beyond the file's " + lengthPages + " pages");
    }
  }

  /** The side file: every frame's bytes, both forms of 2, 5, 6 and 9, the page counts. */
  public String json(String programDir, String inputDir) {
    StringBuilder b = new StringBuilder("{\n");
    b.append(String.format("  \"program\": \"%s\",%n  \"input\": \"%s\",%n", programDir, inputDir));
    b.append(String.format("  \"n\": %d,%n  \"rows\": %d,%n  \"cuts\": %d,%n  \"file_bytes\": %d,%n  \"file_sha256\": \"%s\",%n",
        n, run.totalRows, opt.cuts.length, file.length, PageImage.sha256(file)));
    b.append(String.format("  \"log2_k\": %d,%n  \"image_pages\": %d,%n  \"image_depth\": %d,%n  \"program_pages\": %d,%n"
        + "  \"program_pages_taken\": %d,%n", params.log2K, imagePages, imageDepth, programPages, programPagesTaken));
    b.append("  \"frame_bytes\": {");
    String sep = "";
    for (Map.Entry<String, Long> e : frameBytes.entrySet()) { b.append(sep).append(String.format("\"%s\": %d", e.getKey(), e.getValue())); sep = ", "; }
    b.append("},\n");
    b.append(String.format("  \"table\": {\"mode\": \"%s\", \"k\": %d, \"p1\": %d, \"p3\": %d, \"entries\": %d, \"file_pages\": %d,"
        + " \"pages_taken\": %d, \"base_entries\": %d, \"base_pages_taken\": %d, \"run_targets\": %d},%n", tableMode, table.k,
        table.p1 & 0xffffffffL, table.p3, table.entries, tablePages, tablePagesTaken, base == null ? 0 : base.entries,
        basePagesTaken, run.jumps.runTargets().size()));
    stream(b, "2", programData);
    if (baseData != null) stream(b, "5_base", baseData);
    stream(b, "5", tableData);
    stream(b, "6", dataImage);
    stream(b, "9", segmentsStream);
    b.append(String.format("  \"data_image_lru_threshold\": %d,%n  \"data_image_pages\": %d,%n", dataImageThreshold, dataImagePages));
    b.append("  \"pages_written\": ").append(Arrays.toString(pagesWritten)).append(",\n");
    b.append("  \"segment_words\": ").append(Arrays.toString(segmentWords)).append(",\n");
    b.append("  \"per_asic\": [");
    for (int s = 0; s < n; s++) b.append(s == 0 ? "" : ", ").append(Arrays.toString(perAsic[s]));
    b.append("]\n}\n");
    return b.toString();
  }

  static void stream(StringBuilder b, String name, Stream s) {
    b.append(String.format("  \"stream_%s\": {\"raw_words\": %d, \"lru_words\": %d, \"raw_bytes\": %d, \"lru_bytes\": %d,"
        + " \"raw_frame_bytes\": %d, \"lru_frame_bytes\": %d, \"sent\": \"%s\"},%n", name, s.rawWords.length,
        s.lruWords.length, s.raw.length, s.lru.length, 16 * (1 + Frames.words128(s.raw.length)),
        16 * (1 + Frames.words128(s.lru.length)), s.compression == Frames.LRU ? "lru" : "none"));
  }
}
