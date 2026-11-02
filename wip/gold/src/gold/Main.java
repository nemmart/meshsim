package gold;

import java.io.IOException;
import java.nio.ByteOrder;
import java.nio.LongBuffer;
import java.nio.channels.FileChannel;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardOpenOption;
import java.util.ArrayList;
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
 *       Stage 1: secp256k1 advice from &lt;ref&gt;.advice.bin.
 * </pre>
 */
public final class Main {
  public static void main(String[] args) throws Exception {
    if (args.length >= 4 && args[0].equals("compare")) {
      long maxRows = Long.MAX_VALUE;
      for (int i = 4; i < args.length; i++) {
        if (args[i].startsWith("--max-rows=")) maxRows = Long.parseLong(args[i].substring(11));
        else throw new IllegalArgumentException("unknown option " + args[i]);
      }
      System.exit(compare(Path.of(args[1]), Path.of(args[2]), args[3], maxRows) ? 0 : 1);
    }
    System.err.println("usage: gold compare <program dir> <input dir> <ref prefix> [--max-rows=N]");
    System.exit(2);
  }

  static long paddedRows(long rows) {             // trace_owner.hpp:437-450
    if (rows < 256) return 256;
    return Layout.nextPowerOfTwo(rows + 1);
  }

  static boolean compare(Path programDir, Path inputDir, String ref, long maxRows) throws IOException {
    long t0 = System.nanoTime();
    Map<?, ?> refJson = (Map<?, ?>) Json.parse(Files.readString(Path.of(ref + ".json")));
    Program p = new Program(programDir, inputDir);
    boolean ok = true;

    // layout and bytecode
    Map<?, ?> refLayout = (Map<?, ?>) refJson.get("layout");
    long[] mine = p.layout.fields();
    for (int i = 0; i < mine.length; i++) {
      long theirs = Json.u64(refLayout, Layout.FIELD_NAMES[i]);
      if (theirs != mine[i]) {
        System.out.printf("LAYOUT %s: %#x, jolt-cpp %#x%n", Layout.FIELD_NAMES[i], mine[i], theirs);
        ok = false;
      }
    }
    long refInstrs = Json.u64(refJson, "bytecode_instrs");
    System.out.printf("layout %s; bytecode %d rows (jolt-cpp %d); host I/O %s (jolt-cpp %s)%n", ok ? "matches" : "DIFFERS",
        p.bytecode.size(), refInstrs, p.hostIo, refJson.get("host_io_encoding"));
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
    long tRun = System.nanoTime();
    Machine m = new Machine(p, advice);
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
              if (windowMismatches++ < 5) System.out.printf("WINDOW %d hash %016x, jolt-cpp %016x%n", w, windowHash, refHashes.get(w));
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

    long rows = m.cycle;
    boolean complete = m.halted();
    double runSeconds = (System.nanoTime() - tRun) / 1e9;
    System.out.printf("load and expand %.2f s; run %.2f s, %.1f M rows/s%s%n", (tRun - t0) / 1e9, runSeconds,
        rows / runSeconds / 1e6, rowsChannel != null ? " (comparing every record)" : " (hashing only)");
    System.out.printf("rows %d (jolt-cpp %d), padded %d (jolt-cpp %d)%s%n", rows, refRows, paddedRows(rows),
        Json.u64(refJson, "padded_rows"), complete ? "" : ", stopped at --max-rows");
    System.out.printf("rows compared one by one: %d; windows hashed and compared: %d, %d differ%n", rowsCompared,
        windowsChecked, windowMismatches);
    if (complete) {
      long refRun = Long.parseUnsignedLong((String) refJson.get("run_hash"), 16);
      System.out.printf("run hash %016x (jolt-cpp %016x); secp advice words left unused: %d%n", runHash, refRun,
          advice.remainingWords());
      ok &= rows == refRows && runHash == refRun && advice.remainingWords() == 0;
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
    System.out.printf("  mine      %s%n  jolt-cpp  %s%n", Row.describe(mine), Row.describe(theirs));
    for (int i = 0; i < Row.WORDS; i++) {
      if (mine[i] != theirs[i]) System.out.printf("  word %d (%s): %#x vs %#x%n", i, Row.NAMES[i], mine[i], theirs[i]);
    }
  }
}
