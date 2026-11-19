package gold;

import java.util.Arrays;
import java.util.List;
import java.util.Map;

/**
 * frames --verify: the frame file, as written to disk and read back, against gold's own run.
 * <ul>
 *   <li>the images: for every s, the Reference's start image (and the Tracer's copy) equals gold's memory at the cut
 *       before s; its cycle-0 image gold's memory before the first row; its final image gold's memory at the end;</li>
 *   <li>the page counts: the Reference's trees against frames 3 and 4, for every s;</li>
 *   <li>the jump table: every jump target the run takes, found by the Tracer's probe in the Reference's run's table,
 *       with gold's next instr_idx;</li>
 *   <li>the forms: both forms of frames 2, 5, 6 and 9 (and the base table's 5), decoded by the Reference, equal the
 *       writer's words; frame 6's two forms make the same image; with --compression=auto the smaller form is sent,
 *       sizes counted by the Reference.</li>
 * </ul>
 */
public final class Verify {
  private Verify() {}

  static boolean run(FrameWriter w, byte[] file) {
    long t0 = System.nanoTime();
    FrameFile f = FrameFile.parse(file);
    Reference r = new Reference(f);
    int failures = 0;

    // LRU against raw, for every stream, whichever was sent; with --compression=auto, the smaller form sent
    List<FrameWriter.Stream> streams = new java.util.ArrayList<>(List.of(w.programData, w.tableData, w.dataImage, w.segmentsStream));
    List<String> names = new java.util.ArrayList<>(List.of("frame 2", "frame 5", "frame 6", "frame 9"));
    if (w.baseData != null) { streams.add(w.baseData); names.add("the base table's frame 5"); }
    for (int i = 0; i < streams.size(); i++) {
      FrameWriter.Stream st = streams.get(i);
      long[] raw = Reference.decodeRaw(pad(st.raw), st.rawWords.length);
      long[] lru = Reference.decodeLru(pad(st.lru), st.lruWords.length, null);
      if (!Arrays.equals(raw, st.rawWords) || !Arrays.equals(lru, st.lruWords)) {
        System.out.println("LRU " + names.get(i) + ": a form doesn't decode to its words");
        failures++;
      }
      if (st.rawWords != st.lruWords && !Arrays.equals(st.rawWords, st.lruWords)) {   // frame 6: the same image either way
        Reference.Tree a = Reference.Tree.copyOf(r.program), b = Reference.Tree.copyOf(r.program);
        Reference.apply(a, raw, false, "raw");
        Reference.apply(b, lru, false, "lru");
        if (!Arrays.equals(a.image(), b.image())) { System.out.println("FORMS " + names.get(i) + ": the two forms make different images"); failures++; }
      }
      long rawWords128 = (8L * st.rawWords.length + 15) / 16, lruWords128 = ((Reference.lruBits(st.lruWords) + 7) / 8 + 15) / 16;
      boolean sentLru = st.compression == 1;
      if (w.compression() == FrameWriter.Compression.AUTO && (sentLru ? lruWords128 >= rawWords128 : lruWords128 < rawWords128)) {
        System.out.printf("FORM %s: sent %s, %d 128-bit words raw, %d LRU%n", names.get(i), sentLru ? "LRU" : "raw", rawWords128, lruWords128);
        failures++;
      }
    }

    // the images and the counts
    String c0 = Reference.sha256(r.cycle0.image());
    if (!c0.equals(w.cycle0Digest)) { System.out.println("IMAGE cycle-0: " + c0 + ", gold " + w.cycle0Digest); failures++; }
    int imageFailures = 0, countFailures = 0;
    String finalDigest = null;
    for (int s = 0; s < f.n; s++) {
      r.forSegment(s);
      byte[] st = r.start.image();
      String sd = Reference.sha256(st), fd = Reference.sha256(r.fin.image());
      if (!sd.equals(w.startDigest[s])) {
        if (imageFailures++ < 5) System.out.printf("IMAGE start of segment %d: %s, gold %s%n", s, sd, w.startDigest[s]);
      }
      if (!Arrays.equals(st, r.tracer.image())) {
        if (imageFailures++ < 5) System.out.printf("IMAGE segment %d: the Tracer's copy differs from the start image%n", s);
      }
      if (!fd.equals(w.finalDigest)) {
        if (imageFailures++ < 5) System.out.printf("IMAGE final (s %d): %s, gold %s%n", s, fd, w.finalDigest);
      }
      finalDigest = fd;
      List<String> bad = r.checkCounts(s);
      for (String b : bad) if (countFailures++ < 10) System.out.println("COUNT " + b);
    }
    failures += imageFailures + countFailures;

    // the jump table
    byte[] t = r.tableBytes();
    long found = 0, missed = 0;
    for (Map.Entry<Integer, Integer> e : w.run.jumps.runTargets().entrySet()) {
      long base = Reference.probe(t, f.table.k, f.table.p1, f.table.p3, e.getKey());
      if (base == (e.getValue() & 0xffffffffL)) {
        found++;
      } else {
        if (missed++ < 5) System.out.printf("JUMP target o=%#x: probe gives %d, gold's next instr_idx %d%n", e.getKey(), base, e.getValue());
      }
    }
    failures += (int) missed;

    System.out.printf("verify: images (cycle-0 %s, %d starts and Tracer's copies, final %s) %s; page counts %s;"
        + " jump targets %d found, %d missed (table %s); both forms of every update-word frame decode%s; %.1f s: %s%n",
        c0.substring(0, 16), f.n, finalDigest.substring(0, 16), imageFailures == 0 ? "equal gold's memory" : imageFailures + " FAILED",
        countFailures == 0 ? "equal" : countFailures + " FAILED", found, missed, w.tableMode,
        w.compression() == FrameWriter.Compression.AUTO ? ", the smaller sent" : "",
        (System.nanoTime() - t0) / 1e9, failures == 0 ? "VERIFIED" : "FAILED");
    return failures == 0;
  }

  /** A stream padded to 128 bits, as a frame carries it. */
  static byte[] pad(byte[] b) { return Arrays.copyOf(b, (b.length + 15) / 16 * 16); }
}
