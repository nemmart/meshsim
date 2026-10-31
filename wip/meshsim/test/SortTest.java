import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Random;

/**
 * The sort program on meshsim (worker 6's driver; integrated into the simulator tree as a suite by worker 8; ported to
 * meshsim v1.2 by WORKER8, who owns this file and Inputs, SortSummary and VerifySummary on the sort's branch).
 *
 * C&C's sequence is built with the simulator's Java sequencer (CnC). The harness's own jobs, all simulator-only:
 *   - the INPUT-LOAD STAND-IN: it places each tile's input in big SRAM and its record count in S.n_in while every tile
 *     waits for its first command;
 *   - (P1 only) the L2 test's stand-in for the keys all-to-all;
 *   - (an image with the oracle split) the oracle's answer;
 *   - verification and measurement.
 * Boot (outside the sort's total; PHASE-2 INTERIM, as Mach.boot, until meshsim's boot ROM): C&C broadcasts the interim
 * loader, the image's program memory and data memory as uncounted mode-0 writes, then CMD_JUMP, and raises RUN. v1.2's
 * image has no overlays, so nothing is staged.
 *
 * Accounting: PhaseLog, one phase per GO, named by command (the split's barriers are SPLIT phases). A step's cycles
 * are its GO to the next step's GO (C&C's turnaround after it included); the last step's end is its all-READY. The sum
 * over steps is the total, L1's GO -> L2's all READY. With an oracle split, the oracle's own cycles (tile 0 in
 * split_get_cuts) are subtracted from the total; everything else in SPLIT (zero_planner) is sort work.
 *
 * Usage:
 *   java SortTest [seed] p1 [full]                                  the local sorts
 *   java SortTest [seed] sort [full] [only=<set>]                    the built-in sets (run_tests.sh)
 *   java SortTest [seed] run kind=<builtin>|keys=<file> [n=<per tile>] fmt=k128|kv32 [shaped] [label=..] [ready=N]
 *                            [cfg.<Config field>=<value> ...] [csv=<file>] [phases=<file>] [force=bt (TEST ONLY)]
 * seed: -1 fixed child order, >= 0 shuffled. -Dsort.build=<dir> (default sw/ext/sort/build). Debug: -Dsort.profile,
 * -Dsort.phases (PhaseLog's table), -Dsort.maxcycles=N.
 * The output starts with an "inputs:" line, the SHA-256 of the jar, the image files, hops.txt and the key file
 * (Inputs.java).
 */
final class SortTest {
    // ---- sort.h ----
    static final int AREA = 0x20000, BUF_IN = 0, BUF_X = 3 * AREA, BUF_Y = 4 * AREA, BUF_VIN = AREA, BUF_VS = 5 * AREA;
    static final int CMD_L1 = 0x10, CMD_XB = 0x11, CMD_RED = 0x12, CMD_K = 0x13, CMD_SPLIT = 0x14, CMD_L2 = 0x15, CMD_V = 0x16,
        CMD_XC = 0x20, CMD_TH = 0x21, CMD_SWEEP = 0x22, CMD_DESC = 0x23, CMD_K1 = 0x24, CMD_K2 = 0x25, CMD_V1 = 0x26, CMD_V2 = 0x27,
        CMD_BARRIER = 0x30, CMD_EXIT = 0x7F, FMT_KV32 = 0, FMT_K128 = 1;
    // struct sort_state word offsets (sort.h): fmt, n_in, n_recv, red_in, path, plan_wanted, shaped, groups, bt_wanted
    static final int S_NIN = 1, S_NRECV = 2, S_PATH = 4, S_PLAN = 5, S_BT = 8;
    static final String[] PATHS = { "direct", "LB", "TH", "BT", "PLAN" };

    static Path build = Path.of(System.getProperty("sort.build", "sw/ext/sort/build"));
    static void out(String s) { System.out.println(s); }

    /** Run options. */
    static final class Opts {
        long shuffle = -1; int readyLatency = -1; final Map<String, String> cfg = new LinkedHashMap<>();
        String csv, phasesCsv, label = ""; boolean forceBt;   // forceBt: TEST ONLY, BT whenever feasible
        Config config() {
            Config k = Config.machine(); k.shuffleSeed = shuffle;
            if (readyLatency >= 0) k.readyLatency = readyLatency;
            for (var e : cfg.entrySet()) setField(k, e.getKey(), e.getValue());
            return k;
        }
        static void setField(Config k, String f, String v) {
            try {
                var fld = Config.class.getDeclaredField(f); fld.setAccessible(true);
                Class<?> t = fld.getType();
                if (t == int.class) fld.setInt(k, Integer.parseInt(v));
                else if (t == long.class) fld.setLong(k, Long.parseLong(v));
                else if (t == boolean.class) fld.setBoolean(k, Boolean.parseBoolean(v));
                else throw new IllegalArgumentException("Config." + f + ": unsupported type " + t);
            } catch (ReflectiveOperationException e) { throw new IllegalArgumentException("no Config field " + f); }
        }
    }

