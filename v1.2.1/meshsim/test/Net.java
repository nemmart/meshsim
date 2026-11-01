import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.List;

/**
 * Test harness for A Chip plus per-tile packet queues fed through each tile's read-return holding register
 * (the injection point's top priority, so a packet scheduled for cycle T is taken by the injection point at T),
 * and per-tile arrival logs at the traffic director. Payloads carry (seq << 32 | injection cycle).
 */
final class Net {
    final Chip c;
    @SuppressWarnings("unchecked") final ArrayDeque<long[]>[] q = new ArrayDeque[64];
    @SuppressWarnings("unchecked") final ArrayDeque<long[]>[][] rq = new ArrayDeque[64][3];   // read-queue client requests
    long late;                                  // packets taken later than scheduled (must stay 0)
    final List<long[]> cncSends = new ArrayList<>();   // {T, dst, mode, addr, payload} for C&C, in order

    Net(Config cfg) {
        c = new Chip(cfg);
        for (int i = 0; i < 64; i++) { q[i] = new ArrayDeque<>(); c.tiles[i].dir.log = new ArrayList<>(); for (int k = 0; k < 3; k++) rq[i][k] = new ArrayDeque<>(); }
    }

    static Config cfg(long shuffle) { Config k = Config.defaults(); k.shuffleSeed = shuffle; k.checkUnexpected = false; k.checkErrors = false; return k; }   // network tests count loosely: ERRORS [2] is expected

    /** Schedule tile's packet for injection at cycle T (nondecreasing T per tile): 64 bits, on counter 1 (v1.2: modes 1-3
     *  are always counted; network tests run with Config.checkUnexpected off). */
    void at(int tile, long T, int dst, int mode, int addr, long payload) { at(tile, T, dst, mode, 1, addr, LinkWord.SIZE_64, payload, 0); }
    /** The same, with the counter field, the packet's size and its 128-bit payload. */
    void at(int tile, long T, int dst, int mode, int counter, int addr, int size, long lo, long hi) {
        q[tile].add(new long[] { T, lo, LinkWord.hdr(0, dst, mode, counter, addr, size), hi });
    }
    /** A v1.1 packed controller-memory word (0x000-0x7FF program memory, 0x800-0xFFF data memory) as its v1.2 packet
     *  address (regions 0x01 and 0x00, byte offsets): for tests carried over from v1.1. */
    static int cm(int packedWord) { return packedWord < 0x800 ? LinkWord.R_PMEM << 24 | packedWord << 2 : (packedWord - 0x800) << 2; }
    /** Payload with the injection cycle in the low half. */
    static long tagged(long seq, long T) { return seq << 32 | (T & 0xFFFFFFFFL); }

    /** Schedule a read-queue request from client cl of tile, presented from cycle T (FIFO order per client); dstAddr is
     *  the outgoing packet's 32-bit address (mode 0). */
    void rd(int tile, int cl, long T, int srcChunk, int width, int mode, int counter, int dst, int dstAddr) {
        rq[tile][cl].add(new long[] { T, SRAMReadQueue.reqA(srcChunk, width, mode, counter, dst), SRAMReadQueue.reqB(dstAddr) });
    }

    private void feed() {
        long t = c.cycle;
        for (int i = 0; i < 64; i++) for (int k = 0; k < 3; k++) {
            long[] h = rq[i][k].peek();
            WordFIFO f = c.tiles[i].srqClient[k];
            if (h == null || h[0] > t + 1 || f.size(t) >= 60) continue;
            f.put(t, h[1], h[2], 0);
            rq[i][k].poll();
        }
        for (int i = 0; i < 64; i++) {
            long[] h = q[i].peek();
            if (h == null || h[0] > t + 1 || c.tiles[i].readReturn.ready(t)) continue;
            if (h[0] < t + 1) late++;
            c.tiles[i].readReturn.put(t, h[1], h[2], h[3]);
            q[i].poll();
        }
        if (!cncSends.isEmpty() && cncSends.get(0)[0] <= t + 1 && !c.cnc.send.ready(t)) {
            long[] s = cncSends.remove(0);
            int ctr = s.length >= 8 ? (int) s[7] : 1;
            if (s.length >= 7) c.cnc.send(t, (int) s[1], (int) s[2], ctr, (int) s[3], (int) s[5], s[4], s[6]);   // {T, dst, mode, addr, lo, size, hi[, counter]}
            else c.cnc.send(t, (int) s[1], (int) s[2], ctr, (int) s[3], LinkWord.SIZE_64, s[4], 0);
        }
    }
    boolean pending() {
        for (ArrayDeque<long[]> x : q) if (!x.isEmpty()) return true;
        for (ArrayDeque<long[]>[] r : rq) for (ArrayDeque<long[]> x : r) if (!x.isEmpty()) return true;
        return !cncSends.isEmpty();
    }

    void step() { feed(); c.step(); }
    /** Run until every queue is empty and the machine is quiescent. */
    boolean runAll(long max) {
        for (long i = 0; i < max; i++) { if (!pending() && c.quiescent()) { feed(); c.scanOverflows(); return true; } step(); }
        c.scanOverflows();
        return false;
    }
    List<long[]> log(int tile) { return c.tiles[tile].dir.log; }
    void clearLogs() { for (Tile p : c.tiles) p.dir.log.clear(); for (Endpoint e : c.ccs) e.received.clear(); }
    int maxRouterPeak() { int m = 0; for (Tile p : c.tiles) for (WordFIFO[] fs : p.router.q) for (WordFIFO f : fs) m = Math.max(m, f.highWater); return m; }
}
