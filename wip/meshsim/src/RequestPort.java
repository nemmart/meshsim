import java.util.ArrayDeque;

/**
 * A tile's request port (spec 4.18, v1.2): two directions in every tile.
 *  - Send side: REQUEST_SET_W0-W2 and REQUEST_SEND (stores) push a 128-bit request into the request FIFO (128 entries,
 *    Config.requestFifoDepth), which drains into the tile's row node one 16-bit beat per cycle ({@link RequestBus}). A
 *    REQUEST_SEND store stalls only while the FIFO is full and no entry is freed in that cycle; request_busy = the FIFO
 *    is full, and its fall posts EVENTS [11]. W0-W2 clear at every push. The store pushes at the end of its cycle; the
 *    first beat leaves at t + 1 at the earliest.
 *  - Receive side: on tiles 0-63 connected to nothing (the receive queues are in the tree, read by C&C 1-3): the GET
 *    loads read 0, REQUEST_RECEIVE reads 0 and sets receive underflow (ERRORS [14]), request pending never rises.
 */
final class RequestPort {
    final Tile tile;
    final int[] setW = new int[3];
    final int depth;
    /** The request FIFO: {w0, w1, w2, w3} and its store cycle; an entry pushed at t is the bus's from t + 1. */
    final ArrayDeque<long[]> fifo = new ArrayDeque<>();     // {w0, w1, w2, w3, storeT}
    int beatsSent;                                          // of the FIFO's head request (the leaf link)
    long freedAt = Long.MIN_VALUE;                          // the cycle the bus freed the head entry
    long underflows, sends, stalledPushes;
    RequestBus bus;

    RequestPort(Tile tile, Config cfg) { this.tile = tile; depth = cfg.requestFifoDepth; }
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

    // ---- receive side: connected to nothing on a mesh tile (4.18)
    boolean pending(long t) { return false; }
    int get(long t, int i) { return 0; }
    /** REQUEST_RECEIVE: an empty queue, always: 0, and receive underflow (ERRORS [14]). */
    int receive(long t) { underflows++; return 0; }
    boolean idle() { return fifo.isEmpty(); }
}
