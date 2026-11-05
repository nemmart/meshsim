package jumpmap;

import gold.HashTable;
import gold.StaticTargets;

import gold.Elf;
import gold.Layout;
import gold.Machine;
import gold.Op;
import gold.Program;
import gold.SecpCompute;
import gold.VInstr;

import java.io.BufferedOutputStream;
import java.io.IOException;
import java.io.OutputStream;
import java.io.PrintWriter;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.file.Files;
import java.nio.file.Path;
import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.BitSet;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;

/**
 * The host side of the Tracer's bring-up (WORKER26): one jump-target table per proof (DECISIONS-v1.3 section 18, as
 * revised 332a518: the static analysis plus the run's own targets, merged on the host, sent whole in frame 6 with its
 * constants), the initial image and each segment's pages written (B1's acceptance on one memory, DECISIONS-v1.3), word
 * 66's values, the acceptance and the measurements.
 *
 * <pre>
 *   java -cp gold/build:jumpmap/build jumpmap.Main &lt;program dir&gt; &lt;input dir&gt; &lt;segment file&gt; &lt;out dir&gt;
 *        [--images=write|check|none] [--segment-rows=4194304] [--name=NAME] [--gold-commit=HASH]
 * </pre>
 *
 * The segment file is gold's (`gold.Main segments`) for the same program, input and cuts. Writes into &lt;out dir&gt;:
 * proof.json, frame6.bin, map.lines, initial.pages and seg/NNN.{pages,json} (with --images=write, the default),
 * segments.csv, run_only.csv, report.json (README.md). --images=check checks the memory without writing the image files;
 * none skips it. Exits 1 if any check fails.
 */
public final class Main {
  public static void main(String[] args) throws Exception {
    if (args.length < 4) {
      System.err.println("usage: jumpmap.Main <program dir> <input dir> <segment file> <out dir> [--images=write|check|none]"
          + " [--segment-rows=N] [--name=NAME] [--gold-commit=HASH]");
      System.exit(2);
    }
    String images = "write", name = null, goldCommit = "unknown";
    long segRows = 1L << 22;
    for (int i = 4; i < args.length; i++) {
      String a = args[i];
      if (a.startsWith("--images=")) images = a.substring(9);
      else if (a.startsWith("--segment-rows=")) segRows = Long.parseLong(a.substring(15));
      else if (a.startsWith("--name=")) name = a.substring(7);
      else if (a.startsWith("--gold-commit=")) goldCommit = a.substring(14);
      else throw new IllegalArgumentException("unknown option " + a);
    }
    Path prog = Path.of(args[0]), in = Path.of(args[1]), segFile = Path.of(args[2]), out = Path.of(args[3]);
    if (name == null) name = in.getFileName().toString();
    boolean ok = new Main(prog, in, segFile, out, images, segRows, name, goldCommit).run();
    System.exit(ok ? 0 : 1);
  }

  final Path prog, in, segFile, out;
  final String images, name, goldCommit;
  final long segRows;
  final PrintWriter log;
  boolean ok = true;

  Main(Path prog, Path in, Path segFile, Path out, String images, long segRows, String name, String goldCommit)
      throws IOException {
    this.prog = prog; this.in = in; this.segFile = segFile; this.out = out; this.images = images;
    this.segRows = segRows; this.name = name; this.goldCommit = goldCommit;
    Files.createDirectories(out);
    log = new PrintWriter(System.out, true);
  }

  void fail(String m) { ok = false; log.println("FAIL: " + m); }

