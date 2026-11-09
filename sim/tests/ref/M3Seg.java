import java.io.PrintWriter;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.TreeSet;

/**
 * WORKER13 (sim/tests/AUDIT-M3.md sections 0, 1, 6): one T2 segment on the full chip exactly as TracerT2.chip runs it
 * (the same setup, harness stand-ins, boot, CC_SEGMENT, stop rule and checks; its summary line and CSV fields), with
 * read-only hooks between cycles:
 *   cover=FILE    coverage: ref's own counters per unit group (tiles, C&C 1, 2, 3) at the boot's end, the segment's
 *                 start, C&C 3's exit and the end; the new units' statistics; and per image, every pc's cycles (pcs=FILE)
 *   fp=FILE       the Tracer's fingerprint units (StateFpM3) every fpevery cycles (default 100,000) and at C&C 3's exit,
 *                 with each unit's cost per call
 *   counts        finding 9.2 (a depth-1 post lost to a same-cycle consume: run done, new expansion) and 9.3 (a PULSE_DMA
 *                 on C&C 3 with the Tracer's inputs or landings of the previous cycle pending), printed at the end
 * Latency overrides (finding 9.1): go=N ready=N stage=N error=N set Config's goLatency, readyLatency, stageDelay,
 * errorLatency before the chip is built.
 *
 * Usage: java -cp build M3Seg DELIVERY_DIR NAME --w26 DIR --segment S [--windows N] [key=value ...]
 */
public final class M3Seg {
    public static void main(String[] a) throws Exception {
        Path dl = Path.of(a[0]); String name = a[1];
        Path w26 = null; int seg = -1, windows = 64;
        Map<String, String> kv = new LinkedHashMap<>();
        for (int i = 2; i < a.length; i++) {
            switch (a[i]) {
                case "--w26" -> w26 = Path.of(a[++i]);
                case "--segment" -> seg = Integer.parseInt(a[++i]);
                case "--windows" -> windows = Integer.parseInt(a[++i]);
                default -> { int e = a[i].indexOf('='); if (e < 0) throw new IllegalArgumentException(a[i]); kv.put(a[i].substring(0, e), a[i].substring(e + 1)); }
            }
        }
        long t0 = System.nanoTime();
        // ---- TracerT2.main's setup, for one raw run of segment `seg`
        TracerRig g = new TracerRig();
        TracerT1.W26 in = TracerT1.loadW26(w26, g, name);
        g.image = g.copy = in.img; g.lowest = in.lowest; g.log2K = in.log2K;
        g.ms.mapRootForStats = g.map.root();
        g.loadTables();
        List<TracerT1.Seg> segs = TracerT1.readSegments(w26.resolve("segments.seg"), in.lowest);
        Map<String, Object> man = ReplayDelivery.Json.object(Files.readString(dl.resolve(name + ".manifest.json")));
        Path pd = dl.resolve(name);
        FileStore snap = null; long[] regs = null; TracerT1.Seg sg = null;
        for (int s = 0; s <= seg; s++) {
            sg = segs.get(s);
            TreeSet<Long> written = new TreeSet<>(); for (long[] u : sg.updates) written.add(u[0] / 512);
            g.fs.setOwnership(in.img, written);
            regs = new long[128]; System.arraycopy(sg.state, 3, regs, 0, 128);
            if (s == seg) { snap = g.fs.copy(); break; }
            g.ms.cold();
            g.loadRecord(sg.state[0], (int) sg.state[1], (int) sg.state[2], regs, sg.advice);
            for (int j = 0; j < 64; j++) { Tracer.WindowOut o = g.window(65536, 40L * 65536 + 2_000_000); if (o == null || g.tr.stopped()) throw new IllegalStateException("T1 carry: segment " + s + " stopped: " + g.tr.errorLog); }
            g.tr.writeBackDirtyUntimed();
        }
        TracerT1.Seg nx = seg + 1 < segs.size() ? segs.get(seg + 1) : null;
        double setup = (System.nanoTime() - t0) / 1e9;
        System.out.printf("%s segment %d: setup (T1's carry through segments 0-%d) %.1f s; memory snapshot %d pages, sha %s%n",
                name, seg, seg - 1, setup, snap.pagesAllocated(), Long.toHexString(StateFpM3.filesScan(snap)));
        long w0 = System.nanoTime();
        Res r = chip(g, snap, in, sg, nx, regs, seg, windows, pd, man, kv);
        double wall = (System.nanoTime() - w0) / 1e9;
        System.out.printf("%s segment %d (T2, %d windows): %s  C&C 3 %d cycles (trace rows %d, drain %d), %d transfers, %d DMA packets; replay max %d cycles; %d rows checked; chip %d cycles; %.1f s%n",
                name, seg, windows, r.ok() ? "OK" : "FAIL", r.cc3Cycles, r.rowCycles, r.drainCycles, r.transfers, r.packets, r.replayMax, r.rows, r.chipCycles, wall);
        for (String d : r.detail) System.out.println("    " + d);
        if (kv.containsKey("csv")) try (PrintWriter w = new PrintWriter(Files.newBufferedWriter(Path.of(kv.get("csv"))))) {
            w.println("segment,windows,cc3_cycles,trace_rows_cycles,drain_cycles,transfers,dma_packets,replay_cycles_max,rows_checked,chip_cycles,setup_s,wall_s,ok");
            w.printf("%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%.1f,%.1f,%s%n", seg, windows, r.cc3Cycles, r.rowCycles, r.drainCycles, r.transfers, r.packets, r.replayMax, r.rows, r.chipCycles, setup, wall, r.ok() ? "OK" : "FAIL");
        }
        System.exit(r.ok() ? 0 : 1);
    }

