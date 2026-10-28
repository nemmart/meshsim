import java.util.ArrayList;
import java.util.List;

/**
 * The request port and the request bus (spec 3.3, 4.18, 8.10), from Java, with targets strapped to receivers (a
 * test-only Config.targetReceiver: in meshsim no target has a receiver otherwise, the endpoint map being deferred):
 *  - a request's 128 bits arrive intact, the requestor stamped by the row node (8r + c), the target kept;
 *    reqbus_latency unloaded, from the store to the request in the target's queue [meas];
 *  - one sender's requests to one target stay in order; under saturation each of the 64 tiles gets one of every 64;
 *  - credits: 16 per target; a full target's input is skipped at the root, so another target's requests pass; inside a
 *    row the blocked request holds every request of that row behind it (head-of-line, accepted);
 *  - back-pressure to the request FIFO: request_busy at 128 entries, [12] when it falls;
 *  - the receive side: GET without popping, RECEIVE popping (a credit back), [19] live, receive underflow (condition 14)
 *    on an empty queue and on every unstrapped tile;
 *  - a request for a reserved target or one with no receiver is dropped at the root and flagged;
 *  - HALT keeps the receive queue and the FIFO keeps draining.
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
    static Net machine(int... receivers) {
        Config k = Config.machine(); k.shuffleSeed = shuffle; k.targetReceiver = receivers;
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

    static void basics() {
        T.group("a request end to end");
        Net n = machine(63);                                       // target 0 -> tile 63's receive queue
        Tile s = n.c.tiles[10], r = n.c.tiles[63];
        long t = request(n, s, 0, 0x2ABCDE, 0x11111111, 0x22222222, 0x33333333);
        run(n, 60);
        T.eq(1, n.c.reqBus.delivered.size(), "delivered");
        long lat = n.c.reqBus.delivered.get(0).deliveredAt - t;
        int w0 = load(n, r, Registers.REQUEST_GET_W0.addr());
        T.eq(10, Registers.RQ_REQUESTOR.get(w0), "requestor [6:0] stamped by the row node: tile 10 (8 x 1 + 2)");
        T.eq(0, Registers.RQ_TARGET.get(w0), "target [9:7] kept");
        T.eq(0x2ABCDE, w0 >>> 10, "the request proper [31:10] intact");
        T.eq(0x11111111, (int) load(n, r, Registers.REQUEST_GET_W1.addr()), "word 1"); T.eq(0x22222222, (int) load(n, r, Registers.REQUEST_GET_W2.addr()), "word 2");
        T.eq(0x11111111, (int) load(n, r, Registers.REQUEST_GET_W1.addr()), "GET doesn't pop");
        T.eq(0x33333333, (int) load(n, r, Registers.REQUEST_RECEIVE.addr()), "RECEIVE returns word 3 and pops");
        T.eq(0, (int) load(n, r, Registers.REQUEST_GET_W0.addr()), "empty: GET reads 0");
        T.eq(0, s.req.setW[0] | s.req.setW[1] | s.req.setW[2], "W0-W2 cleared at the push");
        // unloaded latency, from each row
        StringBuilder sb = new StringBuilder();
        long lo = Long.MAX_VALUE, hi = 0;
        for (int row = 0; row < 8; row++) {
            Tile p = n.c.tiles[8 * row + 5];
            long ts = request(n, p, 0, row, 0, 0, row);
            run(n, 60);
            RequestBus.Req q = n.c.reqBus.delivered.get(n.c.reqBus.delivered.size() - 1);
            long l = q.deliveredAt - ts; lo = Math.min(lo, l); hi = Math.max(hi, l);
            load(n, r, Registers.REQUEST_RECEIVE.addr());
        }
        T.eq(lo, hi, "every tile is a leaf at the same depth: the same latency from every row (" + lo + ")");
        T.eq(0L, n.c.violations, "no violations " + n.c.violationReport());
        RequestBusTest.out(String.format("request bus [meas: RequestBusTest]: reqbus_latency unloaded, from the REQUEST_SEND store to the request in its target's queue (readable the next cycle): %d cycles at reqHopLatency %d (8 beats, three hops; spec 3.3's estimate 15-20)", lo, n.c.cfg.reqHopLatency));
    }
    static void out(String s) { System.out.println(s); }

    static void fairness() {
        T.group("order and fairness");
        Net n = machine(63);
        Tile r = n.c.tiles[63];
        for (Tile p : n.c.tiles) for (int j = 0; j < 4; j++) push(n, p, 0, p.idx << 8 | j);
        List<int[]> got = new ArrayList<>();
        for (int i = 0; i < 6000 && got.size() < 256; i++) {
            n.step();
            if (r.req.pending(n.c.cycle + 1)) { int w0 = r.req.get(n.c.cycle + 1, 0); int w3 = r.req.receive(n.c.cycle + 1); got.add(new int[] { Registers.RQ_REQUESTOR.get(w0), w3 }); }
        }
        T.eq(256, got.size(), "256 requests delivered");
        boolean inOrder = true; int[] next = new int[64];
        for (int[] g : got) { if ((g[1] >> 8) != g[0] || (g[1] & 0xFF) != next[g[0]]) inOrder = false; next[g[0]]++; }
        T.check(inOrder, "each sender's requests to one target in the order it issued them, its requestor correct");
        boolean fair = true;
        for (int round = 0; round < 4; round++) { boolean[] seen = new boolean[64]; for (int i = 0; i < 64; i++) seen[got.get(64 * round + i)[0]] = true; for (boolean b : seen) if (!b) fair = false; }
        T.check(fair, "under saturation every one of the 64 tiles gets one request in each 64 (each row 1/8 at the root, each tile 1/8 of its row)");
        long span = n.c.reqBus.delivered.get(255).deliveredAt - n.c.reqBus.delivered.get(64).deliveredAt;
        T.eq(191L * 8, span, "the root passes one beat a cycle: a request every 8 cycles");
    }

    static void credits() {
        T.group("credits and the root's skip");
        Net n = machine(63, 62);                                   // target 0 -> tile 63, target 1 -> tile 62
        // tile 9 (row 1) fills target 0 (no pops: 16 credits) and queues one more; tile 10 (row 1) then asks target 1;
        // tile 20 (row 2) asks target 1
        for (int j = 0; j < 17; j++) push(n, n.c.tiles[9], 0, j);
        run(n, 400);
        T.eq(16, n.c.tiles[63].req.rq.size(), "target 0's receive queue holds 16: its credits are spent");
        T.eq(0, n.c.reqBus.credits[0], "no credit left for target 0");
        push(n, n.c.tiles[10], 1, 0xA); push(n, n.c.tiles[20], 1, 0xB);
        run(n, 200);
        List<RequestBus.Req> d = n.c.reqBus.delivered;
        boolean row2 = d.stream().anyMatch(q -> q.srcTile == 20), row1 = d.stream().anyMatch(q -> q.srcTile == 10);
        T.check(row2, "another row's request for target 1 passes the root (the root skips the full target's input)");
        T.check(!row1, "the same row's request for target 1 waits behind the blocked one (head-of-line inside a row)");
        n.c.tiles[63].req.receive(n.c.cycle + 1); run(n, 100);
        T.check(n.c.reqBus.delivered.stream().anyMatch(q -> q.srcTile == 10), "one pop returns a credit: the 17th goes, and tile 10's request after it");
        T.eq(16, n.c.tiles[63].req.rq.size(), "target 0's queue full again, never over");
    }

    static void backPressure() {
        T.group("back-pressure to the request FIFO");
        Net n = machine(63);
        Tile s = n.c.tiles[30];
        for (int j = 0; j < 128; j++) push(n, s, 0, j);           // a full FIFO ...
        T.check(!s.req.canPush(n.c.cycle), "128 entries: full");
        run(n, 600);                                               // ... drains 16 into the receiver and 2 into the tree
        for (int j = 0; j < 18; j++) push(n, s, 0, 128 + j);      // full again, now held by the bus's back-pressure
        int room = 1 << Registers.EV_REQUEST_ROOM;
        store(n, s, Registers.WAKE_MASK.addr(), room | Registers.WAKE_MASK_RESET); load(n, s, Registers.WAKE_EVENTS.addr());   // consume the first fall's [12]
        run(n, 50);
        T.eq(1, Registers.ST_REQUEST_BUSY.get(load(n, s, Registers.TILE_STATUS.addr())), "request_busy: the FIFO is full (128 entries)");
        T.check(!s.req.canPush(n.c.cycle + 1), "a REQUEST_SEND store would stall");
        T.eq(0, load(n, s, Registers.WAKE_PEEK.addr()) & 1 << Registers.EV_REQUEST_ROOM, "no [12] yet");
        n.c.tiles[63].req.receive(n.c.cycle + 1);
        run(n, 80);
        T.eq(0, Registers.ST_REQUEST_BUSY.get(load(n, s, Registers.TILE_STATUS.addr())), "a pop at the receiver frees an entry");
        T.eq(1 << Registers.EV_REQUEST_ROOM, load(n, s, Registers.WAKE_PEEK.addr()) & 1 << Registers.EV_REQUEST_ROOM, "request FIFO no longer full: [12] posted on request_busy's fall");
    }

    static void receiveSide() {
        T.group("the receive side");
        Net n = machine(40);
        Tile r = n.c.tiles[40], o = n.c.tiles[41];
        int pend = 1 << Registers.EV_REQUEST_PENDING;
        T.eq(0, load(n, r, Registers.WAKE_PEEK.addr()) & pend, "[19] down while the queue is empty");
        for (int j = 0; j < 3; j++) push(n, n.c.tiles[j], 0, j);
        run(n, 150);
        T.eq(pend, load(n, r, Registers.WAKE_PEEK.addr()) & pend, "[19] up while requests wait");
        store(n, r, Registers.WAKE_MASK.addr(), pend | Registers.WAKE_MASK_RESET);
        load(n, r, Registers.WAKE_EVENTS.addr());
        T.eq(pend, load(n, r, Registers.WAKE_PEEK.addr()) & pend, "consuming [19] changes nothing (live)");
        T.eq(3, r.req.rq.size(), "three requests still queued: only RECEIVE removes one");
        for (int j = 0; j < 3; j++) load(n, r, Registers.REQUEST_RECEIVE.addr());
        T.eq(0, load(n, r, Registers.WAKE_PEEK.addr()) & pend, "[19] falls with the last pop");
        T.eq(0, r.ctl.conditions, "no underflow so far");
        T.eq(0, (int) load(n, r, Registers.REQUEST_RECEIVE.addr()), "RECEIVE on an empty queue returns 0");
        T.eq(1 << Conditions.RECV_UNDERFLOW, r.ctl.conditions, "and sets receive underflow (condition 14)");
        load(n, o, Registers.REQUEST_RECEIVE.addr());
        T.eq(1 << Conditions.RECV_UNDERFLOW, o.ctl.conditions, "on an unstrapped tile every RECEIVE is an underflow");
        // HALT keeps the receive queue; a halted tile's FIFO drains
        push(n, n.c.tiles[3], 0, 7);
        n.c.cc.halt(); run(n, 10);
        push(n, n.c.tiles[5], 0, 8);                              // queued in a halted tile: it drains
        run(n, 150);
        T.eq(2, r.req.rq.size(), "HALT keeps the receive queue, and a halted tile's request FIFO drains into it");
    }

    static void drops() {
        T.group("drops at the root");
        Net n = machine(63);
        push(n, n.c.tiles[4], 5, 1);                              // a reserved target
        push(n, n.c.tiles[5], 2, 2);                              // a target with no receiver
        run(n, 120);
        T.eq(2, n.c.reqBus.dropped.size(), "both dropped at the root");
        T.eq(2L, n.c.violations, "and flagged (6.7)");
        T.check(n.c.violationMessages.get(0).contains("reserved") && n.c.violationMessages.get(1).contains("no receiver"), "naming why");
        T.eq(16, n.c.reqBus.credits[0], "target 0's credits untouched");
    }

    static void fromC() {
        T.group("from C");
        Config k = Mach.cfg(shuffle); k.targetReceiver = new int[] { 63 };
        Mach m = new Mach(k);
        Mach.Image im = m.boot("req");
        boolean ok = m.run(m::allStopped, 200000);
        T.check(ok && m.allEbreak(), "every tile ran req to its ebreak: " + m.haltSummary());
        T.eq(600, m.word(0, im, "r", 0), "tile 0 sent 600 requests");
        T.eq(600, m.word(63, im, "r", 2), "tile 63 received 600, waking on REQUEST_PENDING");
        T.eq(0, m.word(63, im, "r", 1), "each with requestor 0, in order, intact");
        T.check(m.c.tiles[0].core.stallRequest > 0, "tile 0's REQUEST_SEND stores stalled on the full FIFO (" + m.c.tiles[0].core.stallRequest + " cycles)");
        T.eq(0L, m.c.violations, "no violations " + m.c.violationReport());
        int c = 0; for (Tile p : m.c.tiles) c |= p.ctl.conditions;
        T.eq(0, c, "no conditions");
    }
}
