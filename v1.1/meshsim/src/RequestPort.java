import java.util.ArrayDeque;

/**
 * The tile's request port (spec 4.18): two directions in every tile.
 *  - Send side: REQUEST_SET_W0-W2 and REQUEST_SEND (stores) push a 128-bit request into the request FIFO (128 entries,
 *    Config.requestFifoDepth), which drains into the tile's row node one 16-bit beat per cycle ({@link RequestBus}). A
 *    REQUEST_SEND store stalls only while the FIFO is full and no entry is freed in that cycle; request_busy = the FIFO
 *    is full, and its fall posts WAKE_EVENTS [12]. W0-W2 clear at every push. The store pushes at the end of its cycle;
 *    the first beat leaves at t + 1 at the earliest.
 *  - Receive side: the 16-entry receive queue (Config.receiveQueueDepth), fed by the root's link for the target this tile
 *    receives (Config.targetReceiver, a test-only strap: on tiles 0-63 nothing feeds it, 4.18). REQUEST_GET_W0-W2 read the
 *    oldest request's words without popping (0 when empty); REQUEST_RECEIVE returns bits [127:96] and pops it, returning
 *    a credit to the root; on an empty queue it returns 0 and sets receive underflow (condition 14). REQUEST_PENDING
 *    (WAKE_EVENTS [19]) is live while the queue holds a request. HALT keeps the queue's contents; the send side and the
 *    tree keep running in HALT.
 */
final class RequestPort {
    final Tile tile;
    final int[] setW = new int[3];
    final int depth, rdepth;
    /** The request FIFO: {w0, w1, w2, w3} and its store cycle; an entry pushed at t is the bus's from t + 1. */
    final ArrayDeque<long[]> fifo = new ArrayDeque<>();     // {w0, w1, w2, w3, storeT}
    /** The receive queue: {w0, w1, w2, w3, deliveredT, storeT}; visible from the cycle after delivery. */
    final ArrayDeque<long[]> rq = new ArrayDeque<>();
    int beatsSent;                                          // of the FIFO's head request (the leaf link)
    long freedAt = Long.MIN_VALUE;                          // the cycle the bus freed the head entry
    long underflows, sends, received, stalledPushes;
    RequestBus bus;
    int receivesTarget = -1;                                // the target this tile's receive queue serves (-1: none, every tile 0-63)

    RequestPort(Tile tile, Config cfg) { this.tile = tile; depth = cfg.requestFifoDepth; rdepth = cfg.receiveQueueDepth; }
    void setW(int i, int v) { setW[i] = v; }
    /** A REQUEST_SEND store may complete at t: the FIFO has room, or an entry is freed this cycle (the bus runs first). */
    boolean canPush(long t) { return fifo.size() < depth || freedAt == t; }
    /** REQUEST_SEND at t: push the request (the controller stalled until canPush). */
    void send(long t, int w3) {
        if (fifo.size() >= depth) { stalledPushes++; tile.ctl.violate(t, "SIMULATOR: a request pushed into a full request FIFO"); return; }
        fifo.add(new long[] { setW[0] & 0xFFFFFFFFL, setW[1] & 0xFFFFFFFFL, setW[2] & 0xFFFFFFFFL, w3 & 0xFFFFFFFFL, t });
        setW[0] = setW[1] = setW[2] = 0; sends++;
    }
    /** request_busy (4.18): the FIFO is full. */
    boolean busy(long t) { return fifo.size() >= depth; }

    // ---- receive side
    private long[] head(long t) { long[] h = rq.peek(); return h != null && h[4] < t ? h : null; }
    boolean pending(long t) { return head(t) != null; }
    int get(long t, int i) { long[] h = head(t); return h == null ? 0 : (int) h[i]; }
    /** REQUEST_RECEIVE sampling at t: bits [127:96] of the oldest request, popped at the end of t; a credit to the root. */
    int receive(long t) {
        long[] h = head(t);
        if (h == null) { underflows++; return 0; }
        rq.poll(); received++;
        if (bus != null) bus.returnCredit(receivesTarget, t);
        return (int) h[3];
    }
    /** The root's link delivers a request at t (its last beat): readable from t + 1. */
    void deliver(long t, long[] w, long storeT) {
        if (rq.size() >= rdepth) { tile.ctl.violate(t, "SIMULATOR: the receive queue overflowed (the root's credits should prevent it)"); return; }
        rq.add(new long[] { w[0], w[1], w[2], w[3], t, storeT });
    }
    boolean idle() { return fifo.isEmpty(); }
}
