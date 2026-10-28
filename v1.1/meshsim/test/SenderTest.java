import java.util.List;

/**
 * The sender (spec 4.17.9, 8.12) with a test peripheral that has two output queues ({@link TestPeripheral}) and the
 * test-only recording sink at the sender's output port:
 *  - the packet split for n mod 4 = 0-3 (128-bit packets, then 64, 32, or 64 + 32), words in queue order;
 *  - addr: SENDER_ADDR for modes 3, 6 and 9, advancing by each packet's words and continuing across sends; 0 for others;
 *  - sender_busy (TILE_STATUS [4]) from the end of the store's cycle to the last packet; sender idle ([20]) on its fall;
 *    a count of 0 rises and falls and posts [20];
 *  - pacing: one word per cycle from the peripheral gives a packet per 4 cycles; a burst gives one packet a cycle;
 *  - ignored and flagged: a store while busy, a queue the peripheral doesn't have, a tile with no output (the replayer);
 *  - HALT abandons a send: busy falls, no [20].
 * Usage: java SenderTest [shuffle seed | -1]
 */
public final class SenderTest {
    static long shuffle = -1;
    public static void main(String[] args) {
        if (args.length > 0) shuffle = Long.parseLong(args[0]);
        if (shuffle >= 0) System.out.println("shuffled child order, seed " + shuffle);
        splits();
        busyAndIgnored();
        halt();
        System.exit(T.done("SenderTest" + (shuffle >= 0 ? " (shuffled, seed " + shuffle + ")" : "")));
    }
    static Integer load(Net n, Tile p, int addr) { Integer v = p.core.load(n.c.cycle + 1, addr, 4); n.step(); return v; }
    static void store(Net n, Tile p, int addr, int v) { String e = p.core.store(n.c.cycle + 1, addr, v, -1); if (e != null) throw new IllegalStateException(e); n.step(); }
    static int send(int dest, int mode, int q, int n) { return Registers.SN_DEST.place(dest) | Registers.SN_MODE.place(mode) | Registers.SN_QUEUE.place(q) | Registers.SN_COUNT.place(n); }
    static void run(Net n, int k) { for (int i = 0; i < k; i++) n.step(); }
    static final int IDLE = 1 << Registers.EV_SENDER_IDLE;

    static void splits() {
        T.group("packet split and addr");
        TestPeripheral[] tp = new TestPeripheral[1];
        Net n = PeripheralSlotTest.machine(6, tp); Tile p = n.c.tiles[6]; TestPeripheral x = tp[0]; x.burst = true;
        Sender s = p.sender;
        store(n, p, Registers.SENDER_ADDR.addr(), 0x100);
        int[] counts = { 8, 6, 5, 7, 1 };
        String[][] want = { { "128", "128" }, { "128", "64" }, { "128", "32" }, { "128", "64", "32" }, { "32" } };
        int word = 0, addr = 0x100;
        for (int c = 0; c < counts.length; c++) {
            int from = s.sink.size();
            long t0 = n.c.cycle + 1;
            store(n, p, Registers.SENDER_SEND.addr(), send(44, LinkWord.M_PERIPH, 0, counts[c]));
            T.eq(1, Registers.ST_SENDER_BUSY.get(load(n, p, Registers.TILE_STATUS.addr())), "n = " + counts[c] + ": sender_busy from the end of the store's cycle");
            run(n, 20);
            List<long[]> l = s.sink.subList(from, s.sink.size());
            T.eq(want[c].length, l.size(), "n = " + counts[c] + ": " + want[c].length + " packets");
            boolean ok = true;
            for (int i = 0; i < l.size() && i < want[c].length; i++) {
                long[] e = l.get(i);
                int sz = (int) e[4], w = 1 << sz;
                if (!Integer.toString(32 << sz).equals(want[c][i]) || e[1] != 44 || e[2] != LinkWord.M_PERIPH || e[3] != addr) ok = false;
                for (int j = 0; j < w; j++) {
                    long v = j < 2 ? e[5] >>> (32 * j) : e[6] >>> (32 * (j - 2));
                    if ((int) v != word++) ok = false;
                }
                addr += w;
                if (i > 0 && e[0] - l.get(i - 1)[0] != 1) ok = false;
            }
            T.check(ok, "n = " + counts[c] + ": sizes " + String.join("+", want[c]) + ", words in queue order (first in [31:0]), addr stepping, one packet a cycle");
            T.eq(0, Registers.ST_SENDER_BUSY.get(load(n, p, Registers.TILE_STATUS.addr())), "n = " + counts[c] + ": sender_busy fell");
            T.eq(IDLE, load(n, p, Registers.WAKE_PEEK.addr()) & IDLE, "n = " + counts[c] + ": sender idle posted");
            store(n, p, Registers.WAKE_MASK.addr(), IDLE | Registers.WAKE_MASK_RESET); load(n, p, Registers.WAKE_EVENTS.addr());
            store(n, p, Registers.WAKE_MASK.addr(), Registers.WAKE_MASK_RESET);
            T.eq(0, load(n, p, Registers.WAKE_PEEK.addr()) & IDLE, "consumed by a load with [20] in the mask");
        }
        // mode 2 carries addr 0, and doesn't advance SENDER_ADDR; queue 1
        int from = s.sink.size();
        store(n, p, Registers.SENDER_SEND.addr(), send(5, LinkWord.M_SRC, 1, 4)); run(n, 10);
        T.check(s.sink.size() == from + 1 && s.sink.get(from)[3] == 0 && s.sink.get(from)[5] == (1L << 24 | (1L << 24 | 1) << 32), "mode 2: addr 0; queue 1's words");
        T.eq(addr, s.addr, "SENDER_ADDR unchanged by a mode-2 send");
        // a count of 0: busy rises and falls, [20] posted
        long t0 = n.c.cycle + 1;
        store(n, p, Registers.SENDER_SEND.addr(), send(5, LinkWord.M_PERIPH, 0, 0));
        T.eq(1, Registers.ST_SENDER_BUSY.get(load(n, p, Registers.TILE_STATUS.addr())) + Registers.ST_SENDER_BUSY.get(p.ctl.status) , "count 0: busy rose (seen the next cycle)");
        run(n, 3);
        T.eq(IDLE, load(n, p, Registers.WAKE_PEEK.addr()) & IDLE, "count 0: and fell, posting sender idle");
        // pacing: one word a cycle -> a 128-bit packet every 4 cycles
        x.burst = false; from = s.sink.size();
        store(n, p, Registers.SENDER_SEND.addr(), send(5, LinkWord.M_PERIPH, 0, 12)); run(n, 30);
        T.check(s.sink.size() == from + 3 && s.sink.get(from + 1)[0] - s.sink.get(from)[0] == 4 && s.sink.get(from + 2)[0] - s.sink.get(from + 1)[0] == 4, "one word a cycle: a 128-bit packet every 4 cycles (the sender waits on its queue)");
        T.eq(0L, n.c.violations, "no violations " + n.c.violationReport());
    }