  boolean run() throws Exception {
    long t0 = System.nanoTime();
    Program p = new Program(prog, in);
    Elf elf = new Elf(prog.resolve("program.elf"));
    Layout L = p.layout;
    long lowest = L.lowestAddress();
    StaticTargets st = new StaticTargets(p, elf);
    SegmentFile sf = new SegmentFile(segFile);
    int nseg = sf.segments.size();
    log.printf("%s: lowest_address %#x, code %#x-%#x, %d instructions (%d bytecode rows), %d segments in %s%n", name, lowest,
        L.codeStart, L.codeEnd, st.instructions, p.bytecode.size(), nseg, segFile.getFileName());
    if (L.codeEnd - lowest > 0xffffffffL) fail("code above lowest_address + 2^32: keys don't fit u32");

    // ---- the cycle-0 image: gold's loaded memory, every section page and every input file's pages listed ----
    boolean doImages = !images.equals("none"), writeImages = images.equals("write");
    Image img = new Image(lowest);
    if (doImages) {
      img.copyNonZero(p.memory);
      for (Elf.Section s : elf.sections) if (s.addr() >= Layout.RAM_START && s.size() > 0) img.cover(s.addr(), s.addr() + s.size());
      cover(img, in.resolve("input.bin"), L.inputStart);
      cover(img, in.resolve("trusted_advice.bin"), L.trustedAdviceStart);
      cover(img, in.resolve("untrusted_advice.bin"), L.untrustedAdviceStart);
      log.printf("cycle-0 image: %d pages of 4 KB%n", img.size());
    }
    String initialSha = null;
    if (doImages) {
      try (OutputStream os = writeImages
          ? new BufferedOutputStream(Files.newOutputStream(out.resolve("initial.pages")), 1 << 20) : null) {
        initialSha = img.write(Set.of(), os);
      }
    }
    int initialPages = img.size();
    Path segDir = out.resolve("seg");
    if (writeImages) Files.createDirectories(segDir);

    // ---- the run: every control transfer's target and gold's next instr_idx; the start images at each cut ----
    Machine m = new Machine(p, new SecpCompute());
    StaticTargets.IntList jKey = new StaticTargets.IntList(), jBase = new StaticTargets.IntList();
    int[] segJump = new int[nseg + 1];
    long[] segRowCount = new long[nseg];
    String[] imgSha = new String[nseg];
    int[] imgPages = new int[nseg], imgOwned = new int[nseg];
    Map<Integer, long[]> runOnly = new TreeMap<>();         // key -> {count, first jumping pc, its source opcode, row op}
    long maxAddr = 0, selfJumps = 0, ends = 0;
    for (int k = 0; k < nseg; k++) {
      SegmentFile.Segment sg = sf.segments.get(k);
      long first = k * segRows;
      if (!m.halted()) {
        if (m.cycle != first) fail("segment " + k + ": the run is at row " + m.cycle + ", not " + first);
        if (sg.state[0] != m.pc || sg.state[1] != m.index) fail("segment " + k + ": frame 2's pc and instr_idx differ from gold's");
      } else if (sg.state[0] != 0 || sg.state[1] != 0) {
        fail("segment " + k + ": past the end, but frame 2's pc is not 0");
      }
      String bad = null;
      if (doImages) {
        bad = img.check(p.memory);
        if (bad != null) fail("segment " + k + ": the initial image with segments 0-" + (k - 1) + "'s updates: " + bad);
      }
      long[] written = Image.sortedOffsets(Image.written(sg.updates));
      imgOwned[k] = written.length;
      imgPages[k] = img.size();
      byte[] wb = new byte[8 * written.length];
      for (int i = 0; i < written.length; i++) Image.putLong(wb, 8 * i, written[i]);
      imgSha[k] = sha256(wb);
      if (writeImages) {
        Files.write(segDir.resolve(String.format("%03d.pages", k)), wb);
        Files.writeString(segDir.resolve(String.format("%03d.json", k)), String.format(Locale.ROOT,
            "{\"segment\": %d, \"first_row\": %d, \"pages_written\": %d, \"sha256\": \"%s\", \"gold_check\": \"%s\"}%n",
            k, first, written.length, imgSha[k], !doImages ? "not run" : bad == null ? "ok" : "FAILED: " + bad));
      }
      segJump[k] = jKey.size();
      long end = k + 1 < nseg ? first + segRows : Long.MAX_VALUE;   // as gold: the last segment runs to the end
      while (m.cycle < end && !m.halted()) {
        VInstr v = m.code.at(m.index);
        long pc = m.pc;
        int op = v.opcode;
        m.step();
        segRowCount[k]++;
        if (m.row[7] != 0 && m.row[7] > maxAddr) maxAddr = m.row[7];
        long target = transfer(v, op, pc, m.row);
        if (target == SEQUENTIAL) continue;
        if (target == END) { ends++; continue; }
        if (target == pc) { selfJumps++; continue; }
        if (m.pc != target) fail(String.format("row %d: gold's new pc %#x, the rule's %#x", m.cycle - 1, m.pc, target));
        int key = st.key(target);
        jKey.add(key);
        jBase.add(m.index);
        if (!st.kinds.containsKey(key)) {
          long[] r = runOnly.computeIfAbsent(key, x -> new long[] {0, pc, v.srcOpcode, op});
          r[0]++;
        }
      }
      img.apply(sg.updates);
    }
    segJump[nseg] = jKey.size();
    if (doImages) {
      String bad = img.check(p.memory);
      if (bad != null) fail("after the last segment: " + bad);
    }
    if (!m.halted()) fail("the run did not end within the segment file's " + nseg + " segments");
    long runRows = m.cycle;
    double runSecs = (System.nanoTime() - t0) / 1e9;
    log.printf("run: %d rows, %d jumps (lookups), %d self-jumps, %d program end; %.1f s%n", runRows, jKey.size(), selfJumps,
        ends, runSecs);

    // ---- the run's distinct targets, checked against the bytecode ----
    Map<Integer, Integer> runTargets = new HashMap<>();
    for (int i = 0; i < jKey.size(); i++) {
      Integer prev = runTargets.put(jKey.get(i), jBase.get(i));
      if (prev != null && prev != jBase.get(i)) fail(String.format("target o=%#x has two bases", jKey.get(i)));
    }
    for (Map.Entry<Integer, Integer> e : runTargets.entrySet()) {
      if (e.getValue() != st.base(st.pc(e.getKey()))) fail(String.format("target o=%#x: run base differs from the bytecode's", e.getKey()));
    }

    // ---- the tables: merged (the deliverable), static only, run only, every instruction start (measurements) ----
    Map<Integer, Integer> merged = new HashMap<>();
    for (int key : st.kinds.keySet()) merged.put(key, st.base(st.pc(key)));
    merged.putAll(runTargets);
    HashTable table = build(merged);
    HashTable tStatic = build(baseMap(st, st.kinds.keySet()));
    HashTable tRun = build(runTargets);
    HashTable tAll = build(baseMap(st, boxed(st.allStarts)));
    log.printf("table: %d entries (%d static, %d run, %d run-only), k=%d (%d lines, %d KB), load %.3f, fullest line %d,"
        + " placed 0/1/2 steps %d/%d/%d; p1=%#010x p3=%d%n", table.entries, st.kinds.size(), runTargets.size(), runOnly.size(),
        table.k, table.lines, table.lines * 128 / 1024, table.load(), table.fullestLine(), table.placedAt[0], table.placedAt[1],
        table.placedAt[2], table.p1, table.p3);

    // ---- frame 6 and map.lines; the acceptance reads them back from disk ----
    byte[] lines = table.image();
    byte[] f6 = frame6(table, lines);
    Files.write(out.resolve("map.lines"), lines);
    Files.write(out.resolve("frame6.bin"), f6);
    ByteBuffer rf = ByteBuffer.wrap(Files.readAllBytes(out.resolve("frame6.bin"))).order(ByteOrder.LITTLE_ENDIAN);
    if (rf.getInt(0) != rf.capacity() / 4 || rf.getInt(4) != 6) fail("frame6.bin's header");
    int fk = rf.getInt(8), fp1 = rf.getInt(12), fp3 = rf.getInt(16);
    ByteBuffer img6 = rf.slice(20, rf.capacity() - 20).order(ByteOrder.LITTLE_ENDIAN);
    if (img6.capacity() != (128 << fk)) fail("frame6.bin's line count");
    long[] probeHist = new long[HashTable.PROBE_LINES + 1];
    long mapErrors = 0, wrongBase = 0;
    for (int i = 0; i < jKey.size(); i++) {
      long r = HashTable.probe(img6, fk, fp1, fp3, jKey.get(i));
      if (r < 0) { mapErrors++; probeHist[(int) -r]++; continue; }
      probeHist[(int) (r >>> 32)]++;
      if ((int) r != jBase.get(i)) wrongBase++;
    }
    if (mapErrors != 0) fail(mapErrors + " jumps: map error");
    if (wrongBase != 0) fail(wrongBase + " jumps: base differs from gold's next instr_idx");
    int longest = 0;
    for (int s = 1; s <= HashTable.PROBE_LINES; s++) if (probeHist[s] > 0) longest = s;
    log.printf("acceptance: %d jumps through frame6.bin's table: %d map errors, %d wrong bases; lines read 1/2/3: %d/%d/%d%n",
        jKey.size(), mapErrors, wrongBase, probeHist[1], probeHist[2], probeHist[3]);

    // ---- per segment: distinct targets, the 64-entry LRU, the cache footprint ----
    PrintWriter csv = new PrintWriter(Files.newBufferedWriter(out.resolve("segments.csv")));
    csv.println("segment,rows,jumps,distinct_targets,run_only_targets,lru64_misses,lines_all,lines_misses,"
        + "allstarts_lines_all,allstarts_lines_misses,image_pages,pages_written,written_sha256");
    BitSet bm = new BitSet(table.lines), bmMiss = new BitSet(table.lines);
    BitSet ba = new BitSet(tAll.lines), baMiss = new BitSet(tAll.lines);
    int[] lr = new int[3];
    int[] lru = new int[64];
    long sumMiss = 0;
    long[][] per = new long[nseg][];
    for (int k = 0; k < nseg; k++) {
      bm.clear(); bmMiss.clear(); ba.clear(); baMiss.clear();
      int lruN = 0;
      long misses = 0;
      HashMap<Integer, Boolean> distinct = new HashMap<>();
      long runOnlyHere = 0;
      for (int i = segJump[k]; i < segJump[k + 1]; i++) {
        int key = jKey.get(i);
        if (distinct.put(key, Boolean.TRUE) == null && runOnly.containsKey(key)) runOnlyHere++;
        int at = -1;
        for (int j = 0; j < lruN; j++) if (lru[j] == key) { at = j; break; }
        boolean miss = at < 0;
        if (miss) {
          misses++;
          if (lruN < 64) lruN++;
          at = lruN - 1;
        }
        System.arraycopy(lru, 0, lru, 1, at);
        lru[0] = key;
        int n = Math.abs(table.linesRead(key, lr));
        for (int j = 0; j < n; j++) { bm.set(lr[j]); if (miss) bmMiss.set(lr[j]); }
        n = Math.abs(tAll.linesRead(key, lr));
        for (int j = 0; j < n; j++) { ba.set(lr[j]); if (miss) baMiss.set(lr[j]); }
      }
      sumMiss += misses;
      per[k] = new long[] {segRowCount[k], segJump[k + 1] - segJump[k], distinct.size(), runOnlyHere, misses,
          bm.cardinality(), bmMiss.cardinality(), ba.cardinality(), baMiss.cardinality(), imgPages[k], imgOwned[k]};
      StringBuilder sb = new StringBuilder().append(k);
      for (long x : per[k]) sb.append(',').append(x);
      csv.println(sb.append(',').append(imgSha[k] == null ? "" : imgSha[k]));
    }
    csv.close();

    PrintWriter ro = new PrintWriter(Files.newBufferedWriter(out.resolve("run_only.csv")));
    ro.println("target_pc,key,base,jumps,first_jump_pc,source_opcode,row_opcode");
    for (Map.Entry<Integer, long[]> e : runOnly.entrySet()) {
      long[] r = e.getValue();
      ro.printf("%#x,%#x,%d,%d,%#x,%s,%s%n", st.pc(e.getKey()), e.getKey(), runTargets.get(e.getKey()), r[0], r[1],
          Op.name((int) r[2]), Op.name((int) r[3]));
    }
    ro.close();

    // ---- word 66's values: K as jolt-cpp chooses it (WORKER17-NOTES: the run's highest load or store, raised to the
    //      program-image floor) ----
    long kRun = Long.numberOfTrailingZeros(ceilPow2((maxAddr - lowest) / 8 + 1));
    long imageWords = (L.programEnd - L.programStart + 7) / 8;
    long kFloor = Long.numberOfTrailingZeros(ceilPow2((L.programStart - lowest) / 8 + imageWords + 1));
    long log2K = Math.max(kRun, kFloor);

    // ---- what each kind of analysis adds, cumulatively: static entries, run targets and jumps covered ----
    int[] order = {StaticTargets.DIRECT, StaticTargets.RETURN, StaticTargets.AUIPC_CALL, StaticTargets.AUIPC_ADDR,
        StaticTargets.DATA, StaticTargets.AFTER_ECALL};
    String[] orderNames = {"direct", "+return", "+auipc_call", "+auipc_addr", "+data", "+after_ecall"};
    StringBuilder cum = new StringBuilder("  \"cumulative\": [");
    int mask = 0;
    for (int c = 0; c < order.length; c++) {
      mask |= order[c];
      long entries = st.count(mask), tCov = 0, jCov = 0;
      for (int key : runTargets.keySet()) { Integer kd = st.kinds.get(key); if (kd != null && (kd & mask) != 0) tCov++; }
      for (int i = 0; i < jKey.size(); i++) { Integer kd = st.kinds.get(jKey.get(i)); if (kd != null && (kd & mask) != 0) jCov++; }
      cum.append(String.format(Locale.ROOT, "%s%n    {\"kinds\": \"%s\", \"static_entries\": %d, \"run_targets_covered\": %d,"
          + " \"run_targets_missed\": %d, \"jumps_missed\": %d}", c == 0 ? "" : ",", orderNames[c], entries, tCov,
          runTargets.size() - tCov, jKey.size() - jCov));
      log.printf("  %-12s static %7d, run targets missed %5d of %d, jumps missed %d%n", orderNames[c], entries,
          runTargets.size() - tCov, runTargets.size(), jKey.size() - jCov);
    }
    cum.append("\n  ],\n");

    // ---- proof.json and report.json ----
    String elfSha = sha256(Files.readAllBytes(prog.resolve("program.elf")));
    String segSha = sha256(Files.readAllBytes(segFile));
    StringBuilder segs = new StringBuilder();
    for (int k = 0; k < nseg; k++) segs.append(k == 0 ? "" : ", ").append('"').append(imgSha[k]).append('"');
    Files.writeString(out.resolve("proof.json"), String.format(Locale.ROOT, "{%n"
        + "  \"name\": \"%s\",%n  \"log2_k\": %d,%n  \"lowest_address\": \"%#x\",%n"
        + "  \"hash_k\": %d,%n  \"hash_p1\": \"%#010x\",%n  \"hash_p3\": %d,%n  \"map_entries\": %d,%n"
        + "  \"program_elf_sha256\": \"%s\",%n  \"input\": \"%s\",%n  \"gold_commit\": \"%s\",%n"
        + "  \"segment_file_sha256\": \"%s\",%n  \"segment_rows\": %d,%n  \"segments\": %d,%n"
        + "  \"sha256\": {\"map.lines\": \"%s\", \"frame6.bin\": \"%s\", \"initial.pages\": \"%s\"},%n"
        + "  \"initial_pages\": %d,%n  \"seg_written_sha256\": [%s]%n}%n",
        name, log2K, lowest, table.k, table.p1, table.p3, table.entries, elfSha, in.toString(), goldCommit, segSha, segRows,
        nseg, sha256(lines), sha256(f6), initialSha, initialPages, segs));

    long[] col = new long[nseg];
    StringBuilder rep = new StringBuilder();
    rep.append(String.format(Locale.ROOT, "{%n  \"name\": \"%s\",%n  \"rows\": %d,%n  \"jumps\": %d,%n  \"self_jumps\": %d,%n",
        name, runRows, jKey.size(), selfJumps));
    rep.append(String.format(Locale.ROOT, "  \"instructions\": %d,%n  \"bytecode_rows\": %d,%n", st.instructions, p.bytecode.size()));
    rep.append(String.format(Locale.ROOT, "  \"static\": {\"entries\": %d, \"direct\": %d, \"return\": %d, \"data\": %d,"
        + " \"auipc_call\": %d, \"auipc_addr\": %d, \"direct_only\": %d, \"return_only\": %d, \"data_only\": %d,"
        + " \"auipc_call_only\": %d, \"auipc_addr_only\": %d, \"direct_refs\": %d, \"return_refs\": %d,"
        + " \"auipc_call_refs\": %d, \"auipc_addr_refs\": %d, \"data_words_scanned\": %d, \"data_in_code_not_start\": %d,"
        + " \"bad_direct\": %d, \"bad_return\": %d, \"bad_auipc_call\": %d, \"after_ecall\": %d, \"after_ecall_only\": %d,"
        + " \"ecall_refs\": %d, \"bad_after_ecall\": %d},%n",
        st.kinds.size(), st.count(StaticTargets.DIRECT), st.count(StaticTargets.RETURN), st.count(StaticTargets.DATA),
        st.count(StaticTargets.AUIPC_CALL), st.count(StaticTargets.AUIPC_ADDR),
        st.onlyKind(StaticTargets.DIRECT), st.onlyKind(StaticTargets.RETURN), st.onlyKind(StaticTargets.DATA),
        st.onlyKind(StaticTargets.AUIPC_CALL), st.onlyKind(StaticTargets.AUIPC_ADDR), st.directRefs,
        st.returnRefs, st.auipcCallRefs, st.auipcAddrRefs, st.dataWords, st.dataInCodeNotStart, st.badDirect, st.badReturn,
        st.badAuipcCall, st.count(StaticTargets.AFTER_ECALL), st.onlyKind(StaticTargets.AFTER_ECALL), st.ecallRefs,
        st.badAfterEcall));
    rep.append(String.format(Locale.ROOT, "  \"run\": {\"distinct_targets\": %d, \"run_only\": %d},%n",
        runTargets.size(), runOnly.size()));
    rep.append(cum);
    rep.append(tableJson("table", table)).append(",\n").append(tableJson("table_static_only", tStatic)).append(",\n")
        .append(tableJson("table_run_only", tRun)).append(",\n").append(tableJson("table_all_starts", tAll)).append(",\n");
    rep.append(String.format(Locale.ROOT, "  \"acceptance\": {\"jumps\": %d, \"map_errors\": %d, \"wrong_bases\": %d,"
        + " \"lines_read\": [%d, %d, %d], \"longest_probe\": %d},%n", jKey.size(), mapErrors, wrongBase, probeHist[1],
        probeHist[2], probeHist[3], longest));
    rep.append(String.format(Locale.ROOT, "  \"lru64_misses\": %d,%n", sumMiss));
    String[] names = {"rows", "jumps", "distinct_targets", "run_only_targets", "lru64_misses", "lines_all", "lines_misses",
        "allstarts_lines_all", "allstarts_lines_misses", "image_pages", "pages_written"};
    rep.append("  \"per_segment\": {");
    for (int c = 0; c < names.length; c++) {
      int active = 0;
      for (int k = 0; k < nseg; k++) if (per[k][0] > 0) col[active++] = per[k][c];
      long[] v = Arrays.copyOf(col, active);
      Arrays.sort(v);
      long sum = 0;
      for (long x : v) sum += x;
      rep.append(String.format(Locale.ROOT, "%s%n    \"%s\": {\"segments_with_rows\": %d, \"min\": %d, \"median\": %d, \"mean\": %.1f, \"max\": %d}",
          c == 0 ? "" : ",", names[c], active, active == 0 ? 0 : v[0], active == 0 ? 0 : v[active / 2],
          active == 0 ? 0.0 : sum / (double) active, active == 0 ? 0 : v[active - 1]));
    }
    rep.append(String.format(Locale.ROOT, "%n  },%n  \"word66\": {\"log2_k\": %d, \"log2_k_run\": %d, \"log2_k_image_floor\": %d,"
        + " \"max_address\": \"%#x\", \"image_words\": %d, \"lowest_address\": \"%#x\"},%n", log2K, kRun, kFloor, maxAddr,
        imageWords, lowest));
    rep.append(String.format(Locale.ROOT, "  \"images\": \"%s\",%n  \"seconds\": %.1f,%n  \"ok\": %s%n}%n", images,
        (System.nanoTime() - t0) / 1e9, ok));
    Files.writeString(out.resolve("report.json"), rep.toString());
    log.printf("word 66: log2 K %d (run %d, image floor %d), lowest_address %#x; %s; %.1f s%n", log2K, kRun, kFloor, lowest,
        ok ? "OK" : "FAILED", (System.nanoTime() - t0) / 1e9);
    return ok;
  }

