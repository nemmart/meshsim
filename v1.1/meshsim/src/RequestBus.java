import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.List;

/**
 * The request bus (spec 3.3): a 16-bit, 8-ary tree from every tile's request port to the targets' receivers.
 *  - Eight row nodes (tiles 8r .. 8r + 7) and a root with one input per row node (and one per sending controller: none
 *    in meshsim, whose controllers wait for the endpoint map, item 82).
 *  - Every request is 128 bits, 8 beats of 16 bits, bits [15:0] first; beat 0 carries the requestor [6:0] and the
 *    target [9:7]. Every hop moves one beat a cycle with back-pressure: a beat leaves only when the next stage has room
 *    (meshsim: credit flow over Config.reqHopLatency-cycle pipelined wires into a 16-byte, one-request buffer per node
 *    input). A request's beats stay together (wormhole per request).
 *  - A row node round-robins over its inputs holding a request's first beat, from the one after the input it last
 *    served, and writes the requestor 8r + c.
 *  - The root round-robins the same way over its inputs, skipping an input whose head request's target has no credit,
 *    and forwards each request whole over one link per target to its receiver's receive queue (4.18). One credit
 *    counter per target, 16 at reset (the receive queue's depth): forwarding takes one, each REQUEST_RECEIVE pop
 *    returns one (usable the next cycle). The root passes at most one beat a cycle in all.
 *  - A request for a reserved target (4-7) or a target with no receiver is dropped at the root (its beats consumed) and
 *    flagged (6.7). In meshsim no target has a receiver unless a test straps one (Config.targetReceiver).
 *  - Head-of-line blocking stays inside a row (owner, 2026-10-26): a request waiting for a target with no credit holds
 *    its row node's link to the root, and every request of that row behind it.
 *  - The bus is not reset by HALT: a halted tile's FIFO drains (4.18, 6.1).
 * It runs first in every cycle (Chip), so a store's canPush sees the entry the bus frees in that cycle.
 */
final class RequestBus implements Component {
    static final int BEATS = 8;
    static final class Req {
        final long[] w; final long storeT; final int srcTile; int arrived, sent; long deliveredAt = -1;
        Req(long[] w, long storeT, int srcTile) { this.w = w; this.storeT = storeT; this.srcTile = srcTile; }
        int target() { return (int) (w[0] >>> 7) & 7; }
    }
    /** A node input: a 16-byte (one-request) buffer; occ counts beats in it and on their way to it. */
    static final class Buf {
        final ArrayDeque<Req> reqs = new ArrayDeque<>(); final ArrayDeque<Long> arrivals = new ArrayDeque<>(); int occ;
        void arrive(long t) { while (!arrivals.isEmpty() && arrivals.peek() <= t) { arrivals.poll(); for (Req r : reqs) if (r.arrived < BEATS) { r.arrived++; break; } } }
        Req head() { return reqs.peek(); }
    }
    final Chip chip; final Config cfg; final int lat, bufBeats = 16 / 2;
    final Buf[][] rowIn = new Buf[8][8]; final Buf[] rootIn = new Buf[8];
    private final int[] rowCur = new int[8], rowRr = new int[8]; private int rootCur = -1, rootRr = 7; private boolean rootDropping;
    final int[] credits = new int[8];
    private final ArrayDeque<long[]> creditReturns = new ArrayDeque<>();    // {usable from, target}
    private final ArrayDeque<Object[]> toReceiver = new ArrayDeque<>();      // {arrival of the last beat, Req, receiver tile}
    final List<Req> delivered = new ArrayList<>(), dropped = new ArrayList<>();
    long beatsAtRoot, violations; String lastViolation;

    RequestBus(Chip chip) {
        this.chip = chip; cfg = chip.cfg; lat = Math.max(1, cfg.reqHopLatency);
        for (int r = 0; r < 8; r++) { rowCur[r] = -1; rowRr[r] = 7; rootIn[r] = new Buf(); for (int c = 0; c < 8; c++) rowIn[r][c] = new Buf(); }
        java.util.Arrays.fill(credits, cfg.receiveQueueDepth);
    }
    int receiverOf(int target) { return cfg.targetReceiver != null && target < cfg.targetReceiver.length ? cfg.targetReceiver[target] : -1; }
    void returnCredit(int target, long t) { if (target >= 0) creditReturns.add(new long[] { t + 1, target }); }

