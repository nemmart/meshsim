import java.util.*;

/**
 * M1 tests: the network and packets (spec 3, 4.5-4.8, 4.17). Arg: shuffle seed (default -1, fixed child order).
 * Prints measured tables for meshsim-v4.0-M1-RESULTS.md.
 */
public final class NetTest {
    static long shuffle = -1;
    static final StringBuilder report = new StringBuilder();

    public static void main(String[] a) {
        if (a.length > 0) shuffle = Long.parseLong(a[0]);
        if (a.length > 1 && a[1].equals("skewtable")) { skewTable(a.length > 2 ? Integer.parseInt(a[2]) : 20); System.out.print(report); System.exit(T.done("NetTest skewtable")); }
        boolean quick = a.length > 1 && a[1].equals("quick");
        structure(); unicast(); broadcast(); routeComparison(); cncPort(); inOrderRandom(); inOrderFullRate();
        director(); injection(); loopback(); fanIn(quick);
        System.out.print(report);
        System.exit(T.done("NetTest" + (shuffle >= 0 ? " (shuffled, seed " + shuffle + ")" : "")));
    }

    static void out(String s) { report.append(s).append('\n'); }
    static void noViolations(Chip c, String what) { c.scanOverflows(); T.check(c.violations == 0, what + ": " + c.violationReport()); }

    // ------------------------------------------------------------------ structure
    static void structure() {
        T.group("structure");
        Net n = new Net(Net.cfg(shuffle));
        int zero = 0, one = 0;
        for (int i = 0; i < 64; i++) {
            for (int d = 0; d < 4; d++) T.eq(3, n.c.linkLatency[i][d], "link t" + i + " dir " + d + " latency");
            if (n.c.startSkew[i] == 0) zero++; else if (n.c.startSkew[i] == 1) one++;
        }
        T.eq(64, zero + one, "every start skew is 0 or +1");
        T.check(zero > 0 && one > 0, "both skews occur (seed 1): " + zero + " / " + one);
        Config k = Net.cfg(shuffle); k.skew = false;
        Chip c0 = new Chip(k); int s = 0; for (int x : c0.startSkew) s += x;
        T.eq(0, s, "skew off: all 0");
        // header fields round-trip (3.1, v1.2): src 7, dst 7, mode 3, counter 3, size 2, address 32 = 54 bits + 128 payload
        // (182 field bits of the 256-bit word; the 74 reserved bits are always 0 and not carried)
        long h = LinkWord.hdr(0x55, 0x2A, 6, 5, 0xFF1ABCDE, LinkWord.SIZE_64);
        T.eq(0x55, LinkWord.src(h), "src"); T.eq(0x2A, LinkWord.dst(h), "dst"); T.eq(6, LinkWord.mode(h), "mode"); T.eq(5, LinkWord.counter(h), "counter");
        T.eq(0xFF1ABCDE, LinkWord.addr(h), "address (32 bits)"); T.eq(LinkWord.SIZE_64, LinkWord.size(h), "size");
        T.eq(0, h >>> 54, "header fits 54 bits");
        long h7 = LinkWord.hdr(0x7F, 0x7F, 7, 7, -1, 3);
        T.eq(7, LinkWord.mode(h7), "mode 7 round-trips (3 bits)"); T.eq(7, LinkWord.counter(h7), "counter 7 beside it"); T.eq(-1, LinkWord.addr(h7), "address all ones");
        T.eq(3, LinkWord.size(h7), "size 11 round-trips"); T.eq(0, h7 >>> 54, "every field set: still 54 bits");
        for (int sz = 0; sz < 3; sz++) { T.eq(1 << sz, LinkWord.chunks(sz), "size " + sz + ": 1 << size chunks"); T.eq(4 << sz, LinkWord.bytes(sz), "size " + sz + ": 4 << size bytes"); }
        T.check(!LinkWord.readPortTag(h) && LinkWord.readPortTag(h | LinkWord.TAG_READPORT), "the simulator-only read-port tag is header bit 63");
        T.eq(0L, LinkWord.TAG_READPORT & LinkWord.HDR_MASK, "the tags are outside the link word's 54 header bits");
        T.eq(0x0300040C, LinkWord.sramAddr(0x103), "the packet address of chunk c is 0x03000000 | c << 2 (1, Addresses)");
        // bad packets (3.1): mode 0 misaligned or to a region a packet can't reach; mode 3 at 128; modes 1-5 ignore the address
        T.check(!LinkWord.badPacket(LinkWord.M_WRITE, LinkWord.SIZE_128, 0x00000010, 1), "mode 0, 128 bits at 0x10: good");
        T.check(LinkWord.badPacket(LinkWord.M_WRITE, LinkWord.SIZE_128, 0x00000018, 1), "mode 0, 128 bits at 0x18: misaligned");
        T.check(LinkWord.badPacket(LinkWord.M_WRITE, LinkWord.SIZE_64, 0x01000004, 1), "mode 0, 64 bits at 0x01000004: misaligned");
        T.check(!LinkWord.badPacket(LinkWord.M_WRITE, LinkWord.SIZE_32, 0x03000004, 1), "mode 0, 32 bits into big SRAM: good");
        T.check(LinkWord.badPacket(LinkWord.M_WRITE, LinkWord.SIZE_32, 0x02000000, 1), "mode 0 to the boot ROM: bad");
        T.check(LinkWord.badPacket(LinkWord.M_WRITE, LinkWord.SIZE_32, 0x07000000, 7), "mode 0 to region 0x07: bad");
        T.check(LinkWord.badPacket(LinkWord.M_WRITE, LinkWord.SIZE_32, 0xFFFFF800, 7), "mode 0 to region 0xFF: bad");
        T.check(!LinkWord.badPacket(LinkWord.M_WRITE, LinkWord.SIZE_32, 0x04000000, 1), "mode 0 to peripheral 1, present: good");
        T.check(LinkWord.badPacket(LinkWord.M_WRITE, LinkWord.SIZE_32, 0x05000000, 1), "mode 0 to peripheral 2, absent: bad");
        T.check(LinkWord.badPacket(LinkWord.M_AUG, LinkWord.SIZE_128, 0, 1), "mode 3 at 128 bits: bad");
        T.check(!LinkWord.badPacket(LinkWord.M_SRC, LinkWord.SIZE_64, 0xFF000004, 0), "mode 1 ignores the address, region included");
        T.check(!LinkWord.badPacket(LinkWord.M_COUNT, LinkWord.SIZE_128, 0x02000001, 0), "mode 5 ignores it too");
        T.check(!LinkWord.badPacket(6, LinkWord.SIZE_32, 0x02000001, 0), "modes 6-7 are not bad packets (discarded silently)");
        T.eq(0x13, LinkWord.src(LinkWord.withSrc(h, 0x13)), "stamp src");
        T.eq(6, LinkWord.mode(LinkWord.withSrc(h, 0x13)), "stamp keeps mode"); T.eq(0xFF1ABCDE, LinkWord.addr(LinkWord.withSrc(h, 0x13)), "and the address");
        T.eq(0x78, LinkWord.digit(0x12345678L, 1), "digit 1 = LS byte"); T.eq(0x12, LinkWord.digit(0x12345678L, 4), "digit 4");
        T.eq(0, LinkWord.digit(0x12345678L, 0), "digit 0 = none");
        out(String.format("structure: links all 3 cycles; start skew seed 1: %d tiles +0, %d tiles +1", zero, one));
    }

