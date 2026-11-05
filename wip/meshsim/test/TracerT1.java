import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.TreeSet;

/**
 * T1 (WORKER22, B1; PLAN.md section 5): the Tracer model, its memory system and the ideal controller on a real run's
 * segments, in order on one memory (owner, 2026-11-05), checked window by window against WORKER9's delivery and by
 * replaying the Tracer's own streams through the replayer model (Stage A) against the delivery's rows.bin.
 *
 * <p><b>Bring-up inputs</b> (until WORKER26's land; no timing taken from them, owner, 2026-11-05): the initial image is
 * gold's cycle-0 memory (wip/gold's loader: the ELF's sections and the inputs), in 4 KB pages, the all-zero ones left as
 * holes; the jump map holds every instruction start (gold's bytecode), hashed by 4.21.4's rule at most half full; each
 * segment's written pages come from its own update list (frames 4 or 5 of gold's segment file); frame 2 gives the state
 * (converted from the flat 131 × u64), frame 3 the advice; log₂ K and lowest_address are jolt-cpp's (capture_writes'
 * JSON), standing in for frame 7.
 *
 * <p>Per segment: ownership set (FileStore.setOwnership), the shared cache cold, the record loaded word 0 first, 64 windows
 * of RUN_CONFIG rows. Per window with a delivered counterpart (window 64·s + j): the start state (pc, instr_idx, next
 * offset, registers) against state.bin; with compression off, each raw stream's values against the delivery's raw
 * stream; the bitmap against the manifest's inline templates; then Stage A on the Tracer's record and streams, every row
 * against rows.bin and every stream consumed exactly. A window past the delivery (padding) must be all padding. At the
 * segment's end: the dirty lines written back (meshsim only), the state against the next segment's frame 2, and every
 * cell of the segment's update list against the memory.
 *
 * <p>Usage: java -cp build:gold TracerT1 DELIVERY_DIR NAME (--w26 DIR | --gold PROGRAM INPUT SEGFILE LOG2K LOWEST)
 *     [--segments a-b] [--compress] [--no-stage-a] [--map-cache-separate] [--csv FILE] [--max-windows N]
 * <p>With --w26, WORKER26's outputs (NOTES.md: proof.json, initial.pages, frame6.bin and map.lines, seg/NNN.pages,
 * segments.seg) are the inputs, and timing is meaningful; --map-cache-separate gives the map's probes a shared cache of
 * their own, to measure the map lines' effect on the data.
 */
