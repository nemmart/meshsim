import java.util.ArrayList;
import java.util.List;

/**
 * The request port and the request bus (spec 3.3, 4.18, 8.10; v1.2), from Java:
 *  - a request's 128 bits arrive intact in its target's receive queue (in the tree), the requestor stamped by the row
 *    node (8r + c) or the root (a controller's: 64 + c), the target kept; reqbus_latency unloaded [meas];
 *  - one sender's requests to one target stay in order; under saturation each of the 64 tiles gets one of every 64;
 *  - credits: 128, 256, 128 for targets 0-2 (the queues' depths); a full target's input is skipped at the root, so
 *    another target's requests pass; inside a row the blocked request holds every request of that row behind it;
 *  - back-pressure to the request FIFO: request_busy at 128 entries, EVENTS [11] when it falls;
 *  - a tile's receive side reads an empty queue: GET 0, RECEIVE 0 with receive underflow (ERRORS [14]), [12] never;
 *  - a request for a reserved target or target 3 (deferred) is dropped at the root and flagged.
 * Usage: java RequestBusTest [shuffle seed | -1]
 */
public final class RequestBusTest {
    static long shuffle = -1;
    public static void main(String[] args) {
        if (args.length > 0) shuffle = Long.parseLong(args[0]);
        if (shuffle >= 0) System.out.println("shuffled child order, seed " + shuffle);
        basics();
        fairness();
        credits();
        backPressure();
        receiveSide();
        drops();
        fromC();
        System.exit(T.done("RequestBusTest" + (shuffle >= 0 ? " (shuffled, seed " + shuffle + ")" : "")));
    }
    static Net machine() {
        Config k = Config.machine(); k.shuffleSeed = shuffle;
        Net n = new Net(k);
        for (Tile p : n.c.tiles) { p.core.pokeLocal(Registers.LOADER_BASE, Controller.WFI); p.core.pokeLocal(Registers.LOADER_BASE + 4, RiscvTest.jal(0, -4)); }
        n.c.cc.then((cc, t) -> { cc.storeRun(t, true); return true; });
        n.c.run(10);
        return n;
    }
    static void store(Net n, Tile p, int addr, int v) { String e = p.core.store(n.c.cycle + 1, addr, v, -1); if (e != null) throw new IllegalStateException(e); n.step(); }
    static Integer load(Net n, Tile p, int addr) { Integer v = p.core.load(n.c.cycle + 1, addr, 4); n.step(); return v; }
    /** The four stores of one request, word 0 carrying target [9:7] and the rest of the words given. */
    static long request(Net n, Tile p, int target, int w0hi, int w1, int w2, int w3) {
        store(n, p, Registers.REQUEST_SET_W0.addr(), Registers.RQ_TARGET.place(target) | w0hi << 10);
        store(n, p, Registers.REQUEST_SET_W1.addr(), w1); store(n, p, Registers.REQUEST_SET_W2.addr(), w2);
        long t = n.c.cycle + 1;
        store(n, p, Registers.REQUEST_SEND.addr(), w3);
        return t;
    }
    /** Queue a request into p's FIFO directly (as a store at the current cycle would). */
    static void push(Net n, Tile p, int target, int tag) { p.req.setW(0, Registers.RQ_TARGET.place(target) | tag << 10); p.req.setW(1, tag); p.req.send(n.c.cycle, tag); }
    static void run(Net n, int k) { for (int i = 0; i < k; i++) n.step(); }
    static int qsize(Net n, int target) { return n.c.reqBus.queue[target].size(); }

