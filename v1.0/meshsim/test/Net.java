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

    static Config cfg(long shuffle) { Config k = Config.defaults(); k.shuffleSeed = shuffle; k.checkUnexpected = false; return k; }

    /** Schedule tile's packet for injection at cycle T (nondecreasing T per tile). */
    void at(int tile, long T, int dst, int mode, int addr, long payload) {
        q[tile].add(new long[] { T, payload, LinkWord.hdr(0, dst, mode, addr) });
    }
    /** Payload with the injection cycle in the low half. */
    static long tagged(long seq, long T) { return seq << 32 | (T & 0xFFFFFFFFL); }

    /** Schedule a read-queue request from client cl of tile, presented from cycle T (FIFO order per client). */
    void rd(int tile, int cl, long T, int srcChunk, int width, int mode, int dst, int dstAddr, int reply) {
        rq[tile][cl].add(new long[] { T, SRAMReadQueue.reqA(srcChunk, width, mode, dst), SRAMReadQueue.reqB(dstAddr, reply) });
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
            c.tiles[i].readReturn.put(t, h[1], h[2], 0);
            q[i].poll();
        }
        if (!cncSends.isEmpty() && cncSends.get(0)[0] <= t + 1 && !c.cnc.send.ready(t)) {
            long[] s = cncSends.remove(0);
            c.cnc.send(t, (int) s[1], (int) s[2], (int) s[3], s[4]);
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
    void clearLogs() { for (Tile p : c.tiles) p.dir.log.clear(); c.cnc.received.clear(); c.memsub.received.clear(); }
    int maxRouterPeak() { int m = 0; for (Tile p : c.tiles) for (WordFIFO[] fs : p.router.q) for (WordFIFO f : fs) m = Math.max(m, f.highWater); return m; }
}
