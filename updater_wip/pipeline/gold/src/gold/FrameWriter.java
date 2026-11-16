package gold;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;
import java.util.TreeSet;

/**
 * The host's frames for one program and one proof (v1.4_wip/planning/HOST-FRAMES.md), in its order: 1 the ELF (the
 * program image), 2 the base jump table (optional), 3 the proof header, 4 the jump table header, 5 the jump table data,
 * 6 the data image, 7 the register states, 8 the advice logs, 9 the memory segments. Gold runs once, through
 * SegmentWriter (whose own file is not written); the per-segment data is v1.3's, repacked. The choices where
 * HOST-FRAMES is open are WORKER1's (updater_wip/planning/sessions/WORKER1/NOTES.md, "Choices", C1-C11).
 */
public final class FrameWriter {
  /** What to write. */
  public static final class Options {
    public long[] cuts;
    /** --base=static: frame 2 is the static targets' table, and the run's table adds to it (C10). */
    public boolean baseStatic;
    /** Which of frames 5, 6 and 9 are sent LRU-coded. */
    public boolean lru5, lru6, lru9;
    public String stem, workdir, basePath;
    /** Take gold's memory at cycle 0, at every cut and at the end, for --verify. */
    public boolean snapshots;
  }

  /** A stream of update words, in both forms. */
  public static final class Stream {
    public final long[] words;
    public final byte[] raw, lru;
    public final boolean sendLru;

    Stream(long[] words, boolean sendLru) {
      this.words = words;
      this.raw = Frames.bytes(words);
      this.lru = UpdateWords.lru(words);
      this.sendLru = sendLru;
    }

    public byte[] sent() { return sendLru ? lru : raw; }
  }

  private final Program program, programOnly;
  private final Path inputDir;
  private final Options opt;

  // ---- results ----
  public byte[] file;
  public SegmentWriter run;
  public ProofParams params;
  public long imagePages, dataImagePages, tablePages, tablePagesTaken;
  public int imageDepth;
  /** "full", "added" (to the static base), or "fallback" (a full table: the run's targets didn't fit the base). */
  public String tableMode;
  public HashTable table, base;
  public Stream tableData, dataImage, segmentsStream;
  public final long[] segmentWords = new long[Frames.SEGMENT_COUNT];
  public final long[] pagesWritten = new long[Frames.SEGMENT_COUNT];
  public final long[][] perAsic = new long[Frames.SEGMENT_COUNT][3];
  public final Map<Integer, Long> frameBytes = new TreeMap<>();
  public int programPages;
  public String programId;
  /** Gold's memory, as image digests: at each segment's cut (start of s), at cycle 0, at the end. */
  public final String[] startDigest = new String[Frames.SEGMENT_COUNT];
  public String cycle0Digest, finalDigest;
  public double seconds;

  public FrameWriter(Path programDir, Path inputDir, Options opt) throws IOException {
    this.program = new Program(programDir, inputDir);
    this.programOnly = new Program(programDir, null);
    this.inputDir = inputDir;
    this.opt = opt;
    if (opt.cuts.length > Frames.SEGMENT_COUNT) {
      throw new IllegalArgumentException(opt.cuts.length + " cuts: the frames carry exactly 128 segments (C9)");
    }
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
      for (int s = opt.cuts.length; s < Frames.SEGMENT_COUNT; s++) startDigest[s] = finalDigest;
    }

    // ---- the 128 segments (C9: past the run's end, as SegmentWriter's own past-the-end segments) ----
    List<SegmentWriter.Segment> segs = new ArrayList<>(run.segments);
    while (segs.size() < Frames.SEGMENT_COUNT) {
      SegmentWriter.Segment sg = new SegmentWriter.Segment();
      for (int r = 1; r < Isa.NUM_REGS; r++) sg.state[3 + r] = finalRegs[0][r];
      segs.add(sg);
    }

    // ---- the files' lengths (C3) ----
    imagePages = FileTree.imagePages(program.layout);
    if (imagePages != Math.max(1, params.maxRamK * 8 / Frames.PAGE_BYTES)) throw new IllegalStateException("max_ram_k");
    imageDepth = FileTree.depth(imagePages);