    // ------------------------------------------------------------------ unicast, every offset
    static int hopsOf(Hops h, int k) { return Math.abs(h.a[k]) + Math.abs(h.b[k]); }

    static void unicast() {
        T.group("unicast");
        Net n = new Net(Net.cfg(shuffle));
        Hops hops = n.c.cfg.hops;
        Map<Integer, Integer> latByHops = new TreeMap<>();
        boolean allPermPeaks1 = true;
        for (int k = 0; k < 64; k++) {
            n.clearLogs(); n.c.resetStats();
            long T0 = n.c.cycle + 2;
            for (int i = 0; i < 64; i++) n.at(i, T0, (i + k) & 63, LinkWord.M_SRC, 0, Net.tagged(k, T0));
            T.check(n.runAll(500), "offset " + k + " drained");
            int lat = -1; boolean same = true;
            for (int i = 0; i < 64; i++) {
                List<long[]> l = n.log((i + k) & 63);
                T.eq(1, l.size(), "offset " + k + ": tile " + ((i + k) & 63) + " receives one packet");
                if (l.size() != 1) continue;
                long[] e = l.get(0);
                T.eq(i, LinkWord.src(e[2]), "offset " + k + ": src stamped");
                T.eq((i + k) & 63, LinkWord.dst(e[2]), "offset " + k + ": dst kept");
                T.eq(LinkWord.M_SRC, LinkWord.mode(e[2]), "offset " + k + ": mode kept");
                int L = (int) (e[0] - T0);
                if (lat < 0) lat = L; else if (L != lat) same = false;
            }
            T.check(same, "offset " + k + ": every tile the same latency (lockstep permutation)");
            if (n.maxRouterPeak() > 1) allPermPeaks1 = false;
            int h = hopsOf(hops, k);
            Integer prev = latByHops.putIfAbsent(h, lat);
            T.check(prev == null || prev == lat, "offset " + k + ": latency depends on hop count only (" + h + " hops: " + lat + " vs " + prev + ")");
        }
        T.check(allPermPeaks1, "a one-packet permutation never queues (every router FIFO peak <= 1)");
        T.eq(0, n.late, "no late injections"); noViolations(n.c, "unicast");
        int base = latByHops.get(0);
        StringBuilder sb = new StringBuilder("unicast latency, injection point take -> director accept [meas: NetTest unicast], by hops h:");
        for (Map.Entry<Integer, Integer> e : latByHops.entrySet()) {
            sb.append(String.format(" h=%d:%d", e.getKey(), e.getValue()));
            T.eq(base + 4L * e.getKey(), e.getValue(), "latency(h) = latency(0) + h * (link 3 + 1)");
        }
        out(sb.toString() + String.format("  (= %d + 4h; spec 5's estimate h(k + 2) + 4 = 5h + 4)", base));
        // a lone packet in an idle machine has the same latency as the lockstep permutation
        n.clearLogs();
        long T0 = n.c.cycle + 2;
        n.at(17, T0, 17 + 27 & 63, LinkWord.M_SRC, 0, 0);
        n.runAll(200);
        T.eq(base + 4L * hopsOf(hops, 27), n.log((17 + 27) & 63).get(0)[0] - T0, "lone packet latency");
    }

    // ------------------------------------------------------------------ broadcast
    static void broadcast() {
        T.group("broadcast");
        Net n = new Net(Net.cfg(shuffle));
        int maxLat = 0;
        for (int s = 0; s <= 64; s++) {
            n.clearLogs(); n.c.resetStats();
            long routedBefore = 0; for (Tile p : n.c.tiles) routedBefore += p.router.routed;
            long T0 = n.c.cycle + 2;
            if (s < 64) n.at(s, T0, LinkWord.BROADCAST, LinkWord.M_SRC, 0, Net.tagged(s, T0));
            else n.cncSends.add(new long[] { T0, LinkWord.BROADCAST, LinkWord.M_SRC, 0, Net.tagged(s, T0) });
            T.check(n.runAll(500), "broadcast from " + s + " drained");
            for (int d = 0; d < 64; d++) {
                List<long[]> l = n.log(d);
                T.eq(1, l.size(), "broadcast from " + s + ": tile " + d + " receives exactly one copy");
                if (l.size() == 1) { T.eq(s, LinkWord.src(l.get(0)[2]), "broadcast src"); maxLat = Math.max(maxLat, (int) (l.get(0)[0] - T0)); }
            }
            T.eq(0, n.c.cnc.received.size(), "broadcast from " + s + " not delivered to C&C");
            long routed = 0; for (Tile p : n.c.tiles) routed += p.router.routed;
            T.eq(64, routed - routedBefore, "broadcast from " + s + ": 64 route-block visits (63 link arrivals + the root), no duplicate copies");
        }
        T.eq(0, n.c.tiles[0].inj.injected, "C&C's broadcast does not use tile 0's injection point");
        noViolations(n.c, "broadcast");
        out("broadcast: every tile exactly one copy from every source and from C&C, none to C&C; slowest copy " + maxLat + " cycles after injection [meas: NetTest broadcast]");
    }

