import java.io.BufferedWriter;
import java.io.OutputStreamWriter;
import java.io.PrintWriter;
import java.util.Random;

/**
 * ReqConf (WORKER14): ref's request bus (RequestBus, the 64 tiles' and the three controllers' RequestPorts) driven
 * directly by seeded traffic, the Chip never stepped, every call's result and the bus's canonical state after every cycle
 * written, so the C++ bus (tests/test_reqbus.cpp) can replay the calls and must reproduce every result and every state.
 * Compiled into ref's default package (D5); nothing in sim/ref changes.
 *
 * Usage: java ReqConf SEED CYCLES [mix|fanin|full|drop|calm]  (stdout)
 *   mix     everything below in turn
 *   fanin   many tiles of one or several rows at once, the controllers too (the row nodes' and the root's round-robin)
 *   full    receivers that stop popping: the queues fill, the root runs out of credit, head-of-line blocking in a row;
 *           one tile pushing every cycle: its 128-entry FIFO fills and pushes are refused
 *   drop    targets 3-7 among the others: dropped at the root (flagged)
 *   calm    one request in the bus at a time (a sender starts only when the bus and every port are empty): no buffer
 *           ever fills, so D34's mode 3 must equal ref on it
 *
 * Per cycle t (ref's order: RequestBus.runCycle(t) first, then the cores' calls):
 *   t T                         the cycle (only cycles with a call or a change)
 *   s K W0 W1 W2 W3 R           sender K (0-63 a tile, 64-66 C&C 1-3) stores REQUEST_SET_W0-W2 then REQUEST_SEND W3:
 *                               R 1 pushed (canPush), 0 refused (the core retries later)
 *   r C P [W0 W1 W2 W3]         C&C C+1's receive side: P pending; if 1, REQUEST_GET_W0-W2 and REQUEST_RECEIVE's value
 *                               (the pop); if 0, REQUEST_RECEIVE on an empty queue (receive underflow, reads 0)
 *   h T BUS PORTS               after the cycle: the hash of StateFp's "reqbus" stream and of the 67 ports' "req"
 *                               streams in sender order (each hashed as a unit, then combined); written when it changes
 *   e T BEATS DROPPED UNDERFLOWS   at the end
 * Values hex.
 */
public final class ReqConf {
    static PrintWriter out = new PrintWriter(new BufferedWriter(new OutputStreamWriter(System.out), 1 << 16));
    static String hx(long v) { return Long.toHexString(v); }