    // ================================================================ the image
    static final class Image {
        final int[] code, data; final Map<String, Integer> sym = new HashMap<>();
        final java.util.TreeMap<Integer, String> funcs = new java.util.TreeMap<>();
        final Map<String, Integer> sizes = new HashMap<>();
        Image() {
            try {
                code = words(build.resolve("sort.code.bin")); data = words(build.resolve("sort.data.bin"));
                for (String l : Files.readAllLines(build.resolve("sort.sym"))) {      // nm -S: addr [size] type name
                    String[] f = l.trim().split("\\s+");
                    if (f.length < 3) continue;
                    int a = (int) Long.parseLong(f[0], 16), size = f.length == 4 ? (int) Long.parseLong(f[1], 16) : 0;
                    String type = f[f.length - 2], name = f[f.length - 1];
                    sym.putIfAbsent(name, a);
                    if (size > 0) sizes.putIfAbsent(name, size);
                    if ((type.equals("T") || type.equals("t")) && a >= Registers.PMEM_BASE && a < Registers.LOADER_BASE) funcs.putIfAbsent(a, name);
                }
            } catch (IOException e) { throw new RuntimeException("run sw/ext/sort/build.sh first: " + e.getMessage()); }
        }
        int sym(String s) { Integer v = sym.get(s); if (v == null) throw new IllegalArgumentException("no symbol " + s); return v; }
        boolean oracle() { return sym.containsKey("ORACLE"); }
        static int[] words(Path p) throws IOException {
            byte[] b = Files.readAllBytes(p);
            int[] w = new int[(b.length + 3) / 4];
            for (int i = 0; i < b.length; i++) w[i >> 2] |= (b[i] & 0xFF) << (8 * (i & 3));
            return w;
        }
    }

    // ================================================================ a machine running the sort
    static final class Machine {
        final Chip c; final Image im = new Image(); final CnC cc;
        final boolean[] wasWfi = new boolean[64];
        long allReadyAt = -1;
        int minSp = Integer.MAX_VALUE;                          // the lowest stack pointer any core reached
        // tile 0's cycles (awake or in WFI) per function, per phase (PhaseLog index), and over the run
        final Map<Integer, Map<String, Long>> profByPhase = new HashMap<>();
        final Map<String, Long> prof = new LinkedHashMap<>();
        // -Dsort.watch=f1,f2,...: every tile's cycles with its pc inside each named function (by its symbol's address and
        // size), per phase.
        final String[] watch = System.getProperty("sort.watch", "").isEmpty() ? new String[0] : System.getProperty("sort.watch").split(",");
        final int[] wLo, wHi;
        final Map<Integer, long[][]> watchByPhase = new HashMap<>();   // phase -> [function][tile]

        Machine(Opts o) {
            c = new Chip(o.config()); cc = c.cc;
            String pr = System.getProperty("sort.prof");       // debug: -Dsort.prof=lo-hi (hex pcs): tile 0's cycles in [lo, hi)
            if (pr != null) { String[] f = pr.split("-"); c.tiles[0].core.profLo = (int) Long.parseLong(f[0], 16); c.tiles[0].core.profHi = (int) Long.parseLong(f[1], 16); }
            wLo = new int[watch.length]; wHi = new int[watch.length];
            for (int i = 0; i < watch.length; i++) {            // a name, or GCC's clone of it (name.constprop.0, name.isra.0)
                final String w = watch[i];
                String nm = im.sym.containsKey(w) ? w : im.sym.keySet().stream().filter(k -> k.startsWith(w + ".")).findFirst().orElse(w);
                wLo[i] = im.sym(nm); wHi[i] = wLo[i] + im.sizes.getOrDefault(nm, 0);
            }
        }
        /** Boot (spec 6.2; PHASE-2 INTERIM, as Mach.boot), outside the sort's total: the interim loader (sw/build/loader.bin,
         *  built by sw/build.sh), program memory, data memory from _data_start, CMD_JUMP to _start, then RUN. */
        void boot() {
            cc.loadWords(LinkWord.BROADCAST, Registers.LOADER_BASE, Mach.loader())
              .loadWords(LinkWord.BROADCAST, Registers.PMEM_BASE, im.code)
              .loadWords(LinkWord.BROADCAST, im.sym("_data_start"), im.data)
              .command(LinkWord.BROADCAST, Registers.CMD_JUMP << 24, im.sym("_start"))
              .runWhenReady();
            boolean ok = c.runUntil(() -> !cc.busy() && allWaiting(), 100_000);
            T.check(ok, "boot: every tile waits for its first command");
        }
        boolean allWaiting() { for (Tile p : c.tiles) if (!p.core.inWfi || p.core.halt != Controller.RUN) return false; return true; }
        boolean allStopped() { for (Tile p : c.tiles) if (p.core.halt == Controller.RUN) return false; return true; }
        boolean allExited() { for (Tile p : c.tiles) if (p.core.halt != Controller.HALT_EXIT) return false; return true; }
        void pokeS(int tile, int field, int v) { c.tiles[tile].core.pokeLocal(im.sym("S") + 4 * field, v); }
        int peekS(int tile, int field) { return c.tiles[tile].core.peekLocal(im.sym("S") + 4 * field); }