    static void basics() {
        T.group("a request end to end");
        Net n = machine();
        RequestBus b = n.c.reqBus;
        Tile s = n.c.tiles[10];
        long t = request(n, s, 1, 0x2ABCDE, 0x11111111, 0x22222222, 0x33333333);
        run(n, 60);
        T.eq(1, b.delivered.size(), "delivered");
        long now = n.c.cycle + 1;
        int w0 = b.get(1, now, 0);
        T.eq(10, Registers.RQ_REQUESTOR.get(w0), "requestor [6:0] stamped by the row node: tile 10 (8 x 1 + 2)");
        T.eq(1, Registers.RQ_TARGET.get(w0), "target [9:7] kept: C&C 2's queue");
        T.eq(0x2ABCDE, w0 >>> 10, "the request proper [31:10] intact");
        T.eq(0x11111111, b.get(1, now, 1), "word 1"); T.eq(0x22222222, b.get(1, now, 2), "word 2");
        T.eq(0x11111111, b.get(1, now, 1), "GET doesn't pop");
        T.eq(0x33333333, b.receive(1, now), "RECEIVE returns word 3 and pops"); n.step();
        T.eq(0, b.get(1, n.c.cycle + 1, 0), "empty: GET reads 0");
        T.eq(0, s.req.setW[0] | s.req.setW[1] | s.req.setW[2], "W0-W2 cleared at the push");
        // unloaded latency, from each row, to each target
        long lo = Long.MAX_VALUE, hi = 0;
        for (int row = 0; row < 8; row++) for (int tg = 0; tg < 3; tg++) {
            Tile p = n.c.tiles[8 * row + 5];
            long ts = request(n, p, tg, row, 0, 0, row);
            run(n, 60);
            RequestBus.Req q = b.delivered.get(b.delivered.size() - 1);
            long l = q.deliveredAt - ts; lo = Math.min(lo, l); hi = Math.max(hi, l);
            b.receive(tg, n.c.cycle + 1); n.step();
        }
        T.eq(lo, hi, "every tile is a leaf at the same depth: the same latency from every row to every target (" + lo + ")");
        // a controller's request enters at the root, which stamps its requestor
        b.controllerSend(n.c.cycle, 1, Registers.RQ_TARGET.place(0) | 0x55 << 10, 1, 2, 3);
        run(n, 40);
        T.eq(65, Registers.RQ_REQUESTOR.get(b.get(0, n.c.cycle + 1, 0)), "C&C 2's request to C&C 1: requestor 65, stamped by the root");
        b.receive(0, n.c.cycle + 1); n.step();
        T.eq(0L, n.c.violations, "no violations " + n.c.violationReport());
        RequestBusTest.out(String.format("request bus [meas: RequestBusTest]: reqbus_latency unloaded, from the REQUEST_SEND store to the request in its target's queue (readable the next cycle): %d cycles at reqHopLatency %d (8 beats, three hops; spec 3.3's estimate 15-20)", lo, n.c.cfg.reqHopLatency));
    }
    static void out(String s) { System.out.println(s); }

    static void fairness() {
        T.group("order and fairness");
        Net n = machine();
        RequestBus b = n.c.reqBus;
        for (Tile p : n.c.tiles) for (int j = 0; j < 4; j++) push(n, p, 1, p.idx << 8 | j);
        List<int[]> got = new ArrayList<>();
        for (int i = 0; i < 6000 && got.size() < 256; i++) {
            n.step();
            long now = n.c.cycle + 1;
            if (b.pending(1, now)) { int w0 = b.get(1, now, 0); int w3 = b.receive(1, now); got.add(new int[] { Registers.RQ_REQUESTOR.get(w0), w3 }); }
        }
        T.eq(256, got.size(), "256 requests delivered");
        boolean inOrder = true; int[] next = new int[64];
        for (int[] g : got) { if ((g[1] >> 8) != g[0] || (g[1] & 0xFF) != next[g[0]]) inOrder = false; next[g[0]]++; }
        T.check(inOrder, "each sender's requests to one target in the order it issued them, its requestor correct");
        boolean fair = true;
        for (int round = 0; round < 4; round++) { boolean[] seen = new boolean[64]; for (int i = 0; i < 64; i++) seen[got.get(64 * round + i)[0]] = true; for (boolean x : seen) if (!x) fair = false; }
        T.check(fair, "under saturation every one of the 64 tiles gets one request in each 64 (each row 1/8 at the root, each tile 1/8 of its row)");
        long span = b.delivered.get(255).deliveredAt - b.delivered.get(64).deliveredAt;
        T.eq(191L * 8, span, "the root passes one beat a cycle: a request every 8 cycles");
    }