public final class TracerT1 {
    public static void main(String[] a) throws Exception {
        Path dl = Path.of(a[0]); String name = a[1];
        Path w26 = null, prog = null, input = null, segFile = null, csv = null; int log2K = 0; long lowest = 0;
        int s0 = 0, s1 = 127, maxWin = Integer.MAX_VALUE; boolean compress = false, stageA = true, mapSeparate = false, quiet = false, diag = false;
        for (int i = 2; i < a.length; i++) {
            switch (a[i]) {
                case "--w26" -> w26 = Path.of(a[++i]);
                case "--gold" -> { prog = Path.of(a[++i]); input = Path.of(a[++i]); segFile = Path.of(a[++i]); log2K = Integer.parseInt(a[++i]); lowest = Long.decode(a[++i]); }
                case "--segments" -> { String[] r = a[++i].split("-"); s0 = Integer.parseInt(r[0]); s1 = Integer.parseInt(r[r.length - 1]); }
                case "--compress" -> compress = true;
                case "--no-stage-a" -> stageA = false;
                case "--map-cache-separate" -> mapSeparate = true;
                case "--csv" -> csv = Path.of(a[++i]);
                case "--max-windows" -> maxWin = Integer.parseInt(a[++i]);
                case "--quiet" -> quiet = true;
                case "--diag" -> diag = true;
                default -> throw new IllegalArgumentException(a[i]);
            }
        }
        long t0 = System.nanoTime();
        TracerRig g = new TracerRig();
        FileStore.File img;
        List<List<Long>> w26Written = null;
        if (w26 != null) {
            // ---- WORKER26's inputs (NOTES.md): proof.json, initial.pages, frame6.bin, seg/NNN.pages, segments.seg
            Map<String, Object> pj = ReplayDelivery.Json.object(Files.readString(w26.resolve("proof.json")));
            log2K = ((Number) pj.get("log2_k")).intValue(); lowest = Long.decode((String) pj.get("lowest_address"));
            segFile = w26.resolve("segments.seg");
            g.lowest = lowest; g.log2K = log2K;
            img = g.fs.create(2);
            ByteBuffer ip = le(Files.readAllBytes(w26.resolve("initial.pages")));
            int n = 0;
            while (ip.remaining() > 0) {
                long off = ip.getLong(); int flags = ip.getInt(); ip.getInt();
                if (flags != 0) throw new IllegalStateException("initial.pages: flags " + flags);
                long[] pg = new long[512]; for (int k = 0; k < 512; k++) pg[k] = ip.getLong();
                g.fs.putPage(img, off / 4096, pg); n++;
            }
            ByteBuffer f6 = le(Files.readAllBytes(w26.resolve("frame6.bin")));
            int len = f6.getInt(), type = f6.getInt(), k = f6.getInt(), p1 = f6.getInt(), p3 = f6.getInt();
            if (type != 6 || len != 5 + 32 * (1 << k)) throw new IllegalStateException("frame6.bin: type " + type + " length " + len);
            byte[] lines = new byte[128 << k]; f6.get(lines);
            if (!java.util.Arrays.equals(lines, Files.readAllBytes(w26.resolve("map.lines")))) throw new IllegalStateException("map.lines differs from frame 6's lines");
            if (k != ((Number) pj.get("hash_k")).intValue() || p1 != Long.decode((String) pj.get("hash_p1")).intValue() || p3 != ((Number) pj.get("hash_p3")).intValue())
                throw new IllegalStateException("frame 6's constants differ from proof.json's");
            g.mapK = k; g.mapMul = p1; g.mapStep = p3;
            g.map = g.fs.create(FileStore.depthFor((long) lines.length));
            ByteBuffer lb = le(lines);
            for (int pgN = 0; pgN < Math.max(1, lines.length / 4096); pgN++) {
                long[] pg = new long[512];
                for (int q = 0; q < 512 && 8 * (512 * pgN + q) < lines.length; q++) pg[q] = lb.getLong(8 * (512 * pgN + q));
                g.fs.putPage(g.map, g.map.depth() == 0 ? 0 : pgN, pg);
            }
            w26Written = new ArrayList<>();
            int segs = ((Number) pj.get("segments")).intValue();
            for (int sgi = 0; sgi < segs; sgi++) {
                ByteBuffer wb = le(Files.readAllBytes(w26.resolve(String.format("seg/%03d.pages", sgi))));
                List<Long> l = new ArrayList<>(); while (wb.remaining() > 0) l.add(wb.getLong() / 4096); w26Written.add(l);
            }
            System.out.printf("%s: WORKER26's inputs: image %d pages, map 2^%d lines (p1 0x%x, p3 %d), log2 K %d, lowest 0x%x%n", name, n, k, p1, p3, log2K, lowest);
        } else {
            // ---- bring-up inputs: gold's cycle-0 image and an every-start map (no timing from these)
            Object program = Class.forName("gold.Program").getConstructor(Path.class, Path.class).newInstance(prog, input);
            Object memory = program.getClass().getField("memory").get(program);
            java.lang.reflect.Field pf = memory.getClass().getDeclaredField("pages"); pf.setAccessible(true);
            long[][] gpages = (long[][]) pf.get(memory);
            Object layout = program.getClass().getField("layout").get(program);
            long goldLowest = (long) layout.getClass().getMethod("lowestAddress").invoke(layout);
            if (goldLowest != lowest) throw new IllegalStateException(String.format("lowest_address: gold 0x%x, given 0x%x", goldLowest, lowest));
            Object bc = program.getClass().getField("bytecode").get(program);
            long codeStart = bc.getClass().getField("codeStart").getLong(bc), codeEnd = bc.getClass().getField("codeEnd").getLong(bc);
            java.lang.reflect.Method indexOf = bc.getClass().getMethod("indexOf", long.class);
            g.lowest = lowest; g.log2K = log2K;
            img = g.fs.create(2);
            int imgPages = 0;
            for (int p = 0; p < gpages.length; p++) {
                if (gpages[p] == null) continue;
                for (int q = 0; q < 4; q++) {
                    long addr = ((long) p << 14) + 4096L * q;
                    if (addr < lowest) continue;
                    long[] pg = new long[512]; boolean any = false;
                    for (int k = 0; k < 512; k++) { pg[k] = gpages[p][512 * q + k]; any |= pg[k] != 0; }
                    if (any) { g.fs.putPage(img, (addr - lowest) / 4096, pg); imgPages++; }
                }
            }
            Map<Long, Long> targets = new java.util.HashMap<>();
            for (long pc = codeStart; pc < codeEnd; pc += 2) { int ix = (int) indexOf.invoke(bc, pc); if (ix != 0) targets.put(pc, (long) ix); }
            g.mapK = Math.max(5, 64 - Long.numberOfLeadingZeros(Math.max(1, targets.size() / 8 - 1)));
            g.mapMul = 0x9E3779B1; g.mapStep = 1000003;
            while (true) { try { g.buildMap(targets); break; } catch (IllegalStateException e) { g.mapK++; } }
            System.out.printf("%s: bring-up inputs: image %d pages, map %d targets (every start) in 2^%d lines%n", name, imgPages, targets.size(), g.mapK);
        }
        g.image = img; g.copy = img;
        g.ms.mapRootForStats = g.map.root();
        MemorySystem mapMs = null;
        if (mapSeparate) { mapMs = new MemorySystem(g.fs); mapMs.mapRootForStats = g.map.root(); g.tr.mapMem = mapMs; }
        g.loadTables();
        java.io.PrintWriter out = csv != null ? new java.io.PrintWriter(Files.newBufferedWriter(csv)) : null;
        if (out != null) out.println("segment,rows,padding_rows,cycles,cycles_real_rows,cycles_per_real_row,ifetch,dmiss,bus,probe,advice,newexp,exp,"
                + "imiss,dmiss_lines,writebacks,lru_hits,lru_misses,probe_reads,mem_index_acc,mem_index_hit,mem_data_acc,mem_data_hit,map_index_acc,map_index_hit,"
                + "map_data_acc,map_data_hit,walk_cycles,data_cycles,lpddr_reads,early_evictions,read_waits,read_wait_cycles,map_sep_lpddr_reads,ok");
        // ---- the segment file
        List<Seg> segs = readSegments(segFile, lowest);
        // ---- the delivery
        Map<String, Object> man = ReplayDelivery.Json.object(Files.readString(dl.resolve(name + ".manifest.json")));
        List<Object> wins = ReplayDelivery.Json.list(ReplayDelivery.Json.object(ReplayDelivery.Json.list(man.get("programs")).get(0)).get("window_entries"));
        Path pd = dl.resolve(name);
        Map<Integer, Map<String, Object>> byIndex = new java.util.HashMap<>();
        for (Object x : wins) { Map<String, Object> we = ReplayDelivery.Json.object(x); if (we.containsKey("files")) byIndex.put(((Number) we.get("index")).intValue(), we); }
        int lastDelivered = wins.isEmpty() ? -1 : ((Number) ReplayDelivery.Json.object(wins.get(wins.size() - 1)).get("index")).intValue();
        ReplayTables tables = g.tables;

        java.util.HashMap<Long, Long> shadow = new java.util.HashMap<>();
        int rc = 65536 | (compress ? Tracer.RC_COMP_CODE | Tracer.RC_COMP_RLOG | Tracer.RC_COMP_WLOG : 0);
        int windowsChecked = 0, failed = 0, windowsSkipped = 0; long rowsChecked = 0;
        for (int s = s0; s <= s1 && s < segs.size(); s++) {
            Seg sg = segs.get(s);
            TreeSet<Long> written = new TreeSet<>(); for (long[] u : sg.updates) written.add(u[0] / 512);
            if (w26Written != null && !new TreeSet<>(w26Written.get(s)).equals(written))
                throw new IllegalStateException("segment " + s + ": seg/NNN.pages differs from the update list's pages");
            int created = g.fs.setOwnership(img, written);
            if (System.getProperty("diag.addr") != null) for (String x : System.getProperty("diag.addr").split(",")) {
                long ad = Long.decode(x), cell = (ad - lowest) / 8;
                System.out.printf("diag segment %d start: 0x%x = 0x%x (shadow %s)%n", s, ad, g.fs.load64(img, ad - lowest), shadow.containsKey(cell) ? String.format("0x%x", shadow.get(cell)) : "-");
            }
            g.ms.cold(); if (mapMs != null) mapMs.cold();
            long r0 = g.tr.rows, p0 = g.tr.rowsPad, im0 = g.tr.iMiss, dm0 = g.tr.dMiss, wb0 = g.tr.writebacks, lh0 = g.tr.lruHits, lm0 = g.tr.lruMisses, pr0 = g.tr.probeReads;
            long[] regs = new long[128]; System.arraycopy(sg.state, 3, regs, 0, 128);
            long segStart = g.t, cycStart = 0;
            g.loadRecord(sg.state[0], (int) sg.state[1], (int) sg.state[2], regs, sg.advice);
            long[] start = g.tr.rec.clone();
            long segCycles = 0;
            for (int j = 0; j < 64; j++) {
                int wi = 64 * s + j;
                Tracer.WindowOut o = g.window(rc, 40L * 65536 + 2_000_000);
                if (o == null || g.tr.stopped()) {
                    System.out.printf("%s window %d: STOPPED %s%n", name, wi, g.tr.errorLog); System.exit(1);
                }
                segCycles += g.tr.rowsEndAt - g.tr.runStartAt;
                List<String> bad = new ArrayList<>();
                if (byIndex.containsKey(wi) && windowsChecked < maxWin) {
                    Map<String, Object> we = byIndex.get(wi);
                    String base = String.format("window_%05d.", wi);
                    // the start state against state.bin (a v1.25 record: bitmap, state, stream, registers)
                    ByteBuffer st = le(Files.readAllBytes(pd.resolve(base + "state.bin")));
                    if (start[0] != st.getLong(16) || (start[1] & 0xFFFFFFFFFFFFL) != (st.getLong(24) & 0xFFFFFFFFFFFFL)) bad.add(String.format("start state: Tracer pc 0x%x idx/off 0x%x, delivery 0x%x 0x%x", start[0], start[1] & 0xFFFFFFFFFFFFL, st.getLong(16), st.getLong(24) & 0xFFFFFFFFFFFFL));
                    for (int r = 1; r < 128 && bad.isEmpty(); r++) if (start[4 + r] != st.getLong(48 + 8 * r)) bad.add(String.format("start register %d: Tracer 0x%x, delivery 0x%x", r, start[4 + r], st.getLong(48 + 8 * r)));
                    List<Object> braw = ReplayDelivery.Json.list(we.get("bits_raw"));
                    int[] vals = { (int) (((Number) braw.get(0)).longValue() / 32), (int) (((Number) braw.get(1)).longValue() / 64), (int) (((Number) braw.get(2)).longValue() / 64) };
                    for (int k = 0; k < 3; k++) if (o.sizes[k].values() != vals[k]) bad.add("stream " + k + ": " + o.sizes[k].values() + " values, delivery " + vals[k]);
                    if (!compress) {
                        String[] f = { "code.bin", "rlog.bin", "wlog.bin" }; long[][] mine = { o.code, o.rlog, o.wlog }; int[] bits = { 32, 64, 64 };
                        for (int k = 0; k < 3; k++) {
                            long[] ref = TracerRig.streamValues(ReplayDelivery.words(pd.resolve(base + f[k])), vals[k], bits[k]);
                            int nv = Math.min(vals[k], o.sizes[k].values());
                            long[] my = TracerRig.streamValues(mine[k], nv, bits[k]);
                            for (int v = 0; v < nv; v++) if (ref[v] != my[v]) {
                                StringBuilder ctx = new StringBuilder();
                                for (int q = Math.max(0, v - 3); q < Math.min(nv, v + 3); q++) ctx.append(String.format(" [%d] 0x%x/0x%x", q, my[q], ref[q]));
                                bad.add(String.format("%s first difference at value %d (Tracer/delivery):%s", f[k], v, ctx)); break;
                            }
                        }
                    }
                    if (compress) {
                        // the coded sizes and the pick against the delivery's encoder (WORKER9: the same rules, 4.19.15)
                        List<Object> blru = ReplayDelivery.Json.list(we.get("bits_lru"));
                        for (int k = 0; k < 3; k++) {
                            long lb = ((Number) blru.get(k)).longValue(), rb = ((Number) braw.get(k)).longValue();
                            int codedW = (int) ((lb + 127) / 128), rawW = (int) ((rb + 127) / 128);
                            boolean pick = codedW < rawW;
                            if (o.sizes[k].compressed() != pick || o.sizes[k].words() != (pick ? codedW : rawW))
                                bad.add(String.format("stream %d: Tracer %s, delivery coded %d bits (%d words), raw %d words", k, o.sizes[k], lb, codedW, rawW));
                        }
                    }
                    long inl = 0; for (Object x : ReplayDelivery.Json.list(we.get("templates"))) { int n = ((Number) x).intValue(); if (n < 64 && tables.cls[n] == 1) inl |= 1L << n; }
                    if (o.record[2 * Tracer.REC_BITMAP] != inl || o.record[2 * Tracer.REC_BITMAP + 1] != 0) bad.add(String.format("bitmap: Tracer 0x%x, delivery's inlines 0x%x", o.record[2 * Tracer.REC_BITMAP], inl));
                    if (stageA && (bad.isEmpty() || diag)) {
                        String r = stageA(tables, start, o, pd.resolve(base + "rows.bin"));
                        if (r != null) bad.add("Stage A: " + r);
                    }
                    windowsChecked++; rowsChecked += 65536;
                    if (!quiet || !bad.isEmpty()) System.out.printf("%s window %5d: %s  rows %d  cycles %d  %.3f/row  sizes %s%n", name, wi, bad.isEmpty() ? "OK  " : "FAIL", 65536,
                            g.tr.rowsEndAt - g.tr.runStartAt, (g.tr.rowsEndAt - g.tr.runStartAt) / 65536.0, java.util.Arrays.toString(o.sizes));
                    for (String b : bad) System.out.println("    " + b);
                    if (!bad.isEmpty()) failed++;
                } else if (wi <= lastDelivered) {
                    windowsSkipped++;
                } else if (o.sizes[0].values() != 65536 || o.sizes[1].values() != 0 || o.sizes[2].values() != 0 || o.record[0] != 0) {
                    // past the delivery: must be padding
                    if (wi > lastDelivered) { System.out.printf("%s window %d: past the delivery but not padding: %s%n", name, wi, java.util.Arrays.toString(o.sizes)); failed++; }
                }
                start = o.record.clone();
            }
            // ---- the segment's end
            int wb = g.tr.writeBackDirtyUntimed();
            List<String> bad = new ArrayList<>();
            if (s + 1 < segs.size()) {
                long[] nx = segs.get(s + 1).state;
                if (g.tr.rec[0] != nx[0] || (int) g.tr.rec[1] != (int) nx[1] || ((g.tr.rec[1] >>> 32) & 0xFFFF) != nx[2]) bad.add(String.format("end state: pc 0x%x idx %d off %d, frame 2 0x%x %d %d", g.tr.rec[0], (int) g.tr.rec[1], (g.tr.rec[1] >>> 32) & 0xFFFF, nx[0], nx[1], nx[2]));
                for (int r = 1; r < 128 && bad.isEmpty(); r++) if (g.tr.rec[4 + r] != nx[3 + r]) bad.add(String.format("end register %d: 0x%x, frame 2 0x%x", r, g.tr.rec[4 + r], nx[3 + r]));
            }
            // every cell updated so far (segments 0 to s) against the cumulative updates: catches a stray write elsewhere
            for (long[] u : sg.updates) shadow.put(u[0], u[1]);
            int strays = 0;
            for (var e : shadow.entrySet()) { long v = g.fs.load64(img, 8 * e.getKey()); if (v != e.getValue()) { if (strays++ < 3) bad.add(String.format("cumulative: cell 0x%x is 0x%x, the updates say 0x%x", lowest + 8 * e.getKey(), v, e.getValue())); } }
            if (strays > 0) bad.add(strays + " cells differ from the cumulative updates");
            long cells = 0;
            for (long[] u : sg.updates) { long v = g.fs.load64(img, 8 * u[0]); cells++; if (v != u[1]) { bad.add(String.format("memory cell 0x%x: 0x%x, the update list 0x%x", lowest + 8 * u[0], v, u[1])); break; } }
            System.out.printf("%s segment %d: %s  %d cycles (%.3f/row, %.2f ms at 1 GHz)  pages written %d (%d created)  dirty lines %d  cells checked %d  stalls %s%n  %s%n",
                    name, s, bad.isEmpty() ? "OK" : "FAIL", segCycles, segCycles / (64.0 * 65536), segCycles / 1e6, written.size(), created, wb, cells,
                    java.util.Arrays.toString(g.tr.stalls), g.ms.summary());
            for (String b : bad) System.out.println("    " + b);
            if (!bad.isEmpty()) failed++;
            long rowsS = g.tr.rows - r0, padS = g.tr.rowsPad - p0, realCyc = segCycles - 4 * padS;
            if (out != null) {
                MemorySystem m = g.ms; long[] st = g.tr.stalls;
                out.printf("%d,%d,%d,%d,%d,%.4f,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%s%n", s, rowsS, padS, segCycles, realCyc,
                        rowsS > padS ? (double) realCyc / (rowsS - padS) : 0.0, st[0], st[1], st[2], st[3], st[4], st[5], st[6],
                        g.tr.iMiss - im0, g.tr.dMiss - dm0, g.tr.writebacks - wb0, g.tr.lruHits - lh0, g.tr.lruMisses - lm0, g.tr.probeReads - pr0,
                        m.acc[0], m.hits[0], m.acc[1], m.hits[1], (mapMs != null ? mapMs : m).acc[2], (mapMs != null ? mapMs : m).hits[2],
                        (mapMs != null ? mapMs : m).acc[3], (mapMs != null ? mapMs : m).hits[3], m.walkCycles, m.dataCycles, m.lpddrReads, m.earlyEvictions,
                        m.readWaits, m.readWaitCycles, mapMs != null ? mapMs.lpddrReads : -1, bad.isEmpty() ? "OK" : "FAIL");
                out.flush();
            }
            java.util.Arrays.fill(g.tr.stalls, 0);
        }
        if (out != null) out.close();
        System.out.printf("%s: %d windows checked (%d rows), %d failed, %.1f s%n", name, windowsChecked, rowsChecked, failed, (System.nanoTime() - t0) / 1e9);
        if (byIndex.size() == lastDelivered + 1 && windowsChecked + windowsSkipped < Math.min(byIndex.size(), 64 * (s1 - s0 + 1)) && maxWin == Integer.MAX_VALUE) { System.out.println("not every delivered window was checked"); failed++; }
        System.exit(failed == 0 ? 0 : 1);
    }