        /** Queue: wait for all READY (recording when), then run 'r' (between READY and GO, harness-only work). */
        CnC atAllReady(Runnable r) {
            return cc.waitUntil(() -> { if (cc.allReady(c.cycle)) { allReadyAt = c.cycle; return true; } return false; }).call(r);
        }
        boolean finish(long max) { return c.runUntil(() -> { monitor(); return !cc.busy() && allStopped(); }, max); }
        void monitor() {
            for (int i = 0; i < 64; i++) {
                Tile p = c.tiles[i];
                if (!p.core.inWfi && wasWfi[i]) p.sram.newPhase();     // invariant 2 per run: a phase at every wake
                wasWfi[i] = p.core.inWfi;
                if (p.core.x[2] != 0 && p.core.x[2] < minSp) minSp = p.core.x[2];
            }
            if (watch.length > 0) {
                long[][] w = watchByPhase.computeIfAbsent(c.phases.phases.size() - 1, x -> new long[watch.length][64]);
                for (int i = 0; i < 64; i++) {
                    int pc = c.tiles[i].core.pc;
                    for (int f = 0; f < watch.length; f++) if (pc >= wLo[f] && pc < wHi[f]) w[f][i]++;
                }
            }
            Tile p = c.tiles[0];               // wall time: a WFI's cycles go to the function waiting in it
            if (p.core.halt == Controller.RUN) {
                String f;
                if (p.core.pc >= Registers.LOADER_BASE && p.core.pc < Registers.LOADER_BASE + Registers.LOADER_SIZE) f = "loader";
                else { Map.Entry<Integer, String> e = im.funcs.floorEntry(p.core.pc); f = e == null ? "?" : e.getValue(); }
                prof.merge(f, 1L, Long::sum);
                profByPhase.computeIfAbsent(c.phases.phases.size() - 1, x -> new LinkedHashMap<>()).merge(f, 1L, Long::sum);
            }
        }
        String haltSummary() {
            StringBuilder sb = new StringBuilder();
            for (Tile p : c.tiles) if (p.core.halt != Controller.HALT_EXIT || p.ctl.simExitValue != 0) { sb.append(p.core.haltReport()).append(" exit=0x").append(Integer.toHexString(p.ctl.simExitValue)).append(" pc=0x").append(Integer.toHexString(p.core.pc)).append("; "); if (sb.length() > 400) break; }
            return sb.toString();
        }
        void dumpState() {
            Map<String, List<Integer>> st = new java.util.TreeMap<>();
            for (Tile p : c.tiles) {
                Map.Entry<Integer, String> e = im.funcs.floorEntry(p.core.pc);
                String k = String.format("pc 0x%x%s wfi %b counters 1-3: sram %d cm %d reply %d; ag running %b done %b",
                    p.core.pc, e == null ? "" : " (" + e.getValue() + ")", p.core.inWfi, p.dir.remaining[1], p.dir.remaining[2], p.dir.remaining[3],
                    p.ag.running, p.ag.done);
                st.computeIfAbsent(k, x -> new ArrayList<>()).add(p.idx);
            }
            out("    state at cycle " + c.cycle + " (C&C at phase " + cc.phaseLog.size() + "), violations " + c.violations + (c.violations > 0 ? "\n" + c.violationReport() : "") + ":");
            out("    " + c.agReport().trim()); out("    " + c.memReport().trim());
            for (var e : st.entrySet()) out("      " + e.getKey() + ": tiles " + e.getValue());
            if (System.getProperty("sort.phases") != null) { c.phases.close(); out(c.phases.report()); }
            StringBuilder sw = new StringBuilder("    tile 0's S, first 16 words:"); for (int i = 0; i < 16; i++) sw.append(" " + peekS(0, i)); out(sw.toString());
        }
        void checkEnd(String what) {
            T.check(allExited(), what + ": every core exited through SIM_EXIT");
            T.check(haltSummary().isEmpty(), what + ": every core exited with 0: " + haltSummary());
            T.check(c.violations == 0, what + ": no invariant violations" + (c.violations == 0 ? "" : "\n" + c.violationReport()));
            for (Tile p : c.tiles) {
                int cond = p.ctl.conditions;            // ERRORS (4.13) = 0 on every tile, drift included
                if (cond != 0) { T.check(false, what + ": tile " + p.idx + " ERRORS 0x" + Integer.toHexString(cond) + " (" + Conditions.names(cond) + ")"); break; }
            }
        }
    }

    // ================================================================ data sets
    /** Built-in sets: n keys per tile; uniform, few (5 distinct values), ragged sizes, sorted, reverse, all equal. */
    static int[][] keys(String kind, int n, long seed) {
        Random r = new Random(seed);
        int[][] k = new int[64][];
        for (int p = 0; p < 64; p++) {
            int np = n;
            if (kind.equals("ragged")) np = p == 3 ? 0 : p == 5 ? 1 : p == 9 ? 2 * n : r.nextInt(2 * n + 1);
            k[p] = new int[np];
            for (int j = 0; j < np; j++)
                k[p][j] = switch (kind) {
                    case "few" -> r.nextInt(5) * 0x01010101;
                    case "sorted" -> p * np + j;
                    case "reverse" -> ~(p * np + j);
                    case "equal" -> 0x5A5A5A5A;
                    default -> r.nextInt();
                };
        }
        return k;
    }
    /** A key file of tools/export_keys.py: magic, 64 counts, the keys tile by tile. */
    static int[][] readKeys(Path f) throws IOException {
        ByteBuffer b = ByteBuffer.wrap(Files.readAllBytes(f)).order(ByteOrder.LITTLE_ENDIAN);
        if (b.getInt() != 0x4B455953) throw new IOException(f + ": not a key file");
        int[][] k = new int[64][];
        for (int p = 0; p < 64; p++) k[p] = new int[b.getInt()];
        for (int p = 0; p < 64; p++) for (int j = 0; j < k[p].length; j++) k[p][j] = b.getInt();
        return k;
    }
    static long value32(int p, int j) { return (long) (p * 0x9E3779B1 + j * 0x85EBCA77) & 0xFFFFFFFFL; }
    /** A 128-bit value of input record (tile p, position j): two 64-bit halves, both functions of (p, j). */
    static long valueHalf(int p, int j, int h) {
        long z = (long) p << 40 ^ (long) j << 8 ^ h * 0x9E3779B97F4A7C15L;
        z = (z ^ z >>> 31) * 0xBF58476D1CE4E5B9L; return z ^ z >>> 29;
    }
    static long[] stableByKey(long[] r) {
        Long[] b = new Long[r.length];
        for (int i = 0; i < r.length; i++) b[i] = r[i];
        Arrays.sort(b, (x, y) -> Integer.compareUnsigned((int) (long) x, (int) (long) y));   // TimSort: stable
        long[] o = new long[r.length];
        for (int i = 0; i < r.length; i++) o[i] = b[i];
        return o;
    }
    static int mismatches(Chip c, int tile, int base, long[] expect) {
        int bad = 0;
        for (int i = 0; i < expect.length; i++) if (c.tiles[tile].sram.peek64(base + 2 * i) != expect[i]) bad++;
        return bad;
    }

