import java.nio.ByteBuffer;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.TreeSet;

/**
 * T2 (WORKER22, B1; PLAN.md section 5): a segment on the full chip. C&C 3 runs tracer_ctl.c (sw/tracer) with the Tracer as
 * its peripheral (Config.cc3Tracer) on the one memory, its DMA (src 74, item 118) sending each window into its tile's
 * replay region; then every tile runs replay_ctl.c (sw/replay, layout version 5) on the window it was sent, and its rows
 * are compared with WORKER9's rows.bin.
 *
 * <ul>
 * <li>The memory is carried to the chosen segment by T1's path (the Tracer model, the ideal controller), on WORKER26's
 *     initial image and map, as the one-memory acceptance (DECISIONS-v1.3, 9cf1b52); the chip's Tracer then traces the
 *     segment on that memory, its written pages owned, the shared cache cold.</li>
 * <li>The harness stands in for C&C 1 and C&C 2 where they are out of scope: it lands C&C 3's tracer_layout.h (the
 *     tables, the templates, the segment's record from frame 2 with frame 3's count, word 66, the advice, the expected end
 *     state) and every tile's replay region (the tables, the templates and the header: C&C 3's template broadcast is out
 *     of scope) before the boot; C&C 1's firmware boots C&C 2 (cc23), C&C 3 (tracer_ctl) and the tiles (replay_ctl, which
 *     waits for a command); when C&C 3 exits, the harness has C&C 1 send the tiles one command and they replay.</li>
 * <li>Per tile: the delivered record's start state against the delivery's state.bin, its streams (raw: value for value),
 *     then the replayed rows against rows.bin and every stream consumed exactly.</li>
 * </ul>
 * Usage: java -cp build TracerT2 DELIVERY_DIR NAME --w26 DIR --segments a,b,... [--windows N] [--compress] [--csv FILE]
 */
public final class TracerT2 {
    // C&C 3's layout (tracer_layout.h)
    static final int C_HDR = 0, C_TABLES = 0x40, C_INDEX = 0xC40, C_RECORD = 0xE40, C_OWN = 0xF80, C_EXPECT = 0x10C0, C_ADVICE = 0x2000, C_TEMPLATES = 0x58000;
    static final int TL_MAGIC = 0x52435254, TL_VERSION = 1;

    public static void main(String[] a) throws Exception {
        Path dl = Path.of(a[0]); String name = a[1];
        Path w26 = null, csv = null; TreeSet<Integer> chosen = new TreeSet<>(); int windows = 64; boolean compress = false;
        for (int i = 2; i < a.length; i++) {
            switch (a[i]) {
                case "--w26" -> w26 = Path.of(a[++i]);
                case "--segments" -> { for (String x : a[++i].split(",")) chosen.add(Integer.parseInt(x)); }
                case "--windows" -> windows = Integer.parseInt(a[++i]);
                case "--compress" -> compress = true;
                case "--csv" -> csv = Path.of(a[++i]);
                default -> throw new IllegalArgumentException(a[i]);
            }
        }
        long t0 = System.nanoTime();
        TracerRig g = new TracerRig();
        TracerT1.W26 in = TracerT1.loadW26(w26, g, name);
        g.image = g.copy = in.img; g.lowest = in.lowest; g.log2K = in.log2K;
        g.ms.mapRootForStats = g.map.root();
        g.loadTables();
        List<TracerT1.Seg> segs = TracerT1.readSegments(w26.resolve("segments.seg"), in.lowest);
        Map<String, Object> man = ReplayDelivery.Json.object(Files.readString(dl.resolve(name + ".manifest.json")));
        Path pd = dl.resolve(name);
        java.io.PrintWriter out = csv != null ? new java.io.PrintWriter(Files.newBufferedWriter(csv)) : null;
        if (out != null) out.println("segment,windows,cc3_cycles,trace_rows_cycles,drain_cycles,transfers,dma_packets,replay_cycles_max,rows_checked,wall_s,ok");
        int rc = (compress ? Tracer.RC_COMP_CODE | Tracer.RC_COMP_RLOG | Tracer.RC_COMP_WLOG : 0);
        int failed = 0;
        for (int s = 0; s <= chosen.last(); s++) {
            TracerT1.Seg sg = segs.get(s);
            TreeSet<Long> written = new TreeSet<>(); for (long[] u : sg.updates) written.add(u[0] / 512);
            g.fs.setOwnership(in.img, written);
            long[] regs = new long[128]; System.arraycopy(sg.state, 3, regs, 0, 128);
            if (!chosen.contains(s)) {
                // ---- carried by T1's path (the model and the ideal controller)
                g.ms.cold();
                g.loadRecord(sg.state[0], (int) sg.state[1], (int) sg.state[2], regs, sg.advice);
                for (int j = 0; j < 64; j++) { Tracer.WindowOut o = g.window(65536 | rc, 40L * 65536 + 2_000_000); if (o == null || g.tr.stopped()) throw new IllegalStateException("T1 carry: segment " + s + " stopped: " + g.tr.errorLog); }
                g.tr.writeBackDirtyUntimed();
                continue;
            }
            long w0 = System.nanoTime();
            Result r = chip(g, in, sg, s + 1 < segs.size() ? segs.get(s + 1) : null, regs, s, windows, rc, pd, man);
            double wall = (System.nanoTime() - w0) / 1e9;
            System.out.printf("%s segment %d (T2, %d windows): %s  C&C 3 %d cycles (trace rows %d, drain %d), %d transfers, %d DMA packets; replay max %d cycles; %d rows checked; %.0f s%n",
                    name, s, windows, r.ok() ? "OK" : "FAIL", r.cc3Cycles, r.rowCycles, r.drainCycles, r.transfers, r.packets, r.replayMax, r.rows, wall);
            for (String d : r.detail) System.out.println("    " + d);
            if (out != null) { out.printf("%d,%d,%d,%d,%d,%d,%d,%d,%d,%.1f,%s%n", s, windows, r.cc3Cycles, r.rowCycles, r.drainCycles, r.transfers, r.packets, r.replayMax, r.rows, wall, r.ok() ? "OK" : "FAIL"); out.flush(); }
            if (!r.ok()) failed++;
            if (windows < 64) break;                                   // a partial segment can't carry the memory on
        }
        if (out != null) out.close();
        System.out.printf("%s: T2 on %d segment(s), %d failed, %.0f s%n", name, chosen.size(), failed, (System.nanoTime() - t0) / 1e9);
        System.exit(failed == 0 ? 0 : 1);
    }

