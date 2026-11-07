import java.io.FileWriter;
import java.io.PrintWriter;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * WORKER7's sort driver (sim/tests/AUDIT-M2.md): the sort exactly as SortTest.sortRun runs it (the same Machine, boot,
 * input-load stand-in, command sequence, monitor and verification, with SortTest's own classes), plus hooks that run
 * between cycles and only read: the canonical state fingerprint (StateFp), the coverage counters per sort step, and the
 * wall-clock split (setup, boot, the counted run). Compiled into ref's default package (D5); sim/ref is not changed.
 *
 *   java -cp REF_CLASSES:THIS M2Sort keys=FILE | kind=uniform n=N   [fmt=k128|kv32] [ready=8] [seed=-1]
 *        [fp=FILE] [fpevery=10000] [fpwin=A-B] [fpdata=0|1] [fpdump=CYCLE:UNIT[,UNIT..]] [dumpto=FILE]
 *        [cover=FILE] [trace=TILE,FILE]   (trace: run with -Dmeshsim.observe=on)
 *
 * fp: a fingerprint every fpevery cycles from the boot's end (cycle 0 of the sort's sequence: the boot's last cycle),
 * at the sort's GO's and all-READY's, at the end; and every cycle in [A, B] (fpwin). fpdata=0 leaves out sram.data (the
 * big SRAM's contents, the costly part) except at the interval points; in a window it is kept incrementally (the rows
 * the area queues write, read before and after each cycle) and checked against a full scan at the window's end.
 * The first output lines are the run's facts (cycles, verification) in the form the golden files record.
 */
final class M2Sort {
    static void out(String s) { System.out.println(s); }

