/**
 * The traffic director (spec 4.8, v1.2): one unit straight after the eject selector and the only path into the tile. It
 * takes at most one packet per cycle and switches on its mode, and for mode 0 on its address's region (2.2):
 *   a bad packet (3.1: mode 0 misaligned or to a region it can't reach, mode 3 at 128 bits) -> dropped, uncounted,
 *     ERRORS [1] ({@link #badTotal}, the error's source);
 *   mode 0, region 0x00 or 0x01 -> port B, through the cm_latency pipe;
 *   mode 0, region 0x03 -> the write engine, at its address;
 *   mode 0, region 0x04-0x06 -> that peripheral's write port (rp_latency, 4.17.5);
 *   modes 1, 2, 3 -> the write engine, at PTR;
 *   mode 4 -> the GATHER engine's request queue; never counted (its counter field travels to the gather values);
 *   mode 5 -> count only: nothing written;
 *   modes 6-7 -> discarded: no write, no count, no error (meshsim flags them, invariant 21).
 * Size 11 is undefined (3.1): meshsim flags it and drops the packet, uncounted, with no error ({@link #undefinedSize}).
 *
 * Counting (3.1, 4.8, 4.12; D3, D14, D40): for a packet it accepts in modes 0-3 or 5 with a counter field of 1-7, the
 * director puts the counter's id into a K-stage delay line, and K cycles later that write counter is decremented. A
 * write the receiver drops after acceptance (a misaligned PTR, a peripheral's refusal) is still counted. Unexpected
 * arrival is judged at the decrement (4.12, the accepted trade-off): a decrement of a counter at 0 leaves it at 0 and
 * sets ERRORS [2] ({@link #unexpectedTotal}); a counter write in the same cycle is judged first (count + N == 0). meshsim
 * also reports the stray at its acceptance, naming it ({@link #strays}: the counter, less the decrements already in the
 * line, plus a same-cycle write, is 0), as a violation of invariant 9 when Config.checkUnexpected is set.
 *
 * K (4.8): Config.K, default 8; the constructor errors if K < max(2 L + 3, L + 4, cm_latency, rp_latency).
 *
 * meshsim's checks here (spec 9): invariant 5 (bad packets, size 11), 11 (the command and reply blocks), 14 (read-port
 * overlap), 19 (bounds), 21 (modes 6-7, count-only on counter 0), 22 (uncounted traffic is commands and overlays only;
 * nothing lands in data memory's first 16 bytes).
 */
final class TrafficDirector implements Component {
    static final int N_COUNTERS = LinkWord.N_COUNTERS;               // 1-7; index 0 unused
    static final int COUNTER_MASK = (1 << 20) - 1;
    static String counterName(int c) { return "WRITE_COUNTER_" + c; }

    final int me;
    final Config cfg;
    final int K;
    WordSource in;                                   // eject selector -> director
    WordSink toGather, toWriteEngine;                // input FIFOs (overflow-checked)
    ReadPort readPort;
    long readPortResults;
    long readPortOverlaps;                           // outside packets accepted with read-port results outstanding (monotonic)
    WordDelayLine toPortB;                           // mode 0 to regions 0x00, 0x01; cm_latency
    int periphPresent = 1;                           // the unit's PARAMS [2:0] (4.17.1): every mesh tile has the replayer as peripheral 1
    final int[] remaining = new int[N_COUNTERS + 1]; // the write counters (20 bits), index 1-7
    private final int[] lineCtr;                     // the K-stage delay line: the counter decremented at cycle (t mod K), 0 = none
    private final long[] lineT;                      // ... and the cycle it is due
    private final int[] inLine = new int[N_COUNTERS + 1];   // decrements scheduled and not yet applied, per counter
    private final long[] decAt = new long[N_COUNTERS + 1];
    { java.util.Arrays.fill(decAt, Long.MIN_VALUE); }
    final long[] counted = new long[N_COUNTERS + 1]; // monotonic: decrements scheduled per counter
    TileControl ctl;
    PeripheralSlot periph;                           // mode 0 to region 0x04 (4.17.5)
    long unexpected, unexpectedTotal, strays;        // decrements that met a counter at 0 (per run; monotonic); strays seen at acceptance
    final long[] perMode = new long[LinkWord.N_MODES];
    long accepted, fromControllers, countOnly, reservedDiscarded;
    long droppedBad, badTotal;                       // bad packets dropped: per run; monotonic
    long undefinedSize;                              // size-11 packets (undefined; flagged and dropped by meshsim), monotonic
    java.util.List<long[]> log;                      // {t, payload [63:0], hdr, payload [127:64]} of every accepted packet, when non-null (tests)
    long violations; String lastViolation;