    /** Stage A on the Tracer's output: the start state with the window's stream word, its three streams; rows against rows.bin. */
    static String stageA(ReplayTables tables, long[] start, Tracer.WindowOut o, Path rowsBin) throws Exception {
        ReplayWindow w = new ReplayWindow();
        w.rows = 65536; w.useTables(tables);
        w.region0 = new long[2 * Replayer.PART0_WORDS];
        System.arraycopy(start, 0, w.region0, 0, w.region0.length);
        w.region0[2] = o.record[2]; w.region0[3] = o.record[3];        // run n's totals (the stream word)
        w.code = o.code; w.rlog = o.rlog; w.wlog = o.wlog;
        ReplayDriver d = new ReplayDriver(w);
        d.load();
        d.pass(40L * 65536 + 100_000);
        ByteBuffer ref = le(Files.readAllBytes(rowsBin));
        int n = ref.capacity() / 80;
        if (d.rows.size() != n) return "row count: replayed " + d.rows.size() + ", rows.bin " + n;
        for (int r = 0; r < n; r++) {
            long[] gr = d.rows.get(r);
            for (int k = 0; k < 10; k++) if (gr[k] != ref.getLong(80 * r + 8 * k)) {
                long[] e = new long[10]; for (int q = 0; q < 10; q++) e[q] = ref.getLong(80 * r + 8 * q);
                return "row " + r + ": replayed " + ReplayDelivery.describe(ReplayDelivery.fields(gr)) + "; rows.bin " + ReplayDelivery.describe(ReplayDelivery.fields(e));
            }
        }
        Replayer.StreamSize[] sz = Replayer.parseStreamWord(o.record[2], o.record[3]);
        String c = ReplayDelivery.consumedExactly(d.r, sz, o.code.length / 2, o.rlog.length / 2, o.wlog.length / 2);
        if (c != null) return c;
        if (!d.r.clean()) return "replayer: " + d.r.errorReport();
        return null;
    }