    public static void main(String[] a) throws Exception {
        long tMain = System.nanoTime();
        long jvmStartMs = java.lang.management.ManagementFactory.getRuntimeMXBean().getStartTime();
        Map<String, String> kv = new LinkedHashMap<>();
        for (String s : a) { int e = s.indexOf('='); if (e > 0) kv.put(s.substring(0, e), s.substring(e + 1)); else kv.put(s, ""); }
        SortTest.Opts op = new SortTest.Opts();
        op.shuffle = Long.parseLong(kv.getOrDefault("seed", "-1"));
        op.readyLatency = Integer.parseInt(kv.getOrDefault("ready", "8"));
        int fmt = kv.getOrDefault("fmt", "k128").equals("kv32") ? SortTest.FMT_KV32 : SortTest.FMT_K128;
        int[][] k; String label;
        if (kv.containsKey("keys")) { k = SortTest.readKeys(Path.of(kv.get("keys"))); label = Path.of(kv.get("keys")).getFileName().toString(); }
        else { String kind = kv.getOrDefault("kind", "uniform"); int n = Integer.parseInt(kv.getOrDefault("n", "2000")); k = SortTest.keys(kind, n, 11 + kind.hashCode()); label = kind + " n=" + n; }
        out(Inputs.line(Inputs.of(SortTest.build, kv.containsKey("keys") ? Path.of(kv.get("keys")) : null)));
        long tKeys = System.nanoTime();
        SortTest.Oracle o = new SortTest.Oracle(k, fmt, false);
        long tOracle = System.nanoTime();

        // ---- setup: the chip, the boot (cycles and seconds of their own), the input
        SortTest.Machine m = new SortTest.Machine(op);
        m.c.phases.namer = SortTest::phaseName;
        long tChip = System.nanoTime();
        PrintWriter traceW = null;
        if (kv.containsKey("trace")) {
            String[] t = kv.get("trace").split(",", 2);
            traceW = new PrintWriter(new FileWriter(t[1]));
            Observer.start(traceW, Integer.parseInt(t[0]));
            TraceRun.header(m.c.cfg, m.c, Integer.parseInt(t[0]), "sort " + label, SortTest.build.resolve("sort.code.bin"), SortTest.build.resolve("sort.data.bin"),
                    m.im.sym("_data_start"), m.im.sym("_start"), List.of("M2Sort " + String.join(" ", a)));
        }
        m.boot();
        long bootCycles = m.c.cycle, tBoot = System.nanoTime();
        SortTest.loadInput(m, k, fmt);
        long tInput = System.nanoTime();

        // ---- the sequence, as SortTest.sortRun queues it (the image has no ORACLE: the real split; checked)
        if (m.im.oracle()) throw new IllegalStateException("M2Sort: an image with the oracle split is not supported");
        List<Integer> cmd = new ArrayList<>(List.of(SortTest.CMD_L1 << 24 | fmt, SortTest.CMD_SPLIT << 24, SortTest.CMD_XB << 24, SortTest.CMD_RED << 24,
                SortTest.CMD_XC << 24, SortTest.CMD_TH << 24, SortTest.CMD_SWEEP << 24, SortTest.CMD_DESC << 24, SortTest.CMD_K << 24));
        for (int l = 0; l < 5; l++) cmd.add(SortTest.CMD_K1 << 24 | l);
        for (int l = 0; l < 5; l++) cmd.add(SortTest.CMD_K2 << 24 | l);
        if (fmt == SortTest.FMT_K128) { cmd.add(SortTest.CMD_V << 24); for (int l = 0; l < 5; l++) cmd.add(SortTest.CMD_V1 << 24 | l); for (int l = 0; l < 5; l++) cmd.add(SortTest.CMD_V2 << 24 | l); }
        cmd.add(SortTest.CMD_L2 << 24);
        int barriers = 66;
        m.cc.command(LinkWord.BROADCAST, cmd.get(0)).go();
        for (int i = 1; i <= cmd.size(); i++) {
            final int prev = i - 1;
            if ((cmd.get(prev) >>> 24) == SortTest.CMD_SPLIT)
                for (int b = 0; b < barriers; b++) { m.cc.command(LinkWord.BROADCAST, SortTest.CMD_BARRIER << 24); m.atAllReady(() -> {}).go(); }
            m.cc.command(LinkWord.BROADCAST, i < cmd.size() ? cmd.get(i) : SortTest.CMD_EXIT << 24);
            m.atAllReady(() -> {}).go();
        }

        // ---- the hooks
        Hooks h = new Hooks(m, kv);
        long tRun = System.nanoTime();
        boolean ok = m.c.runUntil(() -> { m.monitor(); h.cycle(); return !m.cc.busy() && m.allStopped(); }, Long.getLong("sort.maxcycles", 400_000_000L));
        long tEnd = System.nanoTime();
        h.end();
        if (traceW != null) { TraceRun.closing(m.c, m.c.tiles[Integer.parseInt(kv.get("trace").split(",")[0])].core, "run end"); Observer.stop(); }
        if (!ok) { out("FAIL: the sequence did not finish by cycle " + m.c.cycle); m.dumpState(); System.exit(1); }
        m.c.phases.close();

        // ---- verification (SortTest.sortRun's checks)
        int bad = 0, sizeBad = 0, vBad = 0;
        for (Tile p : m.c.tiles) {
            int q = p.idx;
            if (m.peekS(q, SortTest.S_NRECV) != o.expect[q].length) sizeBad++;
            bad += SortTest.mismatches(m.c, q, SortTest.BUF_Y, o.expect[q]);
            if (fmt == SortTest.FMT_K128)
                for (int r = 0; r < o.source[q].length; r++) {
                    int sp = (int) (o.source[q][r] >>> 32), sj = (int) o.source[q][r];
                    if (p.sram.peek64(SortTest.BUF_VS + 4 * r) != SortTest.valueHalf(sp, sj, 0) || p.sram.peek64(SortTest.BUF_VS + 4 * r + 2) != SortTest.valueHalf(sp, sj, 1)) vBad++;
                }
        }
        boolean exited = m.allExited() && m.haltSummary().isEmpty() && m.c.violations == 0;
        int cond = 0; for (Tile p : m.c.tiles) cond |= p.ctl.conditions;
        List<PhaseLog.Phase> ph = m.c.phases.phases;
        int last = ph.size() - 1; while (last >= 0 && !ph.get(last).name.equals("L2")) last--;
        int first = 0; while (first < last && !ph.get(first).name.equals("L1")) first++;
        long goL1 = ph.get(first).goAt, readyL2 = ph.get(last).endAt, total = readyL2 - goL1;
        boolean pass = bad == 0 && sizeBad == 0 && vBad == 0 && exited && cond == 0;
        out(String.format("sort %s %s: TOTAL %d cycles (L1 GO at %d -> L2 all READY at %d); boot %d cycles; run end at cycle %d; phases %d",
                label, fmt == SortTest.FMT_K128 ? "k128" : "kv32", total, goL1, readyL2, bootCycles, m.c.cycle, ph.size()));
        out(String.format("verify: keys misplaced %d, receive sizes wrong %d, values wrong %d, every core exited 0 %b, violations %d, ERRORS %x: %s",
                bad, sizeBad, vBad, exited, m.c.violations, cond, pass ? "PASS" : "FAIL"));
        StringBuilder steps = new StringBuilder("steps (GO -> next GO):");
        for (int i = first; i <= last; i++) steps.append(' ').append(ph.get(i).name).append(' ').append((i < last ? ph.get(i + 1).goAt : ph.get(i).endAt) - ph.get(i).goAt);
        out(steps.toString());
        out("digest " + m.c.digest());

        // ---- the wall clock (D21's machine is recorded by the task that runs this)
        long tVerify = System.nanoTime();
        double s = 1e-9;
        long counted = h.wallAt(readyL2) - h.wallAt(goL1);
        out(String.format("timing: jvm start -> main %.3f s; key file and oracle %.3f s; chip %.3f s; boot %.3f s (%d cycles); input load %.3f s; "
                + "setup total (jvm start -> run start) %.3f s",
                mainDelay(jvmStartMs, tMain), (tOracle - tMain) * s, (tChip - tOracle) * s, (tBoot - tChip) * s, bootCycles,
                (tInput - tBoot) * s, mainDelay(jvmStartMs, tMain) + (tRun - tMain) * s));
        out(String.format("timing: the run (runUntil) %.3f s for %d cycles (%.2f us/cycle); the counted span (L1 GO -> L2 all READY) %.3f s for %d cycles (%.2f us/cycle); "
                + "verification %.3f s; hooks %s",
                (tEnd - tRun) * s, m.c.cycle - bootCycles, (tEnd - tRun) / 1e3 / (m.c.cycle - bootCycles), counted * s, total, counted / 1e3 / total,
                (tVerify - tEnd) * s, h.describe()));
        System.exit(pass ? 0 : 1);
    }
    /** Seconds from the JVM's start to main (the RuntimeMXBean's start time, in ms). */
    static double mainDelay(long jvmStartMs, long tMainNanos) {
        long nowMs = System.currentTimeMillis(), nowNs = System.nanoTime();
        double mainMs = nowMs - (nowNs - tMainNanos) / 1e6;
        return (mainMs - jvmStartMs) / 1e3;
    }

