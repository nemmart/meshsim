import java.io.FileWriter;
import java.io.IOException;
import java.io.PrintWriter;
import java.util.concurrent.ConcurrentLinkedQueue;
import java.util.concurrent.atomic.AtomicInteger;

/**
 * WORKER28, step 1 (branch sim-idle-profile, measurement only, never merged): for every unit of a chip (the 64 tiles and,
 * with Config.ccCores, C&C 1-3), every cycle, whether it had anything to do, to size exact idle-skipping. Off unless
 * -Dmeshsim.probe=FILE; the report is appended to FILE when the JVM exits (a SIGTERM from a timeout included).
 *
 * A unit-cycle is classed at the start of the cycle (pre: its queues, buses and engines, every read one its consumers
 * make at t anyway) and after it (post: the core, by which of its own counters the cycle advanced, no extra read):
 *   IDLE    the core disabled, halted or in a WFI it did not leave, and nothing visible to the router, the engines or
 *           the peripheral (words still in flight on a delay line are counted apart: a skipping design wakes the unit at
 *           the cycle they become visible, a timer, not a flag);
 *   ROUTER  only the router (a word in its FIFOs, or visible on a link, the injection bus or the fifth port);
 *   ENGINES the engines, the memories or the peripheral, the core idle (the router either way);
 *   CORE    the core executed or stalled, or left a WFI.
 * Idle runs are histogrammed (cycles in runs of length [2^k, 2^(k+1))), and long chips get per-million-cycle bins of the
 * 64 tiles' classes. The probe changes nothing: every query it makes is idempotent at t (BusBase.enter, the replayer's and
 * the Tracer's settle), checked by running suites with and without it.
 */
final class Probe {
    static final String FILE = System.getProperty("meshsim.probe");
    static final boolean ON = FILE != null;
    static final int IDLE = 0, ROUTER = 1, ENGINES = 2, CORE = 3, NCLS = 4;
    static final int C_OFF = 0, C_HALT = 1, C_WFI = 2, C_STALL = 3, C_RUN = 4, NCORE = 5;
    static final String[] CLS = { "idle", "router", "engines", "core" }, CORES = { "disabled", "halted", "wfi", "stalled", "running" };
    static final int BIN = 1 << 20, HB = 40;
    private static final ConcurrentLinkedQueue<Probe> ALL = new ConcurrentLinkedQueue<>();
    private static final AtomicInteger SEQ = new AtomicInteger();
    static {
        if (ON) Runtime.getRuntime().addShutdownHook(new Thread(Probe::reportAll));
    }

    final Chip chip; final int seq; final String thread;
    final Tile[] units;                       // 0-63 the tiles, 64-66 C&C 1-3 (if cores)
    final long[][] cls, core;                 // [unit][class], [unit][core state]
    final long[] inFlightIdle;                // idle cycles with a word in flight towards the unit (a timed wake)
    final long[][] runHist; final long[] runs; private final long[] runLen;
    final long[] parkedCycles;
    private final boolean[] preRouter, preEng, preFlight;
    private final long[] wfi0, halt0, stall0;
    long cycles, first = -1, last;
    final java.util.ArrayList<long[]> bins = new java.util.ArrayList<>();    // per 2^20 cycles: the tiles' 4 classes

    static Probe attach(Chip c) { if (!ON) return null; Probe p = new Probe(c); ALL.add(p); return p; }

    private Probe(Chip c) {
        chip = c; seq = SEQ.incrementAndGet(); thread = Thread.currentThread().getName();
        int n = 64 + (c.cores != null ? 3 : 0);
        units = new Tile[n];
        for (int i = 0; i < 64; i++) units[i] = c.tiles[i];
        if (c.cores != null) for (int i = 0; i < 3; i++) units[64 + i] = c.cores.units[i];
        cls = new long[n][NCLS]; core = new long[n][NCORE]; inFlightIdle = new long[n];
        runHist = new long[n][HB]; runs = new long[n]; runLen = new long[n]; parkedCycles = new long[n];
        preRouter = new boolean[n]; preEng = new boolean[n]; preFlight = new boolean[n];
        wfi0 = new long[n]; halt0 = new long[n]; stall0 = new long[n];
    }

    private boolean parked(int u) { return u < 64 && chip.parked != null && chip.parked[u]; }

