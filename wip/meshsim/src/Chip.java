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
    final Endpoint cnc, memsub, cc3;
    final Cnc1Peripheral cc1p;
    /** C&C 1-3 as cores (Config.ccCores, WORKER14); null: the Java model ({@link #cc}) drives the chip. */
    final CcCores cores;
    /** Item S1's measurement (Config.measureS1; spec 4.8, invariant 24). */
    final S1 s1 = new S1();
    private long s1LateSeen;
    private final java.util.List<BusBase> buses;                 // every bus the chip has (the reset empties them)
    private int resetRequest;                                    // 0 none, 1 soft, 2 hard
    long resets, lastResetAt = -1;                                   // C&C 1's peripheral 1: READY, GO, the error lines, the watchdog, the UART (8.14)
    private final boolean[] errSeen = new boolean[64];                             // C&C 1; C&C 2 (the memory system's unit); C&C 3
    final CnC cc;                                                // C&C's sequencer and register block
    final PhaseLog phases;                                       // per-phase accounting
    final RequestBus reqBus;                                     // the request bus (3.3), run first every cycle
    final List<String> log = new ArrayList<>();                  // events worth a line in reports (controller halts)
    private final Component[] children;
    /**
     * Tiles not simulated (null: none), for a run that provably never involves them: WORKER11's Stage B option
     * (-Dstageb.lone), where tile 0 replays from its own big SRAM and the other 63 tiles only wait in their boot ROMs.
     * A parked tile's components don't run; the caller must check that nothing reaches it (StageB compares the result
     * with the full chip). Off by default; nothing else uses it.
     */
    boolean[] parked;
    /** For a run with parked tiles: true if nothing ever reached them (each quiescent, no link into or out of it busy). */
    boolean parkedUntouched() {
        if (parked == null) return true;
        long t = cycle;
        for (int i = 0; i < 64; i++) {
            if (!parked[i]) continue;
            if (!tiles[i].quiescent(t)) return false;
            for (int d = 0; d < 4; d++) if (links[i][d].inFlight(t) > 0) return false;
        }
        for (int i = 0; i < 64; i++) for (int d = 0; d < 4; d++) if (links[i][d].inFlight(t) > 0) return false;
        return true;
    }
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
        java.util.List<BusBase> reg = new java.util.ArrayList<>(); BusBase.REGISTRY.set(reg); buses = reg;
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
        cc1p = new Cnc1Peripheral(cfg);
        cc1p.attach((t, m) -> violation(t, -1, "C&C 1's peripheral: " + m));
        cc1p.expect = -1L;                                       // the model waits for all 64 tiles (READY_EXPECT_0-1)
        cnc = ccs[0]; memsub = ccs[1]; cc3 = ccs[2];
        phases = new PhaseLog(this);
        reqBus = new RequestBus(this);
        for (Tile p : tiles) p.req.bus = reqBus;
        cc = new CnC(this, cnc);
        cc1p.goNet = cc::goNet;
        if (cfg.measureS1) for (Tile p : tiles) p.s1Attach(s1);
        skip = cfg.idleSkip;
        for (int i = 0; i < 64; i++) unit[i] = tiles[i];
        for (int i = 0; i < 64; i++) tiles[i].wakeOnOwnBuses(this);
        for (int i = 0; i < 64; i++) for (int d = 0; d < 4; d++) { links[i][d].wakeChip = this; links[i][d].wakeUnit = neighbour(i, d); }
        for (int c = 0; c < 3; c++) { ccs[c].send.wakeChip = this; ccs[c].send.wakeUnit = c; }
        cores = cfg.ccCores ? new CcCores(this) : null;
        if (cores != null) for (int c = 0; c < 3; c++) { unit[64 + c] = cores.units[c]; cores.units[c].wakeOnOwnBuses(this); }          // WORKER14: built before the registry closes (the reset flushes its buses)
        BusBase.REGISTRY.remove();
        children = new Component[cores != null ? 69 : 68];
        if (cores != null) children[68] = cores;
        children[0] = cnc; children[1] = memsub; children[2] = cc3; children[67] = cc;
        for (int i = 0; i < 64; i++) children[3 + i] = tiles[i];
        shuffle = cfg.shuffleSeed >= 0 ? new Random(cfg.shuffleSeed) : null;
        if (shuffle != null) for (Tile p : tiles) p.shuffle = new Random(shuffle.nextLong());
        if (DIGEST != null) digestRegister(this);
    }

    static int neighbour(int tile, int dir) {
        return switch (dir) { case Router.TO_RIGHT -> (tile + 1) & 63; case Router.TO_LEFT -> (tile + 63) & 63; case Router.TO_DOWN -> (tile + 8) & 63; default -> (tile + 56) & 63; };
    }
    static int arrivalPort(int dir) {
        return switch (dir) { case Router.TO_RIGHT -> Router.FROM_LEFT; case Router.TO_LEFT -> Router.FROM_RIGHT; case Router.TO_DOWN -> Router.FROM_UP; default -> Router.FROM_DOWN; };
    }

    // ================================================================ stage 1 (WORKER28): sleeping units
    /** Stage 1 on (Config.idleSkip): units 0-63 (the tiles) and 64-66 (C&C 1-3, with cores) sleep when nothing can change
     *  in them and are not stepped; a put on a bus into them, a GO, a READY or error line, a request delivered, wakes
     *  them at the cycle it becomes visible. Their per-cycle counters are caught up on waking (and by syncCounters). */
    final boolean skip;
    final Tile[] unit = new Tile[67];
    final boolean[] asleep = new boolean[67];
    private final long[] countedTo = new long[67];
    private static final int RING = 512;
    private final long[] ringLo = new long[RING];
    private final int[] ringHi = new int[RING];
    private final java.util.PriorityQueue<long[]> far = new java.util.PriorityQueue<>((x, y) -> Long.compare(x[0], y[0]));
    long sleeps, wakes;
    private int errHighN;
    /** Unit u must step at cycle at (a no-op unless it sleeps). Called during a cycle (at > cycle) or between cycles. */
    void wake(int u, long at) {
        if (!skip || !asleep[u]) return;
        if (at <= cycle) at = cycle + 1;
        if (at - cycle >= RING) { far.add(new long[] { at, u }); return; }
        int b = (int) (at & (RING - 1));
        if (u < 64) ringLo[b] |= 1L << u; else ringHi[b] |= 1 << (u - 64);
    }
    private void wakeDue(long t) {
        int b = (int) (t & (RING - 1));
        long m = ringLo[b];
        if (m != 0) { ringLo[b] = 0; while (m != 0) { int i = Long.numberOfTrailingZeros(m); m &= m - 1; awaken(i, t); } }
        int h = ringHi[b];
        if (h != 0) { ringHi[b] = 0; for (int i = 0; i < 3; i++) if ((h >> i & 1) != 0) awaken(64 + i, t); }
        while (!far.isEmpty() && far.peek()[0] <= t) awaken((int) far.poll()[1], t);
    }
    private void awaken(int u, long t) { if (!asleep[u]) return; asleep[u] = false; wakes++; catchUp(u, t - 1); }
    private void catchUp(int u, long upTo) { long n = upTo - countedTo[u]; if (n > 0) unit[u].core.skipCycles(n); countedTo[u] = upTo; }
    private void sleepCheck(long t) {
        for (int u = 0; u < 67; u++) {
            Tile p = unit[u];
            if (p == null || asleep[u] || u < 64 && parked != null && parked[u] || !p.canSleep(t)) continue;
            asleep[u] = true; countedTo[u] = t; sleeps++;
            long w = p.sleepWakeAt(t);
            if (w != Long.MAX_VALUE) wake(u, w);
        }
    }
    // ---- the proof's fingerprint (-Dmeshsim.digest=FILE): at exit, per chip in creation order, every unit's counters
    private static final String DIGEST = System.getProperty("meshsim.digest");
    // A thread's chips are used one after another, so a chip is fingerprinted when its thread makes the next one (and the
    // rest at exit) and then dropped: holding every chip to the end ran Stage B's JVMs out of heap (114).
    private static final java.util.Map<Thread, Chip> DIGEST_CURRENT = new java.util.HashMap<>();
    private static final java.util.TreeMap<Integer, String> DIGEST_DONE = new java.util.TreeMap<>();
    private static int digestNext;
    private int digestSeq;
    static { if (DIGEST != null) Runtime.getRuntime().addShutdownHook(new Thread(Chip::writeDigests)); }
    private static synchronized void digestRegister(Chip c) {
        c.digestSeq = digestNext++;
        Chip prev = DIGEST_CURRENT.put(Thread.currentThread(), c);
        if (prev != null) DIGEST_DONE.put(prev.digestSeq, prev.digest());
    }
    private static synchronized void writeDigests() {
        for (Chip c : DIGEST_CURRENT.values()) DIGEST_DONE.put(c.digestSeq, c.digest());
        try (java.io.PrintWriter w = new java.io.PrintWriter(new java.io.FileWriter(DIGEST, true))) {
            for (var e : DIGEST_DONE.entrySet()) w.println("chip " + e.getKey() + " " + e.getValue());
        } catch (java.io.IOException e) { System.err.println("digest: " + e); }
    }
    /** Every unit's counters and state summary at the current cycle, hashed (and the totals in clear). */
    String digest() {
        syncCounters();
        long h = 1125899906842597L, mc = 0, wfi = 0, ins = 0;
        java.util.List<Tile> all = new java.util.ArrayList<>(java.util.Arrays.asList(tiles));
        if (cores != null) all.addAll(java.util.Arrays.asList(cores.units));
        for (Tile p : all) {
            Controller k = p.core;
            long[] v = { k.mcycle, k.cyclesWfi, k.cyclesHalted, k.cyclesStalled, k.instructions, k.pc, k.halt, k.profCycles,
                    p.router.injected, p.router.routed, p.router.ejected, p.dir.accepted, p.sram.accesses, p.ctl.gos, p.ctl.readies,
                    p.ctl.dones, p.ctl.conditions, p.writer.stallCycles, p.srq.stallCycles, p.req.sends };
            for (long x : v) h = 31 * h + x;
            mc += k.mcycle; wfi += k.cyclesWfi; ins += k.instructions;
        }
        h = 31 * h + violations; h = 31 * h + log.size(); h = 31 * h + conditionEvents;
        return String.format("cycle %d units %d mcycle %d wfi %d instructions %d violations %d log %d hash %016x", cycle, all.size(), mc, wfi, ins, violations, log.size(), h);
    }

    /** Every sleeping unit's per-cycle counters brought up to the current cycle (before anything outside reads them). */
    void syncCounters() { if (skip) for (int u = 0; u < 67; u++) if (asleep[u]) catchUp(u, cycle); }
    /** Unit u awake from now (the harness reaches into it directly: a load, a store, a pulse). */
    void wakeNow(int u) { if (asleep[u]) { catchUp(u, cycle); asleep[u] = false; } }
    /** Every unit awake from the next cycle (the harness changed something directly). */
    void wakeAll() { if (skip) for (int u = 0; u < 67; u++) if (asleep[u]) { catchUp(u, cycle); asleep[u] = false; } }

    @Override public void runCycle(long t) {
        if (skip) wakeDue(t);
        if (shuffle != null) {
            for (int i = children.length - 1; i > 0; i--) { int j = shuffle.nextInt(i + 1); Component x = children[i]; children[i] = children[j]; children[j] = x; }
        }
        reqBus.runCycle(t);                                      // first: a request store at t sees the entry it frees
        if (skip) { for (Component c : children) if (!(c instanceof Tile p && (asleep[p.idx] || parked != null && parked[p.idx]))) c.runCycle(t); }
        else if (parked == null) for (Component c : children) c.runCycle(t);
        else for (Component c : children) if (!(c instanceof Tile p && parked[p.idx])) c.runCycle(t);
        boolean err = false;
        for (Tile p : tiles) {
            if (skip && (asleep[p.idx] || parked != null && parked[p.idx])) continue;   // a sleeping or parked tile's lines are as it left them
            if (p.ctl.readyPulseAt == t) { cc.readyPulse(p.idx, t); phases.onReady(p.idx, t); }
            boolean e = p.ctl.errorLine(); err |= e;
            if (e != errSeen[p.idx]) { errSeen[p.idx] = e; errHighN += e ? 1 : -1; cc1p.errorLine(t, p.idx, e); }   // ERROR_LINES (8.14)
        }
        if (skip) err = errHighN > 0;
        if (cores != null) cores.endCycle(t); else cc1p.runCycle(t);   // with cores, C&C 1's slot runs its peripheral
        if (s1.late != s1LateSeen) { s1LateSeen = s1.late; violation(t, -1, s1.lastLate); }
        if (resetRequest != 0) doReset(t);
        cc.errorLine(t, err);                                    // the error lines, OR'd, to C&C's error bit (6.6)
        if (err) for (Tile p : tiles) {                          // invariant 15 (v1.2: a check): every ERRORS stays 0
            int nw = p.ctl.errorsAt(t) & ~condSeen[p.idx];
            if (nw != 0) {
                condSeen[p.idx] |= nw; conditionEvents++;
                if (conditionLog.size() < 200) conditionLog.add("cycle " + t + " tile " + p.idx + ": " + Conditions.names(nw));
                if (cfg.checkErrors) violation(t, p.idx, "ERRORS [" + Integer.toBinaryString(nw) + "] set: " + Conditions.names(nw) + " (invariant 15)");
            }
        }
        if (!err) java.util.Arrays.fill(condSeen, 0);
        checkInvariants(t);
        if (skip) sleepCheck(t);
    }

    void log(long t, int tile, String msg) { log.add("cycle " + t + (tile >= 0 ? " tile " + tile : "") + ": " + msg); }

    /** Advance one cycle (the first call runs cycle 1). */
    void step() { runCycle(++cycle); }
    /** run and runUntil wake every unit first: the harness may have changed any of them directly between cycles (a
     *  field poked, a condition raised), which no bus or line reports; a unit with nothing to do sleeps again after one
     *  cycle. Stepping a unit with nothing to do is exact either way. */
    void run(long n) { wakeAll(); for (long i = 0; i < n; i++) step(); scanOverflows(); syncCounters(); }
    boolean runUntil(java.util.function.BooleanSupplier cond, long max) {
        wakeAll();
        try {
            for (long i = 0; i < max; i++) { if (cond.getAsBoolean()) return true; step(); }
            return cond.getAsBoolean();
        } finally { scanOverflows(); syncCounters(); }
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
            if (skip && (asleep[i] || parked != null && parked[i])) continue;   // its counters move only when it steps
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
            if (p.srq.violations != s2[i][4]) { s2[i][4] = p.srq.violations; violation(t, i, p.srq.lastViolation); }
            if (p.writer.violations != s2[i][5]) { s2[i][5] = p.writer.violations; violation(t, i, p.writer.lastViolation); }
            if (p.readReturn.refusals != 0 && !refusalReported[i]) { refusalReported[i] = true; violation(t, i, "SIMULATOR: a read return was refused by the injection point"); }
        }
        if (cores != null) cores.check(t);
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
        if (cores != null) cores.scanOverflows(reportedOverflows);
    }

    boolean quiescent() {
        long t = cycle;
        for (int i = 0; i < 64; i++) { if (!tiles[i].quiescent(t)) return false; for (int d = 0; d < 4; d++) if (links[i][d].inFlight(t) > 0) return false; }
        for (Endpoint e : ccs) if (!e.send.isEmpty(t) || !e.rx.isEmpty(t)) return false;
        return cores == null || cores.quiescent(t);
    }

    /** Back door (tests): copy words into every tile's controller memory (instruction or data) at a byte address. */
    void pokeAll(int byteAddr, int[] words) {
        wakeAll(); for (Tile p : tiles) for (int i = 0; i < words.length; i++) p.core.pokeLocal(byteAddr + 4 * i, words[i]); }

    /** Arm and/or start every tile's address generator in the same cycle (+ its start skew when skewed is set). */
    void pulseAll(int bits) { wakeAll(); for (Tile p : tiles) p.pulse(bits); }

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

    /** The outside world's reset (6.1), taking effect at the end of the current cycle: hard (everything) or soft (the
     *  LVDS, AXI and LPDDR side kept: nothing meshsim models). Every unit is reset as at power-on except the memories. */
    void reset(boolean hard) { resetRequest = Math.max(resetRequest, hard ? 2 : 1); }
    private void doReset(long t) {
        if (skip) for (int u = 0; u < 67; u++) if (asleep[u]) { catchUp(u, t); asleep[u] = false; }
        errHighN = 0;
        for (BusBase b : buses) b.flush();
        for (Tile p : tiles) p.reset(t);
        reqBus.reset(); cc1p.reset(t); cc.resetModel(t);
        if (cores != null) cores.reset(t);
        java.util.Arrays.fill(errSeen, false);
        log(t, -1, "reset (" + (resetRequest == 2 ? "hard" : "soft") + "): every unit to the boot ROM");
        resets++; lastResetAt = t; resetRequest = 0;
    }

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
