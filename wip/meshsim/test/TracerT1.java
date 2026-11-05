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
 * <p>Usage: java -cp build:gold TracerT1 PROGRAM_DIR INPUT_DIR SEGMENT_FILE DELIVERY_DIR NAME LOG2K LOWEST
 *     [--segments a-b] [--compress] [--no-stage-a] [--max-windows N]
 */
public final class TracerT1 {
    public static void main(String[] a) throws Exception {
        Path prog = Path.of(a[0]), input = Path.of(a[1]), segFile = Path.of(a[2]), dl = Path.of(a[3]);
        String name = a[4]; int log2K = Integer.parseInt(a[5]); long lowest = Long.decode(a[6]);
        int s0 = 0, s1 = 127, maxWin = Integer.MAX_VALUE; boolean compress = false, stageA = true;
        for (int i = 7; i < a.length; i++) {
            switch (a[i]) {
                case "--segments" -> { String[] r = a[++i].split("-"); s0 = Integer.parseInt(r[0]); s1 = Integer.parseInt(r[r.length - 1]); }
                case "--compress" -> compress = true;
                case "--no-stage-a" -> stageA = false;
                case "--max-windows" -> maxWin = Integer.parseInt(a[++i]);
                default -> throw new IllegalArgumentException(a[i]);
            }
        }
        long t0 = System.nanoTime();
        // ---- gold: the cycle-0 image and the bytecode
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

        TracerRig g = new TracerRig();
        g.lowest = lowest; g.log2K = log2K;
        // the image: gold's 16 KB pages as 4 KB pages, all-zero ones as holes
        FileStore.File img = g.fs.create(2);
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
        g.image = img; g.copy = img;
        // the map: every instruction start (bring-up only)
        Map<Long, Long> targets = new java.util.HashMap<>();
        for (long pc = codeStart; pc < codeEnd; pc += 2) { int ix = (int) indexOf.invoke(bc, pc); if (ix != 0) targets.put(pc, (long) ix); }
        g.mapK = Math.max(5, 64 - Long.numberOfLeadingZeros(Math.max(1, targets.size() / 8 - 1)));
        g.mapMul = 0x9E3779B1; g.mapStep = 1000003;
        while (true) { try { g.buildMap(targets); break; } catch (IllegalStateException e) { g.mapK++; } }
        g.loadTables();
        System.out.printf("%s: image %d pages, map %d targets in 2^%d lines, %.1f s%n", name, imgPages, targets.size(), g.mapK, (System.nanoTime() - t0) / 1e9);

        // ---- the segment file
        List<Seg> segs = readSegments(segFile, lowest);
        // ---- the delivery
        Map<String, Object> man = ReplayDelivery.Json.object(Files.readString(dl.resolve(name + ".manifest.json")));
        List<Object> wins = ReplayDelivery.Json.list(ReplayDelivery.Json.object(ReplayDelivery.Json.list(man.get("programs")).get(0)).get("window_entries"));
        Path pd = dl.resolve(name);
        ReplayTables tables = g.tables;

        int rc = 65536 | (compress ? Tracer.RC_COMP_CODE | Tracer.RC_COMP_RLOG | Tracer.RC_COMP_WLOG : 0);
        int windowsChecked = 0, failed = 0; long rowsChecked = 0;
        for (int s = s0; s <= s1 && s < segs.size(); s++) {
            Seg sg = segs.get(s);
            TreeSet<Long> written = new TreeSet<>(); for (long[] u : sg.updates) written.add(u[0] / 512);
            int created = g.fs.setOwnership(img, written);
            g.ms.cold();
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
                if (wi < wins.size() && windowsChecked < maxWin) {
                    Map<String, Object> we = ReplayDelivery.Json.object(wins.get(wi));
                    String base = String.format("window_%05d.", wi);
                    // the start state against state.bin (a v1.25 record: bitmap, state, stream, registers)
                    ByteBuffer st = le(Files.readAllBytes(pd.resolve(base + "state.bin")));
                    if (start[0] != st.getLong(16) || (start[1] & 0xFFFFFFFFFFFFL) != (st.getLong(24) & 0xFFFFFFFFFFFFL)) bad.add(String.format("start state: Tracer pc 0x%x idx/off 0x%x, delivery 0x%x 0x%x", start[0], start[1] & 0xFFFFFFFFFFFFL, st.getLong(16), st.getLong(24) & 0xFFFFFFFFFFFFL));
                    for (int r = 1; r < 128 && bad.isEmpty(); r++) if (start[4 + r] != st.getLong(48 + 8 * r)) bad.add(String.format("start register %d: Tracer 0x%x, delivery 0x%x", r, start[4 + r], st.getLong(48 + 8 * r)));
                    List<Object> braw = ReplayDelivery.Json.list(we.get("bits_raw"));
                    int[] vals = { (int) (((Number) braw.get(0)).longValue() / 32), (int) (((Number) braw.get(1)).longValue() / 64), (int) (((Number) braw.get(2)).longValue() / 64) };
                    for (int k = 0; k < 3; k++) if (o.sizes[k].values() != vals[k]) bad.add("stream " + k + ": " + o.sizes[k].values() + " values, delivery " + vals[k]);
                    if (!compress && bad.isEmpty()) {
                        String[] f = { "code.bin", "rlog.bin", "wlog.bin" }; long[][] mine = { o.code, o.rlog, o.wlog }; int[] bits = { 32, 64, 64 };
                        for (int k = 0; k < 3; k++) {
                            long[] ref = TracerRig.streamValues(ReplayDelivery.words(pd.resolve(base + f[k])), vals[k], bits[k]);
                            long[] my = TracerRig.streamValues(mine[k], vals[k], bits[k]);
                            for (int v = 0; v < vals[k]; v++) if (ref[v] != my[v]) { bad.add(String.format("%s value %d: Tracer 0x%x, delivery 0x%x", f[k], v, my[v], ref[v])); break; }
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
                    if (stageA && bad.isEmpty()) {
                        String r = stageA(tables, start, o, pd.resolve(base + "rows.bin"));
                        if (r != null) bad.add("Stage A: " + r);
                    }
                    windowsChecked++; rowsChecked += 65536;
                    System.out.printf("%s window %5d: %s  rows %d  cycles %d  %.3f/row  sizes %s%n", name, wi, bad.isEmpty() ? "OK  " : "FAIL", 65536,
                            g.tr.rowsEndAt - g.tr.runStartAt, (g.tr.rowsEndAt - g.tr.runStartAt) / 65536.0, java.util.Arrays.toString(o.sizes));
                    for (String b : bad) System.out.println("    " + b);
                    if (!bad.isEmpty()) failed++;
                } else if (o.sizes[0].values() != 65536 || o.sizes[1].values() != 0 || o.sizes[2].values() != 0 || o.record[0] != 0) {
                    // past the delivery: must be padding
                    if (wi >= wins.size()) { System.out.printf("%s window %d: past the delivery but not padding: %s%n", name, wi, java.util.Arrays.toString(o.sizes)); failed++; }
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
            long cells = 0;
            for (long[] u : sg.updates) { long v = g.fs.load64(img, 8 * u[0]); cells++; if (v != u[1]) { bad.add(String.format("memory cell 0x%x: 0x%x, the update list 0x%x", lowest + 8 * u[0], v, u[1])); break; } }
            System.out.printf("%s segment %d: %s  %d cycles (%.3f/row, %.2f ms at 1 GHz)  pages written %d (%d created)  dirty lines %d  cells checked %d  stalls %s%n  %s%n",
                    name, s, bad.isEmpty() ? "OK" : "FAIL", segCycles, segCycles / (64.0 * 65536), segCycles / 1e6, written.size(), created, wb, cells,
                    java.util.Arrays.toString(g.tr.stalls), g.ms.summary());
            for (String b : bad) System.out.println("    " + b);
            if (!bad.isEmpty()) failed++;
            java.util.Arrays.fill(g.tr.stalls, 0);
        }
        System.out.printf("%s: %d windows checked (%d rows), %d failed, %.1f s%n", name, windowsChecked, rowsChecked, failed, (System.nanoTime() - t0) / 1e9);
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