    /** The start of cycle t, before anything runs. */
    void pre(long t) {
        for (int u = 0; u < units.length; u++) {
            if (parked(u)) continue;
            Tile p = units[u];
            Router r = p.router;
            boolean rt = false, fl = false;
            for (WordFIFO[] fs : r.q) { for (WordFIFO f : fs) if (!f.isEmpty(t)) { rt = true; break; } if (rt) break; }
            for (WordSource s : r.linkIn) {
                if (s == null) continue;
                if (s.ready(t)) rt = true;
                else if (s instanceof WordDelayLine d && d.inFlight(t) > 0) fl = true;
            }
            if (p.injBus.ready(t)) rt = true; else if (p.injBus.inFlight(t) > 0) fl = true;
            if (r.fromCnc != null && r.fromCnc.ready(t)) rt = true;
            boolean en = !p.ag.idle() || p.tsBusy(t) || !p.agOut.isEmpty(t) || !p.srq.idle() || !p.sram.idle()
                    || !p.sramRd.isEmpty(t) || !p.sramWr.isEmpty(t) || !p.gatherOut.isEmpty(t) || !p.readPort.idle(t)
                    || !p.readReturn.isEmpty(t) || !p.sendHold.isEmpty(t) || p.dir.inFlight() > 0
                    || !p.weIn.isEmpty(t) || !p.gatherIn.isEmpty(t) || !p.ts.qStream.isEmpty(t) || !p.ts.qGroup.isEmpty(t)
                    || !p.ts.qWe.isEmpty(t) || !p.hist.q.isEmpty(t) || !p.writer.idle(t) || !p.req.idle()
                    || p.slot.inFlight(t) || p.slot.probeBusy(t) || p.ctl.goVisible(t);
            if (!en) {
                for (WordFIFO f : p.srqClient) if (!f.isEmpty(t)) { en = true; break; }
                if (p.gather.in != null && p.gather.busy(t)) en = true;
            }
            if (!en) {
                if (p.ejectBus.ready(t) || p.portB.ready(t)) en = true;
                else if (p.ejectBus.inFlight(t) > 0 || p.portB.inFlight(t) > 0) fl = true;
                for (WordDelayLine l : p.sram.ret) if (l.ready(t)) en = true; else if (l.inFlight(t) > 0) fl = true;
            }
            preRouter[u] = rt; preEng[u] = en; preFlight[u] = fl;
            Controller c = p.core; wfi0[u] = c.cyclesWfi; halt0[u] = c.cyclesHalted; stall0[u] = c.cyclesStalled;
        }
    }

    /** The end of cycle t, after every unit has run it. */
    void post(long t) {
        if (first < 0) first = t;
        last = t; cycles++;
        int b = (int) (t / BIN);
        while (bins.size() <= b) bins.add(new long[NCLS + 1]);
        long[] bin = bins.get(b);
        for (int u = 0; u < units.length; u++) {
            if (parked(u)) { parkedCycles[u]++; continue; }
            Controller c = units[u].core;
            int cs = !c.enabled ? C_OFF : c.cyclesHalted != halt0[u] ? C_HALT : c.cyclesWfi != wfi0[u] ? C_WFI
                    : c.cyclesStalled != stall0[u] ? C_STALL : C_RUN;
            core[u][cs]++;
            boolean coreIdle = cs <= C_WFI;
            int k = !coreIdle ? CORE : preEng[u] ? ENGINES : preRouter[u] ? ROUTER : IDLE;
            cls[u][k]++;
            if (u < 64) bin[k]++;
            if (k == IDLE) { runLen[u]++; if (preFlight[u]) inFlightIdle[u]++; }
            else endRun(u);
        }
        bin[NCLS]++;
    }

    private void endRun(int u) {
        long n = runLen[u]; if (n == 0) return;
        runHist[u][63 - Long.numberOfLeadingZeros(n)] += n; runs[u]++; runLen[u] = 0;
    }