    static void credits() {
        T.group("credits and the root's skip");
        Net n = machine();
        RequestBus b = n.c.reqBus;
        T.eq(128, b.credits[0], "target 0 (C&C 1): 128 credits"); T.eq(256, b.credits[1], "target 1 (C&C 2): 256"); T.eq(128, b.credits[2], "target 2 (C&C 3): 128");
        // tiles 9 and 11 (row 1) fill target 0 (no pops) and queue one more; tile 10 (row 1) then asks target 1; tile 20 (row 2) asks target 1
        for (int j = 0; j < 100; j++) push(n, n.c.tiles[9], 0, j);
        for (int j = 0; j < 29; j++) push(n, n.c.tiles[11], 0, 100 + j);
        run(n, 1500);
        T.eq(128, qsize(n, 0), "target 0's receive queue holds 128: its credits are spent");
        T.eq(0, b.credits[0], "no credit left for target 0");
        push(n, n.c.tiles[10], 1, 0xA); push(n, n.c.tiles[20], 1, 0xB);
        run(n, 200);
        List<RequestBus.Req> d = b.delivered;
        boolean row2 = d.stream().anyMatch(q -> q.srcTile == 20), row1 = d.stream().anyMatch(q -> q.srcTile == 10);
        T.check(row2, "another row's request for target 1 passes the root (the root skips the full target's input)");
        T.check(!row1, "the same row's request for target 1 waits behind the blocked one (head-of-line inside a row)");
        b.receive(0, n.c.cycle + 1); run(n, 100);
        T.check(b.delivered.stream().anyMatch(q -> q.srcTile == 10), "one pop returns a credit: the 129th goes, and tile 10's request after it");
        T.eq(128, qsize(n, 0), "target 0's queue full again, never over");
    }

    static void backPressure() {
        T.group("back-pressure to the request FIFO");
        Net n = machine();
        Tile s = n.c.tiles[30];
        for (int j = 0; j < 128; j++) push(n, n.c.tiles[31], 0, j);   // target 0's 128 credits, all spent ...
        run(n, 1300);
        T.eq(128, qsize(n, 0), "target 0 full");
        for (int j = 0; j < 128; j++) push(n, s, 0, 1000 + j);       // ... then a full FIFO behind them
        run(n, 100);                                                 // two requests drain into the tree's buffers
        for (int j = 0; j < 2; j++) push(n, s, 0, 2000 + j);        // full again, held by the bus's back-pressure
        int room = 1 << Registers.EV_REQUEST_ROOM;
        store(n, s, Registers.EVENTS_MASK.addr(), room | Registers.EVENTS_MASK_RESET); load(n, s, Registers.EVENTS.addr());   // consume an earlier [11]
        run(n, 50);
        T.eq(1 << Registers.ST_REQUEST_BUSY, load(n, s, Registers.STATUS.addr()) & 1 << Registers.ST_REQUEST_BUSY, "request_busy: the FIFO is full (128 entries)");
        T.check(!s.req.canPush(n.c.cycle + 1), "a REQUEST_SEND store would stall");
        T.eq(0, load(n, s, Registers.EVENTS_PEEK.addr()) & room, "no [11] yet");
        n.c.reqBus.receive(0, n.c.cycle + 1);
        run(n, 80);
        T.eq(0, load(n, s, Registers.STATUS.addr()) & 1 << Registers.ST_REQUEST_BUSY, "a pop at the receiver frees an entry");
        T.eq(room, load(n, s, Registers.EVENTS_PEEK.addr()) & room, "request FIFO no longer full: [11] posted on request_busy's fall");
    }