    // ---- frame 1: the program image ----
    PageImage prog = PageImage.of(programOnly.memory, lowest);
    byte[] progPages = prog.canonical();
    programPages = progPages.length / (8 + Frames.PAGE_BYTES);
    checkWithin(prog.pages.isEmpty() ? -1 : prog.pages.lastKey(), imagePages, "the program image");
    byte[] f1 = new Frames.Bytes().u32(Frames.u32(imagePages, "the image's length")).u32(programPages).raw(progPages)
        .toArray();
    programId = PageImage.sha256(f1);                            // C5

    // ---- the jump tables (C8, C10) ----
    HashTable full = run.jumps.table();
    table = full;
    tableMode = "full";
    if (opt.baseStatic) {
      base = run.jumps.staticTable();
      HashTable added = base.copy();
      long[] keys = run.jumps.runTargets().keySet().stream().mapToLong(k -> k & 0xffffffffL).sorted().toArray();
      boolean fits = true;
      for (long key : keys) {
        int idx = run.jumps.runTargets().get((int) key);
        long have = added.find((int) key);
        if (have >= 0) {
          if (have != (idx & 0xffffffffL)) {
            throw new IllegalStateException(String.format("target o=%#x: base %d in the base table, %d in the run", key, have, idx));
          }
          continue;
        }
        if (added.insert((int) key, idx) < 0) { fits = false; break; }
      }
      if (fits) { table = added; tableMode = "added"; } else { tableMode = "fallback"; }
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
    tableData = new Stream(pageRuns(sentPages, tableWords), opt.lru5);
    tablePagesTaken = FileTree.pagesTaken(UpdateWords.pages(tableData.words), FileTree.depth(tablePages));

    // ---- frame 6: the data image (C7) ----
    dataImage = new Stream(dataImageWords(lowest), opt.lru6);
    TreeSet<Long> dataPages = UpdateWords.pages(dataImage.words);
    checkWithin(dataPages.isEmpty() ? -1 : dataPages.last(), imagePages, "the data image");
    dataImagePages = FileTree.pagesTaken(dataPages, imageDepth);

    // ---- frame 9 and the page counts ----
    long[][] lists = new long[Frames.SEGMENT_COUNT][];
    List<TreeSet<Long>> written = new ArrayList<>();
    for (int k = 0; k < Frames.SEGMENT_COUNT; k++) {
      lists[k] = segs.get(k).updates.toArray();
      segmentWords[k] = Frames.u32(lists[k].length, "segment " + k + "'s update words");
      TreeSet<Long> w = UpdateWords.pages(lists[k]);
      checkWithin(w.isEmpty() ? -1 : w.last(), imagePages, "segment " + k);
      written.add(w);
      pagesWritten[k] = w.size();
      if (k < run.segments.size() && w.size() != segs.get(k).pages4k) throw new IllegalStateException("pages written " + k);
    }
    segmentsStream = new Stream(UpdateWords.concat(lists), opt.lru9);
    for (int s = 0; s < Frames.SEGMENT_COUNT; s++) {          // C11, per ASIC: start, final, Tracer's copy
      TreeSet<Long> before = new TreeSet<>(), after = new TreeSet<>();
      for (int k = 0; k < s; k++) before.addAll(written.get(k));
      for (int k = s; k < Frames.SEGMENT_COUNT; k++) after.addAll(written.get(k));
      perAsic[s][0] = FileTree.pagesTaken(before, imageDepth);
      perAsic[s][1] = FileTree.pagesTaken(after, imageDepth);
      perAsic[s][2] = FileTree.pagesTaken(written.get(s), imageDepth);
    }

    // ---- frame 3 ----
    Frames.Bytes f3 = new Frames.Bytes();
    f3.u32(Frames.FORMAT_VERSION).string(opt.stem).raw(hexBytes(programId)).string(opt.workdir);
    byte[] f7 = params.frame7();                                  // u32 version, u32 n, n x u64: take n and the fields
    f3.raw(Arrays.copyOfRange(f7, 4, f7.length));
    f3.u32(Frames.u32(dataImage.words.length, "frame 6's 64-bit words"))
        .u32(Frames.u32(Frames.words128(dataImage.sent().length), "frame 6's 128-bit words"))
        .u32(opt.lru6 ? 1 : 0).u32(Frames.u32(dataImagePages, "the data image's pages"));
    for (int k = 0; k < Frames.SEGMENT_COUNT; k++) f3.u32(segmentWords[k]);
    f3.u32(Frames.u32(Frames.words128(segmentsStream.sent().length), "frame 9's 128-bit words")).u32(opt.lru9 ? 1 : 0);
    for (int k = 0; k < Frames.SEGMENT_COUNT; k++) f3.u32(Frames.u32(pagesWritten[k], "pages written"));
    for (int s = 0; s < Frames.SEGMENT_COUNT; s++) for (int j = 0; j < 3; j++) f3.u32(Frames.u32(perAsic[s][j], "pages"));

    // ---- frame 4 ----
    Frames.Bytes f4 = new Frames.Bytes();
    f4.string(withBase ? opt.basePath : "").u32(table.k).u32(table.p1).u32(table.p3)
        .u32(Frames.u32(tableData.words.length, "frame 5's 64-bit words"))
        .u32(Frames.u32(Frames.words128(tableData.sent().length), "frame 5's 128-bit words"))
        .u32(opt.lru5 ? 1 : 0).u32(Frames.u32(tablePagesTaken, "the jump table's pages"));

    // ---- frames 7 and 8 ----
    Frames.Bytes f7b = new Frames.Bytes(), f8 = new Frames.Bytes();
    for (SegmentWriter.Segment sg : segs) f7b.raw(run.stateRecord(sg));
    for (SegmentWriter.Segment sg : segs) f8.u32(Frames.u32(sg.advice.size(), "advice values"));
    for (SegmentWriter.Segment sg : segs) {
      long[] a = sg.advice.toArray();
      f8.raw(Frames.bytes(a));
      if (a.length % 2 == 1) f8.u64(0);
    }

    // ---- the file ----
    ByteArrayOutputStream out = new ByteArrayOutputStream();
    frameBytes.put(Frames.ELF, Frames.frame(out, Frames.ELF, f1));
    if (opt.baseStatic) frameBytes.put(Frames.BASE_TABLE, Frames.frame(out, Frames.BASE_TABLE, tableFrame(base)));
    frameBytes.put(Frames.PROOF_HEADER, Frames.frame(out, Frames.PROOF_HEADER, f3.toArray()));
    frameBytes.put(Frames.TABLE_HEADER, Frames.frame(out, Frames.TABLE_HEADER, f4.toArray()));
    frameBytes.put(Frames.TABLE_DATA, Frames.frame(out, Frames.TABLE_DATA, tableData.sent()));
    frameBytes.put(Frames.DATA_IMAGE, Frames.frame(out, Frames.DATA_IMAGE, dataImage.sent()));
    frameBytes.put(Frames.REGISTERS, Frames.frame(out, Frames.REGISTERS, f7b.toArray()));
    frameBytes.put(Frames.ADVICE, Frames.frame(out, Frames.ADVICE, f8.toArray()));
    frameBytes.put(Frames.SEGMENTS, Frames.frame(out, Frames.SEGMENTS, segmentsStream.sent()));
    file = out.toByteArray();
    seconds = (System.nanoTime() - t0) / 1e9;
    return file;
  }