    // ------------------------------------------------------------------ unicast vs broadcast routes (in-order scope)
    /** The path a packet takes, as the list of tiles it visits, by walking the route tables. */
    static List<Integer> path(Router[] r, int src, int dst) {
        List<Integer> p = new ArrayList<>(); int at = src; p.add(at);
        for (int i = 0; i < 20; i++) {
            int m = r[at].unicast(src, dst);
            if ((m & (Router.EJ | Router.CNC)) != 0) return p;
            at = (m & Router.RIGHT) != 0 ? (at + 1) & 63 : (m & Router.LEFT) != 0 ? (at + 63) & 63 : (m & Router.DOWN) != 0 ? (at + 8) & 63 : (at + 56) & 63;
            p.add(at);
        }
        throw new IllegalStateException("no route");
    }
    /** The path a broadcast copy takes from src to dst (the tree is unique: walk back from dst). */
    static List<Integer> bpath(Router[] r, int src, int dst) {
        // BFS over the tree: from src (port LOOPBACK) follow forwards
        Map<Integer, Integer> parent = new HashMap<>(); ArrayDeque<int[]> q = new ArrayDeque<>();
        q.add(new int[] { src, Router.LOOPBACK }); parent.put(src, -1);
        while (!q.isEmpty()) {
            int[] x = q.poll(); int at = x[0]; int m = r[at].broadcast(x[1], src);
            int[][] nx = { { Router.RIGHT, (at + 1) & 63, Router.FROM_LEFT }, { Router.LEFT, (at + 63) & 63, Router.FROM_RIGHT }, { Router.DOWN, (at + 8) & 63, Router.FROM_UP }, { Router.UP, (at + 56) & 63, Router.FROM_DOWN } };
            for (int[] e : nx) if ((m & e[0]) != 0) { if (parent.containsKey(e[1])) throw new IllegalStateException("broadcast reaches " + e[1] + " twice"); parent.put(e[1], at); q.add(new int[] { e[1], e[2] }); }
        }
        LinkedList<Integer> p = new LinkedList<>(); for (int at = dst; at != -1; at = parent.get(at)) p.addFirst(at);
        return p;
    }
    static List<Integer> differingOffsets;

    static void routeComparison() {
        T.group("routes");
        Net n = new Net(Net.cfg(shuffle));
        Router[] r = new Router[64]; for (int i = 0; i < 64; i++) r[i] = n.c.tiles[i].router;
        differingOffsets = new ArrayList<>();
        for (int k = 1; k < 64; k++) {
            boolean differ = false;
            for (int s = 0; s < 64; s++) {
                List<Integer> u = path(r, s, (s + k) & 63), b = bpath(r, s, (s + k) & 63);
                T.eq(hopsOf(n.c.cfg.hops, k), u.size() - 1, "unicast offset " + k + " takes the minimal hop count");
                if (!u.equals(b)) differ = true;
            }
            if (differ) differingOffsets.add(k);
        }
        out("routes: a unicast packet and a broadcast copy from one source to one destination take different paths for offsets " + differingOffsets
                + " (for 8 to 64 of the 64 sources each; the broadcast tree's copy can take 8 hops where the unicast takes at most 7), so in-order delivery holds within unicast and within broadcast, not between the two [NetTest routes]");
    }

    // ------------------------------------------------------------------ the controllers' ports (tiles 0-2's fifth ports, 4.16)
    static void cncPort() {
        T.group("C&C 1-3 ports");
        Net n = new Net(Net.cfg(shuffle));
        // C&C 1 -> every tile, one at a time; C&C 2 and C&C 3 likewise (each routed as an injection at its tile)
        for (int c = 0; c < 3; c++) for (int d = 0; d < 64; d++) {
            n.clearLogs(); long T0 = n.c.cycle + 2;
            n.c.ccs[c].send(n.c.cycle, d, LinkWord.M_COUNT, 1, 0, LinkWord.SIZE_32, 0x1234, 0);
            n.runAll(300);
            T.eq(1, n.log(d).size(), "C&C " + (c + 1) + " -> tile " + d);
            if (n.log(d).size() == 1) T.eq(64 + c, LinkWord.src(n.log(d).get(0)[2]), "src " + (64 + c) + " from C&C " + (c + 1));
        }
        // every tile -> C&C 1, 2 and 3
        for (int s = 0; s < 64; s++) {
            n.clearLogs(); long T0 = n.c.cycle + 2;
            for (int c = 0; c < 3; c++) n.at(s, T0 + c, 64 + c, LinkWord.M_WRITE, 3, 0x00000100, LinkWord.SIZE_32, s, 0);
            n.runAll(300);
            for (int c = 0; c < 3; c++) {
                T.eq(1, n.c.ccs[c].received.size(), "tile " + s + " -> C&C " + (c + 1));
                if (n.c.ccs[c].received.size() == 1) T.eq(s, LinkWord.src(n.c.ccs[c].received.get(0)[2]), "C&C " + (c + 1) + " sees src");
            }
            for (int c = 0; c < 3; c++) T.eq(0, n.log(c).size(), "nothing for 64-66 reaches tile " + c + "'s director");
        }
        // C&C 2 -> C&C 3 and back (controller to controller, through the mesh)
        n.clearLogs();
        n.c.ccs[1].send(n.c.cycle, 66, LinkWord.M_WRITE, 2, 0x200, LinkWord.SIZE_64, 0x77, 0);
        n.runAll(300);
        T.eq(1, n.c.cc3.received.size(), "C&C 2 -> C&C 3");
        // a broadcast from C&C 2 reaches tiles 0-63 once each, and no controller
        n.clearLogs();
        n.c.ccs[1].send(n.c.cycle, LinkWord.BROADCAST, LinkWord.M_COUNT, 1, 0, LinkWord.SIZE_32, 9, 0);
        n.runAll(300);
        for (int d = 0; d < 64; d++) T.eq(1, n.log(d).size(), "C&C 2's broadcast reaches tile " + d + " once");
        for (Endpoint e : n.c.ccs) T.eq(0, e.received.size(), "a broadcast is not delivered to C&C " + (e.address - 63));
        noViolations(n.c, "the controllers' ports");
        int unconnected = 0;
        for (Tile p : n.c.tiles) {
            T.eq(6, p.router.nOut, "tile " + p.idx + ": the router has the fifth port's output (4.16: one tile design)");
            T.check(p.router.attached == (p.idx < 3), "tile " + p.idx + ": fifth port connected only at tiles 0-2");
            if (p.idx < 3) continue;
            boolean quiet = true; for (WordFIFO f : p.router.q[Router.TO_CNC]) if (f.highWater > 0) quiet = false;
            for (int o = 0; o < 5; o++) { int f = p.router.slotOfPort(o, Router.FROM_CNC); if (f >= 0 && p.router.q[o][f].highWater > 0) quiet = false; }
            if (quiet) unconnected++;
        }
        T.eq(61, unconnected, "on tiles 3-63 the fifth port never receives and routing never chooses it");
        // a packet to 67-125 has no endpoint (invariant 17)
        Net m = new Net(Net.cfg(shuffle));
        m.at(5, m.c.cycle + 2, 70, LinkWord.M_COUNT, 1, 0, LinkWord.SIZE_32, 0, 0);
        m.runAll(300);
        T.check(m.c.violations >= 1 && m.c.violationMessages.get(0).contains("no endpoint"), "a packet for 70 is flagged: " + m.c.violationReport());
    }

