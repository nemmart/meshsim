import java.util.ArrayDeque;

/**
 * The controller read port (spec 4.3, v1.2). A store to SRAM_RD_ADDR_QUEUE queues a read of 32, 64 or 128 bits (the setup
 * register SRAM_RD_DEST_ADDR's size) at a 21-bit chunk address, with the setup register's current destination (a byte
 * offset in data memory, [16:0]) and counter, after which the destination advances by the size in bytes
 * ({@link TileControl}); the store never stalls. The request queue holds {@code readPortDepth} (64, guaranteed at least
 * 64) entries and drains into the SRAM read queue at the lowest priority (client 2). Each return becomes one mode-0
 * packet to the tile itself (dst = me, address = 0x00000000 + the destination, size and counter = the entry's, payload =
 * the bits read) and leaves through the injection point like any other read return; it loops back through the router and
 * the director writes it through port B, decrementing the entry's counter K cycles later.
 *
 * Alignment (4.3, D32): a read address not aligned to the size is still queued; the SRAM read queue doesn't perform it,
 * its packet goes out with payload 0 and ERRORS [1] is set (v1.1 dropped it and set [7]). A store with no free entry is
 * dropped and sets the read-port error, ERRORS [7] (4.13); the destination still advances (the overflow is also a
 * violation: invariant 8's last sentence). Size 11 is undefined: meshsim flags it and queues a zero-width read.
 *
 * Results outstanding ({@link #outstanding}) = reads queued - results the director has accepted ({@link #acceptedAt}); the
 * director's overlap check (the software rule of 4.3) uses it. Returns are recognised by a simulator-only tag on the
 * packet ({@link LinkWord#TAG_READPORT}, header bit 63), not by any hardware field.
 */
final class ReadPort implements Component {
    final int me;
    final int depth;
    final WordFIFO q;                       // entries waiting for the SRAM read queue (client 2)
    private final ArrayDeque<long[]> host = new ArrayDeque<>();  // test back door: reads queued from Java
    long stores, injected, accepted, overflows, errors, issuedReads;
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

    /** A store to SRAM_RD_ADDR_QUEUE at t (the controller's, or the host's), with the setup register's destination (a
     *  byte offset in data memory), size and counter; false = dropped (no free entry: the read-port error). */
    boolean store(long t, int addr, int dest, int size, int counter) {
        addr &= 0x1FFFFF;
        if (size == LinkWord.SIZE_UNDEF) { violations++; lastViolation = "read-port read with the reserved size 11 (8.8): undefined"; }
        if (free(t) <= 0) {
            overflows++; errors++; violations++;
            lastViolation = "SRAM_RD_ADDR_QUEUE store with no free entry: read of 0x" + Integer.toHexString(addr) + " dropped, ERRORS [7] (4.3; invariant 8)";
            return false;
        }
        int a = (LinkWord.R_DMEM << 24) | (dest & 0x1FFFF);
        if (!LinkWord.aligned(a, size)) { violations++; lastViolation = "read-port destination 0x" + Integer.toHexString(a) + " not aligned to its size: its result is a bad packet (4.3; invariant 20)"; }
        q.put(t, SRAMReadQueue.reqA(addr, size, LinkWord.M_WRITE, counter, me), SRAMReadQueue.reqB(a), 0);
        stores++; issuedReads++;
        int occ = inUse(); if (occ > peak) peak = occ;
        return true;
    }
    /** Test convenience: a 32-bit read on counter 2. */
    boolean store(long t, int addr, int dest) { return store(t, addr, dest, LinkWord.SIZE_32, 2); }
    /** The injection point injected a read-port return at t: its entry is free from t + 1. */
    void injectedAt(long t) {
        if (t != injT) { injBefore += injNow; injNow = 0; injT = t; }
        injNow++; injected++;
    }
    /** The director accepted a read-port return at t (it lands cm_latency later). */
    void acceptedAt(long t) { accepted++; }
    /** Test back door: queue 32-bit reads on counter 2 from Java (applied at the next cycle, as stores). */
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
