import java.util.ArrayDeque;

/**
 * The controller read port (spec 4.3; results through the loopback). A store to
 * SRAM_RD_ADDR_QUEUE queues a 32-bit read of a 20-bit big-SRAM chunk address with the current SRAM_RD_DEST_ADDR (a
 * packed data-memory word), which then advances by 1 ({@link TileControl}); the store never stalls. The request queue
 * holds {@code readPortDepth} (64, guaranteed at least 64) entries and drains into the SRAM read queue at the lowest
 * priority (client 2). Each return becomes a mode-7 packet to the tile itself (dst = me, addr = the destination word,
 * payload = the 32 bits) and leaves through the injection point like any other read return; it loops back through the
 * router and the director writes it as an ordinary mode-7 packet, counting DMA_REPLY_REMAINING.
 *
 * An entry is freed when its return is injected ({@link #injectedAt}). Reading SRAM_RD_ADDR_QUEUE returns the free
 * entries as of the start of the cycle. A store with no free entry is dropped, sets the read-port-overflow sticky bit
 * and is a violation (spec 9, invariant 8's last sentence).
 *
 * Results outstanding ({@link #outstanding}) = stores - results the director has accepted ({@link #acceptedAt}); the
 * director's overlap check (the software rule of 4.3) uses it. Returns are recognised by a simulator-only tag on the
 * packet ({@link #TAG}, the bus's third word), not by any hardware field.
 */
final class ReadPort implements Component {
    /** Simulator-only tag on a read-port return's packet (the third bus word; no hardware bit). */
    static final long TAG = 1;
    final int me;
    final int depth;
    final WordFIFO q;                       // entries waiting for the SRAM read queue (client 2)
    private final ArrayDeque<long[]> host = new ArrayDeque<>();  // test back door: reads queued from Java
    long stores, injected, accepted, overflows, issuedReads;
    private long injBefore, injNow, injT = Long.MIN_VALUE;       // injections before the current cycle / in it
    int peak;                               // the most entries in use (accounting)
    long violations; String lastViolation;

    ReadPort(int me, Config cfg) { this.me = me; depth = cfg.readPortDepth; q = new WordFIFO("t" + me + ".readport.q", depth); }

    /** Returns injected before cycle t. */
    private long injectedBefore(long t) { return injBefore + (injT < t ? injNow : 0); }
    /** Free entries at the start of cycle t (stores before t; returns injected before t). */
    int free(long t) { return depth - (int) (stores - injectedBefore(t)); }
    /** Entries in use now (accounting). */
    int inUse() { return (int) (stores - injected); }
    /** Results issued and not yet accepted by the director. */
    long outstanding() { return stores - accepted; }

    /** A store to SRAM_RD_ADDR_QUEUE at t (the controller's, or the host's); false = dropped (overflow). */
    boolean store(long t, int addr, int dest) {
        if (free(t) <= 0) {
            overflows++; violations++;
            lastViolation = "MEM_RD_ADDR_QUEUE store with no free entry: read of 0x" + Integer.toHexString(addr & 0xFFFFF) + " dropped (invariant 8)";
            return false;
        }
        q.put(t, SRAMReadQueue.reqA(addr, Fields.SIZE_32, LinkWord.M_REPLY, me), SRAMReadQueue.reqB(dest & 0xFFF, 0), 0);
        stores++; issuedReads++;
        int occ = inUse(); if (occ > peak) peak = occ;
        return true;
    }
    /** The injection point injected a read-port return at t: its entry is free from t + 1. */
    void injectedAt(long t) {
        if (t != injT) { injBefore += injNow; injNow = 0; injT = t; }
        injNow++; injected++;
    }
    /** The director accepted a read-port return at t (it lands cm_latency later). */
    void acceptedAt(long t) { accepted++; }
    /** Test back door: queue reads from Java (applied at the next cycle, as stores); dest advances by 1 each. */
    void hostQueue(int addr, int dest) { host.add(new long[] { addr, dest }); }

    @Override public void runCycle(long t) {
        if (!host.isEmpty()) { long[] h = host.poll(); store(t, (int) h[0], (int) h[1]); }
    }
    /** HALT (13a): the queue and the in-flight count clear. */
    void haltReset() {
        q.clear(); host.clear();
        injBefore += injNow; injNow = 0; injT = Long.MIN_VALUE;
        stores = injected = accepted = injBefore;
    }
    /** Every entry free (every return injected) and nothing queued from the host. */
    boolean idle(long t) { return free(t) == depth && host.isEmpty(); }
    /** Every result issued has been accepted by the director. */
    boolean allAccepted() { return outstanding() == 0 && host.isEmpty(); }
}