  /** Frame 2's data (v1.3 frame 6's): u32 k, u32 p1, u32 p3, the lines. */
  static byte[] tableFrame(HashTable t) {
    return new Frames.Bytes().u32(t.k).u32(t.p1).u32(t.p3).raw(t.image()).toArray();
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

  /** Maximal runs of consecutive whole pages (C8). */
  static long[] pageRuns(TreeSet<Long> pages, long[] words) {
    SegmentWriter.LongList out = new SegmentWriter.LongList();
    long[] p = UpdateWords.sorted(pages);
    for (int a = 0; a < p.length; ) {
      int b = a + 1;
      while (b < p.length && p[b] == p[b - 1] + 1) b++;
      out.add(UpdateWords.header(p[a] * 512, (long) (b - a) * 512));
      for (long w = p[a] * 512; w < (p[b - 1] + 1) * 512; w++) out.add(words[(int) w]);
      a = b;
    }
    return out.toArray();
  }

  /** Frame 6 (C7): one run per input file present and not empty, its length rounded up to 8 bytes, in address order. */
  long[] dataImageWords(long lowest) throws IOException {
    Layout l = program.layout;
    TreeMap<Long, byte[]> files = new TreeMap<>();
    String[] names = {"input.bin", "trusted_advice.bin", "untrusted_advice.bin"};
    long[] starts = {l.inputStart, l.trustedAdviceStart, l.untrustedAdviceStart};
    for (int i = 0; i < 3; i++) {
      Path f = inputDir.resolve(names[i]);
      if (!Files.exists(f)) continue;
      byte[] b = Files.readAllBytes(f);
      if (b.length > 0) files.put(starts[i], b);
    }
    SegmentWriter.LongList out = new SegmentWriter.LongList();
    for (Map.Entry<Long, byte[]> e : files.entrySet()) {
      byte[] b = e.getValue();
      int n = (b.length + 7) / 8;
      out.add(UpdateWords.header((e.getKey() - lowest) / 8, n));
      for (int i = 0; i < n; i++) {
        long v = 0;
        for (int j = 0; j < 8 && 8 * i + j < b.length; j++) v |= (b[8 * i + j] & 0xffL) << (8 * j);
        out.add(v);
      }
    }
    return out.toArray();
  }

  static void checkWithin(long lastPage, long lengthPages, String what) {
    if (lastPage >= lengthPages) {
      throw new IllegalStateException(what + " writes page " + lastPage + ", beyond the file's " + lengthPages + " pages");
    }
  }

  static byte[] hexBytes(String hex) {
    byte[] b = new byte[hex.length() / 2];
    for (int i = 0; i < b.length; i++) b[i] = (byte) Integer.parseInt(hex.substring(2 * i, 2 * i + 2), 16);
    return b;
  }

  /** The side file: every frame's bytes, both forms of 5, 6 and 9, the page counts. */
  public String json(String programDir, String inputDir) {
    StringBuilder b = new StringBuilder("{\n");
    b.append(String.format("  \"program\": \"%s\",%n  \"input\": \"%s\",%n", programDir, inputDir));
    b.append(String.format("  \"rows\": %d,%n  \"cuts\": %d,%n  \"file_bytes\": %d,%n  \"file_sha256\": \"%s\",%n",
        run.totalRows, opt.cuts.length, file.length, PageImage.sha256(file)));
    b.append(String.format("  \"log2_k\": %d,%n  \"image_pages\": %d,%n  \"image_depth\": %d,%n  \"program_pages\": %d,%n",
        params.log2K, imagePages, imageDepth, programPages));
    b.append("  \"frame_bytes\": {");
    String sep = "";
    for (Map.Entry<Integer, Long> e : frameBytes.entrySet()) { b.append(sep).append(String.format("\"%d\": %d", e.getKey(), e.getValue())); sep = ", "; }
    b.append("},\n");
    b.append(String.format("  \"table\": {\"mode\": \"%s\", \"k\": %d, \"p1\": %d, \"p3\": %d, \"entries\": %d, \"file_pages\": %d,"
        + " \"pages_taken\": %d, \"base_entries\": %d, \"run_targets\": %d},%n", tableMode, table.k, table.p1 & 0xffffffffL,
        table.p3, table.entries, tablePages, tablePagesTaken, base == null ? 0 : base.entries, run.jumps.runTargets().size()));
    stream(b, "5", tableData);
    stream(b, "6", dataImage);
    stream(b, "9", segmentsStream);
    b.append(String.format("  \"data_image_pages\": %d,%n", dataImagePages));
    b.append("  \"pages_written\": ").append(Arrays.toString(pagesWritten)).append(",\n");
    b.append("  \"segment_words\": ").append(Arrays.toString(segmentWords)).append(",\n");
    b.append("  \"per_asic\": [");
    for (int s = 0; s < Frames.SEGMENT_COUNT; s++) b.append(s == 0 ? "" : ", ").append(Arrays.toString(perAsic[s]));
    b.append("]\n}\n");
    return b.toString();
  }

  static void stream(StringBuilder b, String name, Stream s) {
    b.append(String.format("  \"stream_%s\": {\"words\": %d, \"raw_bytes\": %d, \"lru_bytes\": %d, \"raw_frame_bytes\": %d,"
        + " \"lru_frame_bytes\": %d, \"sent\": \"%s\"},%n", name, s.words.length, s.raw.length, s.lru.length,
        16 * (1 + Frames.words128(s.raw.length)), 16 * (1 + Frames.words128(s.lru.length)), s.sendLru ? "lru" : "raw"));
  }
}
