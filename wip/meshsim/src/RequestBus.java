import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.List;

/**
 * The request bus (spec 3.3, v1.2): a 16-bit, 8-ary tree from every tile's request port, and from the controllers, to
 * the targets' receive queues, which belong to the tree.
 *  - Eight row nodes (tiles 8r .. 8r + 7) and a root with one input per row node and one per controller (C&C 1-3,
 *    {@link #controllerSend}): requests from the controllers enter at the root, which stamps their requestor.
 *  - Every request is 128 bits, 8 beats of 16 bits, bits [15:0] first; beat 0 carries the requestor [6:0] and the
 *    target [9:7]. Every hop moves one beat a cycle with back-pressure: a beat leaves only when the next stage has room
 *    (meshsim: credit flow over Config.reqHopLatency-cycle pipelined wires into a 16-byte, one-request buffer per node
 *    input). A request's beats stay together (wormhole per request).
 *  - A row node round-robins over its inputs holding a request's first beat, from the one after the input it last
 *    served, and writes the requestor 8r + c.
 *  - The root round-robins the same way over its inputs, skipping an input whose head request's target has no credit,
 *    and forwards each request whole into its target's receive queue. The receive queues (4.18): target 0 (C&C 1) 128,
 *    target 1 (C&C 2) 256, target 2 (C&C 3) 128 entries of 128 bits (Config.receiveQueueDepth); one credit counter per
 *    target, starting at its queue's depth: forwarding takes one, each pop ({@link #receive}) returns one (usable the
 *    next cycle). The root passes at most one beat a cycle in all.
 *  - A request for a reserved target (4-7) or target 3 (deferred with the file system, item 119) is dropped at the
 *    root (its beats consumed) and flagged (6.7).
 *  - Head-of-line blocking stays inside a row (accepted): a request waiting for a target with no credit holds its row
 *    node's link to the root, and every request of that row behind it.
 *  - The receivers are C&C 1-3, Java models in meshsim: they (and tests) read a queue through {@link #pending},
 *    {@link #get} and {@link #receive}, as the controllers' REQUEST_GET_W0-W2 and REQUEST_RECEIVE would.
 *  - PHASE-2 INTERIM: HALT doesn't reset the bus (phase 4's reset empties it all, 4.18).
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
    static final int ROOT_INPUTS = 11, TARGETS = 3;                             // 8 rows + C&C 1-3; targets 0-2 have queues
    final Buf[][] rowIn = new Buf[8][8]; final Buf[] rootIn = new Buf[ROOT_INPUTS];
    private final int[] rowCur = new int[8], rowRr = new int[8]; private int rootCur = -1, rootRr = ROOT_INPUTS - 1; private boolean rootDropping;
    final int[] credits = new int[8];
    /** The receive queues, per target: {w0, w1, w2, w3, deliveredT, storeT}; readable from the cycle after delivery. */
    @SuppressWarnings("unchecked") final ArrayDeque<long[]>[] queue = new ArrayDeque[TARGETS];
    long underflows; final long[] received = new long[TARGETS];
    private final ArrayDeque<long[]> creditReturns = new ArrayDeque<>();    // {usable from, target}
    private final ArrayDeque<Object[]> toReceiver = new ArrayDeque<>();      // {arrival of the last beat, Req}
    final List<Req> delivered = new ArrayList<>(), dropped = new ArrayList<>();
    long beatsAtRoot, violations; String lastViolation;

    RequestBus(Chip chip) {
        this.chip = chip; cfg = chip.cfg; lat = Math.max(1, cfg.reqHopLatency);
        for (int r = 0; r < 8; r++) { rowCur[r] = -1; rowRr[r] = 7; for (int c = 0; c < 8; c++) rowIn[r][c] = new Buf(); }
        for (int i = 0; i < ROOT_INPUTS; i++) rootIn[i] = new Buf();
        for (int k = 0; k < TARGETS; k++) { queue[k] = new ArrayDeque<>(); credits[k] = cfg.receiveQueueDepth[k]; }
    }
    /** A target has a receive queue (0-2); 3 is deferred, 4-7 reserved. */
    static boolean hasQueue(int target) { return target < TARGETS; }

    // ---- the receivers' side (C&C 1-3, as their REQUEST_GET_W0-W2 and REQUEST_RECEIVE would read it)
    private long[] head(int target, long t) { long[] h = queue[target].peek(); return h != null && h[4] < t ? h : null; }
    boolean pending(int target, long t) { return head(target, t) != null; }
    int get(int target, long t, int i) { long[] h = head(target, t); return h == null ? 0 : (int) h[i]; }
    /** Bits [127:96] of target's oldest request, popped at the end of t, returning a credit; 0 when empty (underflow). */
    int receive(int target, long t) {
        long[] h = head(target, t);
        if (h == null) { underflows++; return 0; }
        queue[target].poll(); received[target]++;
        returnCredit(target, t);
        return (int) h[3];
    }
    /** The whole request at target's head, {w0, w1, w2, w3}, without popping (tests and the C&C models); null when empty. */
    long[] peekRequest(int target, long t) { long[] h = head(target, t); return h == null ? null : new long[] { h[0], h[1], h[2], h[3] }; }
    /** A controller's request (C&C 1-3, endpoint 64 + cc), entering at the root: its 8 beats arrive over one hop. */
    void controllerSend(long t, int cc, long w0, long w1, long w2, long w3) {
        Buf b = rootIn[8 + cc];
        b.reqs.add(new Req(new long[] { (w0 & ~0x7FL) | (64 + cc), w1, w2, w3 }, t, 64 + cc));
        for (int k = 0; k < BEATS; k++) { b.occ++; b.arrivals.add(t + lat + k); }
    }
    void returnCredit(int target, long t) { if (target >= 0) creditReturns.add(new long[] { t + 1, target }); }

    @Override public void runCycle(long t) {
        while (!creditReturns.isEmpty() && creditReturns.peek()[0] <= t) credits[(int) creditReturns.poll()[1]]++;
        while (!toReceiver.isEmpty() && (long) toReceiver.peek()[0] <= t) {
            Object[] d = toReceiver.poll(); Req q = (Req) d[1];
            q.deliveredAt = t; delivered.add(q);
            int tg = q.target();
            if (queue[tg].size() >= cfg.receiveQueueDepth[tg]) { violations++; lastViolation = "SIMULATOR: target " + tg + "'s receive queue overflowed (the root's credits should prevent it)"; }
            else queue[tg].add(new long[] { q.w[0], q.w[1], q.w[2], q.w[3], t, q.storeT });
        }
        for (Buf b : rootIn) b.arrive(t);
        for (int r = 0; r < 8; r++) for (int c = 0; c < 8; c++) rowIn[r][c].arrive(t);
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
                Req q = rootIn[rootCur].head(); int tg = q.target();
                rootDropping = !hasQueue(tg);
                if (rootDropping) { violations++; lastViolation = "a request from unit " + q.srcTile + " for target " + tg + (tg >= 4 ? " (reserved)" : " (no receiver: deferred with the file system, item 119)") + " dropped at the root (3.3, 6.7)"; }
                else credits[tg]--;
            }
        }
        if (rootCur >= 0) {
            Buf in = rootIn[rootCur]; Req q = in.head();
            if (q.sent < q.arrived) {
                in.occ--; beatsAtRoot++;
                if (++q.sent == BEATS) {
                    in.reqs.poll(); rootRr = rootCur; rootCur = -1;
                    if (rootDropping) dropped.add(q); else toReceiver.add(new Object[] { t + lat, q });
                }
            }
        }
    }
    /** The next input (after `last`, round-robin) whose head request has its first beat in; at the root, one whose target
     *  has a credit or no receiver (a drop needs none). -1: none. */
    private int choose(Buf[] in, int last, boolean root) {
        int n = in.length;
        for (int i = 1; i <= n; i++) {
            int c = (last + i) % n; Req q = in[c].head();
            if (q == null || q.arrived == 0) continue;
            if (root) { int tg = q.target(); if (hasQueue(tg) && credits[tg] <= 0) continue; }
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
