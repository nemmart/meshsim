import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.List;
import java.util.function.BooleanSupplier;

/**
 * C&C (spec 6.6), in Java for now (Q11): a sequencer over a modelled register block, so that an
 * RV32 C&C can replace the sequencer later without changing the machine.
 *
 * Registers modelled:
 *   send   the holding register into tile 0's "from C&C" route block ({@link Endpoint#send}): one packet per cycle,
 *          any dst/mode/addr, src = 64; a refused put is retried;
 *   READY  64 sticky bits, bit s set by a pulse on tile s's READY line (visible ready_latency after the tile's edge);
 *          a clear stored at t clears what is visible at t, and a pulse arriving during t is kept;
 *   GO     a store at t pulses GO; tile s sees it at t + go_latency + skew[s];
 *   RUN    a store at t sets the RUN/HALT line; tile s sees it at t + run_latency + skew[s];
 *   ERROR  (spec 6.6) one sticky bit: the OR of every tile's error line ("my TILE_CONDITIONS is non-zero"),
 *          visible error_latency after the tiles' conditions change; a clear store at t clears it, and it sets again
 *          from t + 1 while the line is still high (only HALT clears the conditions);
 *   RX     (spec 6.6) the receive FIFO: packets for dst 64, taken one per cycle from tile 0's to-C&C output into
 *          cncRxDepth (>= 64) entries of payload + src; read through a register pair (data; src with a valid bit), one
 *          entry per read; a packet arriving with the FIFO full is lost and sets the receive-overflow flag. Room freed by a
 *          read at t is room from t + 1. Endpoint.received stays the trace of every packet for dst 64.
 * C&C's WFI and its wake sources (all READY, a packet in the receive FIFO, the error bit, the watchdog) are modelled by
 * steps that re-check their condition every cycle.
 *
 * The REPORT flow (6.6; opt-in, the lead's ruling of 10 October: a debugging flow, not a default): {@link #stopOnError}
 * makes go() hold GO while the error bit is set; it then waits for all READY (or its watchdog), drops the steps still
 * queued, and runs {@link #report}: broadcast CMD_REPORT, GO, collect one (tile, TILE_CONDITIONS) packet per tile,
 * log them in {@link #reports}, then HALT, which clears every condition, and clear the error bit.
 *
 * The sequencer runs {@link Step}s in order, at most one register action per cycle; a step that waits re-checks every
 * cycle (the Java stand-in for C&C's WFI on all-READY). Seeing all 64 READY bits at t, {@link #go()} stores clear and
 * GO at t + cnc_reaction.
 */
final class CnC implements Component {
    interface Step { boolean run(CnC c, long t); }    // true = finished (move to the next step from the next cycle)

    final Chip chip;
    final Endpoint ep;
    final Config cfg;
    private final ArrayDeque<Step> steps = new ArrayDeque<>();
    // READY bits
    private final long[] arrived = new long[64];       // latest READY arrival time per tile (Long.MIN_VALUE: none)
    private long clearedAt = Long.MIN_VALUE;           // pulses visible before this cycle are cleared
    private final List<long[]> pendingReady = new ArrayList<>();   // {arrival time, tile}
    // stats
    long gos, runStores, packetsSent, readyWaitCycles;
    long lastGoAt = -1, lastAllReadyAt = -1, lastRunAt = -1;
    final List<long[]> phaseLog = new ArrayList<>();   // {allReady t, GO t} per GO
    // ---- The error bit, the receive FIFO, the REPORT flow ----
    private final ArrayDeque<long[]> errLine = new ArrayDeque<>();   // {visible at, 0/1} changes of the OR'd error line
    private boolean errLineSeen, errLineLast, errorBit;
    long errorSetAt = -1, errorSets, errorFirstAt = -1;   // the cycle the bit set (since the last clear), times it set, the first ever
    private final ArrayDeque<long[]> rxq = new ArrayDeque<>();   // {payload [63:0], src, arrival cycle, payload [127:64], size} (6.6, item 29)
    boolean rxOverflow; long rxLost, rxTaken, rxReads; int rxPeak;
    boolean stopOnError;                               // opt-in: go() holds GO while the error bit is set, then REPORT
    int reportSettle = -1;                             // cycles from REPORT's send to the GO; -1: reportSettle() (see report())
    int watchdog = 100_000;                            // cycles go() waits for all READY after the error before reporting anyway
    long reportStartedAt = -1, reportGoAt = -1, reportDoneAt = -1;
    final List<long[]> reports = new ArrayList<>();    // {arrival cycle, src, payload} per REPORT reply