    TrafficDirector(int me, Config cfg) {
        this.me = me; this.cfg = cfg; this.K = cfg.K;
        int L = cfg.tsEngineLatency, need = Math.max(Math.max(2 * L + 3, L + 4), Math.max(cfg.cmLatency, cfg.rpLatency));
        if (K < need) throw new IllegalArgumentException("K = " + K + " is below max(2L + 3, L + 4, cm_latency, rp_latency) = " + need + " at L = " + L + " (4.8; invariant 24)");
        lineCtr = new int[K]; lineT = new long[K]; java.util.Arrays.fill(lineT, Long.MIN_VALUE);
    }

    @Override public void runCycle(long t) {
        decrement(t);
        if (!in.ready(t)) return;
        long d = in.peekA(t), h = in.peekB(t), hi = in.peekC(t);
        in.take(t);
        accepted++;
        if (log != null) log.add(new long[] { t, d, h, hi });
        int mode = LinkWord.mode(h), src = LinkWord.src(h), ctr = LinkWord.counter(h), a = LinkWord.addr(h), size = LinkWord.size(h);
        perMode[mode]++;
        if (LinkWord.controller(src)) fromControllers++;
        boolean command = mode == LinkWord.M_WRITE && ctr == 0 && LinkWord.controller(src) && LinkWord.region(a) == LinkWord.R_DMEM
                && ControllerMemories.inCommand(LinkWord.offset(a));
        if (LinkWord.readPortTag(h)) { readPortResults++; if (readPort != null) readPort.acceptedAt(t); }
        else if (readPort != null && readPort.outstanding() > 0 && src != me && !command) {
            readPortOverlaps++;
            if (cfg.checkReadPortOverlap) violate("packet arrived with " + readPort.outstanding() + " read-port result(s) outstanding (spec 4.3's rule, invariant 14; read-port overlap): " + LinkWord.describe(d, h, hi));
        }
        String bad = LinkWord.whyBad(h, periphPresent);
        if (bad != null) { droppedBad++; badTotal++; violate("bad packet dropped, uncounted, ERRORS [1] (" + bad + "; 3.1, invariant 5): " + LinkWord.describe(d, h, hi)); return; }
        if (size == LinkWord.SIZE_UNDEF) {                                   // undefined (3.1): meshsim's choice, flagged, dropped, no error
            undefinedSize++;
            violate("packet with size 11 (undefined, 3.1; invariant 5) arrived and was dropped: " + LinkWord.describe(d, h, hi));
            return;
        }
        if (LinkWord.reservedMode(mode)) { reservedDiscarded++; violate("packet in the reserved mode " + mode + " discarded (3.1, invariant 21): " + LinkWord.describe(d, h, hi)); return; }
        if (mode == LinkWord.M_COUNT && ctr == 0) violate("count-only packet on counter 0: it does nothing (3.1, invariant 21): " + LinkWord.describe(d, h, hi));
        if (ctr == 0 && mode != LinkWord.M_GATHER && mode != LinkWord.M_COUNT) checkUncounted(d, h, hi, mode, src, a);
        if (ctr == 0 && mode == LinkWord.M_WRITE && LinkWord.controller(src) && (LinkWord.region(a) == LinkWord.R_DMEM || LinkWord.region(a) == LinkWord.R_PMEM)) bootKinds(d, h, hi, a);
        if (ctr != 0 && LinkWord.countedMode(mode)) schedule(t, ctr, d, h, hi);
        switch (mode) {
            case LinkWord.M_WRITE -> {
                int r = LinkWord.region(a);
                if (r == LinkWord.R_DMEM || r == LinkWord.R_PMEM) { checkCmem(d, h, hi, src, ctr, a, size); toPortB.put(t, d, h, hi); }
                else if (r == LinkWord.R_SRAM) {
                    int c = LinkWord.offset(a) >>> 2;
                    if (c + LinkWord.chunks(size) > cfg.memChunks) violate("mode-0 write at or past the end of big SRAM (chunk 0x" + Integer.toHexString(c) + "): undefined (2.2; invariant 19): " + LinkWord.describe(d, h, hi));
                    toWriteEngine.put(t, d, h, hi); if (acceptQ != null) acceptQ.add(t);
                }
                else periph.accept(t, r - LinkWord.R_PERIPH1 + 1, d, h, hi);
            }
            case LinkWord.M_SRC, LinkWord.M_BUCKET, LinkWord.M_AUG -> { toWriteEngine.put(t, d, h, hi); if (acceptQ != null) acceptQ.add(t); }
            case LinkWord.M_GATHER -> toGather.put(t, d, h, hi);
            case LinkWord.M_COUNT -> countOnly++;
            default -> throw new IllegalStateException("unreachable: mode " + mode);
        }
    }