    // ================================================================ P1: the local sorts
    static void p1(String kind, int n, int fmt, Opts op) {
        String what = "P1 " + kind + " n=" + n + (fmt == FMT_K128 ? " k=2 (augmented)" : " 32-bit values (copy)");
        T.group(what);
        Machine m = new Machine(op);
        m.boot();
        int[][] k = keys(kind, n, 11 + kind.hashCode());
        long[][] l1Expect = new long[64][];
        for (Tile p : m.c.tiles) {
            int i = p.idx; long[] rec = new long[k[i].length];
            for (int j = 0; j < k[i].length; j++) {
                if (fmt == FMT_K128) { p.sram.poke(BUF_IN + j, k[i][j]); rec[j] = (k[i][j] & 0xFFFFFFFFL) | (long) j << 32; }
                else { rec[j] = (k[i][j] & 0xFFFFFFFFL) | value32(i, j) << 32; p.sram.poke64(BUF_IN + 2 * j, rec[j]); }
            }
            l1Expect[i] = stableByKey(rec);
            m.pokeS(i, S_NIN, k[i].length);
        }
        int[][] k2 = keys(kind, n, 77 + kind.hashCode());
        long[][] l2Expect = new long[64][];
        final int[] l1Bad = new int[1];
        final int base = m.cc.phaseLog.size();
        m.cc.command(LinkWord.BROADCAST, CMD_L1 << 24 | fmt).go().command(LinkWord.BROADCAST, CMD_L2 << 24);
        final long[] l1Ready = new long[1];
        m.atAllReady(() -> {
            l1Ready[0] = m.allReadyAt;
            for (Tile p : m.c.tiles) {
                int i = p.idx;
                l1Bad[0] += mismatches(m.c, i, BUF_X, l1Expect[i]);
                long[] rec = new long[k2[i].length]; int[] cnt = new int[256];
                for (int j = 0; j < rec.length; j++) { rec[j] = (k2[i][j] & 0xFFFFFFFFL) | (long) j << 32; p.sram.poke64(BUF_Y + 2 * j, rec[j]); cnt[k2[i][j] & 0xFF]++; }
                for (int t = 0; t < 256; t++) p.hist.poke(t, cnt[t]);
                l2Expect[i] = stableByKey(rec);
                m.pokeS(i, S_NRECV, rec.length);
            }
        }).go().command(LinkWord.BROADCAST, CMD_EXIT << 24);
        final long[] l2Ready = new long[1];
        m.atAllReady(() -> l2Ready[0] = m.allReadyAt).go();
        boolean ok = m.finish(30_000_000);
        T.check(ok, what + ": the sequence ran to its end");
        T.eq(0, l1Bad[0], what + ": L1: every tile's BUF_X holds its input, stably sorted by key");
        int l2Bad = 0;
        for (Tile p : m.c.tiles) l2Bad += mismatches(m.c, p.idx, BUF_Y, l2Expect[p.idx]);
        T.eq(0, l2Bad, what + ": L2: every tile's BUF_Y holds its records, stably sorted by key");
        for (Tile p : m.c.tiles) for (int t = 0; t < 256; t++) if (p.hist.peek(t) != 0) { T.check(false, what + ": HIST clear at the end (tile " + p.idx + ")"); t = 256; break; }
        m.checkEnd(what);
        List<long[]> ph = m.cc.phaseLog;
        out(String.format("%s: L1 GO -> all READY %,d; L2 GO -> all READY %,d  [meas: SortTest p1]", what, l1Ready[0] - ph.get(base)[1], l2Ready[0] - ph.get(base + 1)[1]));
    }