    // ------------------------------------------------------------------ in-order delivery
    static void checkOrder(Net n, String what) {
        int unicastBad = 0, bcastBad = 0; long count = 0;
        for (int d = 0; d < 64; d++) {
            long[] lastU = new long[65], lastB = new long[65];
            Arrays.fill(lastU, -1); Arrays.fill(lastB, -1);
            for (long[] e : n.log(d)) {
                count++;
                int s = LinkWord.src(e[2]); long seq = e[1] >>> 32;
                boolean b = LinkWord.dst(e[2]) == LinkWord.BROADCAST;
                long[] last = b ? lastB : lastU;
                if (seq <= last[s]) { if (b) bcastBad++; else unicastBad++; }
                last[s] = seq;
            }
        }
        T.eq(0, unicastBad, what + ": unicast packets out of order per (src, dst)");
        T.eq(0, bcastBad, what + ": broadcast copies out of order per (src, dst)");
        out(String.format("%s: %d packets delivered, in order per (src, dst) within unicast and within broadcast", what, count));
    }

    static void inOrderRandom() {
        T.group("in-order random");
        Net n = new Net(Net.cfg(shuffle));
        Random rnd = new Random(42);
        long[] seq = new long[64]; long sent = 0;
        for (int t = 0; t < 3000; t++)
            for (int s = 0; s < 64; s++) {
                long T0 = 2 + t + n.c.startSkew[s];
                if (rnd.nextDouble() < 0.002) { n.at(s, T0, LinkWord.BROADCAST, LinkWord.M_SRC, 0, seq[s]++ << 32); sent += 64; }
                else if (rnd.nextDouble() < 0.2) { n.at(s, T0, rnd.nextInt(64), LinkWord.M_SRC, 0, seq[s]++ << 32); sent++; }
            }
        T.check(n.runAll(20000), "drained");
        long got = 0; for (int d = 0; d < 64; d++) got += n.log(d).size();
        T.eq(sent, got, "every packet delivered");
        checkOrder(n, "random traffic (p = 0.2 unicast + 0.002 broadcast per tile per cycle, 3,000 cycles, skew on)");
        T.eq(0, n.late, "no late injections"); noViolations(n.c, "random traffic");
        out("  queue peaks:\n" + n.c.queuePeaks());
    }

    static void inOrderFullRate() {
        T.group("in-order full rate");
        // every tile sends 500 packets to one offset, paced at one per P = max(|a_k|, |b_k|, 1) cycles: a ring link
        // carries |b_k| flows and a column link |a_k|, so P is the fastest rate the links carry (the timetable's bound)
        int[] offsets = { 1, 7, 8, 9, 27, 32, 33, 36, 63 };
        int worst = 0;
        Hops hops = Config.defaults().hops;
        for (int k : offsets) {
            Net n = new Net(Net.cfg(shuffle));
            int P = Math.max(1, Math.max(Math.abs(hops.a[k]), Math.abs(hops.b[k])));
            for (int s = 0; s < 64; s++) for (int j = 0; j < 500; j++) n.at(s, 2 + (long) j * P + n.c.startSkew[s], (s + k) & 63, LinkWord.M_SRC, 0, (long) j << 32);
            T.check(n.runAll(5000), "offset " + k + " drained");
            for (int d = 0; d < 64; d++) T.eq(500, n.log(d).size(), "offset " + k + ": tile " + d + " receives 500");
            checkOrder(n, "paced at the link bound, offset " + k + " (one per " + P + ")");
            T.eq(0, n.late, "offset " + k + ": one packet per cycle sustained at every injection point");
            noViolations(n.c, "full rate offset " + k);
            worst = Math.max(worst, n.maxRouterPeak());
        }
        out("permutations at the link bound (every tile 500 packets, one per max(|a|,|b|) cycles, skew 0/+1): router FIFO peak " + worst + " of 16 [meas: NetTest in-order full rate]");
    }

