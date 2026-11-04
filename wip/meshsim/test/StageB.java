import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/**
 * Stage B: the replay controller (sw/replay/replay_ctl.c) on a meshsim tile, feeding the tile's replayer through the
 * peripheral slot, against a tracer delivery (replay-inputs/1). For each window: the Java-side loader writes the window
 * into tile 0's big SRAM as sw/replay/replay_layout.h lays it out; C&C boots replay_ctl on tile 0 (the other tiles run
 * only the loader, which exits); the program loads the tables and part 0, runs the pass with the 4.19.9 credit loop
 * and returns through the exit word (v1.2). The rows the replayer emits are compared byte for byte with rows.bin, as Stage A does
 * (ReplayDelivery), and so are the streams' consumption and the end state.
 *
 * The figure that matters is cycles per row: the pass (the replayer's run to its end), against Stage A's ideal
 * controller (ReplayDriver: every line answered in the cycle it is seen, the port at one packet a cycle).
 *
 * Usage: java StageB <delivery dir> <variant: state | state-lru | state-pick> <program> [window ...]
 * (no windows: every delivered window of the program), or java StageB synthetic: Stage A's synthetic windows
 * (ReplayerUnitTests: the program window in every compression and store-log combination, a controller-fed expansion,
 * and its mid-sequence start), each run on the tile and compared row for row with Stage A's ideal controller. Requires
 * sw/replay/build (sw/replay/build.sh).
 */
public final class StageB {
    static final int HDR = 0x00000, TABLES = 0x00040, INDEX = 0x00C40, RECORD = 0x00E40, TEMPLATES = 0x58000,   // v1.25: 1.4 MB for the universal set
            CODE = 0x08000, RLOG = 0x18000, WLOG = 0x38000, LAYOUT_VERSION = 3;   // v1.26: the 68-word record

    /**
     * Simulate tile 0 only (Chip.parked; -Dstageb.lone, or ReplayShard's stage B): in a Stage B replay the other 63 tiles
     * only wait in their boot ROMs and nothing reaches them, which the run checks at its end (Chip.parkedUntouched).
     * The same windows give the same rows, cycles and stalls as the full chip (ReplayShard's stage F compares).
     */
    static boolean LONE = Boolean.getBoolean("stageb.lone");

    public static void main(String[] args) throws Exception {
        if (args[0].equals("synthetic")) { synthetic(); return; }
        Path dir = Path.of(args[0]);
        String variant = args[1], prog = args[2];
        List<Integer> only = new ArrayList<>();
        for (int i = 3; i < args.length; i++) only.add(Integer.parseInt(args[i]));
        Map<String, Object> m = ReplayDelivery.Json.object(Files.readString(dir.resolve("manifest.json")));
        Path pd = dir.resolve(prog);
        Image im = new Image(Path.of("sw/replay/build"));
        int windows = 0, failed = 0;
        for (Object po : ReplayDelivery.Json.list(m.get("programs"))) {
            Map<String, Object> p = ReplayDelivery.Json.object(po);
            if (!prog.equals(p.get("name"))) continue;
            for (Object wo : ReplayDelivery.Json.list(p.get("window_entries"))) {
                Map<String, Object> we = ReplayDelivery.Json.object(wo);
                if (!we.containsKey("files")) continue;
                int index = ((Number) we.get("index")).intValue();
                if (!only.isEmpty() && !only.contains(index)) continue;
                windows++;
                if (!runWindow(im, ReplayDelivery.tablesFor(dir, pd), pd, we, variant, prog).ok) failed++;
            }
        }
        System.out.println("StageB " + prog + " " + variant + ": " + windows + " windows, " + failed + " failed");
        System.exit(failed == 0 && windows > 0 ? 0 : 1);
    }

    static final class Image {
        final int[] code, data; final Map<String, Integer> sym = new HashMap<>();
        Image(Path b) throws IOException {
            code = words(b.resolve("replay_ctl.code.bin")); data = words(b.resolve("replay_ctl.data.bin"));
            for (String l : Files.readAllLines(b.resolve("replay_ctl.sym"))) { String[] f = l.trim().split("\\s+"); if (f.length == 3) sym.put(f[2], (int) Long.parseLong(f[0], 16)); }
        }
        static int[] words(Path p) throws IOException {
            byte[] b = Files.readAllBytes(p); ByteBuffer bb = ReplayDelivery.le(b);
            int[] w = new int[(b.length + 3) / 4]; for (int i = 0; i < b.length / 4; i++) w[i] = bb.getInt(4 * i); return w;
        }
    }

