import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;

/**
 * WORKER29 (P2): the busy-chip benchmark: all 64 tiles replaying at once, as a proof's sumchecks replay every tile's window
 * about 50 times. Every tile has a replayer in its slot and runs replay_ctl.c (sw/replay/build) on a window preloaded into its
 * big SRAM in Stage B's layout (StageB.java); C&C boots all 64 by broadcast, and the run ends when every tile has exited.
 * Each tile's rows are compared with the reference: Stage A's ideal controller for the synthetic window, rows.bin for a
 * delivery's. A repeat is a fresh chip on the same windows.
 *
 * <p>Usage: java ReplayBench synthetic [ROWS_PER_CODE_WORD=300] [CODE_WORDS=64] [REPEATS=1]
 * <br>(one controller-fed window, CODE_WORDS x ROWS rows, on every tile; runs anywhere)
 * <br>or java ReplayBench DELIVERY PROGRAM [--first F] [--variant V] [--repeats R]
 * <br>(tile i replays window F + i of a v1.26.1 delivery; V as StageB's, default state)
 *
 * <p>Prints, per repeat, the chip's cycles (boot to the last exit), the replay's span (first pass start to last pass end),
 * the rows, the verdict per tile and the wall time; the wall time and the microseconds per cycle are the only numbers that
 * may differ between two engines that are exact.
 */
public final class ReplayBench {
    interface Loader { long load(Tile p) throws IOException; }   // loads tile p's window; returns its rows

    public static void main(String[] a) throws Exception {
        StageB.Image im = new StageB.Image(Path.of(System.getProperty("replay.build", "sw/replay/build")));
        int repeats = 1;
        Loader[] load = new Loader[64];
        RowCheck[] check = new RowCheck[64];
        String what;
        if (a[0].equals("synthetic")) {
            int len = a.length > 1 ? Integer.parseInt(a[1]) : 300, words = a.length > 2 ? Integer.parseInt(a[2]) : 64;
            repeats = a.length > 3 ? Integer.parseInt(a[3]) : 1;
            ReplayWindow w = longWindow(len, words);
            ReplayDriver d = new ReplayDriver(longWindow(len, words), new Replayer(0)); d.load(); List<long[]> ref = d.pass(100L * len * words + 1_000_000);
            for (int i = 0; i < 64; i++) { load[i] = p -> loadSynthetic(p, w); check[i] = new RowCheck(ref); }
            what = "synthetic, " + words + " x " + len + " rows on every tile";
        } else {
            Path dir = Path.of(a[0]); String prog = a[1], variant = "state"; int first = 0;
            for (int i = 2; i < a.length; i++) switch (a[i]) {
                case "--first" -> first = Integer.parseInt(a[++i]);
                case "--variant" -> variant = a[++i];
                case "--repeats" -> repeats = Integer.parseInt(a[++i]);
                default -> throw new IllegalArgumentException(a[i]);
            }
            Path man = Files.exists(dir.resolve("manifest.json")) ? dir.resolve("manifest.json") : dir.resolve(prog + ".manifest.json");
            Map<String, Object> m = ReplayDelivery.Json.object(Files.readString(man));
            Path pd = dir.resolve(prog);
            ReplayTables tb = ReplayDelivery.tablesFor(dir, pd);
            List<Map<String, Object>> ws = new ArrayList<>();
            for (Object po : ReplayDelivery.Json.list(m.get("programs"))) {
                Map<String, Object> p = ReplayDelivery.Json.object(po);
                if (!prog.equals(p.get("name"))) continue;
                for (Object wo : ReplayDelivery.Json.list(p.get("window_entries"))) {
                    Map<String, Object> we = ReplayDelivery.Json.object(wo);
                    int index = ((Number) we.get("index")).intValue();
                    if (we.containsKey("files") && index >= first && index < first + 64) ws.add(we);
                }
            }
            if (ws.size() != 64) throw new IllegalStateException("windows " + first + ".." + (first + 63) + ": " + ws.size() + " delivered");
            ws.sort((x, y) -> Integer.compare(((Number) x.get("index")).intValue(), ((Number) y.get("index")).intValue()));
            final String v = variant;
            for (int i = 0; i < 64; i++) {
                Map<String, Object> we = ws.get(i);
                String base = String.format("window_%05d.", ((Number) we.get("index")).intValue());
                int rows = ((Number) we.get("rows")).intValue();
                load[i] = p -> loadDelivered(p, tb, pd, base, v, rows);
                check[i] = new RowCheck(ReplayDelivery.le(Files.readAllBytes(pd.resolve(base + "rows.bin"))));
            }
            what = prog + " windows " + first + ".." + (first + 63) + " (" + variant + ")";
        }
        System.out.println("ReplayBench: " + what + ", " + repeats + " repeat(s)");
        int failed = 0;
        for (int rep = 0; rep < repeats; rep++) failed += run(im, load, check, rep);
        System.out.println("ReplayBench: " + repeats + " repeat(s), " + failed + " tile replays failed");
        System.exit(failed == 0 ? 0 : 1);
    }

