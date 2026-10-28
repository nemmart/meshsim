/**
 * The traffic director (spec 4.8, v1.1 as renumbered 2026-10-26): one unit straight after the eject selector and the
 * only path into the tile. It takes at most one packet per cycle (169 bits: the bus's three words) and switches on its
 * 4-bit mode alone:
 *   1 -> the GATHER engine's request queue (never counted);
 *   2, 3, 4, 5 -> the write engine's input FIFO (DMA_SRAM_REMAINING; mode 3 at its explicit, aligned addr);
 *   6 -> port B: 1, 2 or 4 words at the packed word addr (DMA_CM_REMAINING);
 *   7 -> port B: command slot addr[3:0] of the command block (32 bits; never counted; slot 0 sets command landed);
 *   8 -> port B: the reply block, word 2 src (32 bits: word 0; 64 bits: both) (DMA_REPLY_REMAINING);
 *   9 -> the peripheral slot's write port (DMA_PERIPH_REMAINING, on landing or drop: {@link PeripheralSlot});
 *   every bad packet (3.1) -> dropped, uncounted, setting the bad-mode-or-size condition (4.13; {@link #badModeTotal}).
 * Size 11 is undefined (3.1): meshsim flags it and drops the packet, uncounted, with no condition ({@link #undefinedSize}).
 *
 * Counting (3.1, 4.12; owner, 2026-10-26): every mode from any source is counted, except modes 1 and 7 and bad packets.
 * A halted tile counts nothing and sets no unexpected arrival ({@link #halted}); routing is the same in HALT and RUN.
 * An arrival for a counter already at 0 (in RUN) leaves it at 0 and sets unexpected arrival (invariant 9). Counter
 * writes (additive, TileControl) are applied at the end of the cycle; the controller's load samples the counter as it
 * stood at its sample point ({@link #counterAtStart}).
 *
 * Read-port overlap (4.3's rule, invariant 14): a packet from outside the tile (src != me) accepted while the tile has
 * read-port results outstanding is a violation when Config.checkReadPortOverlap is set, and is always counted; C&C's
 * command writes (mode 7) are exempt. Read-port results are recognised only by the simulator-only TAG_READPORT.
 *
 * Address checks (invariant 11; packed map, 4.11): every word of a mode-6 packet inside instruction or data memory and
 * outside the command block (0xFF0-0xFFF) and the reply block (0xEF0-0xFEF). The hardware has no check.
 */
final class TrafficDirector implements Component {
    static final int SRAM = 0, CM = 1, REPLY = 2, PERIPH = 3, N_COUNTERS = 4;
    static final String[] COUNTER_NAME = { "SRAM_REMAINING", "CM_REMAINING", "REPLY_REMAINING", "PERIPH_REMAINING" };
    static final int COUNTER_MASK = (1 << 20) - 1;
    /** Packed controller-memory words (4.11): the reply block 0xEF0-0xFEF, the command block 0xFF0-0xFFF. */
    static final int REPLY_WORD = 0xEF0, COMMAND_WORD = 0xFF0;

    final int me;
    final Config cfg;
    WordSource in;                                   // eject selector -> director
    WordSink toGather, toWriteEngine;                // input FIFOs (overflow-checked)
    ReadPort readPort;
    long readPortResults;
    long readPortOverlaps;                           // outside packets accepted with read-port results outstanding (monotonic)
    WordDelayLine toPortB;                           // modes 6-8, cm_latency
    final int[] remaining = new int[N_COUNTERS];     // the down-counters (20 bits)
    private final long[] decAt = new long[N_COUNTERS];
    { java.util.Arrays.fill(decAt, Long.MIN_VALUE); }
    final long[] counted = new long[N_COUNTERS];     // monotonic: counted arrivals per counter (for the per-counter events)
    TileControl ctl;
    PeripheralSlot periph;                           // mode 9 (4.17)
    long unexpected, unexpectedTotal;                // arrivals that met a counter at 0 (per run; monotonic)
    final long[] perMode = new long[LinkWord.N_MODES];
    long accepted, fromCnc, haltedArrivals;
    long droppedBad, badModeTotal;                   // bad packets dropped: per run; monotonic (the condition's source)
    long undefinedSize;                              // size-11 packets (undefined; flagged and dropped by meshsim), monotonic
    java.util.List<long[]> log;                      // {t, payload [63:0], hdr, payload [127:64]} of every accepted packet, when non-null (tests)
    long violations; String lastViolation;

    TrafficDirector(int me, Config cfg) { this.me = me; this.cfg = cfg; }

    /** In HALT the director lands every packet but counts nothing (4.8). */
    boolean halted(long t) { return ctl != null && !ctl.running(t); }