    // ------------------------------------------------------------------ the traffic director (4.8, v1.2)
    static final int RPL = ControllerMemories.REPLY, CMD = ControllerMemories.COMMAND, RW = ControllerMemories.REPLY_DWORD;
    static void director() {
        T.group("director");
        Config k = Net.cfg(shuffle); k.checkUnexpected = true; k.periphType = 0;   // no peripheral: mode 0 to region 0x04 is a bad packet
        Net n = new Net(k);
        Tile r = n.c.tiles[9];
        r.gatherStub.record(); r.writer.record();
        TrafficDirector d = r.dir;
        T.eq(0, d.periphPresent, "a tile built without a peripheral: PARAMS [2:0] = 0");
        d.remaining[1] = 4; d.remaining[2] = 3; d.remaining[3] = 2; d.remaining[4] = 1;
        long T0 = n.c.cycle + 2; long t = T0;
        n.at(9, T0, 9, LinkWord.M_GATHER, 4, 0, LinkWord.SIZE_64, 0x0000000500000077L, 0);   // gather request to self: never counted
        n.at(5, t++, 9, LinkWord.M_SRC, 1, 0, LinkWord.SIZE_64, 0xAAAAL, 0);
        n.at(5, t++, 9, LinkWord.M_WRITE, 1, LinkWord.sramAddr(0x102), LinkWord.SIZE_64, 0xEEEEL, 0);   // mode 0 into big SRAM, at chunk 0x102
        n.at(5, t++, 9, LinkWord.M_BUCKET, 1, 0, LinkWord.SIZE_64, 0xBBBBL, 0);
        n.at(5, t++, 9, LinkWord.M_AUG, 1, 0, LinkWord.SIZE_32, 0xCCCCL, 0);
        n.at(5, t++, 9, LinkWord.M_WRITE, 2, 0x01000040, LinkWord.SIZE_64, 0x2222222211111111L, 0);   // program memory words 0x10, 0x11
        n.at(5, t++, 9, LinkWord.M_WRITE, 2, 0x00000400, LinkWord.SIZE_32, 0x33333333L, 0);   // data memory word 0x100 only
        n.at(5, t++, 9, LinkWord.M_WRITE, 2, 0x000007F0, LinkWord.SIZE_128, 0x6666666655555555L, 0x8888888877777777L);   // data words 0x1FC-0x1FF
        n.at(5, t++, 9, LinkWord.M_WRITE, 3, RPL + 8 * 5, LinkWord.SIZE_32, 0xABCD0006L, 0);   // a 32-bit reply from src 5 at its pair
        n.at(6, t, 9, LinkWord.M_WRITE, 3, RPL + 8 * 6, LinkWord.SIZE_64, 0x1234567800C0FFEEL, 0);   // a 64-bit reply from src 6
        n.at(5, t++, 9, LinkWord.M_COUNT, 4, 0, LinkWord.SIZE_32, 0, 0);       // count only: nothing written, counter 4
        n.at(5, t++, 9, 6, 1, 0, LinkWord.SIZE_32, 0xFFFFL, 0);                 // mode 6: discarded, no count, no error
        // bad packets: dropped, uncounted, ERRORS [1]
        n.at(5, t++, 9, LinkWord.M_WRITE, 2, 0x00000404, LinkWord.SIZE_64, 0xD1L, 0);   // misaligned
        n.at(5, t++, 9, LinkWord.M_WRITE, 2, 0x02000000, LinkWord.SIZE_32, 0xD2L, 0);   // the boot ROM
        n.at(5, t++, 9, LinkWord.M_WRITE, 2, 0xFFFFF800, LinkWord.SIZE_32, 0xD3L, 0);   // region 0xFF
        n.at(5, t++, 9, LinkWord.M_WRITE, 2, 0x04000000, LinkWord.SIZE_32, 0xD4L, 0);   // peripheral 1, absent on this tile
        n.at(5, t++, 9, LinkWord.M_AUG, 1, 0, LinkWord.SIZE_128, 0xD5L, 0);             // mode 3 at 128 bits
        n.at(5, t++, 9, LinkWord.M_WRITE, 1, LinkWord.sramAddr(0x102), LinkWord.SIZE_128, 0xD6L, 0);   // big SRAM, 128 bits at chunk 2 of a row
        // a command from C&C 1: an argument (slot 3), then slot 0; uncounted mode 0
        n.cncSends.add(new long[] { T0, 9, LinkWord.M_WRITE, CMD + 12, 0xCAFE0003L, LinkWord.SIZE_32, 0, 0 });
        n.cncSends.add(new long[] { T0 + 1, 9, LinkWord.M_WRITE, CMD, 0x0000C0DEL, LinkWord.SIZE_32, 0, 0 });
        T.check(n.runAll(300), "drained");
        T.eq(6, d.droppedBad, "six bad packets dropped: misaligned, boot ROM, region 0xFF, absent peripheral, mode 3 at 128, misaligned big SRAM");
        T.check((r.conditions() & (1 << Conditions.BAD_MODE_OR_SIZE)) != 0, "ERRORS [1], bad packet or misaligned access (v1.1's bit " + Conditions.BAD_MODE_OR_SIZE + ")");
        T.eq(1, r.gatherStub.log.size(), "mode 4 -> GATHER queue");
        T.eq(4, r.writer.log.size(), "modes 1, 0 (big SRAM), 2 and 3 -> write engine");
        if (r.writer.log.size() == 4) { T.eq(LinkWord.M_SRC, LinkWord.mode(r.writer.log.get(0)[2]), "mode 1 first"); T.eq(LinkWord.M_WRITE, LinkWord.mode(r.writer.log.get(1)[2]), "mode 0 second");
            T.eq(LinkWord.M_BUCKET, LinkWord.mode(r.writer.log.get(2)[2]), "mode 2 third"); T.eq(LinkWord.M_AUG, LinkWord.mode(r.writer.log.get(3)[2]), "mode 3 fourth"); }
        T.eq(0xEEEE, r.sram.peek(0x102), "mode 0 written at its address's chunk (no PTR)");
        ControllerMemories m = r.cmem;
        T.eq(0x11111111, m.imem.peek(0x10), "program memory word 0x10"); T.eq(0x22222222, m.imem.peek(0x11), "program memory word 0x11");
        T.eq(0x33333333, m.dmem.peek(0x100), "data memory word 0x100"); T.eq(0, m.dmem.peek(0x101), "a 32-bit write writes one word only");
        T.eq(0x55555555, m.dmem.peek(0x1FC), "128 bits: word 0x1FC"); T.eq(0x88888888, m.dmem.peek(0x1FF), "128 bits: word 0x1FF");
        T.eq(0xCAFE0003, m.dmem.peek(ControllerMemories.CMD_DWORD + 3), "command slot 3 (0x7FCC)");
        T.eq(0xABCD0006, m.dmem.peek(RW + 10), "32-bit reply from src 5: word 0 of its pair (0x7BC0 + 40)");
        T.eq(0, m.dmem.peek(RW + 11), "a 32-bit reply leaves word 1 of its pair alone");
        T.eq(0x00C0FFEE, m.dmem.peek(RW + 12), "64-bit reply from src 6: word 0"); T.eq(0x12345678, m.dmem.peek(RW + 13), "and word 1");
        T.check(m.landedAt >= 0, "a write covering slot 0 sets command landed"); T.eq(0x0000C0DE, m.dmem.peek(ControllerMemories.CMD_DWORD), "slot 0 word");
        long[] pm = d.perMode;
        T.eq(1, pm[LinkWord.M_SRC], "accepted mode 1 once"); T.eq(1, pm[LinkWord.M_BUCKET], "mode 2 once"); T.eq(2, pm[LinkWord.M_AUG], "mode 3 twice (one bad)");
        T.eq(1, pm[LinkWord.M_GATHER], "mode 4 once"); T.eq(1, pm[LinkWord.M_COUNT], "mode 5 once"); T.eq(1, pm[6], "mode 6 once"); T.eq(13, pm[LinkWord.M_WRITE], "mode 0: 6 good, 5 bad, 2 commands");
        for (int c = 1; c <= 4; c++) T.eq(0, d.remaining[c], "WRITE_COUNTER_" + c + " counted down to 0");
        T.eq(1L, d.countOnly, "the count-only packet wrote nothing and counted"); T.eq(1L, d.reservedDiscarded, "mode 6 discarded");
        T.eq(0, r.conditions() & (1 << Conditions.UNEXPECTED), "no unexpected arrival (mode 4 and mode 6 aren't counted; bad packets aren't)");
        // violations: 6 bad packets at the director, 5 of them also at the sender (an absent peripheral is the receiver's to judge), mode 6 at both
        T.eq(13, n.c.violations, "13 violations: bad packets (invariant 5) and mode 6 (invariant 21): " + n.c.violationReport());
        // the delay line (4.8): a packet accepted at t decrements its counter at t + K
        d.remaining[1] = 1;
        long acc0 = d.accepted;
        n.at(5, n.c.cycle + 2, 9, LinkWord.M_SRC, 1, 0, LinkWord.SIZE_32, 1, 0);
        long accT = -1, decT = -1;
        for (int i = 0; i < 100 && decT < 0; i++) { n.step(); if (accT < 0 && d.accepted > acc0) accT = n.c.cycle; if (d.remaining[1] == 0) decT = n.c.cycle; }
        T.eq(accT + k.K, decT, "the counter decrements K = " + k.K + " cycles after the director accepts the packet");
        // unexpected arrival: the error at the decrement, the stray named at acceptance
        long v0 = n.c.violations;
        n.at(5, n.c.cycle + 2, 9, LinkWord.M_SRC, 1, 0, LinkWord.SIZE_32, 1, 0);
        n.runAll(300);
        T.eq(1L, d.unexpected, "unexpected arrival at the decrement (counter 1 at 0)");
        T.check((r.conditions() & (1 << Conditions.UNEXPECTED)) != 0, "ERRORS [2] set");
        T.eq(0, d.remaining[1], "the counter stays 0, no wrap");
        T.eq(1L, n.c.violations - v0, "the stray reported once, at acceptance (invariant 9)");
        T.check(n.c.violationMessages.get(n.c.violationMessages.size() - 1).contains("stray"), "named as a stray");
        // the accepted trade-off (4.12): a stray accepted at 0, then the counter armed before its decrement: no error, the
        // stray uses up one of the N; meshsim still names it at acceptance
        long u0 = d.unexpected; v0 = n.c.violations;
        n.at(5, n.c.cycle + 2, 9, LinkWord.M_SRC, 1, 0, LinkWord.SIZE_32, 1, 0);
        acc0 = d.accepted;
        for (int i = 0; i < 50 && d.accepted == acc0; i++) n.step();
        d.remaining[1] = 3;                                                     // armed after acceptance, before the decrement
        n.runAll(300);
        T.eq(u0, d.unexpected, "armed within K: no unexpected arrival");
        T.eq(2, d.remaining[1], "the stray used up one of the three");
        T.eq(1L, n.c.violations - v0, "but meshsim named the stray at acceptance");
        d.remaining[1] = 0;
        // size 11 (3.1): undefined; flagged at the sender and the director, which drops it uncounted with no error
        v0 = n.c.violations; int cond1 = r.conditions(); long bad1 = d.badTotal;
        d.remaining[1] = 1;
        n.at(5, n.c.cycle + 2, 9, LinkWord.M_SRC, 1, 0, LinkWord.SIZE_UNDEF, 0x1111L, 0x2222L);
        n.runAll(300);
        T.eq(1L, d.undefinedSize, "size 11: flagged and dropped at the director");
        T.eq(1, d.remaining[1], "size 11: not counted");
        T.eq(bad1, d.badTotal, "size 11 is not a bad packet");
        T.eq(cond1, r.conditions(), "size 11 sets no error (the hardware has no check)");
        T.eq(2, n.c.violations - v0, "size 11 flagged twice: at the sender's injection and at the receiving director");
        d.remaining[1] = 0;
        // meshsim's checks: invariants 10, 11, 19, 21, 22
        d.remaining[2] = 20; d.remaining[1] = 2;
        String[][] cases = {
            { "uncounted mode 1", "invariant 22" }, { "uncounted mode-0 data-memory write from a tile", "invariant 22" },
            { "uncounted big-SRAM write", "invariant 22" }, { "a tile's write into the command block", "invariant 11" },
            { "a reply at another sender's pair", "invariant 11" }, { "a 128-bit write into the reply block", "invariant 11" },
            { "a write into data memory's first 16 bytes", "invariant 22" }, { "a write past the end of data memory", "invariant 19" },
            { "a big-SRAM write past its end", "invariant 19" }, { "count-only on counter 0", "invariant 21" }, { "mode 4 to another tile", "invariant 10" } };
        long[][] pk = {
            { LinkWord.M_SRC, 0, 0, LinkWord.SIZE_32 }, { LinkWord.M_WRITE, 0, 0x100, LinkWord.SIZE_32 },
            { LinkWord.M_WRITE, 0, LinkWord.sramAddr(0x200), LinkWord.SIZE_32 }, { LinkWord.M_WRITE, 2, CMD + 4, LinkWord.SIZE_32 },
            { LinkWord.M_WRITE, 2, RPL + 8 * 7, LinkWord.SIZE_32 }, { LinkWord.M_WRITE, 2, RPL + 8 * 4, LinkWord.SIZE_128 },
            { LinkWord.M_WRITE, 2, 0x8, LinkWord.SIZE_32 }, { LinkWord.M_WRITE, 2, 0x8000, LinkWord.SIZE_32 },
            { LinkWord.M_WRITE, 1, LinkWord.sramAddr(k.memChunks), LinkWord.SIZE_32 }, { LinkWord.M_COUNT, 0, 0, LinkWord.SIZE_32 },
            { LinkWord.M_GATHER, 0, 0, LinkWord.SIZE_64 } };
        for (int c = 0; c < cases.length; c++) {
            v0 = n.c.violations;
            n.at(5, n.c.cycle + 2, 9, (int) pk[c][0], (int) pk[c][1], (int) pk[c][2], (int) pk[c][3], 0, 0);
            n.runAll(300);
            T.check(n.c.violations > v0 && n.c.violationMessages.get(n.c.violationMessages.size() - 1).contains(cases[c][1]), cases[c][0] + " flagged (" + cases[c][1] + "): " + n.c.violationMessages.get(n.c.violationMessages.size() - 1));
        }
        // K's formula (4.8): meshsim errors below max(2L + 3, L + 4, cm_latency, rp_latency)
        Config bad = Net.cfg(shuffle); bad.K = 4;
        boolean threw = false; try { new Chip(bad); } catch (IllegalArgumentException e) { threw = e.getMessage().contains("below"); }
        T.check(threw, "K = 4 at L = 1 is rejected (the formula's 5)");
        Config ok5 = Net.cfg(shuffle); ok5.K = 5; new Chip(ok5); T.check(true, "K = 5 at L = 1 is accepted");
        // port-B timing: accept at t, written at t + cm_latency, readable from t + cm_latency + 1
        d.remaining[3] = 1;
        n.clearLogs();
        n.at(5, n.c.cycle + 2, 9, LinkWord.M_WRITE, 3, RPL + 40, LinkWord.SIZE_32, 0x5A5A, 0);
        long accept = -1, written = -1; boolean sawIdle = false;
        for (int i = 0; i < 100; i++) {
            n.step();
            if (accept < 0 && !d.log.isEmpty()) accept = d.log.get(0)[0];
            if (accept >= 0 && n.c.cycle < accept + k.cmLatency && d.cmIdle(n.c.cycle)) sawIdle = true;
            if (written < 0 && m.dmem.peek(RW + 10) == 0x5A5A) written = n.c.cycle;
        }
        T.eq(accept + k.cmLatency, written, "a reply written at port B cm_latency cycles after the director accepts it");
        T.check(written < accept + k.K, "and before its counter's decrement at K (4.8)");
        T.check(!sawIdle, "cm_idle low while the write is in the port-B pipe");
        out("director: each mode and region to its unit (bad packets dropped with ERRORS [1]); counters by the counter field, decremented K = " + k.K
                + " cycles after acceptance; unexpected arrival at the decrement, the stray named at acceptance; a reply accepted at t is written at port B at t + " + (written - accept)
                + " (cm_latency " + k.cmLatency + ") [meas: NetTest director]");
    }