    /** Put bytes into tile p's big SRAM from chunk c, one 32-bit word per chunk; returns the chunks used, rounded up to 4. */
    static int put(Tile p, int c, byte[] b) {
        ByteBuffer bb = ReplayDelivery.le(java.util.Arrays.copyOf(b, (b.length + 15) & ~15));
        int n = bb.capacity() / 4;
        for (int i = 0; i < n; i++) p.sram.poke(c + i, bb.getInt(4 * i));
        return n;
    }

    /** One delivered window on the tile; prints its line and returns the result (ok, rows, cycles, the details). */
    static ReplayDelivery.Result runWindow(Image im, ReplayTables tb, Path pd, Map<String, Object> we, String variant, String prog) throws IOException {
        int index = ((Number) we.get("index")).intValue(), rows = ((Number) we.get("rows")).intValue();
        String base = String.format("window_%05d.", index);
        Config k = Config.machine(); k.periphType = 0;
        Replayer[] rp = new Replayer[1];
        k.testPeripheral = tile -> tile == 0 ? (rp[0] = new Replayer(0, rows)) : null;
        Chip c = new Chip(k);
        if (LONE) { c.parked = new boolean[64]; java.util.Arrays.fill(c.parked, 1, 64, true); }
        Tile p = c.tiles[0];
        Replayer r = rp[0];
        List<long[]> got = new ArrayList<>();
        Watch wt = new Watch(r);
        r.rowListener = row -> { got.add(row); wt.row(c.cycle); };

        // ---- the loader: the window into tile 0's big SRAM (replay_layout.h)
        byte[] table = tb.decodeBytes, rom = tb.romBytes;
        byte[] tables = new byte[table.length + rom.length];
        System.arraycopy(table, 0, tables, 0, table.length); System.arraycopy(rom, 0, tables, table.length, rom.length);
        put(p, TABLES, tables);
        byte[] ixb = tb.indexBytes;
        put(p, INDEX, ixb);
        put(p, TEMPLATES, tb.templateBytes);
        byte[] rec = Files.readAllBytes(pd.resolve(base + variant + ".bin"));
        put(p, RECORD, rec);
        ByteBuffer rb = ReplayDelivery.le(rec);
        ReplayDelivery.checkRecord(rb, base + variant + ".bin");
        Replayer.StreamSize[] sz = Replayer.parseStreamWord(rb.getLong(16), rb.getLong(24));   // v1.26: word 1
        boolean cc = sz[0].compressed(), rc = sz[1].compressed(), wc = sz[2].compressed();
        byte[] code = Files.readAllBytes(pd.resolve(base + ReplayDelivery.streamFile("code", cc)));
        byte[] rlog = Files.readAllBytes(pd.resolve(base + ReplayDelivery.streamFile("rlog", rc)));
        byte[] wlog = Files.readAllBytes(pd.resolve(base + ReplayDelivery.streamFile("wlog", wc)));
        put(p, CODE, code); put(p, RLOG, rlog); put(p, WLOG, wlog);
        int[] h = new int[16];
        h[0] = 0x594C5052; h[1] = LAYOUT_VERSION; h[2] = TABLES; h[3] = INDEX; h[4] = ixb.length / 16; h[5] = TEMPLATES; h[6] = RECORD;
        h[7] = CODE; h[8] = code.length / 16; h[9] = RLOG; h[10] = rlog.length / 16; h[11] = WLOG; h[12] = wlog.length / 16; h[13] = 1;
        for (int i = 0; i < 16; i++) p.sram.poke(HDR + i, h[i]);

        // ---- boot: the loader everywhere; replay_ctl and its boot marker on tile 0 only (the others' loaders ebreak)
        c.cc1p.expect = 1;                                      // only tile 0 starts (6.2): the others wait in their ROMs
        c.cc.loadWords(0, Registers.PMEM_BASE, im.code);
        if (im.data.length > 0) c.cc.loadWords(0, im.sym.get("_data_start"), im.data);
        c.cc.command(0, Registers.CMD_JUMP << 24, im.sym.get("_start"))
            .go();                                                   // the ROM jumps at the first GO after the command landed (6.2)
        // -Dstageb.profile: the controller's pc during the replayer's new-expansion stalls, by function
        final boolean prof = System.getProperty("stageb.profile") != null;
        final Map<Integer, Long> pcs = new HashMap<>(); final long[] last = { 0 };
        boolean ok = c.runUntil(() -> {
            if (prof) { long s2 = r.stalls[2]; if (s2 != last[0]) { pcs.merge(p.core.inWfi ? -1 : p.core.pc, 1L, Long::sum); last[0] = s2; } }
            return p.core.halt != Controller.RUN && p.core.halt != 0 || c.cycle > 40L * rows + 2_000_000 || wt.starved(c.cycle);
        }, 60L * rows + 3_000_000);
        if (prof) {
            Map<String, Long> byFn = new java.util.TreeMap<>();
            for (var e : pcs.entrySet()) {
                String fn = "WFI"; if (e.getKey() >= 0) { int best = -1; for (var sy : im.sym.entrySet()) if (sy.getValue() <= e.getKey() && sy.getValue() > best && LinkWord.region(sy.getValue()) == LinkWord.R_PMEM) { best = sy.getValue(); fn = sy.getKey(); } }
                byFn.merge(fn, e.getValue(), Long::sum);
            }
            System.out.println("  new-expansion stall cycles by the controller's pc: " + byFn);
            pcs.entrySet().stream().sorted((x, y) -> Long.compare(y.getValue(), x.getValue())).limit(40)
               .forEach(e -> System.out.println(String.format("    pc 0x%x %d", e.getKey(), e.getValue())));
        }
        ok = p.core.halt == Controller.HALT_EXIT;
        int res = im.sym.get("rp_results");
        int status = p.core.peekLocal(res), passes = p.core.peekLocal(res + 4), cond = p.core.peekLocal(res + 8),
            newexp = p.core.peekLocal(res + 12), refills = p.core.peekLocal(res + 16), missing = p.core.peekLocal(res + 20),
            slow = im.sym.containsKey("rp_results") ? p.core.peekLocal(res + 24) : 0;

        // ---- the rows, the streams, the end state (as Stage A)
        List<String> detail = new ArrayList<>();
        ByteBuffer ref = ReplayDelivery.le(Files.readAllBytes(pd.resolve(base + "rows.bin")));
        int nref = ref.capacity() / 80, same = Math.min(nref, got.size());
        if (got.size() != nref) detail.add("row count: tile " + got.size() + ", reference " + nref);
        for (int i = 0; i < same; i++) {
            long[] g = got.get(i); boolean eq = true;
            for (int f = 0; f < 10; f++) if (g[f] != ref.getLong(80 * i + 8 * f)) { eq = false; break; }
            if (!eq) { detail.add("first mismatching row " + i); break; }
        }
        String sc = ReplayDelivery.consumedExactly(r, sz, code.length / 16, rlog.length / 16, wlog.length / 16);
        if (sc != null) detail.add(sc);
        Path endp = pd.resolve(base + "end.bin");
        if (Files.exists(endp)) {
            ByteBuffer end = ReplayDelivery.le(Files.readAllBytes(endp));
            ReplayDelivery.checkRecord(end, base + "end.bin");
            long epc = end.getLong(0), eHi = end.getLong(8);
            if (r.pc() != epc || (r.instrIdx() & 0xFFFFFFFFL) != (eHi & 0xFFFFFFFFL)) detail.add("end state differs");
            for (int i = 1; i < 128; i++) if (r.reg(i) != end.getLong(32 + 8 * i)) { detail.add("end state: register " + i); break; }
        }
        if (!r.clean())
            detail.add("replayer: violations " + head(r.violations) + ", invalid " + head(r.invalids) + ", overruns " + head(r.overruns) + ", bad writes " + head(r.badWrites)
                    + ", underflows " + head(r.underflows) + ", leftover " + head(r.leftovers));
        if (c.violations > 0) detail.add("meshsim: " + cut(c.violationReport().replace("\n", "; ")));
        if (wt.stuck) detail.add(wt.report(got.size(), p));
        else if (!ok || p.core.halt == Controller.RUN) detail.add(wt.stopReport(c.cycle, 40L * rows + 2_000_000, got.size(), p));
        if (!c.parkedUntouched()) detail.add("tile 0 only (-Dstageb.lone): something reached a parked tile; rerun on the full chip");
        if (status != 1) detail.add("rp_results: status " + status + " conditions 0x" + Integer.toHexString(cond) + " missing 0x" + Integer.toHexString(missing));
        boolean pass = detail.isEmpty();
        long cyc = (r.passEndAt >= r.passStartAt ? r.passEndAt : c.cycle) - r.passStartAt;   // a starved pass: to now
        System.out.println(String.format("%-12s window %5d  %-10s %s  %6d rows  pass %8d cycles  %.3f cycles/row  newexp %d (served %d, slow path %d), refills %d, stalls %s  [meas: StageB]",
                prog, index, variant, pass ? "OK  " : "FAIL", same, cyc, (double) cyc / Math.max(1, same), r.newExpansions, newexp, slow, refills,
                java.util.Arrays.toString(r.stalls)));
        for (String d : detail) System.out.println("    " + d);
        ReplayDelivery.Result out = new ReplayDelivery.Result();
        out.ok = pass; out.rows = same; out.cycles = cyc; out.index = index; out.detail.addAll(detail);
        out.summary = String.format((LONE ? "tile 0 only, " : "full chip, ") + "max row gap " + wt.maxGap + ", %.3f cycles/row, newexp %d (slow %d), refills %d", (double) cyc / Math.max(1, same), r.newExpansions, slow, refills);
        return out;
    }