    /** The between-cycle hooks. Each only reads. */
    static final class Hooks {
        final SortTest.Machine m; final Chip c;
        final long every, winA, winB; final boolean fpData; final PrintWriter fp, cover, dumpW;
        final Map<Long, List<String>> dumps = new LinkedHashMap<>();
        long fpCalls, fpNanos;
        int phasesSeen; long lastReadySeen = -1, lastGoSeen = -1;
        final List<long[]> wall = new ArrayList<>();               // {cycle, nanoTime} at every GO and all-READY
        final List<long[]> snaps = new ArrayList<>(); final List<Integer> snapPhase = new ArrayList<>();
        // the window's incremental big-SRAM hash: per tile, and the rows the area queues will write at the next cycle
        long[] sramH; final List<int[]> pendRows = new ArrayList<>();   // {tile, area, row}, old values captured below
        final List<int[]> pendOld = new ArrayList<>();

        Hooks(SortTest.Machine m, Map<String, String> kv) throws Exception {
            this.m = m; c = m.c;
            every = Long.parseLong(kv.getOrDefault("fpevery", "10000"));
            String w = kv.get("fpwin");
            winA = w == null ? Long.MAX_VALUE : Long.parseLong(w.split("-")[0]); winB = w == null ? -1 : Long.parseLong(w.split("-")[1]);
            fpData = !"0".equals(kv.getOrDefault("fpdata", "1"));
            fp = kv.containsKey("fp") ? new PrintWriter(new FileWriter(kv.get("fp"))) : null;
            cover = kv.containsKey("cover") ? new PrintWriter(new FileWriter(kv.get("cover"))) : null;
            if (kv.containsKey("fpdump")) for (String d : kv.get("fpdump").split(";")) { String[] x = d.split(":"); dumps.put(Long.parseLong(x[0]), List.of(x[1].split(","))); }
            dumpW = dumps.isEmpty() ? null : new PrintWriter(new FileWriter(kv.getOrDefault("dumpto", "fpdump.txt")));
            phasesSeen = c.phases.phases.size();
            if (fp != null) { fp.println("# M2Sort state fingerprint v1 (sim/tests/AUDIT-M2.md section 6): cycle unit hash"); fingerprint(true); }
            if (cover != null) snap();
        }
        String describe() { return fp == null ? "off" : String.format("%d fingerprints, %.3f s in them", fpCalls, fpNanos * 1e-9); }
        long wallAt(long cycle) { for (long[] x : wall) if (x[0] == cycle) return x[1]; return 0; }