    // ================================================================ the write counters' delay line (4.8, 4.12)
    /** Schedule counter c's decrement for t + K; report a stray (invariant 9's diagnostic at acceptance). */
    private void schedule(long t, int c, long d, long h, long hi) {
        int slot = (int) ((t + K) % K);
        lineCtr[slot] = c; lineT[slot] = t + K; inLine[c]++; counted[c]++;
        int pend = ctl != null ? ctl.pendingAdd(c, t) : 0;
        if (remaining[c] - (inLine[c] - 1) + pend <= 0) {
            strays++;
            if (cfg.checkUnexpected) violate("unexpected arrival (a stray, at acceptance): " + counterName(c) + " has nothing armed for it (invariant 9; the error follows at its decrement, K = " + K + " cycles later): " + LinkWord.describe(d, h, hi));
        }
    }
    /** Apply the decrement due at t, if any (before this cycle's acceptance, which schedules t + K). */
    private void decrement(long t) {
        int slot = (int) (t % K);
        if (lineT[slot] != t) return;
        int c = lineCtr[slot]; lineT[slot] = Long.MIN_VALUE; inLine[c]--;
        int pend = ctl != null ? ctl.pendingAdd(c, t) : 0;
        if (remaining[c] + pend == 0) { unexpected++; unexpectedTotal++; }       // stays 0; ERRORS [2] (4.12)
        else { remaining[c]--; decAt[c] = t; }
    }
    /** The counter as it stood at the start of cycle t (a decrement applied at t is undone). */
    int counterAtStart(int c, long t) { return remaining[c] + (decAt[c] == t ? 1 : 0); }
    /** The cycle counter c was last decremented. */
    long lastDecrement(int c) { return decAt[c]; }
    /** Decrements scheduled and not yet applied (all counters): the delay line's occupancy. */
    int inFlight() { int s = 0; for (int c = 1; c <= N_COUNTERS; c++) s += inLine[c]; return s; }
    /** The reset (6.1): the counters and the delay line clear. */
    void resetCounters() { overlayKinds = 0; java.util.Arrays.fill(remaining, 0); java.util.Arrays.fill(inLine, 0); java.util.Arrays.fill(lineT, Long.MIN_VALUE); }