    // ================================================================ CL4: a window that stops making rows fails at once
    /**
     * A running pass that delivers no row for this many cycles has starved (a controller feeding the wrong queue port,
     * say), and the window fails at once instead of running to the overall cycle cap (CL4; as WORKER15's v1.2.1,
     * 090fc40). The largest normal gap measured (REPORT.md section 8) is far below it; -Dstageb.norow=N (run_shard.sh
     * -g N) sets it.
     */
    /** A long list or report, shortened for a window's detail line (a starved window can collect hundreds). */
    static String head(List<String> l) { return l.size() <= 3 ? l.toString() : l.subList(0, 3) + " ... (" + l.size() + " in all)"; }
    static String cut(String s) { return s.length() <= 600 ? s : s.substring(0, 600) + " ... (" + s.length() + " chars)"; }
    static long NO_ROW_LIMIT = Long.getLong("stageb.norow", 50_000);
    static List<String> lastDetail = List.of();

    /** A window's rows as they come: the largest gap between rows (from the pass's start), and starvation. */
    static final class Watch {
        final Replayer r; long lastRow = -1, maxGap; boolean stuck;
        Watch(Replayer r) { this.r = r; }
        void row(long now) { maxGap = Math.max(maxGap, now - (lastRow < r.passStartAt ? r.passStartAt : lastRow)); lastRow = now; }
        boolean starved(long now) {
            if (r.isRunning() && now - Math.max(r.passStartAt, lastRow) > NO_ROW_LIMIT) stuck = true;
            return stuck;
        }
        String report(int rows, Tile p) {
            return "starved: no row for " + NO_ROW_LIMIT + " cycles while a pass ran (pass started at cycle " + r.passStartAt
                + ", last row at " + (lastRow < 0 ? "none" : Long.toString(lastRow)) + ", " + rows + " rows so far): is the"
                + " controller feeding the right queue ports (4.19.10, CL1)? queue occupancy: code " + r.code.occ + ", read log "
                + r.rlog.occ + ", write log " + r.wlog.occ + ", expansion " + r.exp.occ + "; bad writes " + r.badWrites.size()
                + "; controller " + (p.core.inWfi ? "in WFI" : "at pc 0x" + Integer.toHexString(p.core.pc));
        }
        String stopReport(long now, long cap, int rows, Tile p) {
            String where = "controller " + p.core.halt + (p.core.inWfi ? " in WFI" : " at pc 0x" + Integer.toHexString(p.core.pc));
            if (r.passes == 0) return "no pass started by cycle " + now + " (the cap is " + cap + "): " + where;
            if (now > cap) return "cycle cap " + cap + " reached with " + rows + " rows: " + where;
            return "tile 0 did not reach its exit: " + where;
        }
    }

