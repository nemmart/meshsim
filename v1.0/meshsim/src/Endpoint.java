import java.util.ArrayList;
import java.util.List;

/**
 * An endpoint on tile 0's fifth router port (spec 4.17, 7): C&C (address 64) or the memory subsystem (65), as far as
 * M1 needs it. {@link #send} is the holding register into tile 0's from-C&C route block (one packet per cycle, any
 * dst, mode and addr; src stamped here). {@link #rx} receives what the to-C&C selector delivers; the endpoint takes
 * one packet per cycle from it and records it. C&C's sequencer replaces the test that fills {@link #send}.
 */
final class Endpoint implements Component {
    final int address;
    final WordHold send;
    final WordFIFO rx;
    final List<long[]> received = new ArrayList<>();   // {t, payload, hdr}
    long sent;
    long violations; String lastViolation;

    Endpoint(int address, int depth) {
        this.address = address;
        send = new WordHold((address == LinkWord.CNC ? "cnc" : "memsub") + ".send");
        rx = new WordFIFO((address == LinkWord.CNC ? "cnc" : "memsub") + ".rx", depth);
    }

    /** Queue a packet in the send holding register (the caller checks canPut). Stamps src and checks invariant 5. */
    void send(long t, int dst, int mode, int addr, long payload) {
        if (LinkWord.badMode(mode)) { violations++; lastViolation = "mode-" + mode + " packet sent by " + address + " (invariant 5)"; }
        send.put(t, payload, LinkWord.hdr(address, dst, mode, addr), 0);
        sent++;
    }

    /** C&C: its sequencer takes packets from {@link #rx} into C&C's receive FIFO itself (CnC.runCycle), so that the
     *  FIFO's fill and the sequencer's reads are ordered within a cycle; {@link #received} stays the trace of every packet. */
    boolean drainedByCnC;
    @Override public void runCycle(long t) {
        if (drainedByCnC) return;
        if (rx.ready(t)) { received.add(new long[] { t, rx.peekA(t), rx.peekB(t) }); rx.take(t); }
    }
}
