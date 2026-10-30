import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Random;
import java.util.Set;

/**
 * The whole machine (spec 2): 64 tiles, 256 link delay lines (every one link_latency cycles, spec 3.2), and the three
 * controllers' endpoints on tiles 0-2's fifth ports (C&C 1-3, endpoints 64-66; 4.16). C&C 1 is the behavioural model
 * {@link CnC}; C&C 2 (whose peripheral is the memory system) and C&C 3 are endpoints driven by tests. Every child runs once per cycle (in a per-cycle
 * shuffled order when shuffleSeed >= 0), then the invariants of spec 9 are checked.
 *
 * Start skew (spec 3.2): each tile has a fixed offset {@link #startSkew}[i] of 0 or +1 cycle, applied to GO and RUN
 * In M1 the tests apply it to the cycle a tile starts sending.
 */
final class Chip implements Component {
    final Config cfg;
    final Tile[] tiles = new Tile[64];
    final WordDelayLine[][] links = new WordDelayLine[64][4];   // [tile][direction]
    final int[][] linkLatency = new int[64][4];
    final int[] startSkew = new int[64];
    final Endpoint[] ccs = new Endpoint[3];                     // C&C 1-3 at tiles 0-2's fifth ports (4.16)
    final Endpoint cnc, memsub, cc3;                             // C&C 1; C&C 2 (the memory system's unit); C&C 3
    final CnC cc;                                                // C&C's sequencer and register block
    final PhaseLog phases;                                       // per-phase accounting
    final RequestBus reqBus;                                     // the request bus (3.3), run first every cycle
    final List<String> log = new ArrayList<>();                  // events worth a line in reports (controller halts)
    private final Component[] children;
    private final Random shuffle;
    long cycle;

    long violations;
    /** Invariant 15: conditions set, as first seen per tile (cleared when every tile's conditions are 0 again, i.e.
     *  after HALT). Separate from the violations: the hardware's own report, which tests read where they provoke one. */
    long conditionEvents; final List<String> conditionLog = new ArrayList<>(); private final int[] condSeen = new int[64];
    final List<String> violationMessages = new ArrayList<>();
    private final Set<String> reportedOverflows = new HashSet<>();
    private final long[][] seen = new long[64][8];               // per tile: violation counters already reported

    Chip(Config cfg) {
        this.cfg = cfg;
        if (cfg.hops == null) throw new IllegalArgumentException("Config.hops must be supplied");
        Random sk = new Random(cfg.skewSeed);
        for (int i = 0; i < 64; i++) startSkew[i] = cfg.startSkew != null ? cfg.startSkew[i] : cfg.skew ? sk.nextInt(2) : 0;
        for (int i = 0; i < 64; i++) for (int d = 0; d < 4; d++) {
            int lat = cfg.linkLatencyOverride != null ? cfg.linkLatencyOverride[i][d] : cfg.linkLatency;
            linkLatency[i][d] = lat;
            links[i][d] = new WordDelayLine("link.t" + i + "." + Router.OUT_NAME[d], lat);
        }
        for (int i = 0; i < 64; i++) { tiles[i] = new Tile(i, cfg); tiles[i].chip = this; }
        for (int i = 0; i < 64; i++) for (int d = 0; d < 4; d++) {
            tiles[i].router.linkOut[d] = links[i][d];
            tiles[neighbour(i, d)].router.linkIn[arrivalPort(d)] = links[i][d];
        }
        for (int c = 0; c < 3; c++) {
            ccs[c] = new Endpoint(LinkWord.CC1 + c, cfg.queueDepth);
            tiles[c].router.fromCnc = ccs[c].send; tiles[c].router.toCnc = ccs[c].rx;
        }
        cnc = ccs[0]; memsub = ccs[1]; cc3 = ccs[2];
        phases = new PhaseLog(this);
        reqBus = new RequestBus(this);
        for (Tile p : tiles) p.req.bus = reqBus;
        cc = new CnC(this, cnc);
        children = new Component[68];
        children[0] = cnc; children[1] = memsub; children[2] = cc3; children[67] = cc;
        for (int i = 0; i < 64; i++) children[3 + i] = tiles[i];
        shuffle = cfg.shuffleSeed >= 0 ? new Random(cfg.shuffleSeed) : null;
        if (shuffle != null) for (Tile p : tiles) p.shuffle = new Random(shuffle.nextLong());
    }

    static int neighbour(int tile, int dir) {
        return switch (dir) { case Router.TO_RIGHT -> (tile + 1) & 63; case Router.TO_LEFT -> (tile + 63) & 63; case Router.TO_DOWN -> (tile + 8) & 63; default -> (tile + 56) & 63; };
    }
    static int arrivalPort(int dir) {
        return switch (dir) { case Router.TO_RIGHT -> Router.FROM_LEFT; case Router.TO_LEFT -> Router.FROM_RIGHT; case Router.TO_DOWN -> Router.FROM_UP; default -> Router.FROM_DOWN; };
    }