    // ================================================================ synthetic windows (no tracer delivery needed)
    static byte[] bytes(long[] pairs) {
        ByteBuffer b = ReplayDelivery.le(new byte[8 * pairs.length]);
        for (int i = 0; i < pairs.length; i++) b.putLong(8 * i, pairs[i]);
        return b.array();
    }
    static void synthetic() throws Exception {
        Image im = new Image(Path.of("sw/replay/build"));
        int failed = 0, n = 0;
        for (int v = 0; v < 16; v++) {
            ReplayWindow w = ReplayerUnitTests.programWindow((v & 1) != 0, (v & 2) != 0, (v & 4) != 0, (v & 8) != 0);
            w.rows = 25; n++;
            if (!runSynthetic(im, w, "program " + v)) failed++;
        }
        for (int mid = 0; mid < 2; mid++) {
            int inline = ReplayerUnitTests.r(0, 2, 1, 0, 3, ReplayerUnitTests.CUSTOM0), len = 300;
            ReplayWindow w = new ReplayWindow(); w.fixedRom(); w.decodeClass(inline, 1);
            ReplayWindow.W[] words = new ReplayWindow.W[len];
            for (int k = 0; k < len; k++) words[k] = ReplayWindow.W.op(ReplayerAlu.ADDI).rd(137 + (k % 8)).rs1(0).rs2(128).cnst(k + 1).fmt(0);
            words[len - 1].reportC();
            w.template(inline, words);
            w.pc = 0x2000; w.instrIdx = 100; w.nextOffset = mid == 1 ? 250 : 0;
            w.setCode(new int[] { inline, inline }, false); w.setLogs(List.of(), false, false);
            w.rows = mid == 1 ? 50 + len : 2 * len; n++;
            if (!runSynthetic(im, w, mid == 1 ? "controller-fed, mid-sequence" : "controller-fed")) failed++;
        }
        // CL4: the controller on the queue map before CL1 must fail the window, starved, not hang
        {
            int inline = ReplayerUnitTests.r(0, 2, 1, 0, 3, ReplayerUnitTests.CUSTOM0), len = 300;
            ReplayWindow w = new ReplayWindow(); w.fixedRom(); w.decodeClass(inline, 1);
            ReplayWindow.W[] words = new ReplayWindow.W[len];
            for (int k = 0; k < len; k++) words[k] = ReplayWindow.W.op(ReplayerAlu.ADDI).rd(137 + (k % 8)).rs1(0).rs2(128).cnst(k + 1).fmt(0);
            words[len - 1].reportC();
            w.template(inline, words);
            w.pc = 0x2000; w.instrIdx = 100;
            w.setCode(new int[] { inline, inline }, false); w.setLogs(List.of(), false, false);
            w.rows = 2 * len; n++;
            long t0 = System.nanoTime();
            boolean passed = runSynthetic(new Image(Path.of("sw/replay/build/oldmap")), w, "old queue map (CL4: must fail)");
            double s = (System.nanoTime() - t0) / 1e9;
            boolean ok = !passed && lastDetail.stream().anyMatch(x -> x.startsWith("starved"));
            System.out.printf("synthetic %-30s %s the window failed %s in %.1f s%n", "old queue map (CL4)", ok ? "OK  " : "FAIL",
                    ok ? "as starved" : "but not as starved: " + lastDetail, s);
            if (!ok) failed++;
        }
        System.out.println("StageB synthetic: " + n + " windows, " + failed + " failed");
        System.exit(failed == 0 ? 0 : 1);
    }