    // ------------------------------------------------------------------ the injection point
    static void injection() {
        T.group("injection");
        Net n = new Net(Net.cfg(shuffle));
        Tile s = n.c.tiles[3];
        long T0 = n.c.cycle + 2;
        for (int j = 0; j < 10; j++) n.at(3, T0 + j, 4, LinkWord.M_SRC, 0, j);        // read returns, back to back
        n.step();                                                                     // feed puts read 0 at T0-1
        s.sendHold.put(n.c.cycle, 100, LinkWord.hdr(0, 4, LinkWord.M_SRC, 1, 0, LinkWord.SIZE_64), 0);     // send port, visible at T0 too
        T.check(n.runAll(300), "drained");
        List<long[]> l = n.log(4);
        T.eq(11, l.size(), "11 packets");
        for (int j = 0; j < 10; j++) T.eq(j, l.get(j)[1], "read returns first, in order");
        T.eq(100, l.get(10)[1], "send-port packet after the read returns");
        T.eq(10, s.inj.sendWaitCycles, "send port waited 10 cycles");
        T.eq(T0 + 10, l.get(10)[0] - (l.get(0)[0] - T0), "send-port packet injected at T0 + 10");
        // send port alone sustains one packet per cycle (a store each cycle into its holding register)
        n.clearLogs();
        long t0 = n.c.cycle;
        for (int j = 0; j < 20; j++) { s.sendHold.put(n.c.cycle, 200 + j, LinkWord.hdr(0, 4, LinkWord.M_SRC, 1, 0, LinkWord.SIZE_64), 0); n.step(); }
        n.runAll(300);
        T.eq(0, s.sendHold.refusals, "no refusals");
        T.eq(20, n.log(4).size(), "20 send-port packets");
        T.eq(19, n.log(4).get(19)[0] - n.log(4).get(0)[0], "one per cycle");
        noViolations(n.c, "injection");
    }