    static final class Res {
        final List<String> detail = new ArrayList<>(); long cc3Cycles, rowCycles, drainCycles, transfers, packets, replayMax, rows, chipCycles;
        boolean ok() { return detail.isEmpty(); }
    }

    // ---------------------------------------------------------------- the hooks
    static final class Hooks {
        final Mach m; final Chip c; final Tracer tr; final Map<String, String> kv;
        final long[][] pcs = new long[4][8192]; final long[] pcOther = new long[4];   // [0] the 64 tiles, [1-3] C&C 1-3
        final List<String> snapNames = new ArrayList<>(); final List<long[]> snaps = new ArrayList<>();
        final PrintWriter fp; final long fpEvery; long fpCalls; final long[] fpNanos = new long[StateFpM3.TRACER_UNITS.length];
        long lost92, dma93, dma93Pending; long lastTransfers; boolean pendingAtPrev;
        final long[] doneSetSeen = new long[64], newExpSeen = new long[64]; long trNewExpSeen, trDrainSeen = Long.MIN_VALUE;

        Hooks(Mach m, Map<String, String> kv) throws Exception {
            this.m = m; this.c = m.c; this.tr = m.c.cores.tracer; this.kv = kv;
            fp = kv.containsKey("fp") ? new PrintWriter(Files.newBufferedWriter(Path.of(kv.get("fp")))) : null;
            fpEvery = Long.parseLong(kv.getOrDefault("fpevery", "100000"));
            if (fp != null) { for (String l : FpHooks.configLines(c.cfg)) fp.println("# " + l); fp.println("# units " + String.join(",", StateFpM3.TRACER_UNITS)); }
            java.util.Arrays.fill(doneSetSeen, Long.MIN_VALUE); java.util.Arrays.fill(newExpSeen, Long.MIN_VALUE);
        }
        /** After cycle c.cycle - 1 ran (runUntil's condition): read only. */
        long lastT = Long.MIN_VALUE;
        void cycle() {
            long t = c.cycle - 1;
            if (t == lastT) return;                       // runUntil checks its condition again when a new run starts
            lastT = t;
            if (kv.containsKey("cover")) {
                for (int i = 0; i < 64; i++) pc(0, c.tiles[i].core.pc);
                for (int k = 0; k < 3; k++) pc(1 + k, c.cores.unit(k).core.pc);
            }
            if (kv.containsKey("counts")) counts(t);
            if (fp != null && t > 0 && t % fpEvery == 0) fingerprint(t);
        }
        void pc(int img, int pc) { int i = (pc - 0x01000000) >>> 2; if (pc >= 0x01000000 && i < 8192) pcs[img][i]++; else pcOther[img]++; }
        void counts(long t) {
            // 9.2: a depth-1 post at t whose count is 0 at the end of t (consumed in the same cycle: the post was dropped)
            for (int i = 0; i < 64; i++) {
                Replayer rp = (Replayer) c.tiles[i].slot.p[1]; int[] cnt = (int[]) StateFp.o(rp.ev, "count");
                long ds = StateFp.l(rp, "doneSetAt"), ns = StateFp.l(rp, "newExpSetAt");
                if (ds == t && ds != doneSetSeen[i] && cnt[Replayer.EV_RUN_DONE] == 0) lost92++;
                if (ns == t && ns != newExpSeen[i] && cnt[Replayer.EV_NEWEXP] == 0) lost92++;
                doneSetSeen[i] = ds; newExpSeen[i] = ns;
            }
            int[] tc = (int[]) StateFp.o(tr.ev, "count");
            if (tr.newExpansions != trNewExpSeen) { if (tc[Tracer.EV_NEWEXP] == 0) lost92++; trNewExpSeen = tr.newExpansions; }
            if (tr.drainedAt == t && tr.drainedAt != trDrainSeen) { if (tc[Tracer.EV_RUN_DONE] == 0) lost92++; trDrainSeen = tr.drainedAt; }
            // 9.3: a PULSE_DMA at t with the Tracer's landings or inputs of effect cycle t - 1 pending at the end of t - 1
            long tf = c.cores.unit(2).slot.dma[1].transfers;
            if (tf != lastTransfers) { dma93++; if (pendingAtPrev) dma93Pending++; lastTransfers = tf; }
            boolean p = false;
            for (Object x : (java.util.ArrayDeque<?>) StateFp.o(tr, "landing")) if (StateFp.l(x, "t") <= t) p = true;
            for (Object x : (java.util.ArrayDeque<?>) StateFp.o(tr, "inputs")) if (StateFp.l(x, "t") <= t) p = true;
            pendingAtPrev = p;
        }
        void fingerprint(long t) {
            long all = 0x6A09E667F3BCC908L; int n = 0;
            for (int k = 0; k < StateFpM3.TRACER_UNITS.length; k++) {
                long s = System.nanoTime();
                StateFp.Out o = new StateFp.Out(); StateFpM3.unit(o, tr, StateFpM3.TRACER_UNITS[k], t); long h = o.done();
                fpNanos[k] += System.nanoTime() - s;
                fp.printf("%d %s %016x%n", t, StateFpM3.TRACER_UNITS[k], h); all = StateFp.fmix(all ^ h); n++;
            }
            fp.printf("%d tracer %016x%n", t, StateFp.fmix(all ^ n)); fpCalls++;
        }
        // ---- coverage counters: FpHooks' tile counters per group, then the new units'
        void snap(String nm) { if (!kv.containsKey("cover")) return; c.syncCounters(); snapNames.add(nm); snaps.add(counters()); }
        static final String[] NEW = { "slot.accepted", "slot.reservedAccesses", "slot.violations", "dma.transfers", "dma.packets", "dma.ignored",
            "rp.passes", "rp.rowsTotal", "rp.rowsM", "rp.rowsCtrl", "rp.newExpansions", "rp.flushes", "rp.configWrites", "rp.ignoredWrites", "rp.pextNonContiguous",
            "rp.stall.IMAGE", "rp.stall.CODE", "rp.stall.NEWEXP", "rp.stall.EXP", "rp.stall.RLOG", "rp.stall.WLOG", "rp.stall.OUTQ",
            "tr.runs", "tr.stateWrites", "tr.rows", "tr.rowsM", "tr.rowsPad", "tr.instrs", "tr.newExpansions", "tr.runCycles", "tr.drainCycles", "tr.scanStalls",
            "tr.iAccess", "tr.iMiss", "tr.dAccess", "tr.dMiss", "tr.writebacks", "tr.lruHits", "tr.lruMisses", "tr.probeReads", "tr.straddles",
            "tr.stall.IFETCH", "tr.stall.DMISS", "tr.stall.BUS", "tr.stall.PROBE", "tr.stall.ADVICE", "tr.stall.NEWEXP", "tr.stall.EXP", "tr.errors",
            "ms.reads", "ms.writes", "ms.beyond", "ms.indexAccesses", "ms.indexHits", "ms.dataAccesses", "ms.dataHits", "ms.lpddrReads", "ms.earlyEvictions",
            "ms.readWaits", "ms.readWaitCycles", "ms.writeAllocates", "cc.hostPackets", "chip.violations" };
        static String[] names() {
            List<String> n = new ArrayList<>();
            for (String g : new String[] { "tiles", "cc1", "cc2", "cc3" }) for (int j = 0; j < FpHooks.NAMES.length - 5; j++) n.add(g + "." + FpHooks.NAMES[j]);
            for (String x : NEW) n.add(x);
            return n.toArray(new String[0]);
        }
        long[] counters() {
            int T = FpHooks.NAMES.length - 5; long[] v = new long[4 * T + NEW.length];
            for (int i = 0; i < 64; i++) add(v, 0, tileCounters(c.tiles[i]));
            for (int k = 0; k < 3; k++) add(v, (1 + k) * T, tileCounters(c.cores.unit(k)));
            int b = 4 * T; List<Long> x = new ArrayList<>();
            long acc = 0, res = 0, vio = 0, tf = 0, pk = 0, ig = 0;
            for (Tile p : allUnits()) { acc += p.slot.accepted; res += p.slot.reservedAccesses; vio += p.slot.violations;
                for (int n = 1; n <= 3; n++) if (p.slot.dma[n] != null) { tf += p.slot.dma[n].transfers; pk += p.slot.dma[n].packets; ig += p.slot.dma[n].ignored; } }
            x.add(acc); x.add(res); x.add(vio); x.add(tf); x.add(pk); x.add(ig);
            long[] rs = new long[9]; long[] rst = new long[Replayer.Stall.values().length];
            for (int i = 0; i < 64; i++) { Replayer rp = (Replayer) c.tiles[i].slot.p[1];
                long[] y = { rp.passes, rp.rowsTotal, rp.rowsM, rp.rowsCtrl, rp.newExpansions, rp.flushes, rp.configWrites, rp.ignoredWrites, rp.pextNonContiguous };
                for (int j = 0; j < 9; j++) rs[j] += y[j]; for (int j = 0; j < rst.length; j++) rst[j] += rp.stalls[j]; }
            for (long y : rs) x.add(y); for (long y : rst) x.add(y);
            long[] ty = { tr.runs, tr.stateWrites, tr.rows, tr.rowsM, tr.rowsPad, tr.instrs, tr.newExpansions, tr.runCycles, tr.drainCycles, tr.scanStalls,
                tr.iAccess, tr.iMiss, tr.dAccess, tr.dMiss, tr.writebacks, tr.lruHits, tr.lruMisses, tr.probeReads, tr.straddles };
            for (long y : ty) x.add(y); for (long y : tr.stalls) x.add(y); x.add(StateFp.l(tr, "errors"));
            MemorySystem ms = tr.mem;
            long[] my = { ms.reads, ms.writes, ms.beyond, ms.indexAccesses, ms.indexHits, ms.dataAccesses, ms.dataHits, ms.lpddrReads, ms.earlyEvictions, ms.readWaits, ms.readWaitCycles, ms.writeAllocates };
            for (long y : my) x.add(y);
            x.add(c.cores.hostPackets); x.add(c.violations);
            if (x.size() != NEW.length) throw new IllegalStateException("coverage: " + x.size() + " new counters, " + NEW.length + " names");
            for (int j = 0; j < x.size(); j++) v[b + j] = x.get(j);
            return v;
        }
        List<Tile> allUnits() { List<Tile> l = new ArrayList<>(List.of(c.tiles)); for (int k = 0; k < 3; k++) l.add(c.cores.unit(k)); return l; }
        static void add(long[] v, int at, long[] x) { for (int j = 0; j < x.length; j++) v[at + j] += x[j]; }
        /** FpHooks.counters' per-tile vector (its last five, chip-level, left out). */
        static long[] tileCounters(Tile p) {
            Controller k = p.core; TileControl t = p.ctl; Router r = p.router; TrafficDirector d = p.dir;
            long fo = 0; for (WordFIFO f : p.hardwareFifos) fo += f.overflowCount;
            long sr = 0, sw = 0, sc = 0; for (int q = 0; q < BigSRAM.AREAS; q++) { sr += p.sram.reads[q]; sw += p.sram.writes[q]; sc += p.sram.combined[q]; }
            return new long[] { k.instructions, k.stallBranch, k.stallMul, k.stallSend, k.stallRegLoad, k.stallRequest, k.cyclesWfi, k.tableReads, k.tableWrites, k.busErrors, k.illegals, k.traps,
                t.sends, t.sendsBySize[0], t.sendsBySize[1], t.sendsBySize[2], t.gos, t.readies, t.dones, t.goWakes, t.eventWakes, t.traceStores, t.reservedAccesses, t.doneEarlyEvents, t.counterWraps, t.wakeInDone,
                r.injected, r.routed, r.ejected, r.ejectErrors, r.routeErrors, r.fromCncWords, r.toCncWords,
                p.inj.fromRead, p.inj.fromSend, p.inj.sendWaitCycles, p.inj.tokenWaitCycles,
                d.accepted, d.perMode[0], d.perMode[1], d.perMode[2], d.perMode[3], d.perMode[4], d.perMode[5], d.perMode[6], d.perMode[7], d.fromControllers, d.unexpectedTotal,
                d.strays, d.droppedBad, d.undefinedSize, d.readPortResults, d.readPortOverlaps,
                p.cmem.writesData, p.cmem.writesProgram, p.cmem.writesCmd, p.cmem.writesReply, p.cmem.overwrittenUnread, p.cmem.unreadCommands,
                p.sram.accesses, sr, sw, sc, p.sram.readStallCycles, p.sram.writeStallCycles,
                p.srq.issued[0], p.srq.issued[1], p.srq.issued[2], p.srq.packets, p.srq.stallCycles, p.srq.windowFullCycles, p.srq.sendTokenWaits, p.srq.outOfRange,
                p.writer.taken, p.writer.written, p.writer.augmented, p.writer.explicit, p.writer.stallCycles, p.writer.outOfRange,
                p.ag.runs, p.ag.totRequests(), p.ag.totStall(), p.ag.driftEvents, p.ag.totGroupStall(),
                p.ts.tableReads, p.ts.rmws, p.ts.pipe.ctrlWritesLost, p.ts.pipe.ctrlLoadsEngine, p.hist.rmws, p.hist.pipe.ctrlWritesLost, p.hist.pipe.ctrlLoadsEngine,
                p.gather.requests, p.gather.reads, p.gather.stallCycles, p.readPort.stores, p.readPort.overflows,
                p.req.sends, p.req.underflows, fo, p.readReturn.refusals, p.sendHold.refusals };
        }
        void end(Mach.Image[] imgs) throws Exception {
            if (fp != null) {
                fingerprint(c.cycle - 1);
                fp.print("# cost per call, us:"); for (int k = 0; k < fpNanos.length; k++) fp.printf(" %s %.1f", StateFpM3.TRACER_UNITS[k], fpNanos[k] / 1e3 / fpCalls);
                fp.println(); fp.close();
                System.out.print("fingerprints: " + fpCalls + " calls; cost per call, us:");
                for (int k = 0; k < fpNanos.length; k++) System.out.printf(" %s %.1f", StateFpM3.TRACER_UNITS[k], fpNanos[k] / 1e3 / fpCalls);
                System.out.println();
            }
            if (kv.containsKey("counts")) System.out.printf("counts: 9.2 depth-1 posts lost to a same-cycle consume %d; 9.3 PULSE_DMAs on C&C 3 %d, with the Tracer's inputs of the previous cycle pending %d%n", lost92, dma93, dma93Pending);
            if (kv.containsKey("cover")) {
                snap("end");
                try (PrintWriter w = new PrintWriter(Files.newBufferedWriter(Path.of(kv.get("cover"))))) {
                    String[] nm = names(); w.print("counter"); for (String s : snapNames) w.print("," + s); w.println();
                    for (int j = 0; j < nm.length; j++) { w.print(nm[j]); for (long[] s : snaps) w.print("," + s[j]); w.println(); }
                }
                String[] img = { "replay_ctl (64 tiles)", "cc1", "cc23", "tracer_ctl" };
                try (PrintWriter w = new PrintWriter(Files.newBufferedWriter(Path.of(kv.getOrDefault("pcs", kv.get("cover") + ".pcs"))))) {
                    for (int k = 0; k < 4; k++) {
                        w.println("# image " + k + " " + img[k] + " outside program memory " + pcOther[k]);
                        for (int i = 0; i < 8192; i++) if (pcs[k][i] != 0) w.printf("%d %08x %d%n", k, 0x01000000 + 4 * i, pcs[k][i]);
                    }
                }
            }
        }
    }