        void cycle() {
            long t = c.cycle;
            if (c.cc.lastGoAt != lastGoSeen || c.cc.lastAllReadyAt != lastReadySeen) {
                lastGoSeen = c.cc.lastGoAt; lastReadySeen = c.cc.lastAllReadyAt; wall.add(new long[] { t, System.nanoTime() });
            }
            boolean phase = c.phases.phases.size() != phasesSeen;
            if (phase) { phasesSeen = c.phases.phases.size(); if (cover != null) snap(); }
            if (dumpW != null && dumps.containsKey(t)) for (String u : dumps.get(t)) StateFp.dump(dumpW, c, u);
            if (fp == null) return;
            boolean inWin = t >= winA && t <= winB;
            if (inWin && sramH == null) startWindow();
            if (sramH != null) stepWindow();
            if (inWin) fingerprintWindow();
            else if (t % every == 0 || phase || t == lastReadySeen && lastReadySeen >= 0 && wall.get(wall.size() - 1)[0] == t) fingerprint(fpData);
            if (sramH != null && t >= winB) endWindow();
        }
        void end() {
            if (fp != null) { fingerprint(true); fp.close(); }
            if (dumpW != null) dumpW.close();
            if (cover != null) { snap(); writeCover(); cover.close(); }
        }
        void fingerprint(boolean data) {
            long a = System.nanoTime(); StateFp.write(fp, c, data); fpNanos += System.nanoTime() - a; fpCalls++;
        }