    @Override public void runCycle(long t) {
        if (shuffle != null) {
            for (int i = children.length - 1; i > 0; i--) { int j = shuffle.nextInt(i + 1); Component x = children[i]; children[i] = children[j]; children[j] = x; }
        }
        reqBus.runCycle(t);                                      // first: a request store at t sees the entry it frees
        for (Component c : children) c.runCycle(t);
        boolean err = false;
        for (Tile p : tiles) { if (p.ctl.readyPulseAt == t) { cc.readyPulse(p.idx, t); phases.onReady(p.idx, t); } err |= p.ctl.errorLine(); }
        cc.errorLine(t, err);                                    // the error lines, OR'd, to C&C's error bit (6.6)
        if (err) for (Tile p : tiles) {                          // invariant 15: logged per tile and condition, not a violation
            int nw = p.ctl.conditions & ~condSeen[p.idx];
            if (nw != 0) { condSeen[p.idx] |= nw; conditionEvents++; if (conditionLog.size() < 200) conditionLog.add("cycle " + t + " tile " + p.idx + ": " + Conditions.names(nw)); }
        }
        if (!err) java.util.Arrays.fill(condSeen, 0);
        checkInvariants(t);
    }

    void log(long t, int tile, String msg) { log.add("cycle " + t + (tile >= 0 ? " tile " + tile : "") + ": " + msg); }

    /** Advance one cycle (the first call runs cycle 1). */
    void step() { runCycle(++cycle); }
    void run(long n) { for (long i = 0; i < n; i++) step(); scanOverflows(); }
    boolean runUntil(java.util.function.BooleanSupplier cond, long max) {
        try {
            for (long i = 0; i < max; i++) { if (cond.getAsBoolean()) return true; step(); }
            return cond.getAsBoolean();
        } finally { scanOverflows(); }
    }
    /** Run until nothing is in flight anywhere (or max cycles); true if quiescent. */
    boolean drain(long max) { return runUntil(this::quiescent, max); }

    void violation(long t, int tile, String msg) {
        violations++;
        if (violationMessages.size() < cfg.maxViolationMessages) violationMessages.add("cycle " + t + (tile >= 0 ? " tile " + tile : "") + ": " + msg);
    }

    private void checkInvariants(long t) {
        if ((t & 15) == 0) scanOverflows();
        for (int i = 0; i < 64; i++) {
            Tile p = tiles[i];
            long[] s = seen[i];
            if (p.router.ejectErrors != s[0]) { s[0] = p.router.ejectErrors; violation(t, i, "packet ejected at the wrong tile"); }
            if (p.router.routeErrors != s[1]) { s[1] = p.router.routeErrors; violation(t, i, "illegal forward in a route block"); }
            if (p.inj.violations != s[2]) { s[2] = p.inj.violations; violation(t, i, p.inj.lastViolation); }
            if (p.dir.violations != s[3]) { s[3] = p.dir.violations; violation(t, i, p.dir.lastViolation); }
            // invariant 2: reported once per tile and phase (the count keeps growing in the SRAM's own counter)
            if (p.sram.disjointViolations != s[4]) { s[4] = p.sram.disjointViolations; if (s[5] != p.sram.phase + 1) { s[5] = p.sram.phase + 1; violation(t, i, p.sram.lastViolation); } }
            if (p.hist.histOverflow && s[6] == 0) { s[6] = 1; violation(t, i, "a counter exceeded 17 bits (invariant 6)"); }
            if (p.ag.rotationsLate != s[7]) { if (s[7] == 0) violation(t, i, "address generator: a rotation started late (invariant 3; drift, see agReport)"); s[7] = p.ag.rotationsLate; }
            if (p.ctl.violations != s2[i][0]) { s2[i][0] = p.ctl.violations; violation(t, i, p.ctl.lastViolation); }
            if (p.cmem.violations != s2[i][1]) { s2[i][1] = p.cmem.violations; violation(t, i, p.cmem.lastViolation); }
            if (p.readPort.violations != s2[i][2]) { s2[i][2] = p.readPort.violations; violation(t, i, p.readPort.lastViolation); }
            long agv = p.ag.sizeViolations + p.ag.modeViolations + p.ag.aug128Violations + p.ag.destViolations + p.ag.alignViolations + p.ag.startsWhileRunning + p.ag.tableWraps;
            if (agv != s2[i][3]) { s2[i][3] = agv; violation(t, i, p.ag.lastViolation); }
            if (p.ctl.lineChange(t) && !inv7Checked[i]) {
                String f = packetInFlight(t);
                if (f != null) violation(t, i, "the RUN/HALT line changed with a packet in flight (invariant 7): " + f);
            }
            if (p.srq.violations != s2[i][4]) { s2[i][4] = p.srq.violations; violation(t, i, p.srq.lastViolation); }
            if (p.writer.violations != s2[i][5]) { s2[i][5] = p.writer.violations; violation(t, i, p.writer.lastViolation); }
            if (p.readReturn.refusals != 0 && !refusalReported[i]) { refusalReported[i] = true; violation(t, i, "SIMULATOR: a read return was refused by the injection point"); }
        }
        if (reqBus.violations != seenReqBus) { seenReqBus = reqBus.violations; violation(t, -1, reqBus.lastViolation); }
        if (cc.rxLost != seenRxLost) { seenRxLost = cc.rxLost; violation(t, -1, "C&C's receive FIFO was full: a packet for C&C was lost (6.6)"); }
        long ev = 0; String el = null; for (Endpoint e : ccs) { ev += e.violations; if (e.lastViolation != null) el = e.lastViolation; }
        if (ev != seenEndpoint) { seenEndpoint = ev; violation(t, -1, el); }
    }
    private long seenEndpoint, seenRxLost, seenReqBus;
    private final long[][] s2 = new long[64][6];
    private final boolean[] inv7Checked = new boolean[64];

