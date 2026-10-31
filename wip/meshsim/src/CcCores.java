import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.List;

/**
 * C&C 1-3 as cores (WORKER14; Config.ccCores, off by default): each controller is a tile instance (spec 6.6) with the
 * tile's controller, memories, register block, boot ROM, big SRAM and engines, and one peripheral, its engine: C&C 1's
 * Ingress/Egress with the UART and the chip lines ({@link Cnc1Peripheral}, the chip's {@link Chip#cc1p}), C&C 2's and
 * C&C 3's engines as stubs that raise nothing ({@link Stub}, types 2 and 3, 4.17.1).
 *
 * The link (W14-01, item 118 deferred): the controller's router takes tile c's to-controller output on its
 * {@link Router#CC_LINK_IN} input and sends on its {@link Router#CC_LINK_OUT} output into tile c's from-controller route
 * block, Config.ccLinkLatency cycles each way, with no back-pressure (4.16). These replace the Java model's endpoints
 * ({@link Chip#ccs}), which stay built but unconnected.
 *
 * The host port (W14-02, meshsim only): the outside world's writes into a controller, standing in for Ingress/Egress
 * (deferred), enter its router on {@link Router#CC_HOST_IN} and are ejected to its traffic director, one packet a cycle,
 * in order, as uncounted mode-0 writes from src 64. The harness lands C&C 1's overlays and commands this way (6.2 step 3);
 * tests may land C&C 2's and C&C 3's directly too (standing in for C&C 1).
 *
 * The request tree (3.3, 4.18): each controller's request port feeds the root's controller input
 * ({@link RequestBus#ccTiles}), and its receive side reads its target's queue (C&C c + 1 reads target c).
 *
 * The error lines (6.1, 8.14): C&C 2's and C&C 3's set ERROR_LINES_2 [1] and [2]; C&C 1's own errors interrupt it
 * directly and raise the toast pin ({@link Cnc1Peripheral#unitErrors}).
 */
final class CcCores implements Component {
    final Chip chip;
    final Config cfg;
    final Tile[] units = new Tile[3];                  // C&C 1-3: endpoints 64-66
    final WordDelayLine[] toCc = new WordDelayLine[3], fromCc = new WordDelayLine[3];
    final WordFIFO[] host = new WordFIFO[3];
    @SuppressWarnings("unchecked") private final ArrayDeque<long[]>[] hostQ = new ArrayDeque[3];   // {lo, hdr, hi}
    private final boolean[] errSeen = new boolean[3];
    private final long[][] seen = new long[3][12];
    final List<WordFIFO> fifos = new ArrayList<>();
    long hostPackets;

    CcCores(Chip chip) {
        this.chip = chip; this.cfg = chip.cfg;
        for (int c = 0; c < 3; c++) {
            Peripheral p1 = c == 0 ? chip.cc1p : new Stub(2 + c - 1);
            Tile u = new Tile(LinkWord.CC1 + c, cfg, p1);
            u.chip = chip; units[c] = u;
            String n = "cc" + (c + 1);
            toCc[c] = new WordDelayLine("t" + c + ".to-" + n, cfg.ccLinkLatency);
            fromCc[c] = new WordDelayLine(n + ".link", cfg.ccLinkLatency);
            host[c] = new WordFIFO(n + ".host", 4);
            hostQ[c] = new ArrayDeque<>();
            chip.tiles[c].router.toCnc = toCc[c]; u.router.linkIn[Router.CC_LINK_IN] = toCc[c];
            u.router.linkOut[Router.CC_LINK_OUT] = fromCc[c]; chip.tiles[c].router.fromCnc = fromCc[c];
            u.router.linkIn[Router.CC_HOST_IN] = host[c];
            u.req.bus = chip.reqBus; u.req.target = c;
            for (WordFIFO[] fs : u.router.q) for (WordFIFO f : fs) fifos.add(f);
            fifos.add(u.weIn); fifos.add(u.gatherIn);
        }
        chip.reqBus.ccTiles = units;
        if (cfg.measureS1) for (Tile u : units) u.s1Attach(chip.s1);
    }

    Tile unit(int cc) { return units[cc]; }

    // ================================================================ the host port (W14-02)
    /** A write from the outside world into controller cc (0-2): an uncounted mode-0 packet from src 64, in order. */
    void hostWrite(int cc, int addr, int size, long lo, long hi) {
        long h = LinkWord.hdr(LinkWord.CC1, LinkWord.CC1 + cc, LinkWord.M_WRITE, 0, addr, size);
        hostQ[cc].add(new long[] { LinkWord.maskLo(lo, size), h, LinkWord.maskHi(hi, size) });
    }
    /** Words into controller cc's memory at a byte address (region 0x00 or 0x01), as 128-bit packets (6.2: padded to whole
     *  packets with zero words). */
    void hostLoad(int cc, int byteAddr, int[] words) {
        for (int i = 0; i < words.length; i += 4) {
            long[] w = new long[4];
            for (int k = 0; k < 4; k++) w[k] = i + k < words.length ? words[i + k] & 0xFFFFFFFFL : 0;
            hostWrite(cc, byteAddr + 4 * i, LinkWord.SIZE_128, w[1] << 32 | w[0], w[3] << 32 | w[2]);
        }
    }
    /** Commands landed in each controller so far: the next one's parity. */
    final int[] commandsTo = new int[3];
    /** A command into controller cc's command block (6.3): the arguments into the parity's block, then the word, with the
     *  parity in bit 23, into slot 0, last. */
    void hostCommand(int cc, int word, int... args) {
        if (args.length > Registers.ARGS_PER_BLOCK) throw new IllegalArgumentException("more than 7 arguments");
        int par = commandsTo[cc]++ & 1, base = par == 0 ? Registers.ARGS_EVEN : Registers.ARGS_ODD;
        for (int i = 0; i < args.length; i++) hostWrite(cc, ControllerMemories.COMMAND + 4 * (base + i), LinkWord.SIZE_32, args[i] & 0xFFFFFFFFL, 0);
        int w = (word & ~(1 << Registers.CMD_PARITY_BIT)) | par << Registers.CMD_PARITY_BIT;
        hostWrite(cc, ControllerMemories.COMMAND, LinkWord.SIZE_32, w & 0xFFFFFFFFL, 0);
    }
    boolean hostIdle(long t) { for (int c = 0; c < 3; c++) if (!hostQ[c].isEmpty() || !host[c].isEmpty(t)) return false; return true; }