    /** One synthetic window: Stage A's rows (ReplayDriver), then the tile's (replay_ctl.c) from the same window. */
    static boolean runSynthetic(Image im, ReplayWindow w, String name) throws IOException {
        ReplayDriver d = new ReplayDriver(w, new Replayer(0, w.rows)); d.load(); List<long[]> ref = d.pass(400_000);
        Config k = Config.machine(); k.periphType = 0;
        Replayer[] rp = new Replayer[1];
        k.testPeripheral = tile -> tile == 0 ? (rp[0] = new Replayer(0, w.rows)) : null;
        Chip c = new Chip(k);
        Tile p = c.tiles[0];
        Replayer r = rp[0];
        List<long[]> got = new ArrayList<>();
        Watch wt = new Watch(r);
        r.rowListener = row -> { got.add(row); wt.row(c.cycle); };
        // the tables: the decode table (256 x 128 bits, entry e at word e >> 3, bits 16 (e & 7)), then the ROM (512 words)
        long[] tab = new long[2 * (256 + 512)];
        for (int e = 0; e < 2048; e++) tab[2 * (e >> 3) + ((e & 7) >> 2)] |= (w.table[e] & 0xFFFFL) << (16 * (e & 3));
        for (int j = 0; j < 512; j++) { tab[2 * (256 + j)] = w.romLo[j]; tab[2 * (256 + j) + 1] = w.romHi[j]; }
        put(p, TABLES, bytes(tab));
        // the template index (16 B each: key, offset in 128-bit words, length in words, rd0 | class << 8), sorted by key
        List<Integer> ws = new ArrayList<>(w.templates.keySet()); ws.sort(Integer::compareUnsigned);
        long[] ix = new long[2 * ws.size()]; List<long[]> tw = new ArrayList<>(); int off = 0;
        for (int q = 0; q < ws.size(); q++) {
            long[] t = w.templates.get(ws.get(q)); int words = t.length / 2;
            int rd0 = w.templateRd0.getOrDefault(ws.get(q), words > 0 ? (int) ((t[0] >>> 46) & 3) : 0);
            ix[2 * q] = (ws.get(q) & 0xFFFFFFFFL) | (long) off << 32; ix[2 * q + 1] = words | (long) (rd0 | 1 << 8) << 32;
            tw.add(t); off += words;
        }
        long[] tpl = new long[2 * off]; int at = 0; for (long[] t : tw) { System.arraycopy(t, 0, tpl, at, t.length); at += t.length; }
        put(p, INDEX, bytes(ix)); put(p, TEMPLATES, bytes(tpl));
        // the record (v1.26): the state word, the stream word, the 64-word image, the files word, the bitmap (every template)
        long[] bm = new long[2];
        for (int q = 0; q < ws.size(); q++) bm[q >>> 6] |= 1L << (q & 63);
        put(p, RECORD, bytes(w.record(bm)));
        put(p, CODE, bytes(w.code)); put(p, RLOG, bytes(w.rlog)); put(p, WLOG, bytes(w.wlog));
        int[] h = new int[16];
        h[0] = 0x594C5052; h[1] = LAYOUT_VERSION; h[2] = TABLES; h[3] = INDEX; h[4] = ws.size(); h[5] = TEMPLATES; h[6] = RECORD;
        h[7] = CODE; h[8] = w.code.length / 2; h[9] = RLOG; h[10] = w.rlog.length / 2; h[11] = WLOG; h[12] = w.wlog.length / 2; h[13] = 1;
        for (int i = 0; i < 16; i++) p.sram.poke(HDR + i, h[i]);
        c.cc1p.expect = 1;                                      // only tile 0 starts (6.2): the others wait in their ROMs
        c.cc.loadWords(0, Registers.PMEM_BASE, im.code);
        if (im.data.length > 0) c.cc.loadWords(0, im.sym.get("_data_start"), im.data);
        c.cc.command(0, Registers.CMD_JUMP << 24, im.sym.get("_start")).go();
        c.runUntil(() -> p.core.halt != Controller.RUN && p.core.halt != 0 || wt.starved(c.cycle), 2_000_000);
        int res = im.sym.get("rp_results"), status = p.core.peekLocal(res);
        List<String> detail = lastDetail = new ArrayList<>();
        if (wt.stuck) detail.add(wt.report(got.size(), p));
        else if (p.core.halt != Controller.HALT_EXIT) detail.add("tile 0 did not reach its exit: " + p.core.haltReport());
        if (status != 1) detail.add("rp_results status " + status + " missing 0x" + Integer.toHexString(p.core.peekLocal(res + 20)));
        if (got.size() != ref.size()) detail.add("rows: tile " + got.size() + ", Stage A " + ref.size());
        for (int i = 0; i < Math.min(got.size(), ref.size()); i++) if (!java.util.Arrays.equals(got.get(i), ref.get(i))) { detail.add("first mismatching row " + i); break; }
        if (!r.clean())
            detail.add("replayer: violations " + head(r.violations) + ", overruns " + head(r.overruns) + ", bad writes " + head(r.badWrites) + ", invalid " + head(r.invalids)
                    + ", underflows " + head(r.underflows) + ", leftover " + head(r.leftovers));
        if (c.violations > 0) detail.add("meshsim: " + cut(c.violationReport().replace("\n", "; ")));
        boolean pass = detail.isEmpty();
        long cyc = (r.passEndAt >= r.passStartAt ? r.passEndAt : c.cycle) - r.passStartAt;   // a starved pass: to now
        System.out.println(String.format("synthetic %-30s %s %5d rows  pass %7d cycles  %.3f cycles/row (Stage A %.3f)  newexp %d, refills %d  [meas: StageB synthetic]",
                name, pass ? "OK  " : "FAIL", got.size(), cyc, (double) cyc / Math.max(1, got.size()),
                (double) (d.r.passEndAt - d.r.passStartAt) / Math.max(1, ref.size()), r.newExpansions, p.core.peekLocal(res + 16)));
        for (String x : detail) System.out.println("    " + x);
        return pass;
    }
}
