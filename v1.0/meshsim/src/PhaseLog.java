import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.function.IntFunction;

/**
 * Per-phase accounting for a whole run. Each C&C GO store opens a phase, named from the command word C&C most
 * recently queued for slot 0 through {@link #namer} (a driver supplies the table; the default is the command type).
 * A phase ends when C&C sees all 64 READY bits for the next GO, or at {@link #close}. For each phase it records:
 *   cycles: GO store -> all 64 READY seen by C&C; per tile, its READY pulse minus the cycle it saw GO (spread, slowest);
 *   controller: instructions, WFI cycles, send-port stall cycles (sums over the 64 tiles);
 *   engines: address-generator requests, request stalls, late rotations/groups, runs; gather requests; read-port reads;
 *            read-queue latency per client (mean, max);
 *   utilization: injected and ejected packets per tile-cycle;
 *   peaks (reset at every GO; the whole run's are kept too): link, eject, to-C&C FIFOs, write-engine input, GATHER
 *            queue, area queues, PTR and HIST request FIFOs, the AG's table-read FIFO, read-port entries in use;
 *   C&C: all-READY seen -> this GO store;
 *   the slowest tile's own controller: its instructions and its busy cycles (instructions + stall cycles: taken
 *            branches, multiplies, send retries) in the phase, and every tile's busy cycles (one field, ';'-separated),
 *            so a cycle change can be traced to the code that made it (PhaseNeutral);
 *   invariant 8 as a statistic: mode-7 words (and read-port results) overwritten before the core read them.
 * Counters are sums of deltas over tiles; peaks are maxima over tiles.
 */
final class PhaseLog {
    final Chip c;
    IntFunction<String> namer = w -> w < 0 ? "-" : String.format("cmd %02X", w >>> 24);
    final List<Phase> phases = new ArrayList<>();
    private Phase open;
    private long[] base;
    private long[] baseInstr = new long[64], baseBusy = new long[64];
    final Map<String, Integer> runPeak = new LinkedHashMap<>();
    private long lastAllReadySeen = -1;

    static final String[] PEAKS = { "link", "eject", "toCnC", "weIn", "gatherQ", "area", "ptrReq", "histReq", "agTable", "readPort" };
    static final String[] COUNTS = { "instr", "wfiCycles", "sendStalls", "agReq", "agStalls", "agLate", "agRuns", "gatherReq",
            "rpReads", "injected", "ejected", "latSumG", "latNG", "latSumA", "latNA", "latSumR", "latNR", "unreadOverwrites" };

    static final class Phase {
        int index, word; String name;
        long goAt, endAt = -1, cncReaction = -1;
        final long[] tileGo = new long[64], tileReady = new long[64];
        final Map<String, Long> n = new LinkedHashMap<>();
        final Map<String, Integer> peak = new LinkedHashMap<>();
        final long[] latMax = new long[3];
        final long[] tileInstr = new long[64], tileBusy = new long[64];      // Per-tile deltas over the phase
        long cycles() { return endAt - goAt; }
    }

    PhaseLog(Chip c) { this.c = c; }

    // ---------------------------------------------------------------- hooks (CnC, Chip)
    void onGo(long t, int word) {
        if (open != null) finish(open, open.endAt >= 0 ? open.endAt : t);
        Phase p = new Phase();
        p.index = phases.size(); p.word = word; p.name = namer.apply(word); p.goAt = t;
        if (lastAllReadySeen >= 0) p.cncReaction = t - lastAllReadySeen;
        for (int i = 0; i < 64; i++) { p.tileGo[i] = t + c.cfg.goLatency + c.startSkew[i]; p.tileReady[i] = -1; }
        phases.add(p); open = p;
        base = counters();
        for (Tile x : c.tiles) { baseInstr[x.idx] = x.core.instructions; baseBusy[x.idx] = x.core.instructions + x.core.cyclesStalled; }
        resetPeaks();
    }
    void onAllReady(long t) { lastAllReadySeen = t; if (open != null && open.endAt < 0) open.endAt = t; }
    void onReady(int tile, long t) { if (open != null && open.tileReady[tile] < 0 && t >= open.tileGo[tile]) open.tileReady[tile] = t; }
    /** End the open phase at the run's end (its last READY as seen by C&C, else now). */
    void close() {
        if (open == null) return;
        long end = open.endAt;
        if (end < 0) { long m = -1; for (long r : open.tileReady) m = Math.max(m, r); end = m >= 0 ? m + c.cfg.readyLatency : c.cycle; }
        finish(open, end);
        open = null;
    }