        // ---- the window: every cycle, the big SRAM's hash kept from the rows written
        void startWindow() {
            sramH = new long[64]; for (Tile p : c.tiles) sramH[p.idx] = StateFp.sram(p.sram);
            capture();
        }
        /** Before cycle t + 1: every area whose queue head is a write will serve it at t + 1 (BigSRAM.runCycle serves
         *  every non-empty area's head): note the row and its four chunks now. */
        void capture() {
            pendRows.clear(); pendOld.clear();
            for (Tile p : c.tiles) {
                BigSRAM s = p.sram;
                boolean[][] qw = (boolean[][]) StateFp.o(s, "qWrite"); int[][] row = (int[][]) StateFp.o(s, "qRow");
                int[] head = (int[]) StateFp.o(s, "qHead"), count = (int[]) StateFp.o(s, "qCount");
                for (int a = 0; a < BigSRAM.AREAS; a++) if (count[a] > 0 && qw[a][head[a]]) {
                    int base = a * BigSRAM.AREA_CHUNKS + row[a][head[a]] * 4;
                    pendRows.add(new int[] { p.idx, base });
                    pendOld.add(new int[] { s.peek(base), s.peek(base + 1), s.peek(base + 2), s.peek(base + 3) });
                }
            }
        }
        void stepWindow() {
            for (int k = 0; k < pendRows.size(); k++) {
                int tile = pendRows.get(k)[0], base = pendRows.get(k)[1]; int[] old = pendOld.get(k); BigSRAM s = c.tiles[tile].sram;
                for (int j = 0; j < 4; j++) {
                    int nv = s.peek(base + j);
                    if (nv == old[j]) continue;
                    if (old[j] != 0) sramH[tile] -= StateFp.term(StateFp.R_SRAM, base + j, old[j]);
                    if (nv != 0) sramH[tile] += StateFp.term(StateFp.R_SRAM, base + j, nv);
                }
            }
            capture();
        }
        void fingerprintWindow() {
            long a = System.nanoTime(); long t = c.cycle, all = 0x6A09E667F3BCC908L; int n = 0;
            for (Tile p : c.tiles) for (String u : StateFp.TILE_UNITS) {
                long hh;
                if (u.equals("sram.data")) { StateFp.Out o = new StateFp.Out(); o.w("data", sramH[p.idx]); hh = o.done(); }
                else { StateFp.Out o = new StateFp.Out(); StateFp.unit(o, c, p, u, t); hh = o.done(); }
                fp.printf("%d t%d.%s %016x%n", t, p.idx, u, hh); all = StateFp.fmix(all ^ hh); n++;
            }
            for (String u : StateFp.CHIP_UNITS) { StateFp.Out o = new StateFp.Out(); StateFp.chipUnit(o, c, u, t); long hh = o.done(); fp.printf("%d %s %016x%n", t, u, hh); all = StateFp.fmix(all ^ hh); n++; }
            fp.printf("%d chip %016x%n", t, StateFp.fmix(all ^ n));
            fpNanos += System.nanoTime() - a; fpCalls++;
        }
        void endWindow() {
            for (Tile p : c.tiles) if (StateFp.sram(p.sram) != sramH[p.idx]) throw new IllegalStateException("fpwin: tile " + p.idx + "'s incremental big-SRAM hash differs from a full scan at cycle " + c.cycle);
            fp.println("# fpwin: the incremental big-SRAM hashes equal a full scan at cycle " + c.cycle);
            sramH = null;
        }