    static void receiveSide() {
        T.group("a tile's receive side");
        Net n = machine();
        Tile r = n.c.tiles[40];
        int pend = 1 << Registers.EV_REQUEST_PENDING;
        for (int j = 0; j < 3; j++) push(n, n.c.tiles[j], 0, j);
        run(n, 150);
        T.eq(3, qsize(n, 0), "three requests in target 0's queue");
        T.eq(0, load(n, r, Registers.EVENTS_PEEK.addr()) & pend, "[12] never rises on a mesh tile");
        T.eq(0, (int) load(n, r, Registers.REQUEST_GET_W0.addr()), "GET reads 0: no queue");
        T.eq(0, r.ctl.conditions, "no underflow from GET");
        T.eq(0, (int) load(n, r, Registers.REQUEST_RECEIVE.addr()), "RECEIVE reads 0");
        T.eq(1 << Conditions.RECV_UNDERFLOW, r.ctl.conditions, "and sets receive underflow (ERRORS [14]): every such load on a mesh tile");
        T.eq(3, qsize(n, 0), "the controllers' queues untouched");
        n.c.reqBus.receive(1, n.c.cycle + 1);
        T.eq(1L, n.c.reqBus.underflows, "a receiver's RECEIVE on its empty queue is an underflow too");
    }

    static void drops() {
        T.group("drops at the root");
        Net n = machine();
        push(n, n.c.tiles[4], 5, 1);                              // a reserved target
        push(n, n.c.tiles[5], 3, 2);                              // target 3: deferred, no receive queue
        run(n, 120);
        T.eq(2, n.c.reqBus.dropped.size(), "both dropped at the root");
        T.eq(2L, n.c.violations, "and flagged (6.7)");
        T.check(n.c.violationMessages.get(0).contains("reserved") && n.c.violationMessages.get(1).contains("no receiver"), "naming why");
        T.eq(128, n.c.reqBus.credits[0], "target 0's credits untouched");
    }

    static void fromC() {
        T.group("from C");
        Mach m = new Mach(Mach.cfg(shuffle));
        Mach.Image im = m.boot("req");
        RequestBus b = m.c.reqBus;
        List<int[]> got = new ArrayList<>();
        // C&C 2 drains its queue slowly (one request every 64 cycles): 600 > 256 (its queue) + 128 (the FIFO) + 2 (the tree), so
        // tile 0's FIFO fills and its stores stall
        boolean ok = m.run(() -> {
            long now = m.c.cycle + 1;
            if ((m.c.cycle & 63) == 0 && b.pending(1, now)) { int w0 = b.get(1, now, 0), w1 = b.get(1, now, 1); got.add(new int[] { w0, w1, b.receive(1, now) }); }
            return m.allStopped() && got.size() >= 600;
        }, 400000);
        T.check(ok && m.allExited(), "every tile ran req to its exit, and C&C 2 drained 600: " + m.haltSummary());
        T.eq(600, m.word(0, im, "r", 0), "tile 0 sent 600 requests");
        int bad = 0;
        for (int i = 0; i < got.size(); i++) { int[] g = got.get(i); if (g[2] != i || Registers.RQ_REQUESTOR.get(g[0]) != 0 || g[0] >>> 10 != i || g[1] != 0x1111 || Registers.RQ_TARGET.get(g[0]) != 1) bad++; }
        T.eq(0, bad, "each with requestor 0 and target 1, in order, intact");
        T.check(m.c.tiles[0].core.stallRequest > 0, "tile 0's REQUEST_SEND stores stalled on the full FIFO (" + m.c.tiles[0].core.stallRequest + " cycles)");
        T.eq(0L, m.c.violations, "no violations " + m.c.violationReport());
        int c = 0; for (Tile p : m.c.tiles) c |= p.ctl.conditions;
        T.eq(0, c, "no errors");
    }
}