    // ---------------------------------------------------------------- the report
    private static synchronized void reportAll() {
        try (PrintWriter w = new PrintWriter(new FileWriter(FILE, true))) {
            java.util.Map<String, long[][]> agg = new java.util.TreeMap<>();
            for (Probe p : ALL) {
                for (int u = 0; u < p.units.length; u++) p.endRun(u);
                String kind = p.kind();
                long[][] a = agg.computeIfAbsent(kind, z -> new long[4][]);
                a[0] = add(a[0], p.groupCls(0, 64)); a[1] = add(a[1], p.groupHist(0, 64));
                a[2] = add(a[2], new long[] { 1, p.cycles, sum(p.inFlightIdle, 0, 64), sumParked(p) });
                a[3] = add(a[3], p.groupCore(0, 64));
            }
            w.printf("# meshsim probe (WORKER28): %d chip(s) in this JVM, %s%n", ALL.size(), new java.util.Date());
            for (var e : agg.entrySet()) {
                long[][] a = e.getValue(); long chips = a[2][0], cyc = a[2][1], tc = sum(a[0], 0, NCLS);
                w.printf("AGG %s: %d chips, %d cycles; tiles (unparked) %d unit-cycles, parked %d%n", e.getKey(), chips, cyc, tc, a[2][3]);
                for (int k = 0; k < NCLS; k++) w.printf("AGG %s   %-8s %6.2f%%%n", e.getKey(), CLS[k], 100.0 * a[0][k] / Math.max(1, tc));
                w.printf("AGG %s   idle with a word in flight towards the tile %.2f%%%n", e.getKey(), 100.0 * a[2][2] / Math.max(1, tc));
                for (int k = 0; k < NCORE; k++) w.printf("AGG %s   core %-8s %6.2f%%%n", e.getKey(), CORES[k], 100.0 * a[3][k] / Math.max(1, tc));
                hist(w, "AGG " + e.getKey() + "   ", a[1]);
            }
            for (Probe p : ALL) p.report(w);
        } catch (IOException ex) { System.err.println("probe: " + ex); }
    }
    private String kind() { return chip.parked != null ? "lone" : chip.cores != null ? "full+cc" : "full"; }

    private void report(PrintWriter w) {
        String id = "CHIP " + seq + " (" + thread + ", " + kind() + ")";
        w.printf("%s: cycles %d (%d-%d)%n", id, cycles, first, last);
        if (cycles < 2_000_000) {                                  // short chips (Stage B, suites): the tile group only
            long[] g = groupCls(0, 64); long tc = sum(g, 0, NCLS);
            w.printf("%s tiles: idle %.2f%%, router %.2f%%, engines %.2f%%, core %.2f%%%n", id,
                    100.0 * g[0] / Math.max(1, tc), 100.0 * g[1] / Math.max(1, tc), 100.0 * g[2] / Math.max(1, tc), 100.0 * g[3] / Math.max(1, tc));
            return;
        }
        w.printf("%s unit, then cycles idle/router/engines/core, idle with a word in flight, core disabled/halted/wfi/stalled/running, idle runs%n", id);
        for (int u = 0; u < units.length; u++) {
            w.printf("%s U %s %d %d %d %d  %d  %d %d %d %d %d  %d%n", id, u < 64 ? "t" + u : "cc" + (u - 63),
                    cls[u][0], cls[u][1], cls[u][2], cls[u][3], inFlightIdle[u], core[u][0], core[u][1], core[u][2], core[u][3], core[u][4], runs[u]);
        }
        hist(w, id + " tiles' ", groupHist(0, 64));
        w.printf("%s bins of 2^20 cycles: start, then the 64 tiles' unit-cycles idle/router/engines/core%n", id);
        for (int b = 0; b < bins.size(); b++) { long[] x = bins.get(b); if (x[NCLS] > 0) w.printf("%s B %d %d %d %d %d%n", id, (long) b * BIN, x[0], x[1], x[2], x[3]); }
    }
    private static void hist(PrintWriter w, String pre, long[] h) {
        long tot = sum(h, 0, HB); if (tot == 0) return;
        StringBuilder sb = new StringBuilder(pre + "idle cycles by run length (2^k: share):");
        for (int k = 0; k < HB; k++) if (h[k] > 0) sb.append(String.format(" %d:%.2f%%", k, 100.0 * h[k] / tot));
        w.println(sb);
    }
    private long[] groupCls(int a, int b) { long[] s = new long[NCLS]; for (int u = a; u < b; u++) for (int k = 0; k < NCLS; k++) s[k] += cls[u][k]; return s; }
    private long[] groupCore(int a, int b) { long[] s = new long[NCORE]; for (int u = a; u < b; u++) for (int k = 0; k < NCORE; k++) s[k] += core[u][k]; return s; }
    private long[] groupHist(int a, int b) { long[] s = new long[HB]; for (int u = a; u < b; u++) for (int k = 0; k < HB; k++) s[k] += runHist[u][k]; return s; }
    private static long sumParked(Probe p) { long s = 0; for (long x : p.parkedCycles) s += x; return s; }
    private static long sum(long[] x, int a, int b) { long s = 0; for (int i = a; i < b; i++) s += x[i]; return s; }
    private static long[] add(long[] a, long[] b) { if (a == null) return b.clone(); for (int i = 0; i < a.length; i++) a[i] += b[i]; return a; }
}
