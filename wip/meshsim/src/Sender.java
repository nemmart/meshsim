import java.util.ArrayList;
import java.util.List;

/**
 * The sender (spec 4.17.9, 8.12): moves one transfer at a time from one of the peripheral's output queues to the
 * sender's output port.
 *  - A store to SENDER_SEND at t starts a send of n 32-bit words (word count [28:13]) from queue q ([12:11]) to dest
 *    ([6:0]) with mode ([10:7]): sender_busy is 1 from the end of t; the peripheral is told at t (outStart), so it
 *    produces those words, and nothing unsolicited.
 *  - Packets: floor(n / 4) 128-bit packets, then the remainder as a 64-bit packet (2), a 32-bit one (1), or a 64-bit and
 *    a 32-bit one (3); words in queue order, the first in a packet's bits [31:0]. At most one packet a cycle; the sender
 *    takes words as the peripheral makes them available (up to four a cycle) and waits while its queue is empty.
 *  - addr: for modes 3, 6 and 9 SENDER_ADDR, advancing by each packet's size in words; other modes carry 0 (Proposed).
 *  - sender_busy falls at the end of the cycle the last packet leaves; the tile posts sender idle (WAKE_EVENTS [20]).
 *    A count of 0: busy rises and falls in the next cycle, and [20] is posted.
 *  - A store while busy, on a tile whose peripheral has no output (every mesh tile today), or naming an output queue the
 *    peripheral doesn't have, is ignored, and meshsim flags it. The sender checks nothing else (bad modes, dest).
 *  - HALT and a halted controller abandon a send in progress: busy falls with no sender-idle event.
 * The output port: on tiles 0-63 nothing is attached (the connection waits for the endpoint map, item 82); meshsim
 * records what leaves in {@link #sink}, a test-only recording sink (DEVIATIONS.md).
 */
final class Sender {
    final Tile tile;
    int addr;                                        // SENDER_ADDR (sticky)
    private boolean busy;
    private int dest, mode, q, left;                 // the send in progress: words still to send
    private final int[] buf = new int[4]; private int have;
    private long abandonedAt = Long.MIN_VALUE, startedAt = Long.MIN_VALUE;
    long ignored, sends, packets;
    /** The test-only sink: {t, dest, mode, addr, size, payload [63:0], payload [127:64]} of every packet sent. */
    final List<long[]> sink = new ArrayList<>();

    Sender(Tile tile) { this.tile = tile; }
    boolean busy() { return busy; }
    boolean abandonedAt(long t) { return abandonedAt == t; }

    /** A SENDER_SEND store at t (applied at the end of t). */
    void start(long t, int v) {
        Peripheral p = tile.slot.p;
        int qq = Registers.SN_QUEUE.get(v);
        if (busy) { ignored++; tile.ctl.violate(t, "SENDER_SEND while sender_busy: ignored (4.17.9)"); return; }
        if (p == null || p.outputQueues() == 0) { ignored++; tile.ctl.violate(t, "SENDER_SEND on a tile whose peripheral has no output: ignored (4.17.9)"); return; }
        if (qq >= p.outputQueues()) { ignored++; tile.ctl.violate(t, "SENDER_SEND naming output queue " + qq + ", which the peripheral doesn't have: ignored (4.17.9)"); return; }
        dest = Registers.SN_DEST.get(v); mode = Registers.SN_MODE.get(v); q = qq; left = Registers.SN_COUNT.get(v); have = 0;
        busy = true; startedAt = t; sends++;
        if (left > 0) p.outStart(t, q, left);
    }
    /** The size in words of the next packet with `left` words to go: 4, then 2 for 2 and 3, 1 for 1. */
    private static int nextWords(int left) { return left >= 4 ? 4 : left >= 2 ? 2 : 1; }

    void runCycle(long t) {
        if (!busy || t <= startedAt) return;
        if (left == 0) { busy = false; return; }
        Peripheral p = tile.slot.p;
        int need = nextWords(left);
        while (have < need && p.outReady(t, q)) buf[have++] = p.outTake(t, q);
        if (have < need) return;
        int size = need == 4 ? LinkWord.SIZE_128 : need == 2 ? LinkWord.SIZE_64 : LinkWord.SIZE_32;
        boolean addrMode = mode == LinkWord.M_EXPLICIT || mode == LinkWord.M_CMEM || mode == LinkWord.M_PERIPH;
        long lo = buf[0] & 0xFFFFFFFFL, hi = 0;
        if (need >= 2) lo |= (long) buf[1] << 32;
        if (need == 4) hi = (buf[2] & 0xFFFFFFFFL) | (long) buf[3] << 32;
        sink.add(new long[] { t, dest, mode, addrMode ? addr : 0, size, lo, hi });
        packets++;
        if (addrMode) addr = (addr + need) & 0x1FFFFF;
        left -= need; have = 0;
        if (left == 0) busy = false;
    }
    /** HALT or a halted controller: a send in progress is abandoned, with no sender-idle event (4.17.7). */
    void haltReset(long t) { if (busy) { busy = false; abandonedAt = t; left = 0; have = 0; } }
}
