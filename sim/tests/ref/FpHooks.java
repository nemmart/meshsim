import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * WORKER7's between-cycle hooks, shared by M2Sort and M2Prog (sim/tests/AUDIT-M2.md sections 1 and 6). Called from a
 * Chip.runUntil condition, i.e. after cycle c and before c + 1; every hook only reads (proven: the digest and the cycle
 * counts are unchanged with them on). Options: fp=FILE fpevery=N fpwin=A-B fpdata=0|1 fptiles=LIST fpdump=C:UNIT,..
 * dumpto=FILE cover=FILE.
 */
/** The between-cycle hooks. Each only reads. */
final class FpHooks {
    final Chip c; final boolean[] tiles = new boolean[64];
    final long every, winA, winB; final boolean fpData; final PrintWriter fp, cover, dumpW;
    final Map<Long, List<String>> dumps = new LinkedHashMap<>();
    long fpCalls, fpNanos;
    int phasesSeen; long lastReadySeen = -1, lastGoSeen = -1;
    final List<long[]> wall = new ArrayList<>();               // {cycle, nanoTime} at every GO and all-READY
    final List<long[]> snaps = new ArrayList<>(); final List<Integer> snapPhase = new ArrayList<>();
    // the window's incremental big-SRAM hash: per tile, and the rows the area queues will write at the next cycle
    long[] sramH; final List<int[]> pendRows = new ArrayList<>();   // {tile, area, row}, old values captured below
    final List<int[]> pendOld = new ArrayList<>();

    FpHooks(Chip chip, Map<String, String> kv) throws Exception {
        c = chip;
        if (kv.containsKey("fptiles")) for (String x : kv.get("fptiles").split(",")) tiles[Integer.parseInt(x)] = true; else java.util.Arrays.fill(tiles, true);
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
        long a = System.nanoTime(); StateFp.write(fp, c, data, tiles); fpNanos += System.nanoTime() - a; fpCalls++;
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
            if (!tiles[p.idx]) continue;
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
