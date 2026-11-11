package gold;

import java.io.IOException;
import java.nio.ByteOrder;
import java.nio.LongBuffer;
import java.nio.channels.FileChannel;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardOpenOption;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Map;

/**
 * gold: Jolt's tracer in Java. Commands:
 *
 * <pre>
 *   compare &lt;program dir&gt; &lt;input dir&gt; &lt;ref prefix&gt; [--max-rows=N]
 *       Runs the guest and checks every row against row_dump's output for the same run: the record of every row where
 *       &lt;ref&gt;.rows.bin exists (stopping at the first difference, with context), else the hash of every 2^16-row
 *       window against &lt;ref&gt;.hashes.csv; and the layout, the bytecode size and the row counts against &lt;ref&gt;.json.
 *       Stage 1: secp256k1 advice from &lt;ref&gt;.advice.bin; with --computed-advice, computed (stage 2).
 *
 *   run &lt;program dir&gt; &lt;input dir&gt; [--tohost]
 *       Runs the guest with nothing to compare against: the row count and run hash, or where it aborts or is
 *       rejected at load (an instruction Jolt can't prove; owner's D4 ruling); with --tohost, also an ACT4-style
 *       verdict from the tohost word.
 *
 *   segments &lt;program dir&gt; &lt;input dir&gt; &lt;out file&gt; [--cuts=0,a,...|--cuts-file=F] [--segment-rows=N]
 *            [--segments=N] [--compare=&lt;reference file&gt;]
 *       Stage 2: runs the guest, computing everything, and writes section 16's segment file (SegmentWriter), with
 *       &lt;out&gt;.cuts and &lt;out&gt;.json beside it. With --compare, checks it byte for byte against a reference file
 *       (WORKER16's segment_writer) and reports the first difference by frame.
 * </pre>
 */
public final class Main {
  public static void main(String[] args) throws Exception {
    if (args.length >= 4 && args[0].equals("compare")) {
      long maxRows = Long.MAX_VALUE;
      boolean computed = false;
      for (int i = 4; i < args.length; i++) {
        if (args[i].startsWith("--max-rows=")) maxRows = Long.parseLong(args[i].substring(11));
        else if (args[i].equals("--computed-advice")) computed = true;
        else if (args[i].equals("--coverage")) coverage = true;
        else throw new IllegalArgumentException("unknown option " + args[i]);
      }
      System.exit(compare(Path.of(args[1]), Path.of(args[2]), args[3], maxRows, computed) ? 0 : 1);
    }
    if (args.length >= 3 && args[0].equals("run")) {
      java.util.List<String> opts = java.util.Arrays.asList(args).subList(3, args.length);
      System.exit(run(Path.of(args[1]), Path.of(args[2]), opts.contains("--tohost")) ? 0 : 1);
    }
    if (args.length >= 4 && args[0].equals("segments")) {
      System.exit(segments(args) ? 0 : 1);
    }
    System.err.println("usage: gold segments <program dir> <input dir> <out file> [--cuts=0,a,b,...|--cuts-file=F]"
        + " [--segment-rows=4194304] [--segments=128] [--compare=<reference file>]");
    System.err.println("usage: gold compare <program dir> <input dir> <ref prefix> [--max-rows=N] [--computed-advice]");
    System.exit(2);
  }