    @Override public void runCycle(long t) {
        while (!creditReturns.isEmpty() && creditReturns.peek()[0] <= t) credits[(int) creditReturns.poll()[1]]++;
        while (!toReceiver.isEmpty() && (long) toReceiver.peek()[0] <= t) {
            Object[] d = toReceiver.poll(); Req q = (Req) d[1];
            q.deliveredAt = t; delivered.add(q);
            chip.tiles[(int) d[2]].req.deliver(t, q.w, q.storeT);
        }
        for (int r = 0; r < 8; r++) { rootIn[r].arrive(t); for (int c = 0; c < 8; c++) rowIn[r][c].arrive(t); }
        // leaves: each tile's FIFO head, one beat a cycle into its row node's input (upstream first: start-of-cycle room)
        for (Tile p : chip.tiles) {
            RequestPort port = p.req;
            long[] h = port.fifo.peek();
            if (h == null || h[4] >= t) continue;                // a push at t is the bus's from t + 1
            Buf b = rowIn[p.idx >> 3][p.idx & 7];
            if (b.occ >= bufBeats) continue;
            if (port.beatsSent == 0) b.reqs.add(new Req(new long[] { h[0], h[1], h[2], h[3] }, h[4], p.idx));
            b.occ++; b.arrivals.add(t + lat);
            if (++port.beatsSent == BEATS) { port.fifo.poll(); port.beatsSent = 0; port.freedAt = t; }
        }
        // row nodes: wormhole, round-robin over inputs holding a first beat; stamp the requestor
        for (int r = 0; r < 8; r++) {
            if (rowCur[r] < 0) rowCur[r] = choose(rowIn[r], rowRr[r], false);
            int c = rowCur[r]; if (c < 0) continue;
            Buf in = rowIn[r][c], out = rootIn[r]; Req q = in.head();
            if (q.sent >= q.arrived || out.occ >= bufBeats) continue;
            if (q.sent == 0) { long[] w = q.w.clone(); w[0] = (w[0] & ~0x7FL) | (8 * r + c); out.reqs.add(new Req(w, q.storeT, q.srcTile)); }
            out.occ++; out.arrivals.add(t + lat); in.occ--;
            if (++q.sent == BEATS) { in.reqs.poll(); rowRr[r] = c; rowCur[r] = -1; }
        }
        // the root: round-robin skipping inputs whose head request's target has no credit; one beat a cycle
        if (rootCur < 0) {
            rootCur = choose(rootIn, rootRr, true);
            if (rootCur >= 0) {
                Req q = rootIn[rootCur].head(); int tg = q.target(), rcv = receiverOf(tg);
                rootDropping = tg >= 4 || rcv < 0;
                if (rootDropping) { violations++; lastViolation = "a request from tile " + q.srcTile + " for target " + tg + (tg >= 4 ? " (reserved)" : " (no receiver)") + " dropped at the root (3.3, 6.7)"; }
                else credits[tg]--;
            }
        }
        if (rootCur >= 0) {
            Buf in = rootIn[rootCur]; Req q = in.head();
            if (q.sent < q.arrived) {
                in.occ--; beatsAtRoot++;
                if (++q.sent == BEATS) {
                    in.reqs.poll(); rootRr = rootCur; rootCur = -1;
                    if (rootDropping) dropped.add(q); else toReceiver.add(new Object[] { t + lat, q, receiverOf(q.target()) });
                }
            }
        }
    }
    /** The next input (after `last`, round-robin) whose head request has its first beat in; at the root, one whose target
     *  has a credit or no receiver (a drop needs none). -1: none. */
    private int choose(Buf[] in, int last, boolean root) {
        for (int i = 1; i <= 8; i++) {
            int c = (last + i) & 7; Req q = in[c].head();
            if (q == null || q.arrived == 0) continue;
            if (root) { int tg = q.target(); if (tg < 4 && receiverOf(tg) >= 0 && credits[tg] <= 0) continue; }
            return c;
        }
        return -1;
    }
    boolean idle() {
        if (!toReceiver.isEmpty()) return false;
        for (Buf b : rootIn) if (b.occ > 0 || !b.reqs.isEmpty()) return false;
        for (Buf[] row : rowIn) for (Buf b : row) if (b.occ > 0 || !b.reqs.isEmpty()) return false;
        return true;
    }
}