    /** One run: a fresh chip, every tile's window loaded, the broadcast boot, until every tile has exited. */
    static int run(StageB.Image im, Loader[] load, RowCheck[] check, int rep) throws IOException {
        Config k = Config.machine(); k.periphType = 0;
        Replayer[] rp = new Replayer[64];
        k.testPeripheral = tile -> tile < 64 ? (rp[tile] = new Replayer(tile)) : null;
        Chip c = new Chip(k);
        long rows = 0, maxRows = 0;
        for (int i = 0; i < 64; i++) {
            long n = load[i].load(c.tiles[i]); rows += n; maxRows = Math.max(maxRows, n);
            RowCheck ck = check[i]; ck.reset();
            rp[i].rowListener = ck::row;
        }
        c.cc1p.expect = -1L;                                          // all 64 tiles start (6.2)
        c.cc.loadWords(LinkWord.BROADCAST, Registers.PMEM_BASE, im.code);
        if (im.data.length > 0) c.cc.loadWords(LinkWord.BROADCAST, im.sym.get("_data_start"), im.data);
        c.cc.command(LinkWord.BROADCAST, Registers.CMD_JUMP << 24, im.sym.get("_start")).go();
        long cap = 60L * maxRows + 3_000_000, t0 = System.nanoTime();
        // the boot (until every tile's pass has started), then the replay (until every tile has exited), timed apart
        c.runUntil(() -> { for (Replayer r : rp) if (r.passes == 0) return false; return true; }, cap);
        long t1 = System.nanoTime(), c1 = c.cycle;
        c.runUntil(() -> { for (Tile p : c.tiles) if (p.core.halt == Controller.RUN || p.core.halt == 0) return false; return true; }, cap);
        double s = (System.nanoTime() - t0) / 1e9, sr = (System.nanoTime() - t1) / 1e9;
        long start = Long.MAX_VALUE, end = 0; int bad = 0; List<String> detail = new ArrayList<>();
        int res = im.sym.get("rp_results");
        for (int i = 0; i < 64; i++) {
            Tile p = c.tiles[i]; Replayer r = rp[i];
            start = Math.min(start, r.passStartAt); end = Math.max(end, r.passEndAt);
            String d = check[i].verdict();
            if (d == null && p.core.halt != Controller.HALT_EXIT) d = "did not exit: " + p.core.haltReport();
            if (d == null && p.core.peekLocal(res) != 1) d = "rp_results status " + p.core.peekLocal(res);
            if (d == null && !r.clean()) d = "replayer not clean";
            if (d != null) { bad++; if (detail.size() < 8) detail.add("tile " + i + ": " + d); }
        }
        if (c.violations > 0) detail.add("meshsim: " + StageB.cut(c.violationReport().replace("\n", "; ")));
        System.out.println(String.format("repeat %d: %s, chip %,d cycles, replay %,d cycles (first start %d, last end %d), %,d rows, %d of 64 tiles failed, violations %d; wall %.1f s; the replay from cycle %d: %.1f s, %.2f us/cycle  [meas: ReplayBench]",
                rep, bad == 0 && c.violations == 0 ? "OK" : "FAIL", c.cycle, end - start, start, end, rows, bad, c.violations, s, c1, sr, 1e6 * sr / (c.cycle - c1)));
        for (String d : detail) System.out.println("    " + d);
        return bad + (c.violations > 0 ? 1 : 0);
    }