    // ---------------------------------------------------------------- TracerT2.chip, with the hooks
    static Res chip(TracerRig g, FileStore fs, TracerT1.W26 in, TracerT1.Seg sg, TracerT1.Seg next, long[] regs, int s, int windows, Path pd, Map<String, Object> man, Map<String, String> kv) throws Exception {
        Res res = new Res(); int rc = 0;
        Config k = Mach.coresCfg(-1); k.cc3Tracer = true;
        if (kv.containsKey("go")) k.goLatency = Integer.parseInt(kv.get("go"));
        if (kv.containsKey("ready")) k.readyLatency = Integer.parseInt(kv.get("ready"));
        if (kv.containsKey("stage")) k.stageDelay = Integer.parseInt(kv.get("stage"));
        if (kv.containsKey("error")) k.errorLatency = Integer.parseInt(kv.get("error"));
        System.out.printf("config: goLatency %d, readyLatency %d, errorLatency %d (%d), stageDelay %d, idleSkip %s%n", k.goLatency, k.readyLatency, k.errorLatency, k.errorLatency(), k.stageDelay, k.idleSkip);
        Mach m = new Mach(k);
        Tracer tr = m.c.cores.tracer;
        MemorySystem ms = new MemorySystem(fs); ms.mapRootForStats = g.map.root();
        tr.mem = ms; tr.onStateWrite = ms::cold;
        ReplayTables tb = g.tables;
        byte[] tables = new byte[tb.decodeBytes.length + tb.romBytes.length];
        System.arraycopy(tb.decodeBytes, 0, tables, 0, tb.decodeBytes.length); System.arraycopy(tb.romBytes, 0, tables, tb.decodeBytes.length, tb.romBytes.length);
        BigSRAM s3 = m.c.cores.unit(2).sram;
        TracerT2.putBytes(s3, TracerT2.C_TABLES, tables); TracerT2.putBytes(s3, TracerT2.C_INDEX, tb.indexBytes); TracerT2.putBytes(s3, TracerT2.C_TEMPLATES, tb.templateBytes);
        long[] rec = new long[2 * 68];
        rec[0] = sg.state[0]; rec[1] = (sg.state[1] & 0xFFFFFFFFL) | (sg.state[2] & 0xFFFF) << 32;
        rec[3] = (long) sg.advice.length << 38;
        System.arraycopy(regs, 0, rec, 4, 128);
        long[] fw = Tracer.filesWord(in.img.root(), in.img.depth(), g.map.root(), g.map.depth(), in.log2K, in.lowest);
        rec[132] = fw[0]; rec[133] = fw[1];
        TracerT2.put(s3, TracerT2.C_RECORD, rec);
        long[] adv = java.util.Arrays.copyOf(sg.advice, sg.advice.length + (sg.advice.length & 1));
        TracerT2.put(s3, TracerT2.C_ADVICE, adv);
        if (next != null) {
            long[] ex = new long[2 * 66];
            ex[0] = next.state[0]; ex[1] = (next.state[1] & 0xFFFFFFFFL) | (next.state[2] & 0xFFFF) << 32;
            System.arraycopy(next.state, 3, ex, 4, 128);
            TracerT2.put(s3, TracerT2.C_EXPECT, ex);
        }
        int[] h = new int[32];
        h[0] = TracerT2.TL_MAGIC; h[1] = TracerT2.TL_VERSION; h[2] = TracerT2.C_TABLES; h[3] = TracerT2.C_INDEX; h[4] = tb.indexBytes.length / 16; h[5] = TracerT2.C_TEMPLATES; h[6] = TracerT2.C_RECORD;
        h[7] = TracerT2.C_OWN; h[8] = next != null && windows == 64 ? TracerT2.C_EXPECT : 0; h[9] = TracerT2.C_ADVICE; h[10] = sg.advice.length; h[11] = 65536 | rc;
        h[12] = g.mapMul; h[13] = g.mapStep; h[14] = g.mapK; h[15] = windows;
        h[16] = StageB.RECORD; h[17] = StageB.CODE; h[18] = StageB.RLOG; h[19] = StageB.WLOG; h[20] = 0;
        for (int i = 0; i < 32; i++) s3.poke(TracerT2.C_HDR + i, h[i]);
        List<List<long[]>> rows = new ArrayList<>();
        for (int t = 0; t < 64; t++) {
            Tile p = m.c.tiles[t];
            StageB.put(p, StageB.TABLES, tables); StageB.put(p, StageB.INDEX, tb.indexBytes); StageB.put(p, StageB.TEMPLATES, tb.templateBytes);
            int[] th = new int[16];
            th[0] = 0x594C5052; th[1] = 5; th[2] = StageB.TABLES; th[3] = StageB.INDEX; th[4] = tb.indexBytes.length / 16; th[5] = StageB.TEMPLATES; th[6] = StageB.RECORD;
            th[7] = StageB.CODE; th[8] = -1; th[9] = StageB.RLOG; th[10] = -1; th[11] = StageB.WLOG; th[12] = -1; th[13] = t < windows ? 1 : 0;
            th[14] = Replayer.runConfig(65536, true, true);
            th[15] = t >= windows ? 1 : 2;
            for (int i = 0; i < 16; i++) p.sram.poke(StageB.HDR + i, th[i]);
            List<long[]> l = new ArrayList<>(); rows.add(l);
            ((Replayer) p.slot.p[1]).rowListener = l::add;
        }
        Mach.Image tile = new Mach.Image(Path.of("sw/replay/build"), "replay_ctl"), cc3 = new Mach.Image(Path.of("sw/tracer/build"), "tracer_ctl");
        Hooks hk = new Hooks(m, kv);
        m.bootCores(tile, cc3);
        Tile u3 = m.c.cores.unit(2);
        if (!m.run(() -> { hk.cycle(); return m.coresBooted(); }, 2_000_000)) { res.detail.add("the boot didn't finish: " + m.haltSummary()); return res; }
        hk.snap("boot-end");
        m.cc1Command(0x54, 2, 1 << 24, 0x53 << 24);
        hk.snap("segment-start");
        long cap = (long) windows * 65536 * 12 + 5_000_000;
        final long[] prog = { -1, 0, -1 };
        final String[] stall = { null };
        final PeripheralSlot.Dma dma3 = u3.slot.dma[1];
        final boolean[] cc3Snapped = { false };
        m.run(() -> {
            hk.cycle();
            if (tr.stopped()) return true;
            long c = m.c.cycle;
            if (c % 250_000 == 0) {
                long p = tr.rows + dma3.packets; for (List<long[]> l : rows) p += l.size();
                if (p != prog[0]) { prog[0] = p; prog[1] = c; }
                else if (c - prog[1] >= 3_000_000) { stall[0] = "no progress for 3 M cycles (to cycle " + c + ")"; return true; }
            }
            if (u3.core.halt == Controller.RUN || u3.core.firstImageAt <= 0) return false;
            if (prog[2] < 0) prog[2] = c;
            if (!cc3Snapped[0]) { cc3Snapped[0] = true; hk.snap("cc3-exit"); }
            for (int t = 0; t < 64; t++) if (m.c.tiles[t].core.halt == Controller.RUN) {
                if (c - prog[2] >= 1_000_000) { stall[0] = "tile " + t + " not exited 1 M cycles after C&C 3's exit (cycle " + prog[2] + ")"; return true; }
                return false;
            }
            return true;
        }, cap);
        res.chipCycles = m.c.cycle;
        hk.end(new Mach.Image[] { tile, null, null, cc3 });
        if (stall[0] != null) { res.detail.add("STOPPED: " + stall[0]); res.detail.addAll(TracerT2.dump(m, windows)); return res; }
        long cc3End = u3.core.halt == Controller.HALT_EXIT ? u3.ctl.simExitAt : m.c.cycle;
        int R = cc3.sym("tr_results");
        int status = u3.core.peekLocal(R), wins = u3.core.peekLocal(R + 4), transfers = u3.core.peekLocal(R + 16), terr = u3.core.peekLocal(R + 24),
            left = u3.core.peekLocal(R + 28), diff = u3.core.peekLocal(R + 32);
        res.cc3Cycles = cc3End - u3.core.firstImageAt; res.transfers = transfers; res.packets = dma3.packets;
        res.rowCycles = tr.runCycles; res.drainCycles = tr.drainCycles;
        if (tr.stopped()) res.detail.add("the Tracer stopped: " + tr.errorLog);
        if (u3.core.halt != Controller.HALT_EXIT) res.detail.add("C&C 3 didn't exit by cycle " + m.c.cycle);
        if (status != 1 && !(status == 5 && windows < 64)) res.detail.add(String.format("tr_results: status %d, windows %d, ERRORS 0x%x, advice left %d, end-state word %d", status, wins, terr, left, diff - 1));
        if (!res.ok()) return res;
        List<Object> wl = ReplayDelivery.Json.list(ReplayDelivery.Json.object(ReplayDelivery.Json.list(man.get("programs")).get(0)).get("window_entries"));
        Map<Integer, Map<String, Object>> byIndex = new java.util.HashMap<>();
        for (Object x : wl) { Map<String, Object> we = ReplayDelivery.Json.object(x); if (we.containsKey("files")) byIndex.put(((Number) we.get("index")).intValue(), we); }
        for (int t = 0; t < windows; t++) {
            int wi = 64 * s + t;
            Tile p = m.c.tiles[t];
            long[] got = TracerT2.getWords(p.sram, StageB.RECORD, 68);
            java.nio.ByteBuffer st = ReplayDelivery.le(Files.readAllBytes(pd.resolve(String.format("window_%05d.state.bin", wi))));
            if (got[0] != st.getLong(16) || (got[1] & 0xFFFFFFFFFFFFL) != (st.getLong(24) & 0xFFFFFFFFFFFFL)) res.detail.add("tile " + t + ": the delivered state word differs");
            for (int r = 1; r < 128; r++) if (got[4 + r] != st.getLong(48 + 8 * r)) { res.detail.add("tile " + t + ": delivered register " + r + " differs from state.bin"); break; }
            if (byIndex.containsKey(wi)) {
                Replayer.StreamSize[] sz = Replayer.parseStreamWord(got[2], got[3]);
                String[] f = { "code.bin", "rlog.bin", "wlog.bin" }; int[] at = { StageB.CODE, StageB.RLOG, StageB.WLOG }, bits = { 32, 64, 64 };
                for (int q = 0; q < 3; q++) {
                    long[] mine = TracerRig.streamValues(TracerT2.getWords(p.sram, at[q], sz[q].words()), sz[q].values(), bits[q]);
                    long[] ref = TracerRig.streamValues(ReplayDelivery.words(pd.resolve(String.format("window_%05d.%s", wi, f[q]))), sz[q].values(), bits[q]);
                    if (!java.util.Arrays.equals(mine, ref)) { res.detail.add("tile " + t + ": the delivered " + f[q] + " differs from the delivery's"); break; }
                }
            }
        }
        if (!res.ok()) return res;
        for (int t = 0; t < windows; t++) {
            int wi = 64 * s + t;
            Tile p = m.c.tiles[t]; Replayer rp = (Replayer) p.slot.p[1];
            res.replayMax = Math.max(res.replayMax, rp.passEndAt - rp.passStartAt);
            if (p.core.halt != Controller.HALT_EXIT || m.exitValue(t) != 1) { res.detail.add("tile " + t + ": replay_ctl " + p.core.haltReport() + " exit " + m.exitValue(t)); continue; }
            java.nio.ByteBuffer ref = ReplayDelivery.le(Files.readAllBytes(pd.resolve(String.format("window_%05d.rows.bin", wi))));
            List<long[]> l = rows.get(t);
            int n = ref.capacity() / 80;
            if (l.size() != n) { res.detail.add("tile " + t + ": " + l.size() + " rows, rows.bin " + n); continue; }
            for (int r = 0; r < n; r++) {
                long[] gr = l.get(r); boolean eq = true;
                for (int q = 0; q < 10; q++) if (gr[q] != ref.getLong(80 * r + 8 * q)) { eq = false; break; }
                if (!eq) { res.detail.add("tile " + t + " (window " + wi + "): row " + r + " differs"); break; }
            }
            res.rows += n;
            if (!rp.clean()) res.detail.add("tile " + t + ": replayer " + rp.errorReport());
        }
        if (m.c.violations > 0) res.detail.add("meshsim: " + StageB.cut(m.c.violationReport().replace("\n", "; ")));
        return res;
    }
}