    private void finish(Phase p, long end) {
        p.endAt = end;
        long[] now = counters();
        for (int i = 0; i < COUNTS.length; i++) p.n.put(COUNTS[i], now[i] - base[i]);
        int[] pk = peaks();
        for (int i = 0; i < PEAKS.length; i++) { p.peak.put(PEAKS[i], pk[i]); runPeak.merge(PEAKS[i], pk[i], Math::max); }
        for (Tile t : c.tiles) for (int k = 0; k < 3; k++) p.latMax[k] = Math.max(p.latMax[k], t.srq.latMax[k]);
        for (Tile x : c.tiles) { p.tileInstr[x.idx] = x.core.instructions - baseInstr[x.idx]; p.tileBusy[x.idx] = x.core.instructions + x.core.cyclesStalled - baseBusy[x.idx]; }
        base = now;
    }

    // ---------------------------------------------------------------- counters and peaks
    private long[] counters() {
        long[] v = new long[COUNTS.length];
        for (Tile t : c.tiles) {
            Controller k = t.core;
            v[0] += k.instructions; v[1] += k.cyclesWfi; v[2] += k.stallSend;
            v[3] += t.ag.totRequests(); v[4] += t.ag.totStall(); v[5] += t.ag.totLate(); v[6] += t.ag.runs;
            v[7] += t.gather.requests; v[8] += t.readPort.issuedReads;
            v[9] += t.inj.injected; v[10] += t.router.ejected;
            v[11] += t.srq.latSum[SRAMReadQueue.GATHER]; v[12] += t.srq.latN[SRAMReadQueue.GATHER];
            v[13] += t.srq.latSum[SRAMReadQueue.AG]; v[14] += t.srq.latN[SRAMReadQueue.AG];
            v[15] += t.srq.latSum[SRAMReadQueue.READPORT]; v[16] += t.srq.latN[SRAMReadQueue.READPORT];
            v[17] += t.cmem.overwrittenUnread;
        }
        return v;
    }
    private int[] peaks() {
        int[] pk = new int[PEAKS.length];
        for (Tile t : c.tiles) {
            for (int o = 0; o < 4; o++) for (WordFIFO f : t.router.q[o]) pk[0] = Math.max(pk[0], f.highWater);
            for (WordFIFO f : t.router.q[Router.EJECT]) pk[1] = Math.max(pk[1], f.highWater);
            if (t.router.q.length > Router.TO_CNC) for (WordFIFO f : t.router.q[Router.TO_CNC]) pk[2] = Math.max(pk[2], f.highWater);
            pk[3] = Math.max(pk[3], t.weIn.highWater); pk[4] = Math.max(pk[4], t.gatherIn.highWater);
            for (int a : t.sram.queuePeak) pk[5] = Math.max(pk[5], a);
            pk[6] = Math.max(pk[6], t.ts.qWe.highWater); pk[7] = Math.max(pk[7], t.hist.q.highWater); pk[8] = Math.max(pk[8], Math.max(t.ts.qStream.highWater, t.ts.qGroup.highWater));
            pk[9] = Math.max(pk[9], t.readPort.peak);
        }
        return pk;
    }
    private void resetPeaks() {
        for (Tile t : c.tiles) {
            for (WordFIFO[] fs : t.router.q) for (WordFIFO f : fs) f.resetPeak();
            t.weIn.resetPeak(); t.gatherIn.resetPeak(); t.ts.qWe.resetPeak(); t.hist.q.resetPeak(); t.ts.qStream.resetPeak(); t.ts.qGroup.resetPeak();
            t.sram.resetQueuePeaks();
            t.readPort.peak = t.readPort.inUse();
            java.util.Arrays.fill(t.srq.latMax, 0);
        }
    }

    // ---------------------------------------------------------------- output
    static double mean(long s, long n) { return n == 0 ? 0 : (double) s / n; }