  /** run: no reference; computes everything and prints the row count and run hash, or where the run aborts. */
  static boolean run(Path programDir, Path inputDir, boolean tohost) throws IOException {
    Program p;
    try {
      p = new Program(programDir, inputDir);
    } catch (Bytecode.UnprovableInstruction e) {
      System.out.println("REJECTED at load: " + e.getMessage());
      return false;
    }
    Machine m = new Machine(p, new SecpCompute());
    m.checkStoresAgainst(p.textRanges);
    long windowHash = 0, runHash = 0;
    try {
      while (!m.halted()) {
        m.step();
        windowHash = Row.mix(windowHash, Row.hash(m.row));
        if (m.cycle % (1 << 16) == 0) { runHash = Row.mix(runHash, windowHash); windowHash = 0; }
      }
    } catch (EmulationError e) {
      System.out.printf("ABORT at row %d: %s%n", m.cycle, e.getMessage());
      return false;
    }
    if (m.cycle % (1 << 16) != 0) runHash = Row.mix(runHash, windowHash);
    // the host's checks after the run (DECISIONS-v1.3 section 25): stores into code; the provable range and K
    if (m.codeStoreRow >= 0) {
      System.out.printf("REJECTED: the store at row %d to %#x falls in the code (a Text section)%n", m.codeStoreRow,
          m.codeStoreAddress);
      return false;
    }
    ProofParams pp;
    try {
      pp = new ProofParams(p, m);
      pp.check(m);
    } catch (ProofParams.Unprovable e) {
      System.out.println("REJECTED: " + e.getMessage());
      return false;
    }
    System.out.printf("%d rows, run hash %016x, log2 K %d, lowest_address %#x", m.cycle, runHash, pp.log2K,
        pp.lowestAddress);
    if (!tohost) { System.out.println(); return true; }
    // ACT4's verdict (tests/arch-tests/jolt/rvmodel_macros.h): tohost = 1 passes, (gp << 1) | 1 fails test case gp
    Long addr = p.symbols.get("tohost");
    if (addr == null) { System.out.println(", no tohost symbol"); return false; }
    long v = p.memory.load(addr);
    System.out.printf(", tohost %#x: %s%n", v, v == 1 ? "PASS" : v == 0 ? "NO VERDICT" : "FAIL (case " + (v >>> 1) + ")");
    return v == 1;
  }

  static boolean segments(String[] args) throws Exception {
    long segmentRows = 1L << 22;
    int count = 128;
    long[] cuts = null;
    String compareWith = null;
    SegmentWriter.Format format = SegmentWriter.Format.V1_3;
    for (int i = 4; i < args.length; i++) {
      String a = args[i];
      if (a.startsWith("--segment-rows=")) segmentRows = Long.parseLong(a.substring(15));
      else if (a.startsWith("--segments=")) count = Integer.parseInt(a.substring(11));
      else if (a.startsWith("--cuts=")) cuts = Arrays.stream(a.substring(7).split(",")).mapToLong(Long::parseLong).toArray();
      else if (a.startsWith("--cuts-file=")) {
        cuts = Files.readAllLines(Path.of(a.substring(12))).stream().filter(l -> !l.isBlank()).mapToLong(Long::parseLong).toArray();
      } else if (a.startsWith("--compare=")) compareWith = a.substring(10);
      else if (a.equals("--format=v1.2")) format = SegmentWriter.Format.V1_2;
      else if (a.equals("--format=v1.3")) format = SegmentWriter.Format.V1_3;
      else throw new IllegalArgumentException("unknown option " + a);
    }
    if (cuts == null) cuts = SegmentWriter.defaultCuts(segmentRows, count);
    long t0 = System.nanoTime();
    Program p = new Program(Path.of(args[1]), Path.of(args[2]));
    SegmentWriter w = new SegmentWriter(p, cuts, format);
    byte[] file;
    try {
      file = w.write();
    } catch (SegmentWriter.Rejected e) {
      System.out.println("REJECTED: " + e.getMessage());
      return false;
    }
    double seconds = (System.nanoTime() - t0) / 1e9;
    Path out = Path.of(args[3]);
    Files.write(out, file);
    StringBuilder cutsText = new StringBuilder();
    for (long c : cuts) cutsText.append(c).append('\n');
    Files.writeString(Path.of(args[3] + ".cuts"), cutsText);
    String sha = sha256(file);
    Files.writeString(Path.of(args[3] + ".json"), String.format(
        "{%n  \"program\": \"%s\",%n  \"input\": \"%s\",%n  \"rows\": %d,%n  \"segments\": %d,%n  \"file_bytes\": %d,%n"
            + "  \"file_sha256\": \"%s\",%n  \"advice_words\": %d,%n  \"update_words\": %d%n}%n",
        args[1], args[2], w.totalRows, cuts.length, file.length, sha, w.adviceWords, w.updateWords));
    System.out.printf("%d rows, %d segments, file %d bytes, SHA-256 %s; %d advice words, %d update words; %.1f s%n",
        w.totalRows, cuts.length, file.length, sha, w.adviceWords, w.updateWords, seconds);
    if (compareWith == null) return true;
    byte[] ref = Files.readAllBytes(Path.of(compareWith));
    String where = FrameDiff.firstDifference(file, ref);
    System.out.println(where == null ? "IDENTICAL to " + compareWith + " (SHA-256 " + sha256(ref) + ")" : "DIFFERS: " + where);
    return where == null;
  }