    static void busyAndIgnored() {
        T.group("ignored stores");
        TestPeripheral[] tp = new TestPeripheral[1];
        Net n = PeripheralSlotTest.machine(6, tp); Tile p = n.c.tiles[6]; TestPeripheral x = tp[0];
        store(n, p, Registers.SENDER_SEND.addr(), send(5, LinkWord.M_PERIPH, 0, 40));
        long v0 = p.ctl.violations;
        store(n, p, Registers.SENDER_SEND.addr(), send(5, LinkWord.M_PERIPH, 1, 4));
        T.check(p.ctl.violations == v0 + 1 && p.ctl.lastViolation.contains("sender_busy"), "a store while busy: ignored and flagged");
        run(n, 80);
        T.eq(10L, p.sender.packets, "the first send went out whole (10 packets); the second never started");
        store(n, p, Registers.SENDER_SEND.addr(), send(5, LinkWord.M_PERIPH, 2, 4));
        T.check(p.ctl.violations == v0 + 2 && p.ctl.lastViolation.contains("queue 2"), "a queue the peripheral doesn't have: ignored and flagged");
        T.eq(0, Registers.ST_SENDER_BUSY.get(load(n, p, Registers.TILE_STATUS.addr())), "and busy stays 0");
        Tile r = n.c.tiles[7];                                             // a replayer: no output
        store(n, r, Registers.SENDER_SEND.addr(), send(5, LinkWord.M_PERIPH, 0, 4));
        T.check(r.ctl.lastViolation != null && r.ctl.lastViolation.contains("no output"), "on a tile whose peripheral has no output (the replayer): ignored and flagged");
        run(n, 5);
        T.eq(0, Registers.ST_SENDER_BUSY.get(load(n, r, Registers.TILE_STATUS.addr())), "busy stays 0");
        T.eq(0, load(n, r, Registers.WAKE_PEEK.addr()) & IDLE, "no sender-idle event");
        T.eq(0, (int) load(n, r, Registers.SENDER_ADDR.addr()), "SENDER_ADDR reads 0 (write-only)");
    }

    static void halt() {
        T.group("HALT abandons a send");
        TestPeripheral[] tp = new TestPeripheral[1];
        Net n = PeripheralSlotTest.machine(6, tp); Tile p = n.c.tiles[6];
        store(n, p, Registers.SENDER_SEND.addr(), send(5, LinkWord.M_PERIPH, 0, 400));
        run(n, 20);
        long sent = p.sender.packets;
        T.check(p.sender.busy() && sent > 0, "a long send under way (" + sent + " packets)");
        n.c.cc.halt(); run(n, 10);
        T.check(!p.sender.busy(), "HALT: sender_busy dropped");
        T.eq(0, p.ctl.events & IDLE, "with no sender-idle event");
        long after = p.sender.packets; run(n, 50);
        T.eq(after, p.sender.packets, "and nothing more is sent");
    }
}