    // ================================================================ the sort, end to end
    /** The host's view of one sort: the global order and the exact split. */
    static final class Oracle {
        final int[][] k; final int fmt; final long N;
        final int[][] c = new int[64][65];         // c[p][q]
        final int[][] cnt = new int[64][64];       // cnt[p][q]
        final long[][] expect = new long[64][];    // the final contents of each owner's BUF_Y
        final long[][] source = new long[64][];    // each final record's (tile << 32 | input position)
        final int[] R = new int[16];               // the direct path's group maxima (schedule-4 groups)
        Oracle(int[][] k, int fmt, boolean shaped) {
            this.k = k; this.fmt = fmt;
            long n = 0; for (int[] x : k) n += x.length; N = n;
            long[] g = new long[(int) N]; int at = 0;
            for (int p = 0; p < 64; p++) for (int j = 0; j < k[p].length; j++) g[at++] = (long) (k[p][j] ^ 0x80000000) << 32 | (long) p << 26 | j;
            Arrays.sort(g);                        // (key unsigned, tile, position)
            long[] Rq = new long[65];
            for (int q = 0; q <= 64; q++) Rq[q] = shaped ? (q == 0 ? 0 : Rq[q - 1] + k[q - 1].length) : q * N / 64;
            int q = 0;
            for (int r = 0; r < N; r++) { while (r >= Rq[q + 1]) q++; cnt[(int) (g[r] >>> 26 & 63)][q]++; }
            for (int p = 0; p < 64; p++) for (int qq = 0; qq < 64; qq++) c[p][qq + 1] = c[p][qq] + cnt[p][qq];
            int[][] prefix = new int[64][64];
            for (int qq = 0; qq < 64; qq++) for (int p = 1; p < 64; p++) prefix[qq][p] = prefix[qq][p - 1] + cnt[p - 1][qq];
            int[] pos = new int[64];
            q = 0; int r0 = 0;
            for (int qq = 0; qq < 64; qq++) { expect[qq] = new long[(int) (Rq[qq + 1] - Rq[qq])]; source[qq] = new long[expect[qq].length]; }
            for (int r = 0; r < N; r++) {
                while (r >= Rq[q + 1]) { q++; r0 = r; }
                int p = (int) (g[r] >>> 26 & 63), j = (int) (g[r] & 0x3FFFFFF), s = pos[p]++;
                long key = (g[r] >>> 32 ^ 0x80000000L) & 0xFFFFFFFFL;
                long hi = fmt == FMT_KV32 ? value32(p, j) : prefix[q][p] + s - c[p][q];
                expect[q][r - r0] = key | hi << 32;
                source[q][r - r0] = (long) p << 32 | j;
            }
            for (int gi = 0; gi < 16; gi++) for (int p = 0; p < 64; p++) for (int i = 0; i < 4; i++) R[gi] = Math.max(R[gi], cnt[p][(p + G4[gi][i]) & 63]);
        }
    }
    static final int[][] G4 = { { 1, 29, 35, 63 }, { 2, 30, 34, 62 }, { 3, 31, 33, 61 }, { 5, 23, 41, 59 }, { 6, 22, 42, 58 },
        { 7, 21, 43, 57 }, { 8, 28, 36, 56 }, { 9, 27, 37, 55 }, { 10, 26, 38, 54 }, { 11, 25, 39, 53 }, { 12, 24, 40, 52 },
        { 13, 15, 49, 51 }, { 14, 18, 46, 50 }, { 16, 20, 44, 48 }, { 17, 19, 45, 47 }, { 0, 4, 32, 60 } };

    static void loadInput(Machine m, int[][] k, int fmt) {
        for (Tile p : m.c.tiles) {
            int i = p.idx;
            for (int j = 0; j < k[i].length; j++) {
                if (fmt == FMT_K128) {
                    p.sram.poke(BUF_IN + j, k[i][j]);
                    p.sram.poke64(BUF_VIN + 4 * j, valueHalf(i, j, 0)); p.sram.poke64(BUF_VIN + 4 * j + 2, valueHalf(i, j, 1));
                }
                else p.sram.poke64(BUF_IN + 2 * j, (k[i][j] & 0xFFFFFFFFL) | value32(i, j) << 32);
            }
            m.pokeS(i, S_NIN, k[i].length);
        }
    }

    /** The phase name of a command word (PhaseLog's namer; agreed with the accounting). */
    static String phaseName(int w) {
        if (w < 0) return "-";
        int t = w >>> 24, a = w & 0x7FFFFF;
        return switch (t) {
            case CMD_L1 -> "L1"; case CMD_SPLIT, CMD_BARRIER -> "SPLIT"; case CMD_XB -> "XB"; case CMD_RED -> "RED";
            case CMD_XC -> "XC"; case CMD_TH -> "TH"; case CMD_SWEEP -> "SWEEP"; case CMD_DESC -> "DESC"; case CMD_K -> "K"; case CMD_V -> "V";
            case CMD_K1 -> "K1." + a; case CMD_K2 -> "K2." + a; case CMD_V1 -> "V1." + a; case CMD_V2 -> "V2." + a;
            case CMD_L2 -> "L2"; case CMD_EXIT -> "EXIT"; default -> String.format("cmd %02X", t);
        };
    }
    static final java.util.regex.Pattern SKEW_CMD = java.util.regex.Pattern.compile("XC|TH|SWEEP|DESC|K|V|..\\..");

    /** One step's measurement: cycles (GO -> next step's GO), GO -> all READY summed over its phases, phases, peaks. */
    static final class Step {
        final String name; long cycles, goReady, ovLoad; int phases; final Map<String, Integer> peak = new LinkedHashMap<>();
        Step(String n) { name = n; }
    }
    record Result(String path, boolean stage2, boolean btWanted, long total, long oracle, List<Step> steps, int checksFailed) {}

