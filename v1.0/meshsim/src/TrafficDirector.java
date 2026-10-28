/**
 * The traffic director (spec 4.8): one unit straight after the eject selector and the only path into the tile.
 * It takes at most one packet per cycle and switches on its 4-bit mode alone (modes renumbered 9 October):
 *   1 -> the GATHER engine's request queue, 2, 3 and 4 -> the write engine's input FIFO, 5/6/7 -> port B of the
 *   controller memories (a fixed-latency pipe of cm_latency, no queue), 0 and 8-15 -> dropped, setting the bad-mode
 *   sticky bit (spec 4.13; invariant 5 at the receiver; {@link #badModeTotal}).
 * It decrements the three down-counters (spec 4.12) as it accepts each packet: modes 2, 3 and 4 DMA_SRAM_REMAINING,
 * mode 5 DMA_CM_REMAINING, mode 7 DMA_REPLY_REMAINING; an arrival for a counter already at 0 leaves it at 0 and sets the
 * unexpected-arrival sticky bit (proposed, as implemented; invariant 9). cm_idle = the port-B pipe is empty.
 * Packets from C&C (src 64) are not counted (lead, 2 October, Q12: staging between phases is not unexpected traffic;
 * extended in M4 to DMA_CM_REMAINING, since boot loads instruction and data memory by mode 5 from C&C: DEV).
 * The controller reads a counter as it stood at the start of the cycle ({@link #counterAtStart}); its stores are
 * applied by TileControl at the end of the cycle and override that cycle's decrement.
 * Read-port results (4.3) arrive as ordinary mode-7 packets through the loopback; the
 * director recognises them only by the simulator-only {@link ReadPort#TAG} for accounting.
 *
 * Read-port overlap check (spec 4.3's software rule; 4 October: actual overlap in time): a packet from outside
 * the tile (src != me) accepted while the tile has read-port results outstanding (issued and not yet accepted here) is
 * a violation when Config.checkReadPortOverlap is set, and is always counted ({@link #readPortOverlaps}). C&C's mode-6
 * command words are exempt: C&C sends the next command right after each GO, so one routinely lands during a search
 * step (spec 6.6). C&C's other packets count as incoming.
 *
 * Address checks (spec 9, invariants 11-13; packed map, words 0x000-0x7FF instruction, 0x800-0xFFF data,
 * mailbox at mailboxBaseWord..+255): mode 5 must address two words inside the controller memories and outside
 * the mailbox; mode 7 one word of data memory outside the mailbox. The hardware has no check: the packet is
 * still delivered and the write lands where the decode puts it (ControllerMemories).
 */
final class TrafficDirector implements Component {
    static final int SRAM = 0, CM = 1, REPLY = 2;
    static final String[] COUNTER_NAME = { "SRAM_REMAINING", "CM_REMAINING", "REPLY_REMAINING" };
    static final int COUNTER_MASK = (1 << 20) - 1;

    final int me;
    final Config cfg;
    WordSource in;                                   // eject selector -> director
    WordSink toGather, toWriteEngine;                // input FIFOs (overflow-checked)
    ReadPort readPort;
    long readPortResults;
    long readPortOverlaps;                           // outside packets accepted with read-port results outstanding (monotonic)
    WordDelayLine toPortB;                           // modes 5-7, cm_latency
    final int[] remaining = new int[3];              // the down-counters (20 bits)
    private final long[] decAt = { Long.MIN_VALUE, Long.MIN_VALUE, Long.MIN_VALUE };
    TileControl ctl;
    long unexpected, unexpectedTotal;                // arrivals that met a counter at 0 (per run; monotonic)
    final long[] perMode = new long[LinkWord.N_MODES];
    long accepted, fromCnc;
    long droppedBad, badModeTotal;                   // bad modes (0, 8-15) dropped: per run; monotonic (the sticky bit's source)
    java.util.List<long[]> log;                      // {t, payload, hdr} of every accepted packet, when non-null (tests)
    long violations; String lastViolation;

    TrafficDirector(int me, Config cfg) { this.me = me; this.cfg = cfg; }