    // ------------------------------------------------------------------ the loopback: tokens and the eject priority (4.5, 4.7)
    static void loopback() {
        T.group("loopback");
        Net n = new Net(Net.cfg(shuffle));
        Tile r = n.c.tiles[9];
        r.dir.log = new ArrayList<>();
        long T0 = n.c.cycle + 2;
        for (int j = 0; j < 300; j++) n.at(8, T0 + j, 9, LinkWord.M_SRC, 0, 1_000_000L + j);   // a full-rate stream from tile 8
        int sent = 0; long firstSelf = -1;
        for (int i = 0; i < 2000 && (sent < 120 || !n.c.quiescent()); i++) {
            if (sent < 120 && r.sendHold.isEmpty(n.c.cycle + 1) && n.c.cycle >= T0 + 10) { r.sendHold.put(n.c.cycle + 1, sent, LinkWord.hdr(0, 9, LinkWord.M_SRC, 1, 0, LinkWord.SIZE_32), 0); sent++; }
            n.step();
        }
        List<long[]> l = r.dir.log;
        T.eq(420, l.size(), "300 stream packets and 120 self-sends delivered");
        long lastStream = -1, firstSelfAt = -1, selfDuring = 0;
        for (long[] e : l) { if (LinkWord.src(e[2]) == 8) lastStream = e[0]; }
        for (long[] e : l) if (LinkWord.src(e[2]) == 9) { if (firstSelfAt < 0) firstSelfAt = e[0]; if (e[0] < lastStream) selfDuring++; }
        int lbSlot = r.router.slotOfPort(Router.EJECT, Router.LOOPBACK);
        WordFIFO lb = r.router.q[Router.EJECT][lbSlot];
        T.eq(0, selfDuring, "the loopback is served only when every other eject FIFO is empty: no self packet lands while the stream does");
        T.check(lb.highWater <= n.c.cfg.readTokens, "the loopback FIFO peaked at " + lb.highWater + " of " + lb.capacity + ": at most the token count");
        T.check(r.inj.tokenWaitCycles > 0, "the send port waited for tokens (" + r.inj.tokenWaitCycles + " cycles) instead of overflowing");
        T.eq(0, r.srq.held(), "every token free again");
        noViolations(n.c, "loopback");
        out(String.format("loopback [meas: NetTest loopback]: a full-rate stream into tile 9 with 120 self-sends from its send port: the loopback FIFO peaked at %d (depth %d), the send port waited %d cycles for tokens, no overflow; the self-sends landed after the stream (eject serves the loopback last)", lb.highWater, lb.capacity, r.inj.tokenWaitCycles));
    }

    // ------------------------------------------------------------------ fan-in under the start skew (for spec 3.2)
    /** Safe W per sender count, minimised over sender sets (nearest, farthest, 3 random) and skew patterns 1..seeds. */
    static void skewTable(int seeds) {
        T.group("fan-in under skew");
        int r = 27;
        Hops hops = Config.defaults().hops;
        Integer[] byNear = new Integer[63]; for (int i = 0; i < 63; i++) byNear[i] = (r + 1 + i) & 63;
        Arrays.sort(byNear, Comparator.comparingInt((Integer s) -> hopsOf(hops, (r - s) & 63)).thenComparingInt(s -> (s - r) & 63));
        int[] counts = { 63, 48, 32, 24, 16, 12, 8, 6, 4, 3, 2, 1 };
        out("fan-in with 0/+1 start skew [meas: NetTest skewtable " + seeds + "]: receiver tile " + r + "; every sender bursts W packets from cycle 2 + its skew;");
        out("safe W = no FIFO overflow anywhere, for every sender set (nearest, farthest, 3 random) and every skew pattern (seeds 1.." + seeds + ")");
        out(String.format("  %-8s %-10s %-10s %-10s %-10s %-12s %s", "senders", "safe W", "nearest", "farthest", "random", "total", "W with no skew (nearest)"));
        for (int S : counts) {
            int[] near = new int[S], far = new int[S];
            for (int i = 0; i < S; i++) { near[i] = byNear[i]; far[i] = byNear[62 - i]; }
            List<int[]> sets = new ArrayList<>(List.of(near, far));
            for (long rs = 1; rs <= 3; rs++) { List<Integer> all = new ArrayList<>(Arrays.asList(byNear)); Collections.shuffle(all, new Random(rs * 7919)); int[] x = new int[S]; for (int i = 0; i < S; i++) x[i] = all.get(i); sets.add(x); }
            int[] best = new int[sets.size()];
            for (int k = 0; k < sets.size(); k++) { best[k] = 64; for (long seed = 1; seed <= seeds; seed++) best[k] = Math.min(best[k], largestSafe(r, sets.get(k), seed, true)); }
            int w = 64; for (int b : best) w = Math.min(w, b);
            int rnd = Math.min(best[2], Math.min(best[3], best[4]));
            out(String.format("  %-8d %-10s %-10d %-10d %-10d %-12s %d", S, w == 64 ? ">= 64" : "" + w, best[0], best[1], rnd, w == 64 ? "-" : "" + S * w, largestSafe(r, near, 1, false)));
        }
    }