    /** The sort end to end; returns the measurement (null if it did not finish). */
    static Result sortRun(String label, int[][] k, int fmt, boolean shaped, Opts op) {
        int n = 0; for (int[] x : k) n = Math.max(n, x.length);
        String what = "sort " + label + (fmt == FMT_K128 ? " 128-bit values" : " 32-bit values") + (shaped ? " shaped" : "");
        T.group(what);
        int failed0 = T.failed;
        Oracle o = new Oracle(k, fmt, shaped);
        Machine m = new Machine(op);
        m.c.phases.namer = SortTest::phaseName;
        m.boot();
        loadInput(m, k, fmt);
        boolean oracle = m.im.oracle();
        // the fixed sequence (skew-v2.md 5): L1 SPLIT XB RED XC TH SWEEP DESC K K1.0-4 K2.0-4 [V V1.0-4 V2.0-4] L2
        List<Integer> cmd = new ArrayList<>(List.of(CMD_L1 << 24 | fmt, CMD_SPLIT << 24 | (shaped ? 1 : 0), CMD_XB << 24, CMD_RED << 24,
            CMD_XC << 24, CMD_TH << 24 | (op.forceBt ? 1 : 0), CMD_SWEEP << 24, CMD_DESC << 24, CMD_K << 24));
        for (int l = 0; l < 5; l++) cmd.add(CMD_K1 << 24 | l);
        for (int l = 0; l < 5; l++) cmd.add(CMD_K2 << 24 | l);
        if (fmt == FMT_K128) {
            cmd.add(CMD_V << 24);
            for (int l = 0; l < 5; l++) cmd.add(CMD_V1 << 24 | l);
            for (int l = 0; l < 5; l++) cmd.add(CMD_V2 << 24 | l);
        }
        cmd.add(CMD_L2 << 24);
        int barriers = oracle ? 0 : shaped ? 65 : 66;            // the split's own barriers (BINDING.md)
        int oracleAt = oracle ? m.im.sym("ORACLE") : 0;
        m.cc.command(LinkWord.BROADCAST, cmd.get(0)).go();
        for (int i = 1; i <= cmd.size(); i++) {
            final int prev = i - 1;
            if ((cmd.get(prev) >>> 24) == CMD_SPLIT)               // the split's barriers: one-line commands + GO
                for (int b = 0; b < barriers; b++) { m.cc.command(LinkWord.BROADCAST, CMD_BARRIER << 24); m.atAllReady(() -> {}).go(); }
            m.cc.command(LinkWord.BROADCAST, i < cmd.size() ? cmd.get(i) : CMD_EXIT << 24);
            m.atAllReady(() -> {
                if (oracle && (cmd.get(prev) >>> 24) == CMD_L1)   // SIMULATOR-ONLY ORACLE: the split's answer, for split_get_cuts()
                    for (Tile p : m.c.tiles) {
                        for (int q = 0; q <= 64; q++) p.core.pokeLocal(oracleAt + 4 * q, o.c[p.idx][q]);
                        for (int q = 0; q < 64; q++) p.core.pokeLocal(oracleAt + 4 * (65 + q), k[q].length);
                    }
            }).go();
        }
        boolean ok = m.finish(Long.getLong("sort.maxcycles", 400_000_000L));
        T.check(ok, what + ": the sequence ran to its end");
        if (!ok) { m.dumpState(); return null; }
        m.c.phases.close();

        // ---- verification
        int bad = 0, sizeBad = 0, rBad = 0, vBad = 0;
        for (Tile p : m.c.tiles) {
            int q = p.idx;
            if (m.peekS(q, S_NRECV) != o.expect[q].length) sizeBad++;
            bad += mismatches(m.c, q, BUF_Y, o.expect[q]);
            if (fmt == FMT_K128)
                for (int r = 0; r < o.source[q].length; r++) {
                    int sp = (int) (o.source[q][r] >>> 32), sj = (int) o.source[q][r];
                    if (p.sram.peek64(BUF_VS + 4 * r) != valueHalf(sp, sj, 0) || p.sram.peek64(BUF_VS + 4 * r + 2) != valueHalf(sp, sj, 1)) vBad++;
                }
        }
        int path0 = m.peekS(0, S_PATH), plan0 = m.peekS(0, S_PLAN), bt0 = m.peekS(0, S_BT), pathBad = 0;
        for (Tile p : m.c.tiles) if (m.peekS(p.idx, S_PATH) != path0 || m.peekS(p.idx, S_PLAN) != plan0) pathBad++;
        if (path0 <= 1) for (Tile p : m.c.tiles) for (int g = 0; g < 15; g++) if ((p.ts.peek(TableStore.GROUP + 2 * g) & 0x3FFFF) != o.R[g]) rBad++;
        T.eq(0, pathBad, what + ": every tile took the same path");
        int end = m.im.sym("_end"), headroom = m.minSp - end;
        T.check(headroom > 0, what + ": the stack stayed above .bss (headroom " + headroom + " B)");
        if (headroom < 100) out("    WARNING (lead, 3 Oct): stack headroom " + headroom + " B, within 100 B of .bss");
        T.eq(0, sizeBad, what + ": every owner received R_(q+1) - R_q records");
        if (path0 <= 1) T.eq(0, rBad, what + ": every tile's GROUP R = the global group maxima (groups 0-14)");
        T.eq(0, bad, what + ": the keys are in global (key, tile, position) order on every owner, "
                + (fmt == FMT_KV32 ? "every value with its key" : "every ordinal naming its key's receive position"));
        if (fmt == FMT_K128) T.eq(0, vBad, what + ": the final values (A5-A6) are in the same order as the final keys, every value its key's");
        for (Tile p : m.c.tiles) for (int t = 0; t < 256; t++) if (p.hist.peek(t) != 0) { T.check(false, what + ": HIST clear at the end (tile " + p.idx + ")"); t = 256; break; }
        m.checkEnd(what);

        // ---- measurement (PhaseLog): steps in sequence order, the split's barrier phases inside SPLIT
        List<PhaseLog.Phase> ph = m.c.phases.phases;
        List<Step> steps = new ArrayList<>(); Step cur = null;
        int lastSort = ph.size() - 1;                             // the EXIT phase is not the sort's
        while (lastSort >= 0 && !ph.get(lastSort).name.equals("L2")) lastSort--;
        int firstSort = 0;                                        // anything before L1 is not the sort's
        while (firstSort < lastSort && !ph.get(firstSort).name.equals("L1")) firstSort++;
        for (int i = firstSort; i <= lastSort; i++) {
            PhaseLog.Phase p = ph.get(i);
            if (cur == null || !cur.name.equals(p.name)) { cur = new Step(p.name); steps.add(cur); }
            long next = i < lastSort ? ph.get(i + 1).goAt : p.endAt;
            cur.cycles += next - p.goAt; cur.goReady += p.cycles(); cur.phases++;
            for (var e : p.peak.entrySet()) cur.peak.merge(e.getKey(), e.getValue(), Math::max);
            Map<String, Long> pf = m.profByPhase.get(i);
            if (pf != null) cur.ovLoad += pf.getOrDefault("ov_load", 0L);
        }
        long total = ph.get(lastSort).endAt - ph.get(firstSort).goAt;
        long oracleCyc = 0;
        if (oracle) for (int i = firstSort; i <= lastSort; i++) if (ph.get(i).name.equals("SPLIT") && m.profByPhase.get(i) != null) oracleCyc += m.profByPhase.get(i).getOrDefault("split_get_cuts", 0L);
        String path = PATHS[path0] + (plan0 != 0 ? " (stage 2 ran" + (bt0 != 0 && path0 != 3 ? "; BT wanted)" : ")") : "");
        if (op.forceBt) T.check(path0 == 3, label + ": forced BT took the BT path (" + PATHS[path0] + ")");

        StringBuilder sb = new StringBuilder(String.format("%s [meas: SortTest]: path %s; steps (GO -> next GO):", what, path));
        long empty = 0; int emptyN = 0, gq = 0;
        for (Step s : steps) {
            gq = Math.max(gq, s.peak.getOrDefault("gatherQ", 0));
            if (SKEW_CMD.matcher(s.name).matches() && s.cycles <= 150) { empty += s.cycles; emptyN++; continue; }
            sb.append(String.format(" %s %,d%s;", s.name, s.cycles, s.ovLoad > 0 ? " (overlay load " + s.ovLoad + ")" : ""));
        }
        sb.append(String.format(" %d empty commands %,d", emptyN, empty));
        out(sb.toString());
        long sumR = 0; for (int r : o.R) sumR += r;
        out(String.format("    TOTAL %,d cycles (L1 GO -> L2 all READY%s), of which C&C turnaround %,d; ready_latency %d; "
            + "direct-path bound 4*sum(R) = %,d%s; GATHER queue peak %d of 16; stack headroom %d B",
            total - oracleCyc, oracle ? String.format("; the oracle split's own %,d cycles taken out", oracleCyc) : "; real split",
            total - steps.stream().mapToLong(s -> s.goReady).sum(), m.c.cfg.readyLatency, 4 * sumR,
            fmt == FMT_K128 ? String.format(", V 8*sum(R) = %,d", 8 * sumR) : "", gq, headroom));
        for (Step s : steps) if (s.name.matches("..\\..") && s.cycles > 150)
            out(String.format("    peaks in %s: link %d, eject %d, write-engine input %d, area %d", s.name, s.peak.get("link"), s.peak.get("eject"), s.peak.get("weIn"), s.peak.get("area")));
        out("    " + m.c.agReport().trim().replace("\n", "\n    "));
        { long z = 0, zg = 0, gs = 0; for (Tile p : m.c.tiles) { z += p.ag.totZeroGroups(); zg += p.ag.totZeroGroupsWithGap(); gs += p.ag.totGroupStall(); }
          out("    M14: zero-length groups " + z + " (summed over tiles), of which with a gap " + zg + " (1 + gap since 10 October); group-run stall cycles " + gs + " (drift)"); }
        if (System.getProperty("sort.profile") != null) out("    tile 0 cycles per function: " + m.prof);
        if (m.watch.length > 0) {
            Map<String, long[][]> byStep = new LinkedHashMap<>();
            for (int i = 0; i <= lastSort; i++) {
                long[][] w = m.watchByPhase.get(i); if (w == null) continue;
                long[][] acc = byStep.computeIfAbsent(ph.get(i).name, x -> new long[m.watch.length][64]);
                for (int f = 0; f < w.length; f++) for (int t = 0; t < 64; t++) acc[f][t] += w[f][t];
            }
            for (var e : byStep.entrySet()) {
                StringBuilder b = new StringBuilder("    watch in " + e.getKey() + " (cycles per tile: mean / max):");
                for (int f = 0; f < m.watch.length; f++) {
                    long sum = 0, mx = 0; for (long v : e.getValue()[f]) { sum += v; mx = Math.max(mx, v); }
                    if (mx > 0) b.append(String.format(" %s %,d / %,d;", m.watch[f], sum / 64, mx));
                }
                out(b.toString());
            }
        }
        if (System.getProperty("sort.phases") != null) out(m.c.phases.report());
        if (System.getProperty("sort.prof") != null) out("    sort.prof " + System.getProperty("sort.prof") + ": tile 0 cycles " + m.c.tiles[0].core.profCycles);
        Result res = new Result(PATHS[path0], plan0 != 0, bt0 != 0, total - oracleCyc, oracleCyc, steps, T.failed - failed0);
        try {
            if (op.phasesCsv != null) m.c.phases.csv(Path.of(op.phasesCsv));
            if (op.csv != null) writeCsv(Path.of(op.csv), label, fmt, n, op, m.c.cfg, res);
        } catch (IOException e) { throw new RuntimeException(e); }
        return res;
    }