    String report() {
        StringBuilder sb = new StringBuilder();
        sb.append("  #  phase              GO at    cycles  READY spread (slowest)  instr/tile  WFI%   AG req/tile  late  gather/tile  rd-port/tile  AG read lat (max)  inj%  ej%   peaks: link ej toCnC weIn gQ area ptr hist rdPort  C&C\n");
        long tot = 0;
        for (Phase p : phases) {
            if (p.endAt < 0) continue;
            long cyc = p.cycles(); tot += cyc;
            long lo = Long.MAX_VALUE, hi = -1; int slow = -1;
            for (int i = 0; i < 64; i++) { if (p.tileReady[i] < 0) continue; long d = p.tileReady[i] - p.tileGo[i]; if (d < lo) lo = d; if (d > hi) { hi = d; slow = i; } }
            double tc = 64.0 * Math.max(1, cyc);
            sb.append(String.format("  %-2d %-16s %8d %9d  %6s (t%-2s)         %9.0f  %4.0f  %11.0f  %4d  %11.0f  %12.0f  %8.1f (%4d)  %4.0f  %3.0f   %9d %2d %5d %4d %2d %4d %3d %3d %6d  %s%n",
                    p.index, p.name, p.goAt, cyc, hi >= 0 ? String.valueOf(hi - lo) : "-", slow >= 0 ? String.valueOf(slow) : "-",
                    p.n.get("instr") / 64.0, 100.0 * p.n.get("wfiCycles") / tc, p.n.get("agReq") / 64.0, p.n.get("agLate"),
                    p.n.get("gatherReq") / 64.0, p.n.get("rpReads") / 64.0, mean(p.n.get("latSumA"), p.n.get("latNA")), p.latMax[SRAMReadQueue.AG],
                    100.0 * p.n.get("injected") / tc, 100.0 * p.n.get("ejected") / tc,
                    p.peak.get("link"), p.peak.get("eject"), p.peak.get("toCnC"), p.peak.get("weIn"), p.peak.get("gatherQ"), p.peak.get("area"),
                    p.peak.get("ptrReq"), p.peak.get("histReq"), p.peak.get("readPort"), p.cncReaction >= 0 ? p.cncReaction + "" : "-"));
        }
        sb.append(String.format("  total GO -> all READY over %d phases: %,d cycles; whole-run peaks: %s%n", phases.size(), tot, runPeak));
        return sb.toString();
    }

    void csv(Path f) throws IOException {
        StringBuilder sb = new StringBuilder("phase,name,word,go_at,end_at,cycles,ready_spread,slowest_tile,cnc_reaction");
        for (String k : COUNTS) sb.append(',').append(k);
        for (String k : PEAKS) sb.append(",peak_").append(k);
        sb.append(",latmax_gather,latmax_ag,latmax_readport,slow_instr,slow_busy,tile_busy\n");
        for (Phase p : phases) {
            if (p.endAt < 0) continue;
            long lo = Long.MAX_VALUE, hi = -1; int slow = -1;
            for (int i = 0; i < 64; i++) { if (p.tileReady[i] < 0) continue; long d = p.tileReady[i] - p.tileGo[i]; lo = Math.min(lo, d); if (d > hi) { hi = d; slow = i; } }
            sb.append(p.index).append(',').append(p.name).append(',').append(String.format("0x%08X", p.word)).append(',').append(p.goAt).append(',').append(p.endAt).append(',').append(p.cycles())
              .append(',').append(hi >= 0 ? hi - lo : -1).append(',').append(slow).append(',').append(p.cncReaction);
            for (String k : COUNTS) sb.append(',').append(p.n.get(k));
            for (String k : PEAKS) sb.append(',').append(p.peak.get(k));
            sb.append(',').append(p.latMax[0]).append(',').append(p.latMax[1]).append(',').append(p.latMax[2]);
            sb.append(',').append(slow >= 0 ? p.tileInstr[slow] : -1).append(',').append(slow >= 0 ? p.tileBusy[slow] : -1).append(',');
            for (int i = 0; i < 64; i++) sb.append(i > 0 ? ";" : "").append(p.tileBusy[i]);
            sb.append('\n');
        }
        Files.writeString(f, sb.toString());
    }
}