    // ================================================================ section 16's segment file (frames 1-5; gold, no 6 or 7)
    static final class Seg { long[] state; long[] advice = new long[0]; final List<long[]> updates = new ArrayList<>(); }   // updates: {cell, value}

    static List<Seg> readSegments(Path f, long lowest) throws Exception {
        ByteBuffer b = le(Files.readAllBytes(f));
        List<Seg> out = new ArrayList<>(); Seg cur = null;
        while (b.remaining() >= 8) {
            int len = b.getInt(), type = b.getInt();
            int dataBytes = 4 * (len - 2), at = b.position();
            switch (type) {
                case 1, 6, 7 -> {}
                case 2 -> { cur = new Seg(); out.add(cur); cur.state = new long[131]; for (int i = 0; i < 131; i++) cur.state[i] = b.getLong(at + 8 * i); }
                case 3 -> { cur.advice = new long[dataBytes / 8]; for (int i = 0; i < cur.advice.length; i++) cur.advice[i] = b.getLong(at + 8 * i); }
                case 4 -> { long[] w = new long[dataBytes / 8]; for (int i = 0; i < w.length; i++) w[i] = b.getLong(at + 8 * i); runs(w, cur); }
                case 5 -> runs(lru32(b, at, dataBytes), cur);
                default -> throw new IllegalStateException("frame type " + type);
            }
            b.position(at + dataBytes);
        }
        return out;
    }
    /** The update words: runs of a header (the start cell in the low 32 bits, the count in the high 32), then the values. */
    static void runs(long[] w, Seg s) {
        for (int i = 0; i < w.length; ) {
            long cell = w[i] & 0xFFFFFFFFL; int n = (int) (w[i] >>> 32); i++;
            for (int k = 0; k < n; k++) s.updates.add(new long[] { cell + k, w[i++] });
        }
    }
    /** Frame 5: a u32 count, then the 32-entry LRU codes, least significant bit first within each byte. */
    static long[] lru32(ByteBuffer b, int at, int bytes) {
        int n = b.getInt(at);
        long[] e = new long[32], out = new long[n];
        long pos = 8L * (at + 4);
        for (int k = 0; k < n; k++) {
            if (bit(b, pos) == 1) { int ix = (int) bits(b, pos + 1, 5); pos += 6; long v = e[ix]; out[k] = v; System.arraycopy(e, 0, e, 1, ix); e[0] = v; }
            else { int m = bit(b, pos + 1) == 1 ? 64 : 32; long v = bits(b, pos + 2, m); pos += 2 + m; out[k] = v; System.arraycopy(e, 0, e, 1, 31); e[0] = v; }
        }
        return out;
    }
    static int bit(ByteBuffer b, long pos) { return (b.get((int) (pos >>> 3)) >>> (int) (pos & 7)) & 1; }
    static long bits(ByteBuffer b, long pos, int n) { long v = 0; for (int k = 0; k < n; k++) v |= (long) bit(b, pos + k) << k; return v; }
    static ByteBuffer le(byte[] x) { return ByteBuffer.wrap(x).order(ByteOrder.LITTLE_ENDIAN); }
}