    /** Invariant 7: a packet anywhere between an injection point and the units behind a director (null if none). */
    String packetInFlight(long t) {
        for (int i = 0; i < 64; i++) {
            Tile p = tiles[i];
            for (int d = 0; d < 4; d++) if (links[i][d].inFlight(t) > 0) return "link t" + i + "." + Router.OUT_NAME[d];
            for (WordFIFO[] fs : p.router.q) for (WordFIFO f : fs) if (!f.isEmpty(t)) return f.name;
            if (!p.sendHold.isEmpty(t) || !p.readReturn.isEmpty(t)) return "t" + i + " injection";
            if (p.injBus.inFlight(t) > 0 || p.ejectBus.inFlight(t) > 0 || p.portB.inFlight(t) > 0 || p.slot.inFlight(t)) return "t" + i + " intra-tile stage";
            if (!p.weIn.isEmpty(t) || !p.gatherIn.isEmpty(t)) return "t" + i + " engine input FIFO";
        }
        for (Endpoint e : ccs) if (!e.send.isEmpty(t) || !e.rx.isEmpty(t)) return "the fifth port of tile " + (e.address - 64);
        return null;
    }
    private final boolean[] refusalReported = new boolean[64];

    /** A phase boundary for the disjoint-buffer check on every tile (the test, until GO marks it in M4). */
    void newPhase() { for (Tile p : tiles) p.sram.newPhase(); }

    /** Invariant 1: every overflow flag (they are sticky and carry their own cycle). */
    void scanOverflows() {
        for (int i = 0; i < 64; i++)
            for (WordFIFO f : tiles[i].hardwareFifos) if (f.overflow && reportedOverflows.add(f.name)) violation(f.overflowCycle, i, "FIFO overflow " + f.name + " (invariant 1)");
        for (WordFIFO f : new WordFIFO[] { cnc.rx, memsub.rx, cc3.rx }) if (f.overflow && reportedOverflows.add(f.name)) violation(f.overflowCycle, -1, "FIFO overflow " + f.name + " (invariant 1)");
    }

    boolean quiescent() {
        long t = cycle;
        for (int i = 0; i < 64; i++) { if (!tiles[i].quiescent(t)) return false; for (int d = 0; d < 4; d++) if (links[i][d].inFlight(t) > 0) return false; }
        for (Endpoint e : ccs) if (!e.send.isEmpty(t) || !e.rx.isEmpty(t)) return false;
        return true;
    }

    /** Back door (tests): copy words into every tile's controller memory (instruction or data) at a byte address. */
    void pokeAll(int byteAddr, int[] words) { for (Tile p : tiles) for (int i = 0; i < words.length; i++) p.core.pokeLocal(byteAddr + 4 * i, words[i]); }

    /** Arm and/or start every tile's address generator in the same cycle (+ its start skew when skewed is set). */
    void pulseAll(int bits) { for (Tile p : tiles) p.pulse(bits); }