    @Override public void runCycle(long t) {
        if (!in.ready(t)) return;
        long d = in.peekA(t), h = in.peekB(t), hi = in.peekC(t);
        in.take(t);
        accepted++;
        if (log != null) log.add(new long[] { t, d, h, hi });
        int mode = LinkWord.mode(h), src = LinkWord.src(h);
        perMode[mode]++;
        if (src == LinkWord.CNC) fromCnc++;
        if (LinkWord.readPortTag(h)) { readPortResults++; if (readPort != null) readPort.acceptedAt(t); }
        else if (readPort != null && readPort.outstanding() > 0 && src != me && mode != LinkWord.M_CMD) {
            readPortOverlaps++;
            if (cfg.checkReadPortOverlap) violate("packet arrived with " + readPort.outstanding() + " read-port result(s) outstanding (spec 4.3's rule, invariant 14; read-port overlap): " + LinkWord.describe(d, h, hi));
        }
        if (LinkWord.badPacket(h)) { droppedBad++; badModeTotal++; return; }      // dropped, uncounted, condition 1
        if (LinkWord.size(h) == LinkWord.SIZE_UNDEF) {                             // undefined (3.1): meshsim's choice, flagged, dropped, no condition
            undefinedSize++;
            violate("packet with size 11 (undefined, 3.1; invariant 5) arrived and was dropped: " + LinkWord.describe(d, h, hi));
            return;
        }
        boolean halted = halted(t);
        if (halted) haltedArrivals++;
        switch (mode) {
            case LinkWord.M_GATHER -> toGather.put(t, d, h, hi);
            case LinkWord.M_SRC, LinkWord.M_EXPLICIT, LinkWord.M_BUCKET, LinkWord.M_AUG -> { if (!halted) count(t, SRAM, d, h); toWriteEngine.put(t, d, h, hi); }
            case LinkWord.M_CMEM -> { if (!halted && count(t, CM, d, h)) countedPortB = t; checkCmem(d, h, hi); toPortB.put(t, d, h, hi); }
            case LinkWord.M_CMD -> toPortB.put(t, d, h, hi);
            case LinkWord.M_REPLY -> { if (!halted && count(t, REPLY, d, h)) countedPortB = t; toPortB.put(t, d, h, hi); }
            case LinkWord.M_PERIPH -> periph.accept(t, halted, d, h, hi);           // counted when it lands or is dropped (4.17.5)
            default -> throw new IllegalStateException("unreachable: mode " + mode);
        }
    }

    /** Decrement counter c for an accepted packet (in RUN); true (it is counted). */
    private boolean count(long t, int c, long d, long h) {
        countArrival(t, c, LinkWord.describe(d, h));
        return true;
    }
    /** One counted arrival for counter c at t (the director's, or the peripheral's landing): decrement it, or set
     *  unexpected arrival if it is at 0 (judged after this cycle's counter write: TileControl adds it first). */
    void countArrival(long t, int c, String what) {
        counted[c]++;
        int pend = ctl != null ? ctl.pendingAdd(c, t) : 0;
        if (remaining[c] + pend == 0) {
            unexpected++; unexpectedTotal++;
            if (cfg.checkUnexpected) violate("unexpected arrival, " + COUNTER_NAME[c] + " already 0 (invariant 9): " + what);
        } else { remaining[c]--; decAt[c] = t; }
    }
    /** The counter as it stood at the start of cycle t (an arrival accepted at t has already decremented it). */
    int counterAtStart(int c, long t) { return remaining[c] + (decAt[c] == t ? 1 : 0); }
    /** The cycle counter c was last decremented. */
    long lastDecrement(int c) { return decAt[c]; }

    static boolean inCommand(int w) { return w >= COMMAND_WORD && w < COMMAND_WORD + 16; }
    static boolean inReply(int w) { return w >= REPLY_WORD && w < REPLY_WORD + 256; }

    /** Invariant 11, per word (4.11): each of the packet's 1, 2 or 4 words inside instruction or data memory (packed
     *  0x000-0xFFF) and outside the command and reply blocks. One report per packet. */
    private void checkCmem(long d, long h, long hi) {
        int a = LinkWord.addr(h), n = LinkWord.chunks(LinkWord.size(h));
        for (int i = 0; i < n; i++) {
            int w = a + i;
            if (w > 0xFFF) { violate("mode-6 packet: word " + i + " outside controller memory (invariant 11): " + LinkWord.describe(d, h, hi)); return; }
            if (inCommand(w) || inReply(w)) { violate("mode-6 packet: word " + i + " in the " + (inCommand(w) ? "command" : "reply") + " block (invariant 11): " + LinkWord.describe(d, h, hi)); return; }
        }
    }

    /** cm_idle (spec 4.12): the port-B pipeline is empty. */
    boolean cmIdle(long t) { return toPortB.inFlight(t) == 0; }
    private long countedPortB = Long.MIN_VALUE / 2;
    /** No counted packet (mode 6 or 8) is in the port-B pipe at the end of t (invariant 4's check). */
    boolean countedCmIdle(long t) { return countedPortB + cfg.cmLatency <= t; }

    void violate(String m) { violations++; lastViolation = m; }
    void resetStats() { java.util.Arrays.fill(perMode, 0); accepted = droppedBad = unexpected = 0; }
}