    /** One run's CSV: a row per step and a TOTAL row. */
    static void writeCsv(Path f, String label, int fmt, int n, Opts op, Config k, Result r) throws IOException {
        StringBuilder sb = new StringBuilder("label,fmt,n,seed,ready_latency,params,path,stage2,bt_wanted,step,cycles,go_ready,phases,overlay_load,peak_link,peak_eject,peak_weIn,peak_gatherQ,peak_area,checks_failed\n");
        String params = op.cfg.isEmpty() ? "default" : String.join(" ", op.cfg.entrySet().stream().map(e -> e.getKey() + "=" + e.getValue()).toList());
        String head = String.join(",", label, fmt == FMT_K128 ? "k128" : "kv32", "" + n, "" + op.shuffle, "" + k.readyLatency, params, r.path(), "" + r.stage2(), "" + r.btWanted());
        for (Step s : r.steps())
            sb.append(head).append(',').append(String.join(",", s.name, "" + s.cycles, "" + s.goReady, "" + s.phases, "" + s.ovLoad,
                "" + s.peak.getOrDefault("link", 0), "" + s.peak.getOrDefault("eject", 0), "" + s.peak.getOrDefault("weIn", 0),
                "" + s.peak.getOrDefault("gatherQ", 0), "" + s.peak.getOrDefault("area", 0), "" + r.checksFailed())).append('\n');
        sb.append(head).append(",TOTAL,").append(r.total()).append(",,,,,,,,,").append(r.checksFailed()).append('\n');
        Files.writeString(f, sb.toString());
    }