        // ---- coverage: chip-wide sums of ref's own counters, snapshotted at every GO (sim/tests/AUDIT-M2.md section 1)
        static final String[] NAMES = {
            "core.instructions", "core.stallBranch", "core.stallMul", "core.stallSend", "core.stallRegLoad", "core.stallRequest", "core.cyclesWfi", "core.tableReads", "core.tableWrites",
            "core.busErrors", "core.illegals", "core.traps",
            "ctl.sends", "ctl.send32", "ctl.send64", "ctl.send128", "ctl.gos", "ctl.readies", "ctl.dones", "ctl.goWakes", "ctl.eventWakes", "ctl.traceStores", "ctl.reservedAccesses",
            "ctl.doneEarlyEvents", "ctl.counterWraps", "ctl.wakeInDone",
            "router.injected", "router.routed", "router.ejected", "router.ejectErrors", "router.routeErrors", "router.fromCncWords", "router.toCncWords",
            "inj.fromRead", "inj.fromSend", "inj.sendWaitCycles", "inj.tokenWaitCycles",
            "dir.accepted", "dir.mode0", "dir.mode1", "dir.mode2", "dir.mode3", "dir.mode4", "dir.mode5", "dir.mode6", "dir.mode7", "dir.fromControllers", "dir.unexpectedTotal",
            "dir.strays", "dir.droppedBad", "dir.undefinedSize", "dir.readPortResults", "dir.readPortOverlaps",
            "cmem.writesData", "cmem.writesProgram", "cmem.writesCmd", "cmem.writesReply", "cmem.overwrittenUnread", "cmem.unreadCommands",
            "sram.accesses", "sram.reads", "sram.writes", "sram.combined", "sram.readStallCycles", "sram.writeStallCycles",
            "srq.issuedGather", "srq.issuedAg", "srq.issuedReadPort", "srq.packets", "srq.stallCycles", "srq.windowFullCycles", "srq.sendTokenWaits", "srq.outOfRange",
            "we.taken", "we.written", "we.augmented", "we.explicit", "we.stallCycles", "we.outOfRange",
            "ag.runs", "ag.requests", "ag.stall", "ag.driftEvents", "ag.groupRunStall",
            "ts.tableReads", "ts.rmws", "ts.ctrlWritesLost", "ts.ctrlLoadsEngine", "hist.rmws", "hist.ctrlWritesLost", "hist.ctrlLoadsEngine",
            "gather.requests", "gather.reads", "gather.stallCycles", "rdport.stores", "rdport.overflows",
            "req.sends", "req.underflows", "fifo.overflows", "hold.readReturnRefusals", "hold.sendRefusals",
            "reqbus.beatsAtRoot", "reqbus.delivered", "cnc.packetsSent", "cnc.gos", "cnc.rxTaken" };
        long[] counters() {
            long[] v = new long[NAMES.length]; c.syncCounters();   // sleeping tiles' per-cycle counters (stats; behaviour-neutral, as runUntil's own finally)
            for (Tile p : c.tiles) {
                Controller k = p.core; TileControl t = p.ctl; Router r = p.router; TrafficDirector d = p.dir;
                long fo = 0; for (WordFIFO f : p.hardwareFifos) fo += f.overflowCount;
                long sr = 0, sw = 0, sc = 0; for (int a = 0; a < BigSRAM.AREAS; a++) { sr += p.sram.reads[a]; sw += p.sram.writes[a]; sc += p.sram.combined[a]; }
                long[] x = { k.instructions, k.stallBranch, k.stallMul, k.stallSend, k.stallRegLoad, k.stallRequest, k.cyclesWfi, k.tableReads, k.tableWrites, k.busErrors, k.illegals, k.traps,
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
                    p.req.sends, p.req.underflows, fo, p.readReturn.refusals, p.sendHold.refusals, 0, 0, 0, 0, 0 };
                for (int i = 0; i < v.length; i++) v[i] += x[i];
            }
            int n = NAMES.length;
            v[n - 5] = c.reqBus.beatsAtRoot; v[n - 4] = c.reqBus.delivered.size(); v[n - 3] = c.cc.packetsSent; v[n - 2] = c.cc.gos; v[n - 1] = c.cc.rxTaken;
            return v;
        }
        void snap() { snaps.add(counters()); snapPhase.add(c.phases.phases.size() - 1); }
        void writeCover() {
            // per step: the deltas between the snapshots taken at its phases' GOs (a step's barrier phases summed into it)
            Map<String, long[]> by = new LinkedHashMap<>();
            for (int i = 0; i + 1 < snaps.size(); i++) {
                int pi = snapPhase.get(i);
                String nm = pi < 0 ? "boot-end" : c.phases.phases.get(pi).name;
                long[] acc = by.computeIfAbsent(nm, x -> new long[NAMES.length]);
                for (int j = 0; j < NAMES.length; j++) acc[j] += snaps.get(i + 1)[j] - snaps.get(i)[j];
            }
            cover.print("counter,before-L1"); for (String s : by.keySet()) cover.print("," + s); cover.println(",total-from-boot-end");
            long[] base = snaps.get(0), fin = snaps.get(snaps.size() - 1);
            for (int j = 0; j < NAMES.length; j++) {
                cover.print(NAMES[j] + "," + base[j]);
                for (long[] x : by.values()) cover.print("," + x[j]);
                cover.println("," + (fin[j] - base[j]));
            }
        }
    }
}