    // ------------------------------------------------------------------ fan-in (spec 3.2's table)
    /** Burst W packets from each sender to receiver r from the same cycle (+ start skew); true if no overflow. */
    static int[] fanRun(int r, int[] senders, int W, long skewSeed, boolean skew, StringBuilder firstOvf) {
        Config k = Net.cfg(shuffle); k.skewSeed = skewSeed; k.skew = skew;
        Net n = new Net(k);
        for (int s : senders) for (int j = 0; j < W; j++) n.at(s, 2 + j + n.c.startSkew[s], r, LinkWord.M_SRC, 0, (long) j << 32);
        n.runAll(100000);
        String first = null; long fc = Long.MAX_VALUE;
        for (Tile p : n.c.tiles) for (WordFIFO f : p.hardwareFifos) if (f.overflow && f.overflowCycle < fc) { fc = f.overflowCycle; first = f.name; }
        if (firstOvf != null && first != null) firstOvf.append(first);
        int peak = 0; for (WordFIFO f : n.c.tiles[r].router.q[Router.EJECT]) peak = Math.max(peak, f.highWater);
        long got = n.log(r).size();
        return new int[] { first == null ? 1 : 0, peak, (int) got };
    }

    static int largestSafe(int r, int[] senders, long seed, boolean skew) {
        int lo = 0, hi = 64;                    // lo safe, find the largest safe W <= 64 by scanning up then binary
        if (fanRun(r, senders, 64, seed, skew, null)[0] == 1) return 64;
        while (hi - lo > 1) { int mid = (lo + hi) / 2; if (fanRun(r, senders, mid, seed, skew, null)[0] == 1) lo = mid; else hi = mid; }
        return lo;
    }

    static void fanIn(boolean quick) {
        T.group("fan-in");
        int r = 27;
        Hops hops = Config.defaults().hops;
        Integer[] byNear = new Integer[63]; for (int i = 0; i < 63; i++) byNear[i] = (r + 1 + i) & 63;
        Arrays.sort(byNear, Comparator.comparingInt((Integer s) -> hopsOf(hops, (r - s) & 63)).thenComparingInt(s -> (s - r) & 63));
        int[] counts = { 63, 32, 16, 8, 4, 1 };
        int[] spec = { 1, 1, 3, 5, 21, 64 };
        out("fan-in to tile " + r + ": largest W per sender with no FIFO overflow, every sender bursting W packets from the same cycle (+ its start skew) [meas: NetTest fan-in]");
        out(String.format("  %-8s %-6s %-9s %-9s %-9s %-14s %-12s %s", "senders", "spec", "nearest", "farthest", "random*3", "nearest,noskew", "eject peak", "first overflow at W+1 (nearest)"));
        for (int ci = 0; ci < counts.length; ci++) {
            int S = counts[ci];
            int[] near = new int[S], far = new int[S];
            for (int i = 0; i < S; i++) { near[i] = byNear[i]; far[i] = byNear[62 - i]; }
            int wn = largestSafe(r, near, 1, true), wf = largestSafe(r, far, 1, true);
            int wr = 64;
            if (!quick) for (long seed = 1; seed <= 3; seed++) {
                List<Integer> all = new ArrayList<>(Arrays.asList(byNear)); Collections.shuffle(all, new Random(seed * 7919));
                int[] rs = new int[S]; for (int i = 0; i < S; i++) rs[i] = all.get(i);
                wr = Math.min(wr, largestSafe(r, rs, seed, true));
            }
            int wns = largestSafe(r, near, 1, false);
            int[] at = fanRun(r, near, wn, 1, true, null);
            T.eq(S * wn, at[2], S + " senders x " + wn + ": every packet arrives");
            StringBuilder fo = new StringBuilder();
            if (wn < 64) fanRun(r, near, wn + 1, 1, true, fo);
            out(String.format("  %-8d %-6s %-9d %-9d %-9s %-14d %-12s %s", S, spec[ci] == 64 ? "none" : "" + spec[ci], wn, wf, quick ? "-" : "" + wr, wns, at[1] + " of 16", wn < 64 ? fo : "no overflow to 64"));
            T.check(Math.min(wn, Math.min(wf, wr)) >= 1 || S == 63, "at least one packet per sender is safe below 63 senders");
        }
        // 63 x 1 sits exactly at the eject FIFO's depth: how often does the 0/+1 start skew push it over?
        int[] all = new int[63]; for (int i = 0; i < 63; i++) all[i] = (r + 1 + i) & 63;
        int[] ns = fanRun(r, all, 1, 1, false, null);
        T.eq(0, ns[0], "63 x 1 with no skew: overflows (v1.0: a push at full overflows even with a same-cycle pop; M15: fitted at 16 of 16)");
        int seeds = quick ? 10 : 40, ovf = 0;
        for (int rr : new int[] { 27, 0, 63 }) {
            int[] s3 = new int[63]; for (int i = 0; i < 63; i++) s3[i] = (rr + 1 + i) & 63;
            for (long seed = 1; seed <= seeds; seed++) if (fanRun(rr, s3, 1, seed, true, null)[0] == 0) ovf++;
        }
        out(String.format("  63 x 1: overflows with no skew (v1.0's FIFO rule); with 0/+1 skew it overflows in %d of %d skew patterns (receivers 27, 0, 63; seeds 1..%d) [meas: NetTest fan-in]", ovf, 3 * seeds, seeds));
    }
}