    @Override public void runCycle(long t) {
        if (!in.ready(t)) return;
        long d = in.peekA(t), h = in.peekB(t), tag = in.peekC(t);
        in.take(t);
        accepted++;
        if (log != null) log.add(new long[] { t, d, h });
        int mode = LinkWord.mode(h);
        perMode[mode]++;
        if (tag == ReadPort.TAG) { readPortResults++; if (readPort != null) readPort.acceptedAt(t); }
        else if (readPort != null && readPort.outstanding() > 0 && LinkWord.src(h) != me && !(LinkWord.src(h) == LinkWord.CNC && mode == LinkWord.M_MBOX)) {
            readPortOverlaps++;
            if (cfg.checkReadPortOverlap) violate("packet arrived with " + readPort.outstanding() + " read-port result(s) outstanding (spec 4.3's rule; read-port overlap): " + LinkWord.describe(d, h));
        }
        switch (mode) {
            case LinkWord.M_GATHER -> toGather.put(t, d, h, 0);
            case LinkWord.M_SRC, LinkWord.M_BUCKET, LinkWord.M_AUG -> { count(t, SRAM, d, h); toWriteEngine.put(t, d, h, 0); }
            case LinkWord.M_CMEM -> { if (count(t, CM, d, h)) countedPortB = t; checkCmem(d, h); toPortB.put(t, d, h, 0); }
            case LinkWord.M_MBOX -> toPortB.put(t, d, h, 0);
            case LinkWord.M_REPLY -> { if (count(t, REPLY, d, h)) countedPortB = t; checkReply(d, h); toPortB.put(t, d, h, 0); }
            default -> { droppedBad++; badModeTotal++; }                          // 0, 8-15: dropped, bad-mode sticky bit (4.13)
        }
    }


    /** Decrement counter c for an accepted packet; true if the packet is a counted one (not from C&C). */
    private boolean count(long t, int c, long d, long h) {
        if (LinkWord.src(h) == LinkWord.CNC) { fromCnc++; return false; }    // C&C's packets are never counted (Q12, extended: DEV)
        if (remaining[c] == 0) {
            unexpected++; unexpectedTotal++;
            if (cfg.checkUnexpected) violate("unexpected arrival, " + COUNTER_NAME[c] + " already 0 (invariant 9): " + LinkWord.describe(d, h));
        } else { remaining[c]--; decAt[c] = t; }
        return true;
    }
    /** The counter as it stood at the start of cycle t (an arrival accepted at t has already decremented it). */
    int counterAtStart(int c, long t) { return remaining[c] + (decAt[c] == t ? 1 : 0); }

    boolean inMailbox(int w) { return w >= cfg.mailboxBaseWord && w < cfg.mailboxBaseWord + 256; }

    private void checkCmem(long d, long h) {
        int a = LinkWord.addr(h);
        if (a > 0xFFE) violate("mode-5 packet outside controller memory (invariant 11): " + LinkWord.describe(d, h));
        else if (inMailbox(a) || inMailbox(a + 1)) violate("mode-5 packet into the mailbox range (invariant 11): " + LinkWord.describe(d, h));
    }
    private void checkReply(long d, long h) {
        int a = LinkWord.addr(h);
        if (a < 0x800 || a > 0xFFF) violate("mode-7 packet outside data memory (invariant 13): " + LinkWord.describe(d, h));
        else if (inMailbox(a)) violate("mode-7 packet into the mailbox range (invariant 12): " + LinkWord.describe(d, h));
    }

    /** cm_idle (spec 4.12): the port-B pipeline is empty. */
    boolean cmIdle(long t) { return toPortB.inFlight(t) == 0; }
    private long countedPortB = Long.MIN_VALUE / 2;
    /** No counted packet (mode 5 or 7, not from C&C) is in the port-B pipe at the end of t (invariant 4's check). */
    boolean countedCmIdle(long t) { return countedPortB + cfg.cmLatency <= t; }

    private void violate(String m) { violations++; lastViolation = m; }
    void resetStats() { java.util.Arrays.fill(perMode, 0); accepted = droppedBad = unexpected = 0; }
}