  static final long SEQUENTIAL = -1, END = -2;

  /**
   * The row's new pc if it is a control transfer (spec 4.21.4: a branch taken, a JAL, a JALR; gold's finish rule), END
   * for the program's end (a JAL with immediate 0), else SEQUENTIAL. A transfer to its own pc is returned as pc and
   * counted as sequential by the caller (the self-jump rule). The operands are the row's values before it ran.
   */
  static long transfer(VInstr v, int op, long pc, long[] row) {
    long a = v.rs1Imm ? v.rs1 : row[4], b = v.rs2Imm ? v.rs2 : row[5];
    switch (op) {
      case Op.BEQ: return a == b ? pc + v.rd : SEQUENTIAL;
      case Op.BNE: return a != b ? pc + v.rd : SEQUENTIAL;
      case Op.BLT: return a < b ? pc + v.rd : SEQUENTIAL;
      case Op.BGE: return a >= b ? pc + v.rd : SEQUENTIAL;
      case Op.BLTU: return Long.compareUnsigned(a, b) < 0 ? pc + v.rd : SEQUENTIAL;
      case Op.BGEU: return Long.compareUnsigned(a, b) >= 0 ? pc + v.rd : SEQUENTIAL;
      case Op.JAL: return a == 0 ? END : pc + a;
      case Op.JALR: return (a + b) & ~1L;
      default: return SEQUENTIAL;
    }
  }