    // ================================================================ meshsim's checks
    /** W4-03. Invariant 22: uncounted traffic is mode 0 only, and only commands and boot overlays into controller memory, from a
     *  controller (3.1, 6.2). Modes 1-3 are always counted; big-SRAM data and peripheral writes are always counted. */
    private void checkUncounted(long d, long h, long hi, int mode, int src, int a) {
        if (mode != LinkWord.M_WRITE) { violate("uncounted mode-" + mode + " packet: modes 1-3 are always counted (3.1, invariant 22): " + LinkWord.describe(d, h, hi)); return; }
        int r = LinkWord.region(a);
        if (r != LinkWord.R_DMEM && r != LinkWord.R_PMEM) violate("uncounted write to region 0x" + Integer.toHexString(r) + ": big-SRAM data and peripheral writes are always counted (3.1, 6.2; invariant 22): " + LinkWord.describe(d, h, hi));
        else if (!LinkWord.controller(src)) violate("uncounted write into controller memory from unit " + src + ": only commands and boot overlays, from C&C 1-3, are uncounted (3.1; invariant 22): " + LinkWord.describe(d, h, hi));
        else if (ctl != null && ctl.landed && !(r == LinkWord.R_DMEM && ControllerMemories.inCommand(LinkWord.offset(a))))
            violate("an overlay write landed after the unit's command and before its start (6.2: the overlays and the first command are of one kind, the command last): " + LinkWord.describe(d, h, hi));
    }
    /** Invariants 11, 19 and 22 for a mode-0 write into controller memory (4.11, 4.12, 2.2). */
    private void checkCmem(long d, long h, long hi, int src, int ctr, int a, int size) {
        int off = LinkWord.offset(a), n = LinkWord.bytes(size);
        if (off + n > ControllerMemories.BYTES) { violate("write past the end of " + (LinkWord.region(a) == 0 ? "data" : "program") + " memory: the high offset bits are ignored and it aliases (2.2; invariant 19): " + LinkWord.describe(d, h, hi)); return; }
        if (LinkWord.region(a) != LinkWord.R_DMEM) return;
        if (off < 16) violate(   /* W4-02: at any time */"write into data memory's reserved first 16 bytes (2.2; invariant 22): " + LinkWord.describe(d, h, hi));
        boolean cmd = ControllerMemories.inCommand(off) || ControllerMemories.inCommand(off + n - 1);
        boolean rep = ControllerMemories.inReply(off) || ControllerMemories.inReply(off + n - 1);
        if (cmd && (ctr != 0 || !LinkWord.controller(src) || !ControllerMemories.inCommand(off) || !ControllerMemories.inCommand(off + n - 1)))
            violate("write into the command block other than an uncounted one from C&C 1-3 inside it (4.12; invariant 11): " + LinkWord.describe(d, h, hi));
        if (rep && (size == LinkWord.SIZE_128 || off != ControllerMemories.REPLY + 8 * src))
            violate("write into the reply block other than a 32- or 64-bit reply at the sender's own pair, REPLY + 8 x " + src + " (4.12; invariant 11): " + LinkWord.describe(d, h, hi));
    }

    /** Invariant 25 (6.2): a unit's overlays and its first command are all broadcast or all unicast (the mesh orders
     *  packets only within a kind). Overlays since the last command: bit 0 unicast, bit 1 broadcast. */
    private int overlayKinds;
    private void bootKinds(long d, long h, long hi, int a) {
        int kind = LinkWord.dst(h) == LinkWord.BROADCAST ? 2 : 1;
        int off = LinkWord.offset(a);
        boolean slot0 = LinkWord.region(a) == LinkWord.R_DMEM && off <= ControllerMemories.COMMAND && off + LinkWord.bytes(LinkWord.size(h)) > ControllerMemories.COMMAND;
        if (ControllerMemories.inCommand(off) && LinkWord.region(a) == LinkWord.R_DMEM) {
            if (slot0) {
                if ((overlayKinds & ~kind) != 0) violate("a command (" + (kind == 2 ? "broadcast" : "unicast") + ") after overlays of the other kind: the mesh orders packets only within a kind (6.2; invariant 25): " + LinkWord.describe(d, h, hi));
                overlayKinds = 0;
            }
        } else overlayKinds |= kind;
    }

    /** S1's bookkeeping: the acceptance cycle of every packet handed to the write engine, in order (simulator only). */
    java.util.ArrayDeque<Long> acceptQ;

    /** cm_idle (a diagnostic only in v1.2, 4.8): the port-B pipeline is empty. */
    boolean cmIdle(long t) { return toPortB.inFlight(t) == 0; }

    void violate(String m) { violations++; lastViolation = m; }
    void resetStats() { java.util.Arrays.fill(perMode, 0); accepted = droppedBad = unexpected = 0; }
}