  static String sha256(byte[] data) throws Exception {
    byte[] d = java.security.MessageDigest.getInstance("SHA-256").digest(data);
    StringBuilder b = new StringBuilder();
    for (byte x : d) b.append(String.format("%02x", x));
    return b.toString();
  }

  /** Which source instruction kinds (rd = x0 forms counted apart) and row opcodes the run executed. */
  static void printCoverage(Machine m) {
    StringBuilder src = new StringBuilder(), rows = new StringBuilder();
    for (int op = 0; op < Op.NUM_OPCODES; op++) {
      if (m.sourceCount[op] > 0) src.append(' ').append(Op.name(op)).append(':').append(m.sourceCount[op]);
      if (m.opCount[op] > 0) rows.append(' ').append(Op.name(op));
    }
    System.out.println("COVERAGE source instructions executed:" + src);
    System.out.println("COVERAGE row opcodes executed:" + rows);
  }

  static long paddedRows(long rows) {             // trace_owner.hpp:437-450
    if (rows < 256) return 256;
    return Layout.nextPowerOfTwo(rows + 1);
  }

  static boolean coverage;

  static boolean compare(Path programDir, Path inputDir, String ref, long maxRows, boolean computed) throws IOException {
    long t0 = System.nanoTime();
    Map<?, ?> refJson = (Map<?, ?>) Json.parse(Files.readString(Path.of(ref + ".json")));
    Program p = new Program(programDir, inputDir);
    boolean ok = true;

    // layout and bytecode
    Map<?, ?> refLayout = (Map<?, ?>) refJson.get("layout");
    long[] mine = p.layout.fields();
    for (int i = 0; i < mine.length; i++) {
      Long theirs = Json.u64(refLayout, Layout.FIELD_NAMES[i]);
      if (theirs == null) continue;                  // a field the reference doesn't report (upstream's layout)
      if (theirs != mine[i]) {
        System.out.printf("LAYOUT %s: %#x, reference %#x%n", Layout.FIELD_NAMES[i], mine[i], theirs);
        ok = false;
      }
    }
    long refInstrs = Json.u64(refJson, "bytecode_instrs");
    System.out.printf("layout %s; bytecode %d rows (reference %d)%n", ok ? "matches" : "DIFFERS", p.bytecode.size(),
        refInstrs);
    if (refInstrs != p.bytecode.size()) ok = false;

    // reference rows and window hashes
    long refRows = Json.u64(refJson, "rows"), window = Json.u64(refJson, "window");
    List<Long> refHashes = new ArrayList<>();
    for (String line : Files.readAllLines(Path.of(ref + ".hashes.csv"))) {
      if (line.startsWith("window")) continue;
      refHashes.add(Long.parseUnsignedLong(line.split(",")[3], 16));
    }
    Path rowsFile = Path.of(ref + ".rows.bin");
    long refRowsWritten = Files.exists(rowsFile) ? Files.size(rowsFile) / Row.BYTES : 0;
    FileChannel rowsChannel = refRowsWritten > 0 ? FileChannel.open(rowsFile, StandardOpenOption.READ) : null;
    LongBuffer refWindow = null;

    AdviceFile advice = new AdviceFile(Path.of(ref + ".advice.bin"));
    SecpCompute secp = computed ? new SecpCompute() : null;
    long tRun = System.nanoTime();
    Machine m = new Machine(p, computed ? secp : advice);
    long[][] recent = new long[8][Row.WORDS];
    long windowHash = 0, runHash = 0;
    int windowsChecked = 0, windowMismatches = 0;
    long rowsCompared = 0;
    long[] theirs = new long[Row.WORDS];

    try {
      while (!m.halted() && m.cycle < maxRows) {
        long n = m.cycle;
        if (rowsChannel != null && n < refRowsWritten && n % window == 0) {
          long count = Math.min(window, refRowsWritten - n);
          refWindow = rowsChannel.map(FileChannel.MapMode.READ_ONLY, n * Row.BYTES, count * Row.BYTES)
              .order(ByteOrder.LITTLE_ENDIAN).asLongBuffer();
        }
        m.step();
        System.arraycopy(m.row, 0, recent[(int) (n % recent.length)], 0, Row.WORDS);
        windowHash = Row.mix(windowHash, Row.hash(m.row));

        if (rowsChannel != null && n < refRowsWritten) {
          refWindow.get(theirs);
          rowsCompared++;
          for (int i = 0; i < Row.WORDS; i++) {
            if (theirs[i] != m.row[i]) {
              report(n, recent, theirs, m, p);
              return false;
            }
          }
        }
        if (m.cycle % window == 0 || m.halted()) {
          int w = (int) (n / window);
          if (w < refHashes.size()) {
            windowsChecked++;
            if (refHashes.get(w) != windowHash) {
              if (windowMismatches++ < 5) System.out.printf("WINDOW %d hash %016x, reference %016x%n", w, windowHash, refHashes.get(w));
              ok = false;
            }
          }
          runHash = Row.mix(runHash, windowHash);
          windowHash = 0;
        }
      }
    } catch (EmulationError e) {
      System.out.printf("STOPPED at row %d: %s%n", m.cycle, e.getMessage());
      return false;
    }

    if (coverage) printCoverage(m);
    long rows = m.cycle;
    boolean complete = m.halted();
    double runSeconds = (System.nanoTime() - tRun) / 1e9;
    System.out.printf("load and expand %.2f s; run %.2f s, %.1f M rows/s%s%n", (tRun - t0) / 1e9, runSeconds,
        rows / runSeconds / 1e6, rowsChannel != null ? " (comparing every record)" : " (hashing only)");
    System.out.printf("rows %d (reference %d), padded %d (reference %d)%s%n", rows, refRows, paddedRows(rows),
        Json.u64(refJson, "padded_rows"), complete ? "" : ", stopped at --max-rows");
    System.out.printf("rows compared one by one: %d; windows hashed and compared: %d, %d differ%n", rowsCompared,
        windowsChecked, windowMismatches);
    if (complete) {
      long refRun = Long.parseUnsignedLong((String) refJson.get("run_hash"), 16);
      if (computed) {
        System.out.printf("run hash %016x (reference %016x); secp advice computed for %d operations%n", runHash, refRun,
            secp.operations());
        ok &= rows == refRows && runHash == refRun;
      } else {
        System.out.printf("run hash %016x (reference %016x); secp advice words left unused: %d%n", runHash, refRun,
            advice.remainingWords());
        ok &= rows == refRows && runHash == refRun && advice.remainingWords() == 0;
      }
    }
    System.out.printf("%s in %.1f s%n", ok ? "MATCH" : "MISMATCH", (System.nanoTime() - t0) / 1e9);
    return ok;
  }

  static void report(long n, long[][] recent, long[] theirs, Machine m, Program p) {
    System.out.printf("ROW %d differs:%n", n);
    for (long k = Math.max(0, n - recent.length + 1); k < n; k++) {
      System.out.printf("  %d  %s%n", k, Row.describe(recent[(int) (k % recent.length)]));
    }
    long[] mine = recent[(int) (n % recent.length)];
    System.out.printf("  mine       %s%n  reference  %s%n", Row.describe(mine), Row.describe(theirs));
    for (int i = 0; i < Row.WORDS; i++) {
      if (mine[i] != theirs[i]) System.out.printf("  word %d (%s): %#x vs %#x%n", i, Row.NAMES[i], mine[i], theirs[i]);
    }
  }
}