    /** Compares a tile's rows, as they come, with the reference (a list, or rows.bin's 80-byte rows). */
    static final class RowCheck {
        final List<long[]> ref; final ByteBuffer bin; final int n;
        int got, firstBad = -1;
        RowCheck(List<long[]> ref) { this.ref = ref; this.bin = null; n = ref.size(); }
        RowCheck(ByteBuffer bin) { this.ref = null; this.bin = bin; n = bin.capacity() / 80; }
        void reset() { got = 0; firstBad = -1; }
        void row(long[] r) {
            if (firstBad < 0 && got < n) {
                boolean eq;
                if (ref != null) eq = java.util.Arrays.equals(r, ref.get(got));
                else { eq = true; for (int f = 0; f < 10; f++) if (r[f] != bin.getLong(80 * got + 8 * f)) { eq = false; break; } }
                if (!eq) firstBad = got;
            }
            got++;
        }
        String verdict() {
            if (firstBad >= 0) return "first mismatching row " + firstBad;
            if (got != n) return "rows: tile " + got + ", reference " + n;
            return null;
        }
    }

    /** StageB's controller-fed synthetic window, made long: CODE_WORDS inline words, each expanding to LEN ADDIs. */
    static ReplayWindow longWindow(int len, int words) {
        int inline = ReplayerUnitTests.r(0, 2, 1, 0, 3, ReplayerUnitTests.CUSTOM0);
        ReplayWindow w = new ReplayWindow(); w.fixedRom(); w.decodeClass(inline, 1);
        ReplayWindow.W[] t = new ReplayWindow.W[len];
        for (int k = 0; k < len; k++) t[k] = ReplayWindow.W.op(ReplayerAlu.ADDI).rd(137 + (k % 8)).rs1(0).rs2(128).cnst(k + 1).fmt(0);
        t[len - 1].reportC();
        w.template(inline, t);
        w.pc = 0x2000; w.instrIdx = 100;
        int[] code = new int[words]; java.util.Arrays.fill(code, inline);
        w.setCode(code, false); w.setLogs(List.of(), false, false);
        w.rows = words * len;
        return w;
    }