    public static void main(String[] a) throws Exception {
        long seed = Long.parseLong(a[0]); int cycles = Integer.parseInt(a[1]); String fam = a.length > 2 ? a[2] : "mix";
        long dumpAt = a.length > 3 ? Long.parseLong(a[3]) : -1;   // debugging: StateFp's named reqbus stream after that cycle, on stderr
        Config k = Config.defaults(); k.ccCores = true;
        Chip ch = new Chip(k);
        RequestBus bus = ch.reqBus;
        Tile[] cc = bus.ccTiles;
        RequestPort[] port = new RequestPort[67];
        Tile[] owner = new Tile[67];
        for (int i = 0; i < 64; i++) { port[i] = ch.tiles[i].req; owner[i] = ch.tiles[i]; }
        for (int c = 0; c < 3; c++) { port[64 + c] = cc[c].req; owner[64 + c] = cc[c]; }
        Random r = new Random(seed);
        out.println("# reqconf v1: ref's side, ReqConf " + seed + " " + cycles + " " + fam);
        long[][] pend = new long[67][];                 // a request waiting for its push (the core stalled)
        double[] rate = new double[67]; double[] popRate = new double[3]; int[] tgtBias = new int[67];
        String lastH = "";
        int episode = -1;
        for (int t = 1; t < cycles; t++) {           // from cycle 1: the framework's first step (cycle 0 is the bus idle)
            // a new episode every 600-1200 cycles: who sends, how fast, to whom; how fast the receivers pop
            if (t == 1 || r.nextInt(900) == 0) {
                episode++;
                String f = fam.equals("mix") ? new String[] { "fanin", "full", "drop", "calm" }[episode % 4] : fam;
                java.util.Arrays.fill(rate, 0);
                switch (f) {
                    case "fanin" -> {
                        int rows = 1 + r.nextInt(3);
                        for (int q = 0; q < rows; q++) { int row = r.nextInt(8); for (int c = 0; c < 8; c++) if (r.nextInt(3) != 0) rate[8 * row + c] = 0.05 + 0.3 * r.nextDouble(); }
                        for (int c = 0; c < 3; c++) if (r.nextBoolean()) rate[64 + c] = 0.05 + 0.2 * r.nextDouble();
                        for (int c = 0; c < 3; c++) popRate[c] = 0.5 + 0.5 * r.nextDouble();
                    }
                    case "full" -> {
                        int hog = r.nextInt(64); rate[hog] = 1.0;                         // its FIFO fills: refused pushes
                        for (int q = 0; q < 6; q++) rate[r.nextInt(64)] = 0.2 + 0.5 * r.nextDouble();
                        rate[64 + r.nextInt(3)] = 0.5;
                        for (int c = 0; c < 3; c++) popRate[c] = r.nextInt(3) == 0 ? 0.0 : 0.02 * r.nextDouble();   // queues fill
                    }
                    case "drop" -> {
                        for (int q = 0; q < 10; q++) rate[r.nextInt(67)] = 0.05 + 0.2 * r.nextDouble();
                        for (int c = 0; c < 3; c++) popRate[c] = 0.3 + 0.7 * r.nextDouble();
                    }
                    default -> {                                                          // calm: one request at a time
                        for (int q = 0; q < 6; q++) rate[r.nextInt(67)] = 0.05 + 0.1 * r.nextDouble();
                        for (int c = 0; c < 3; c++) popRate[c] = 1.0;
                    }
                }
                boolean drops = f.equals("drop");
                for (int s = 0; s < 67; s++) tgtBias[s] = drops && r.nextInt(3) == 0 ? 3 + r.nextInt(5) : r.nextInt(3);
                out.println("# episode " + episode + " " + f);
            }
            bus.runCycle(t);
            StringBuilder calls = new StringBuilder();
            // the receivers (C&C c + 1): GET_W0-W2 then RECEIVE (one pop), or a RECEIVE on an empty queue now and then
            for (int c = 0; c < 3; c++) {
                RequestPort p = cc[c].req;
                boolean pending = p.pending(t);
                if (pending && r.nextDouble() < popRate[c]) {
                    int w0 = p.get(t, 0), w1 = p.get(t, 1), w2 = p.get(t, 2), w3 = p.receive(t);
                    calls.append("r ").append(c).append(" 1 ").append(hx(w0 & 0xFFFFFFFFL)).append(' ').append(hx(w1 & 0xFFFFFFFFL)).append(' ')
                         .append(hx(w2 & 0xFFFFFFFFL)).append(' ').append(hx(w3 & 0xFFFFFFFFL)).append('\n');
                } else if (!pending && r.nextInt(400) == 0) {
                    int v = p.receive(t);
                    calls.append("r ").append(c).append(" 0 ").append(hx(v & 0xFFFFFFFFL)).append('\n');
                }
            }
            // the senders: a waiting request retries; else a new one at the episode's rate (calm: only into an empty bus)
            boolean calmEp = fam.equals("calm") || fam.equals("mix") && episode % 4 == 3;
            boolean empty = bus.idle();
            for (RequestPort p : port) empty &= p.fifo.isEmpty();
            for (int s = 0; s < 67; s++) {
                if (pend[s] == null && rate[s] > 0 && (!calmEp || empty) && r.nextDouble() < rate[s]) {
                    empty = false;
                    int tg = r.nextInt(5) == 0 ? r.nextInt(3) : tgtBias[s];
                    long w0 = (r.nextInt() & ~0x3FFL) | (long) tg << 7 | r.nextInt(128);
                    pend[s] = new long[] { w0 & 0xFFFFFFFFL, r.nextInt() & 0xFFFFFFFFL, r.nextInt() & 0xFFFFFFFFL, r.nextInt() & 0xFFFFFFFFL };
                }
                if (pend[s] == null) continue;
                RequestPort p = port[s];
                for (int i = 0; i < 3; i++) p.setW(i, (int) pend[s][i]);
                boolean ok = p.canPush(t);
                if (ok) p.send(t, (int) pend[s][3]);
                calls.append("s ").append(s);
                for (long x : pend[s]) calls.append(' ').append(hx(x));
                calls.append(ok ? " 1\n" : " 0\n");
                if (ok) pend[s] = null;
            }
            if (t == dumpAt) { PrintWriter pw = new PrintWriter(System.err); StateFp.reqbus(new StateFp.Dump(pw, "reqbus"), bus); pw.flush(); }
            String h = hashes(bus, owner, t);
            if (calls.length() > 0 || !h.equals(lastH)) {
                out.print("t " + t + "\n" + calls);
                if (!h.equals(lastH)) { out.println("h " + t + h); lastH = h; }
            }
        }
        long under = 0; for (RequestPort p : port) under += p.underflows;
        out.println("e " + cycles + " " + bus.beatsAtRoot + " " + bus.dropped.size() + " " + under);
        out.flush();
    }

    /** StateFp's "reqbus" stream's hash, and the 67 ports' "req" streams (each a unit's hash; combined in order). */
    static String hashes(RequestBus bus, Tile[] owner, long t) {
        StateFp.Out o = new StateFp.Out();
        StateFp.reqbus(o, bus);
        long all = 0x6A09E667F3BCC908L;
        for (Tile p : owner) { StateFp.Out q = new StateFp.Out(); StateFp.req(q, p, t); all = StateFp.fmix(all ^ q.done()); }
        all = StateFp.fmix(all ^ owner.length);
        return " " + hx(o.done()) + " " + hx(all);
    }
}