    static final class Result {
        final List<String> detail = new ArrayList<>(); long cc3Cycles, rowCycles, drainCycles, transfers, packets, replayMax, rows;
        boolean ok() { return detail.isEmpty(); }
    }

    static void put(BigSRAM s, int chunk, long[] words) { for (int i = 0; i < words.length; i++) { s.poke(chunk + 2 * i, (int) words[i]); s.poke(chunk + 2 * i + 1, (int) (words[i] >>> 32)); } }
    static void putBytes(BigSRAM s, int chunk, byte[] b) {
        ByteBuffer bb = ReplayDelivery.le(java.util.Arrays.copyOf(b, (b.length + 15) & ~15));
        for (int i = 0; i < bb.capacity() / 4; i++) s.poke(chunk + i, bb.getInt(4 * i));
    }
    static long[] getWords(BigSRAM s, int chunk, int n128) { long[] w = new long[2 * n128]; for (int i = 0; i < 2 * n128; i++) w[i] = (s.peek(chunk + 2 * i) & 0xFFFFFFFFL) | (long) s.peek(chunk + 2 * i + 1) << 32; return w; }

    static Result chip(TracerRig g, TracerT1.W26 in, TracerT1.Seg sg, TracerT1.Seg next, long[] regs, int s, int windows, int rc, Path pd, Map<String, Object> man) throws Exception {
        Result res = new Result();
        Config k = Mach.coresCfg(-1); k.cc3Tracer = true; k.tracerDeliveriesUncounted = true;   // Q15's interim
        Mach m = new Mach(k);
        Tracer tr = m.c.cores.tracer;
        MemorySystem ms = new MemorySystem(g.fs); ms.mapRootForStats = g.map.root();
        tr.mem = ms; tr.onStateWrite = ms::cold;
        ReplayTables tb = g.tables;
        byte[] tables = new byte[tb.decodeBytes.length + tb.romBytes.length];
        System.arraycopy(tb.decodeBytes, 0, tables, 0, tb.decodeBytes.length); System.arraycopy(tb.romBytes, 0, tables, tb.decodeBytes.length, tb.romBytes.length);

        // ---- C&C 3's big SRAM (tracer_layout.h)
        BigSRAM s3 = m.c.cores.unit(2).sram;
        putBytes(s3, C_TABLES, tables); putBytes(s3, C_INDEX, tb.indexBytes); putBytes(s3, C_TEMPLATES, tb.templateBytes);
        long[] rec = new long[2 * 68];
        rec[0] = sg.state[0]; rec[1] = (sg.state[1] & 0xFFFFFFFFL) | (sg.state[2] & 0xFFFF) << 32;
        rec[3] = (long) sg.advice.length << 38;                       // the stream word's advice field [124:102]
        System.arraycopy(regs, 0, rec, 4, 128);
        long[] fw = Tracer.filesWord(in.img.root(), in.img.depth(), g.map.root(), g.map.depth(), in.log2K, in.lowest);
        rec[132] = fw[0]; rec[133] = fw[1];
        put(s3, C_RECORD, rec);
        long[] adv = java.util.Arrays.copyOf(sg.advice, sg.advice.length + (sg.advice.length & 1));
        put(s3, C_ADVICE, adv);
        if (next != null) {
            long[] ex = new long[2 * 66];
            ex[0] = next.state[0]; ex[1] = (next.state[1] & 0xFFFFFFFFL) | (next.state[2] & 0xFFFF) << 32;
            System.arraycopy(next.state, 3, ex, 4, 128);
            put(s3, C_EXPECT, ex);
        }
        int[] h = new int[32];
        h[0] = TL_MAGIC; h[1] = TL_VERSION; h[2] = C_TABLES; h[3] = C_INDEX; h[4] = tb.indexBytes.length / 16; h[5] = C_TEMPLATES; h[6] = C_RECORD;
        h[7] = C_OWN; h[8] = next != null && windows == 64 ? C_EXPECT : 0; h[9] = C_ADVICE; h[10] = sg.advice.length; h[11] = 65536 | rc;
        h[12] = g.mapMul; h[13] = g.mapStep; h[14] = g.mapK; h[15] = windows;
        h[16] = StageB.RECORD; h[17] = StageB.CODE; h[18] = StageB.RLOG; h[19] = StageB.WLOG; h[20] = 0;
        for (int i = 0; i < 32; i++) s3.poke(C_HDR + i, h[i]);

        // ---- every tile's replay region (replay_layout.h, version 5): the record and the streams come by C&C 3's DMA
        List<List<long[]>> rows = new ArrayList<>();
        for (int t = 0; t < 64; t++) {
            Tile p = m.c.tiles[t];
            StageB.put(p, StageB.TABLES, tables); StageB.put(p, StageB.INDEX, tb.indexBytes); StageB.put(p, StageB.TEMPLATES, tb.templateBytes);
            int[] th = new int[16];
            th[0] = 0x594C5052; th[1] = 5; th[2] = StageB.TABLES; th[3] = StageB.INDEX; th[4] = tb.indexBytes.length / 16; th[5] = StageB.TEMPLATES; th[6] = StageB.RECORD;
            th[7] = StageB.CODE; th[8] = -1; th[9] = StageB.RLOG; th[10] = -1; th[11] = StageB.WLOG; th[12] = -1; th[13] = t < windows ? 1 : 0;
            th[14] = Replayer.runConfig(65536, true, true); th[15] = 1;
            for (int i = 0; i < 16; i++) p.sram.poke(StageB.HDR + i, th[i]);
            List<long[]> l = new ArrayList<>(); rows.add(l);
            ((Replayer) p.slot.p[1]).rowListener = l::add;
        }

        // ---- boot (6.2): C&C 1's firmware boots C&C 2, C&C 3 (tracer_ctl) and the tiles (replay_ctl, waiting)
        Mach.Image tile = new Mach.Image(Path.of("sw/replay/build"), "replay_ctl"), cc3 = new Mach.Image(Path.of("sw/tracer/build"), "tracer_ctl");
        m.bootCores(tile, cc3);
        Tile u3 = m.c.cores.unit(2);
        long cap = (long) windows * 65536 * 12 + 5_000_000;
        m.run(() -> u3.core.halt != Controller.RUN && u3.core.firstImageAt > 0 || tr.stopped(), cap);
        long cc3End = m.c.cycle;
        int R = cc3.sym("tr_results");
        int status = u3.core.peekLocal(R), wins = u3.core.peekLocal(R + 4), transfers = u3.core.peekLocal(R + 16), terr = u3.core.peekLocal(R + 24),
            left = u3.core.peekLocal(R + 28), diff = u3.core.peekLocal(R + 32);
        res.cc3Cycles = cc3End - u3.core.firstImageAt; res.transfers = transfers; res.packets = m.c.cores.unit(2).slot.dma[1].packets;
        res.rowCycles = tr.runCycles; res.drainCycles = tr.drainCycles;
        if (tr.stopped()) res.detail.add("the Tracer stopped: " + tr.errorLog);
        if (u3.core.halt != Controller.HALT_EXIT) res.detail.add("C&C 3 didn't exit by cycle " + m.c.cycle + ": " + u3.core.haltReport() + " pc 0x" + Integer.toHexString(u3.core.pc) + (u3.core.inWfi ? " (WFI)" : ""));
        if (status != 1 && !(status == 5 && windows < 64)) res.detail.add(String.format("tr_results: status %d, windows %d, ERRORS 0x%x, advice left %d, end-state word %d", status, wins, terr, left, diff - 1));
        if (!res.ok()) return res;

        // ---- the tiles: what each was sent, then the command, the replay, the rows
        List<Object> wl = ReplayDelivery.Json.list(ReplayDelivery.Json.object(ReplayDelivery.Json.list(man.get("programs")).get(0)).get("window_entries"));
        Map<Integer, Map<String, Object>> byIndex = new java.util.HashMap<>();
        for (Object x : wl) { Map<String, Object> we = ReplayDelivery.Json.object(x); if (we.containsKey("files")) byIndex.put(((Number) we.get("index")).intValue(), we); }
        for (int t = 0; t < windows; t++) {
            int wi = 64 * s + t;
            Tile p = m.c.tiles[t];
            long[] got = getWords(p.sram, StageB.RECORD, 68);
            ByteBuffer st = ReplayDelivery.le(Files.readAllBytes(pd.resolve(String.format("window_%05d.state.bin", wi))));   // v1.25's order
            if (got[0] != st.getLong(16) || (got[1] & 0xFFFFFFFFFFFFL) != (st.getLong(24) & 0xFFFFFFFFFFFFL)) res.detail.add(String.format("tile %d: the delivered state word 0x%x 0x%x, the delivery's 0x%x 0x%x", t, got[0], got[1], st.getLong(16), st.getLong(24)));
            for (int r = 1; r < 128; r++) if (got[4 + r] != st.getLong(48 + 8 * r)) { res.detail.add("tile " + t + ": delivered register " + r + " differs from state.bin"); break; }
            if (rc == 0 && byIndex.containsKey(wi)) {
                Replayer.StreamSize[] sz = Replayer.parseStreamWord(got[2], got[3]);
                String[] f = { "code.bin", "rlog.bin", "wlog.bin" }; int[] at = { StageB.CODE, StageB.RLOG, StageB.WLOG }, bits = { 32, 64, 64 };
                for (int q = 0; q < 3; q++) {
                    long[] mine = TracerRig.streamValues(getWords(p.sram, at[q], sz[q].words()), sz[q].values(), bits[q]);
                    long[] ref = TracerRig.streamValues(ReplayDelivery.words(pd.resolve(String.format("window_%05d.%s", wi, f[q]))), sz[q].values(), bits[q]);
                    if (!java.util.Arrays.equals(mine, ref)) { res.detail.add("tile " + t + ": the delivered " + f[q] + " differs from the delivery's"); break; }
                }
            }
        }
        if (!res.ok()) return res;
        long cmdAt = m.c.cycle;
        m.run(m::coresBooted, 1_000_000);
        m.phase(1 << 24);
        m.run(() -> { for (int t = 0; t < 64; t++) if (m.c.tiles[t].core.halt == Controller.RUN) return false; return true; }, 4_000_000);
        for (int t = 0; t < windows; t++) {
            int wi = 64 * s + t;
            Tile p = m.c.tiles[t]; Replayer rp = (Replayer) p.slot.p[1];
            res.replayMax = Math.max(res.replayMax, rp.passEndAt - rp.passStartAt);
            if (p.core.halt != Controller.HALT_EXIT || m.exitValue(t) != 1) { res.detail.add("tile " + t + ": replay_ctl " + p.core.haltReport() + " exit " + m.exitValue(t)); continue; }
            ByteBuffer ref = ReplayDelivery.le(Files.readAllBytes(pd.resolve(String.format("window_%05d.rows.bin", wi))));
            List<long[]> l = rows.get(t);
            int n = ref.capacity() / 80;
            if (l.size() != n) { res.detail.add("tile " + t + ": " + l.size() + " rows, rows.bin " + n); continue; }
            for (int r = 0; r < n; r++) {
                long[] gr = l.get(r); boolean eq = true;
                for (int q = 0; q < 10; q++) if (gr[q] != ref.getLong(80 * r + 8 * q)) { eq = false; break; }
                if (!eq) { res.detail.add("tile " + t + " (window " + wi + "): row " + r + " differs: " + ReplayDelivery.describe(ReplayDelivery.fields(gr))); break; }
            }
            res.rows += n;
            if (!rp.clean()) res.detail.add("tile " + t + ": replayer " + rp.errorReport());
        }
        if (m.c.violations > 0) res.detail.add("meshsim: " + StageB.cut(m.c.violationReport().replace("\n", "; ")));
        return res;
    }
}