  static HashTable build(Map<Integer, Integer> m) {
    int[] keys = new int[m.size()];
    int i = 0;
    for (int k : m.keySet()) keys[i++] = k;
    long[] l = new long[keys.length];
    for (i = 0; i < keys.length; i++) l[i] = keys[i] & 0xffffffffL;
    Arrays.sort(l);
    int[] bases = new int[keys.length];
    for (i = 0; i < keys.length; i++) { keys[i] = (int) l[i]; bases[i] = m.get(keys[i]); }
    return HashTable.choose(keys, bases);
  }

  static Map<Integer, Integer> baseMap(StaticTargets st, Iterable<Integer> keys) {
    Map<Integer, Integer> m = new HashMap<>();
    for (int k : keys) m.put(k, st.base(st.pc(k)));
    return m;
  }

  static List<Integer> boxed(int[] a) {
    List<Integer> l = new ArrayList<>(a.length);
    for (int x : a) l.add(x);
    return l;
  }

  /** Frame 6 (section 16, as revised): u32 length in words, u32 type 6, u32 k, u32 p1, u32 p3, then the 2^k lines. */
  static byte[] frame6(HashTable t, byte[] lines) {
    ByteBuffer b = ByteBuffer.allocate(20 + lines.length).order(ByteOrder.LITTLE_ENDIAN);
    b.putInt(5 + lines.length / 4).putInt(6).putInt(t.k).putInt(t.p1).putInt(t.p3).put(lines);
    return b.array();
  }

  static String tableJson(String key, HashTable t) {
    return String.format(Locale.ROOT, "  \"%s\": {\"entries\": %d, \"k\": %d, \"lines\": %d, \"bytes\": %d, \"load\": %.4f,"
        + " \"fullest_line\": %d, \"placed_0_1_2\": [%d, %d, %d], \"p1\": \"%#010x\", \"p3\": %d}", key, t.entries, t.k,
        t.lines, t.lines * 128L, t.load(), t.fullestLine(), t.placedAt[0], t.placedAt[1], t.placedAt[2], t.p1, t.p3);
  }

  static long ceilPow2(long x) { return x <= 1 ? 1 : Long.highestOneBit(x - 1) << 1; }

  static void cover(Image img, Path f, long start) throws IOException {
    if (Files.exists(f)) img.cover(start, start + Files.size(f));
  }

  static String sha256(byte[] b) {
    try { return hex(MessageDigest.getInstance("SHA-256").digest(b)); } catch (Exception e) { throw new IllegalStateException(e); }
  }

  static String hex(byte[] b) {
    StringBuilder s = new StringBuilder();
    for (byte x : b) s.append(String.format("%02x", x));
    return s.toString();
  }
}