    CnC(Chip chip, Endpoint ep) {
        this.chip = chip; this.ep = ep; this.cfg = chip.cfg;
        java.util.Arrays.fill(arrived, Long.MIN_VALUE);
        ep.drainedByCnC = true;
    }

    // ---- the error bit (6.6) ----
    /** The OR'd error line as it stands at the end of cycle t (Chip, after every tile has run t). */
    void errorLine(long t, boolean high) {
        if (high != errLineLast) { errLine.add(new long[] { t + Math.max(1, cfg.errorLatency()), high ? 1 : 0 }); errLineLast = high; }
    }
    boolean error() { return errorBit; }
    /** A clear store at t: the bit is 0 from t + 1 unless the line is still high. */
    void clearError(long t) { errorBit = false; errorSetAt = -1; }
    // ---- the receive FIFO (6.6) ----
    int rxCount() { return rxq.size(); }
    /** Read the receive registers (6.6, item 29): {payload [63:0], src, arrival, payload [127:64], size}, or null when the FIFO is empty (the valid bit 0). */
    long[] rxPop() { long[] e = rxq.poll(); if (e != null) rxReads++; return e; }

    // ================================================================ registers
    void readyPulse(int tile, long tEdge) { pendingReady.add(new long[] { tEdge + cfg.readyLatency, tile }); }
    boolean ready(int tile, long t) { return arrived[tile] != Long.MIN_VALUE && arrived[tile] >= clearedAt && arrived[tile] <= t; }
    boolean allReady(long t) { for (int i = 0; i < 64; i++) if (!ready(i, t)) return false; return true; }
    long readyBits(long t) { long b = 0; for (int i = 0; i < 64; i++) if (ready(i, t)) b |= 1L << i; return b; }
    /** A clear stored at t acts at the end of t: every pulse visible at t is cleared; one arriving during t (visible at
     *  t + 1) is kept. */
    void clearReady(long t) { clearedAt = t + 1; }
    int lastCommandWord = -1;                          // the command word most recently queued for slot 0 (phase names)
    void storeGo(long t) {
        gos++; lastGoAt = t;
        chip.phases.onGo(t, lastCommandWord);
        for (Tile p : chip.tiles) p.ctl.scheduleGo(t + cfg.goLatency + chip.startSkew[p.idx]);
    }
    void storeRun(long t, boolean run) {
        runStores++; lastRunAt = t;
        for (Tile p : chip.tiles) p.ctl.scheduleRun(t + cfg.runLatency + chip.startSkew[p.idx], run);
    }

    // ================================================================ cycle
    @Override public void runCycle(long t) {
        while (!errLine.isEmpty() && errLine.peek()[0] <= t) errLineSeen = errLine.poll()[1] == 1;
        if (errLineSeen && !errorBit) { errorBit = true; errorSetAt = t; if (errorSets++ == 0) errorFirstAt = t; }
        if (ep.rx.ready(t)) {                          // tile 0's to-C&C output into the receive FIFO, one packet per cycle
            long d = ep.rx.peekA(t), h = ep.rx.peekB(t), hi = ep.rx.peekC(t); ep.rx.take(t);
            ep.received.add(new long[] { t, d, h, hi });
            if (rxq.size() >= cfg.cncRxDepth) { rxOverflow = true; rxLost++; }
            else { rxq.add(new long[] { d, LinkWord.src(h), t, hi, LinkWord.size(h) }); rxTaken++; rxPeak = Math.max(rxPeak, rxq.size()); }
        }
        for (int i = pendingReady.size() - 1; i >= 0; i--) {
            long[] r = pendingReady.get(i);
            if (r[0] <= t) { arrived[(int) r[1]] = Math.max(arrived[(int) r[1]] == Long.MIN_VALUE ? r[0] : arrived[(int) r[1]], r[0]); pendingReady.remove(i); }
        }
        if (ep.send.refused(t)) { ep.send.retry(t); return; }
        while (!steps.isEmpty()) {
            Step s = steps.peek();
            boolean fin = s.run(this, t);
            if (!fin) return;
            steps.poll();
            if (!(s instanceof Instant)) return;       // one register action per cycle
        }
    }
    boolean busy() { return !steps.isEmpty(); }