    public static void main(String[] a) throws IOException {
        List<String> args = new ArrayList<>(List.of(a));
        Opts op = new Opts();
        if (!args.isEmpty() && args.get(0).matches("-?\\d+")) op.shuffle = Long.parseLong(args.remove(0));
        boolean full = args.remove("full");
        Map<String, String> kv = new LinkedHashMap<>();
        for (String s : args) if (s.contains("=")) kv.put(s.substring(0, s.indexOf('=')), s.substring(s.indexOf('=') + 1));
        for (var e : kv.entrySet()) if (e.getKey().startsWith("cfg.")) op.cfg.put(e.getKey().substring(4), e.getValue());
        if (kv.containsKey("ready")) op.readyLatency = Integer.parseInt(kv.get("ready"));
        op.csv = kv.get("csv"); op.phasesCsv = kv.get("phases"); op.forceBt = "bt".equals(kv.get("force"));
        String only = kv.get("only");
        // Every file the run reads, by SHA-256, so results from different kits cannot be mixed unnoticed
        out(Inputs.line(Inputs.of(build, args.contains("run") && kv.containsKey("keys") ? Path.of(kv.get("keys")) : null)));
        if (args.contains("p1")) {
            int n = full ? 65536 : 2000;
            Object[][] sets = { { "uniform", n, FMT_K128 }, { "uniform", n, FMT_KV32 }, { "few", full ? 65536 : 1000, FMT_K128 },
                { "ragged", full ? 32768 : 1000, FMT_KV32 }, { "sorted", full ? 4096 : 500, FMT_K128 }, { "equal", full ? 4096 : 500, FMT_KV32 } };
            for (Object[] s : sets) if (only == null || only.equals(s[0])) p1((String) s[0], (Integer) s[1], (Integer) s[2], op);
        }
        if (args.contains("sort")) {
            int n = full ? 65536 : 2000;
            Object[][] sets = { { "uniform", n, FMT_K128 }, { "uniform", n, FMT_KV32 }, { "few", full ? 65536 : 1000, FMT_K128 },
                { "ragged", full ? 32768 : 1000, FMT_KV32 }, { "sorted", n, FMT_K128 }, { "equal", n, FMT_KV32 },
                { "equal", n, FMT_K128 }, { "reverse", n, FMT_K128 }, { "reverse", n, FMT_KV32 } };
            for (Object[] x : sets) {
                String nm = x[0] + (x[2].equals(FMT_KV32) ? "-kv32" : "-k128");
                if (only == null || only.equals(nm)) sortRun(x[0] + " n=" + x[1], keys((String) x[0], (Integer) x[1], 11 + x[0].hashCode()), (Integer) x[2], false, op);
            }
            if (only == null || only.equals("shaped")) { int sn = full ? 32768 : 1000; sortRun("ragged n=" + sn, keys("ragged", sn, 11 + "ragged".hashCode()), FMT_K128, true, op); }
        }
        if (args.contains("run")) {
            int fmt = kv.getOrDefault("fmt", "k128").equals("kv32") ? FMT_KV32 : FMT_K128;
            int[][] k; String label;
            if (kv.containsKey("keys")) { k = readKeys(Path.of(kv.get("keys"))); label = kv.getOrDefault("label", Path.of(kv.get("keys")).getFileName().toString().replace(".keys", "")); }
            else {
                String kind = kv.getOrDefault("kind", "uniform"); int n = Integer.parseInt(kv.getOrDefault("n", full ? "65536" : "2000"));
                k = keys(kind, n, 11 + kind.hashCode()); label = kv.getOrDefault("label", kind + " n=" + n);
            }
            op.label = label;
            sortRun(label, k, fmt, args.contains("shaped"), op);
        }
        System.exit(T.done("SortTest" + (op.shuffle >= 0 ? " (shuffled, seed " + op.shuffle + ")" : "")));
    }
}