    /** Lateness and drift of the last address-generator run on every tile (invariant 3; the 2 October ruling). */
    String agReport() {
        long rotLate = 0, maxRot = 0, grpLate = 0, maxGrp = 0, stalls = 0; int lateTiles = 0, maxDrift = 0, driftGroup = -1;
        for (Tile p : tiles) {
            AddressGenerator a = p.ag;
            rotLate += a.rotationsLate; maxRot = Math.max(maxRot, a.maxRotLate); grpLate += a.groupsLatePrefetch; maxGrp = Math.max(maxGrp, a.maxGroupLate);
            stalls += a.stallCycles; if (a.rotationsLate > 0) lateTiles++;
        }
        for (int g = 0; g < AddressGenerator.MAXGROUPS; g++) {
            long lo = Long.MAX_VALUE, hi = Long.MIN_VALUE;
            for (Tile p : tiles) { long v = p.ag.groupStart[g]; if (v < 0) continue; lo = Math.min(lo, v); hi = Math.max(hi, v); }
            if (hi >= lo && hi - lo > maxDrift) { maxDrift = (int) (hi - lo); driftGroup = g; }
        }
        return String.format("    address generator: %d request stall cycles; rotations late %d on %d tiles (max %d cycles); group starts waiting for read-ahead %d (max %d, timetable); drift between tiles at a group start %d%s%n",
                stalls, rotLate, lateTiles, maxRot, grpLate, maxGrp, maxDrift, driftGroup >= 0 ? " (group " + driftGroup + ")" : "");
    }

    /** The big SRAM and its read and write queues, max over tiles: area queue peaks, stalls, read latency (for reports). */
    String memReport() {
        StringBuilder sb = new StringBuilder();
        int qp = 0, qpAt = -1; long rdSt = 0, wrSt = 0, srqSt = 0, wSt = 0, lat = 0, acc = 0; int weInPk = 0, ejPk = 0;
        for (Tile p : tiles) {
            for (int a = 0; a < BigSRAM.AREAS; a++) if (p.sram.queuePeak[a] > qp) { qp = p.sram.queuePeak[a]; qpAt = p.idx; }
            rdSt += p.sram.readStallCycles; wrSt += p.sram.writeStallCycles; srqSt += p.srq.stallCycles; wSt += p.writer.stallCycles; acc += p.sram.accesses;
            for (long m : p.srq.latMax) lat = Math.max(lat, m);
            weInPk = Math.max(weInPk, p.weIn.highWater);
            for (WordFIFO f : p.router.q[Router.EJECT]) ejPk = Math.max(ejPk, f.highWater);
        }
        sb.append(String.format("    area queue peak %d of %d (t%d); SRAM accesses %d; SRAM read queue stalled %d cycles, SRAM write queue stalled %d cycles; read latency max %d%n",
                qp, cfg.areaQueueDepth, qpAt, acc, srqSt, wSt, lat));
        long rdSq = 0; for (Tile p : tiles) rdSq += p.sram.readStallCycles;
        if (wSt > 0 || rdSq > 0) {
            String first = null;
            for (String v : violationMessages) if (v.contains("overflow")) { first = v; break; }
            sb.append(String.format("    FINDING: an area was asked for more than one access a cycle: the SRAM read queue waited %d cycles at a full area queue, the SRAM write queue %d; write-engine input peak %d of %d, eject FIFO peak %d of %d; %s%n",
                    rdSq, wSt, weInPk, cfg.weInDepth, ejPk, cfg.queueDepth, first == null ? "no FIFO overflowed" : "first overflow: " + first));
        }
        return sb.toString();
    }

    void resetStats() { for (Tile p : tiles) p.resetStats(cycle); for (Endpoint e : ccs) e.rx.resetStats(cycle); }

    String violationReport() {
        scanOverflows();
        StringBuilder sb = new StringBuilder("violations: " + violations + "\n");
        for (String s : violationMessages) sb.append("  ").append(s).append("\n");
        return sb.toString();
    }

    /** Peak occupancy of every router FIFO class, max over tiles (for reports). */
    String queuePeaks() {
        scanOverflows();
        StringBuilder sb = new StringBuilder();
        for (int o = 0; o < 6; o++) for (int port = 0; port < 6; port++) {
            int best = -1, at = -1;
            for (Tile p : tiles) { Router r = p.router; int f = o < r.nOut ? r.slotOfPort(o, port) : -1; if (f >= 0 && r.q[o][f].highWater > best) { best = r.q[o][f].highWater; at = p.idx; } }
            if (best > 0) sb.append(String.format("    %-22s %3d  (t%d)%n", Router.OUT_NAME[o] + "<" + Router.PORT_NAME[port], best, at));
        }
        int b = 0, at = -1; for (Tile p : tiles) if (p.weIn.highWater > b) { b = p.weIn.highWater; at = p.idx; }
        if (b > 0) sb.append(String.format("    %-22s %3d  (t%d)%n", "write-engine input", b, at));
        return sb.toString();
    }
}