    // ================================================================ the cycle
    @Override public void runCycle(long t) {
        for (int c = 0; c < 3; c++) if (!hostQ[c].isEmpty() && host[c].canPut(t)) {
            long[] p = hostQ[c].poll(); host[c].put(t, p[0], p[1], p[2]); hostPackets++;
        }
        for (Tile u : units) u.runCycle(t);
    }
    /** After every unit has run t (Chip): the controllers' error lines into C&C 1's peripheral. */
    void endCycle(long t) {
        for (int c = 0; c < 3; c++) {
            boolean e = units[c].ctl.errorLine();
            if (e == errSeen[c]) continue;
            errSeen[c] = e;
            if (c == 0) chip.cc1p.unitErrors(t, e);            // C&C 1's own ERRORS: its hard interrupt, the toast pin (6.1)
            else chip.cc1p.errorLine(t, LinkWord.CC1 + c, e);   // ERROR_LINES_2 [1], [2] (8.14)
        }
    }

    /** The controllers' share of the chip's checks (Chip.checkInvariants): a unit's violation counters, reported as they grow. */
    void check(long t) {
        for (int c = 0; c < 3; c++) {
            Tile p = units[c]; long[] s = seen[c]; int id = p.idx;
            if (p.router.ejectErrors != s[0]) { s[0] = p.router.ejectErrors; chip.violation(t, id, "packet ejected at the wrong unit"); }
            if (p.router.routeErrors != s[1]) { s[1] = p.router.routeErrors; chip.violation(t, id, "illegal forward in a route block"); }
            if (p.inj.violations != s[2]) { s[2] = p.inj.violations; chip.violation(t, id, p.inj.lastViolation); }
            if (p.dir.violations != s[3]) { s[3] = p.dir.violations; chip.violation(t, id, p.dir.lastViolation); }
            if (p.ctl.violations != s[4]) { s[4] = p.ctl.violations; chip.violation(t, id, p.ctl.lastViolation); }
            if (p.cmem.violations != s[5]) { s[5] = p.cmem.violations; chip.violation(t, id, p.cmem.lastViolation); }
            if (p.readPort.violations != s[6]) { s[6] = p.readPort.violations; chip.violation(t, id, p.readPort.lastViolation); }
            long agv = p.ag.sizeViolations + p.ag.modeViolations + p.ag.aug128Violations + p.ag.destViolations + p.ag.alignViolations + p.ag.startsWhileRunning + p.ag.tableWraps;
            if (agv != s[7]) { s[7] = agv; chip.violation(t, id, p.ag.lastViolation); }
            if (p.srq.violations != s[8]) { s[8] = p.srq.violations; chip.violation(t, id, p.srq.lastViolation); }
            if (p.writer.violations != s[9]) { s[9] = p.writer.violations; chip.violation(t, id, p.writer.lastViolation); }
            int err = p.ctl.errorsAt(t);
            if (err != s[10] && err != 0 && cfg.checkErrors) chip.violation(t, id, "ERRORS [" + Integer.toBinaryString(err) + "] set: " + Conditions.names(err) + " (invariant 15)");
            s[10] = err;
        }
    }
    void scanOverflows(java.util.Set<String> reported) {
        for (WordFIFO f : fifos) if (f.overflow && reported.add(f.name)) chip.violation(f.overflowCycle, -1, "FIFO overflow " + f.name + " (invariant 1)");
    }
    boolean quiescent(long t) {
        for (int c = 0; c < 3; c++) {
            if (!units[c].quiescent(t) || toCc[c].inFlight(t) > 0 || fromCc[c].inFlight(t) > 0) return false;
        }
        return hostIdle(t);
    }
    /** The reset (6.1), with the chip's: the units as at power-on except their memories; the host's queue is the outside
     *  world's and stays (Chip.reset empties the host FIFOs with every other bus). */
    void reset(long t) { for (Tile u : units) u.reset(t); java.util.Arrays.fill(errSeen, false); }

    /** C&C 2's and C&C 3's engines (the Updater/Binder/RW engine, the Tracer: 4.17.1 types 2 and 3) as stubs: present,
     *  typed, never running, no events, no errors, no words beyond the standard ones; a write into the region is flagged. */
    static final class Stub implements Peripheral {
        final int type; Host host;
        final EventCounts ev = new EventCounts(new int[] { 1, 1 }, new boolean[2]);
        Stub(int type) { this.type = type; }
        @Override public int params() { return type; }
        @Override public int type() { return type; }
        @Override public void accept(long t, int src, int offset, int size, long lo, long hi) { if (host != null) host.violate(t, "a write into a stub engine's region (type " + type + ", WORKER14)"); }
        @Override public void run(long t) {}
        @Override public boolean running(long t) { return false; }
        @Override public int errors(long t) { return 0; }
        @Override public EventCounts events() { return ev; }
        @Override public void attach(Host h) { host = h; }
        @Override public void reset(long t) { ev.clearAll(); }
        @Override public void runCycle(long t) {}
    }
}