    // ================================================================ sequencer steps
    /** A step that takes no cycle of its own (bookkeeping). */
    private interface Instant extends Step {}
    CnC then(Step s) { steps.add(s); return this; }
    CnC call(Runnable r) { steps.add((Instant) (c, t) -> { r.run(); return true; }); return this; }

    /** Send packets in order, one per cycle. Each packet: {dst, mode, addr, payload [63:0]} (a 64-bit packet) or
     *  {dst, mode, addr, payload [63:0], size, payload [127:64]}. */
    CnC send(List<long[]> packets) {
        final int[] i = { 0 };
        steps.add((c, t) -> {
            if (i[0] >= packets.size()) return true;
            long[] p = packets.get(i[0]++);
            if (p.length >= 6) ep.send(t, (int) p[0], (int) p[1], (int) p[2], (int) p[4], p[3], p[5]);
            else ep.send(t, (int) p[0], (int) p[1], (int) p[2], LinkWord.SIZE_64, p[3], 0);
            packetsSent++;
            return i[0] >= packets.size();
        });
        return this;
    }
    CnC send(int dst, int mode, int addr, long payload) { return send(List.of(new long[] { dst, mode, addr, payload })); }
    CnC send(int dst, int mode, int addr, int size, long lo, long hi) { return send(List.of(new long[] { dst, mode, addr, lo, size, hi })); }

    /** Commands sent to each tile so far: its next command's parity (argument block). */
    final int[] commandsTo = new int[64];
    /** A command (6.3): the arguments into the tile's next argument block (even commands slots 1-7, odd 8-15; owner,
     *  2026-10-26), then the command word, with the block's parity in bit 23, into slot 0, last. dst is one tile or a
     *  broadcast (every tile must then be at the same parity). The parity is fixed when the step is queued. */
    CnC command(int dst, int word, int... args) {
        if (args.length > Registers.ARGS_PER_BLOCK) throw new IllegalArgumentException("more than 7 arguments");
        int par;
        if (dst == LinkWord.BROADCAST) {
            par = commandsTo[0] & 1;
            for (int i = 0; i < 64; i++) { if ((commandsTo[i] & 1) != par) throw new IllegalStateException("broadcast command: tiles at different parities"); commandsTo[i]++; }
        } else par = commandsTo[dst]++ & 1;
        int base = par == 0 ? Registers.ARGS_EVEN : Registers.ARGS_ODD;
        List<long[]> ps = new ArrayList<>();
        for (int i = 0; i < args.length; i++) ps.add(new long[] { dst, LinkWord.M_CMD, base + i, args[i] & 0xFFFFFFFFL, LinkWord.SIZE_32, 0 });
        int w = (word & ~(1 << Registers.CMD_PARITY_BIT)) | par << Registers.CMD_PARITY_BIT;
        final int cw = word;
        steps.add((Instant) (c, t) -> { lastCommandWord = cw; return true; });
        ps.add(new long[] { dst, LinkWord.M_CMD, 0, w & 0xFFFFFFFFL, LinkWord.SIZE_32, 0 });   // a command write: mode 7, 32 bits (3.1)
        return send(ps);
    }

