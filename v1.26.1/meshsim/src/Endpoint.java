import java.util.ArrayList;
import java.util.List;

/**
 * A controller's end of its tile's fifth router port (spec 4.16): C&C 1, 2 or 3 (endpoints 64-66, at tiles 0-2).
 * {@link #send} is the holding register into the tile's from-controller route block (one packet per cycle, any dst,
 * mode, counter and address; src stamped here). {@link #rx} receives what the to-controller selector delivers; the
 * endpoint takes one packet per cycle from it and records it. C&C 1's behavioural model fills {@link #send}; tests drive
 * C&C 2 and C&C 3 until their firmware exists (6.2).
 */
final class Endpoint implements Component {
    final int address;
    final WordHold send;
    final WordFIFO rx;
    final List<long[]> received = new ArrayList<>();   // {t, payload [63:0], hdr, payload [127:64]}
    long sent;
    long violations; String lastViolation;

    Endpoint(int address, int depth) {
        this.address = address;
        send = new WordHold("cc" + (address - 63) + ".send");
        rx = new WordFIFO("cc" + (address - 63) + ".rx", depth);
    }

    /** Queue a packet in the send holding register (the caller checks canPut). Stamps src and checks invariant 5. */
    void send(long t, int dst, int mode, int counter, int addr, int size, long lo, long hi) {
        long h = LinkWord.hdr(address, dst, mode, counter, addr, size);
        String bad = LinkWord.whyBad(h, 1);
        if (bad != null) { violations++; lastViolation = "bad packet (" + bad + ") sent by " + address + " (3.1, invariant 5)"; }
        if (size == LinkWord.SIZE_UNDEF) { violations++; lastViolation = "packet with size 11, undefined, sent by " + address + " (3.1, invariant 5)"; }
        if (LinkWord.reservedMode(mode)) { violations++; lastViolation = "packet in the reserved mode " + mode + " sent by " + address + " (3.1, invariant 21)"; }
        send.put(t, LinkWord.maskLo(lo, size), h, LinkWord.maskHi(hi, size));
        sent++;
    }

    /** C&C: its sequencer takes packets from {@link #rx} into C&C's receive FIFO itself (CnC.runCycle), so that the
     *  FIFO's fill and the sequencer's reads are ordered within a cycle; {@link #received} stays the trace of every packet. */
    boolean drainedByCnC;
    @Override public void runCycle(long t) {
        if (drainedByCnC) return;
        if (rx.ready(t)) { received.add(new long[] { t, rx.peekA(t), rx.peekB(t), rx.peekC(t) }); rx.take(t); }
    }
}