    /** As StageB.runSynthetic's loader. */
    static long loadSynthetic(Tile p, ReplayWindow w) {
        long[] tab = new long[2 * (256 + 512)];
        for (int e = 0; e < 2048; e++) tab[2 * (e >> 3) + ((e & 7) >> 2)] |= (w.table[e] & 0xFFFFL) << (16 * (e & 3));
        for (int j = 0; j < 512; j++) { tab[2 * (256 + j)] = w.romLo[j]; tab[2 * (256 + j) + 1] = w.romHi[j]; }
        StageB.put(p, StageB.TABLES, StageB.bytes(tab));
        List<Integer> ws = new ArrayList<>(w.templates.keySet()); ws.sort(Integer::compareUnsigned);
        long[] ix = new long[2 * ws.size()]; List<long[]> tw = new ArrayList<>(); int off = 0;
        for (int q = 0; q < ws.size(); q++) {
            long[] t = w.templates.get(ws.get(q)); int words = t.length / 2;
            int rd0 = w.templateRd0.getOrDefault(ws.get(q), words > 0 ? (int) ((t[0] >>> 46) & 3) : 0);
            ix[2 * q] = (ws.get(q) & 0xFFFFFFFFL) | (long) off << 32; ix[2 * q + 1] = words | (long) (rd0 | 1 << 8) << 32;
            tw.add(t); off += words;
        }
        long[] tpl = new long[2 * off]; int at = 0; for (long[] t : tw) { System.arraycopy(t, 0, tpl, at, t.length); at += t.length; }
        StageB.put(p, StageB.INDEX, StageB.bytes(ix)); StageB.put(p, StageB.TEMPLATES, StageB.bytes(tpl));
        long[] bm = new long[2];
        for (int q = 0; q < ws.size(); q++) bm[q >>> 6] |= 1L << (q & 63);
        StageB.put(p, StageB.RECORD, StageB.bytes(w.record(bm)));
        StageB.put(p, StageB.CODE, StageB.bytes(w.code)); StageB.put(p, StageB.RLOG, StageB.bytes(w.rlog)); StageB.put(p, StageB.WLOG, StageB.bytes(w.wlog));
        int[] h = new int[16];
        h[0] = 0x594C5052; h[1] = StageB.LAYOUT_VERSION; h[2] = StageB.TABLES; h[3] = StageB.INDEX; h[4] = ws.size(); h[5] = StageB.TEMPLATES; h[6] = StageB.RECORD;
        h[7] = StageB.CODE; h[8] = w.code.length / 2; h[9] = StageB.RLOG; h[10] = w.rlog.length / 2; h[11] = StageB.WLOG; h[12] = w.wlog.length / 2; h[13] = 1;
        h[14] = w.runConfig();
        for (int i = 0; i < 16; i++) p.sram.poke(StageB.HDR + i, h[i]);
        return w.rows;
    }

    /** As StageB.runWindow's loader. */
    static long loadDelivered(Tile p, ReplayTables tb, Path pd, String base, String variant, int rows) throws IOException {
        byte[] table = tb.decodeBytes, rom = tb.romBytes;
        byte[] tables = new byte[table.length + rom.length];
        System.arraycopy(table, 0, tables, 0, table.length); System.arraycopy(rom, 0, tables, table.length, rom.length);
        StageB.put(p, StageB.TABLES, tables);
        byte[] ixb = tb.indexBytes;
        StageB.put(p, StageB.INDEX, ixb);
        StageB.put(p, StageB.TEMPLATES, tb.templateBytes);
        byte[] rec = Files.readAllBytes(pd.resolve(base + variant + ".bin"));
        StageB.put(p, StageB.RECORD, rec);
        ByteBuffer rb = ReplayDelivery.le(rec);
        Replayer.StreamSize[] sz = Replayer.parseStreamWord(rb.getLong(16), rb.getLong(24));
        byte[] code = Files.readAllBytes(pd.resolve(base + ReplayDelivery.streamFile("code", sz[0].compressed())));
        byte[] rlog = Files.readAllBytes(pd.resolve(base + ReplayDelivery.streamFile("rlog", sz[1].compressed())));
        byte[] wlog = Files.readAllBytes(pd.resolve(base + ReplayDelivery.streamFile("wlog", sz[2].compressed())));
        StageB.put(p, StageB.CODE, code); StageB.put(p, StageB.RLOG, rlog); StageB.put(p, StageB.WLOG, wlog);
        int[] h = new int[16];
        h[0] = 0x594C5052; h[1] = StageB.LAYOUT_VERSION; h[2] = StageB.TABLES; h[3] = StageB.INDEX; h[4] = ixb.length / 16; h[5] = StageB.TEMPLATES; h[6] = StageB.RECORD;
        h[7] = StageB.CODE; h[8] = code.length / 16; h[9] = StageB.RLOG; h[10] = rlog.length / 16; h[11] = StageB.WLOG; h[12] = wlog.length / 16; h[13] = 1;
        h[14] = Replayer.runConfig(rows, true, true);
        for (int i = 0; i < 16; i++) p.sram.poke(StageB.HDR + i, h[i]);
        return rows;
    }
}