    /** Words into controller memory by mode 6 from a byte address (4.11 packed map), as 128-bit packets: four words each,
     *  addr + 4 per packet, the image padded with zero words to whole packets (spec 6.2, v1.1; v1.0 sent two words per
     *  64-bit packet). */
    CnC loadWords(int dst, int byteAddr, int[] words) {
        List<long[]> ps = new ArrayList<>();
        for (int i = 0; i < words.length; i += 4) {
            long[] w = new long[4];
            for (int k = 0; k < 4; k++) w[k] = i + k < words.length ? words[i + k] & 0xFFFFFFFFL : 0;
            ps.add(new long[] { dst, LinkWord.M_CMEM, cmWord(byteAddr + 4 * i), w[1] << 32 | w[0], LinkWord.SIZE_128, w[3] << 32 | w[2] });
            loadPackets++;
        }
        return ps.isEmpty() ? this : send(ps);
    }
    long loadPackets;
    static int cmWord(int byteAddr) { return byteAddr < Registers.DMEM_BASE ? byteAddr >> 2 : 0x800 + ((byteAddr - Registers.DMEM_BASE) >> 2); }

    /** Wait until all 64 READY bits are set, then (cnc_reaction later) clear them and GO. */
    CnC go() {
        final long[] seen = { -1 };
        steps.add((c, t) -> {
            if (stopOnError && errorBit) return reportOnError(t);
            if (seen[0] < 0) { if (!allReady(t)) { readyWaitCycles++; return false; } seen[0] = t; lastAllReadyAt = t; chip.phases.onAllReady(t); }
            if (t < seen[0] + cfg.cncReaction) return false;
            clearReady(t); storeGo(t); phaseLog.add(new long[] { seen[0], t });
            return true;
        });
        // GO must reach every tile before the next command can land there: GO clears "command landed", and a command
        // landing first would be overwritten unread and its READY lost (meas: hangs at go_latency >= 6 without this)
        if (cfg.cncGoSettle) steps.add((c, t) -> t >= lastGoAt + cfg.goLatency + 1);
        return this;
    }
    /** Wait for all READY, clear them, then raise RUN (boot, 6.2). */
    CnC runWhenReady() {
        steps.add((c, t) -> { if (!allReady(t)) { readyWaitCycles++; return false; } lastAllReadyAt = t; clearReady(t); storeRun(t, true); return true; });
        return lineSettles();
    }
    CnC halt() { steps.add((c, t) -> { storeRun(t, false); return true; }); return lineSettles(); }
    /** After a RUN/HALT store, C&C sends nothing until every tile has seen the line (run_latency + the +1 skew): a packet
     *  in flight across the change breaks invariant 7, and a command landing before HALT -> RUN would be cleared. */
    private CnC lineSettles() { steps.add((c, t) -> t >= lastRunAt + cfg.runLatency + 1); return this; }
    // ================================================================ the REPORT flow (6.6)
    /** go() found the error bit set (stopOnError): no more GOs. Wait for every tile to finish its phase (all READY) or
     *  the watchdog, then drop the queued steps and report. Returns false: the step queue is replaced. */
    private long errorSeenAt = -1;
    /* RECIPE cnc_stop */
    private boolean reportOnError(long t) {
        if (errorSeenAt < 0) errorSeenAt = t;
        if (!allReady(t) && t < errorSeenAt + watchdog) return false;
        steps.clear();
        report();
        return false;
    }
    /* END */
    /** The REPORT flow: broadcast CMD_REPORT, wait reportSettle() cycles for it to land everywhere, clear READY and GO;
     *  each tile sets WAKE_TIMER = (me + 1) * 4, WFIs and sends (tile index, TILE_CONDITIONS) to C&C; collect one per
     *  tile (or until the watchdog), log them, wait for READY, HALT (which clears every condition), clear the error bit.
     *
     *  REPORT usually lands on a command C&C sent right after the last GO and the tiles have not read (it is why they
     *  are waiting): that command is discarded, by design. Its READY has already fired, so READY cannot say when REPORT
     *  has landed; C&C waits a fixed reportSettle() cycles instead, twice the worst-case broadcast latency. The command-read check counts the
     *  discarded word as discarded, not as a violation (ControllerMemories.discardNext, simulator only). */
    /* RECIPE cnc_report */
    CnC report() {
        steps.add((Instant) (c, t) -> {
            reportStartedAt = t;
            for (Tile p : chip.tiles) p.cmem.discardNext = true;
            return true;
        });
        command(LinkWord.BROADCAST, Registers.CMD_REPORT << 24);
        final long[] sentAt = { -1 };
        steps.add((Instant) (c, t) -> { sentAt[0] = t; return true; });
        steps.add((c, t) -> t >= sentAt[0] + reportSettle());
        steps.add((c, t) -> { clearReady(t); storeGo(t); reportGoAt = t; return true; });
        if (cfg.cncGoSettle) steps.add((c, t) -> t >= lastGoAt + cfg.goLatency + 1);
        collect(64, 64 * 4 + 1000, reports);           // the receive FIFO is C&C's wake source; one read per cycle
        steps.add((c, t) -> allReady(t) || t >= reportGoAt + watchdog);
        steps.add((Instant) (c, t) -> { reportDoneAt = t; return true; });
        halt();
        steps.add((c, t) -> t >= lastRunAt + cfg.runLatency + 2 + Math.max(1, cfg.errorLatency()));   // the cleared line reaches C&C
        steps.add((c, t) -> { clearError(t); return true; });
        return this;
    }
    /* END */
    /** The depth of C&C's broadcast tree: a broadcast from C&C is rooted at tile 0 (4.17), and its farthest tile (36)
     *  is 8 hops away [meas: meshsim v4.0 M14, CondTest broadcast]. */
    static final int BROADCAST_DEPTH = 8;
    /** The worst-case latency of a one-packet broadcast from C&C, from its send to "command landed" at the farthest tile,
     *  on an idle mesh: 2 + intra_tile_latency + cm_latency + BROADCAST_DEPTH x (link_latency + 1) = 37 at the defaults
     *  [meas: CondTest broadcast, link latency 2-4, cm_latency 2-4, intra_tile_latency 1-2, exact]. */
    int broadcastBound() { return 2 + cfg.intraTileLatency + cfg.cmLatency + BROADCAST_DEPTH * (cfg.linkLatency + 1); }
    /** REPORT's settle (10 October: no READY change; a fixed settle from the bound; with margin): twice
     *  the idle bound, 74 cycles at the defaults. The margin covers queueing behind stray packets; REPORT is sent when
     *  every tile has reported READY, so the mesh carries nothing counted. */
    int reportSettle() { return reportSettle >= 0 ? reportSettle : 2 * broadcastBound(); }

    /** A step: read the receive FIFO, one entry per cycle, into {arrival cycle, src, payload} rows until n have
     *  come or max cycles have passed (the watchdog). */
    CnC collect(int n, int max, List<long[]> into) {
        final long[] from = { -1 }; final int[] got = { 0 };
        steps.add((c, t) -> {
            if (from[0] < 0) from[0] = t;
            long[] e = rxPop();
            if (e != null) { into.add(new long[] { e[2], e[1], e[0] }); got[0]++; }
            return got[0] >= n || t >= from[0] + max;
        });
        return this;
    }
    /** The REPORT replies as {tile index (payload high word), TILE_CONDITIONS (low word)}, per src; -1 if absent. */
    long[] reportBySrc() {
        long[] r = new long[64]; java.util.Arrays.fill(r, -1);
        for (long[] e : reports) if (e[1] >= 0 && e[1] < 64) r[(int) e[1]] = e[2];
        return r;
    }

    CnC waitCycles(int n) { final int[] k = { n }; steps.add((c, t) -> --k[0] <= 0); return this; }
    CnC waitUntil(BooleanSupplier cond) { steps.add((c, t) -> cond.getAsBoolean()); return this; }
}
