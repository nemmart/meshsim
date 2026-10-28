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
        // header fields round-trip; 7 + 7 + 4 + 21 + 2 + 128 = 169 bits (21-bit addr, the 2-bit size, the 128-bit payload)
        long h = LinkWord.hdr(0x55, 0x2A, 6, 0x1ABCDE, LinkWord.SIZE_64);
        T.eq(0x55, LinkWord.src(h), "src"); T.eq(0x2A, LinkWord.dst(h), "dst"); T.eq(6, LinkWord.mode(h), "mode"); T.eq(0x1ABCDE, LinkWord.addr(h), "addr (21 bits)");
        T.eq(LinkWord.SIZE_64, LinkWord.size(h), "size");
        T.eq(0, h >>> 41, "header fits 41 bits (+ 128 payload = 169)");
        long h15 = LinkWord.hdr(0x7F, 0x7F, 15, 0x1FFFFF, 3);
        T.eq(15, LinkWord.mode(h15), "mode 15 round-trips (4 bits)"); T.eq(0x1FFFFF, LinkWord.addr(h15), "addr beside mode 15"); T.eq(3, LinkWord.size(h15), "size 11 round-trips beside mode 15");
        T.eq(0, h15 >>> 41, "every field set: still 41 bits");
        for (int sz = 0; sz < 3; sz++) T.eq(1 << sz, LinkWord.chunks(sz), "size " + sz + ": 1 << size chunks");
        T.check(!LinkWord.readPortTag(h) && LinkWord.readPortTag(h | LinkWord.TAG_READPORT), "the simulator-only read-port tag is header bit 63");
        T.eq(0, (int) ((LinkWord.TAG_READPORT & LinkWord.HDR_MASK) >>> 1), "the tag is outside the link word's 41 header bits");
        T.eq(0x13, LinkWord.src(LinkWord.withSrc(h, 0x13)), "stamp src");
        T.eq(6, LinkWord.mode(LinkWord.withSrc(h, 0x13)), "stamp keeps mode");
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

    // ------------------------------------------------------------------ C&C port (tile 0's fifth port) and address 65
    static void cncPort() {
        T.group("C&C port");
        Net n = new Net(Net.cfg(shuffle));
        Hops hops = n.c.cfg.hops;
        // C&C -> every tile, one at a time (latency by hops from tile 0)
        for (int d = 0; d < 64; d++) {
            n.clearLogs(); long T0 = n.c.cycle + 2;
            n.cncSends.add(new long[] { T0, d, LinkWord.M_CMD, 1, 0x1234, LinkWord.SIZE_32, 0 });
            n.runAll(300);
            T.eq(1, n.log(d).size(), "C&C -> tile " + d);
            if (n.log(d).size() == 1) T.eq(LinkWord.CNC, LinkWord.src(n.log(d).get(0)[2]), "src 64 from C&C");
        }
        // every tile -> C&C and -> memory subsystem
        for (int s = 0; s < 64; s++) {
            n.clearLogs(); long T0 = n.c.cycle + 2;
            n.at(s, T0, LinkWord.CNC, LinkWord.M_REPLY, 0, Net.tagged(s, T0));
            n.at(s, T0 + 1, LinkWord.MEMSUB, LinkWord.M_SRC, 0, Net.tagged(s, T0 + 1));
            n.runAll(300);
            T.eq(1, n.c.cnc.received.size(), "tile " + s + " -> C&C");
            T.eq(1, n.c.memsub.received.size(), "tile " + s + " -> memory subsystem");
            if (n.c.cnc.received.size() == 1) T.eq(s, LinkWord.src(n.c.cnc.received.get(0)[2]), "C&C sees src");
            T.eq(0, n.log(0).size(), "nothing for 64/65 reaches tile 0's director");
        }
        T.eq(0, n.c.tiles[0].inj.injected - 2, "tile 0's injection point carried only its own two packets");
        T.eq(64L, n.c.violations, "every packet sent to dst 65 flagged (2.1: no endpoint; the memory system is served at C&C's port, 64)");
        int unconnected = 0;
        for (Tile p : n.c.tiles) {
            T.eq(6, p.router.nOut, "tile " + p.idx + ": the router has the fifth port's output (4.16: one tile design)");
            if (p.idx == 0) continue;
            boolean quiet = true; for (WordFIFO f : p.router.q[Router.TO_CNC]) if (f.highWater > 0) quiet = false;
            for (int o = 0; o < 5; o++) { int f = p.router.slotOfPort(o, Router.FROM_CNC); if (f >= 0 && p.router.q[o][f].highWater > 0) quiet = false; }
            if (quiet && !p.router.attached) unconnected++;
        }
        T.eq(63, unconnected, "on tiles 1-63 the fifth port is unconnected: it never receives and routing never chooses it");
        T.check(n.c.violationMessages.stream().allMatch(v -> v.contains("dst 65")), "and nothing else");
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

    // ------------------------------------------------------------------ the traffic director
    static void director() {
        T.group("director");
        Config k = Net.cfg(shuffle); k.checkUnexpected = true; k.periphType = 0;   // tile 9's slot empty: mode 9 is a bad peripheral write
        Net n = new Net(k);
        Tile r = n.c.tiles[9];
        r.gatherStub.record(); r.writer.record();
        TrafficDirector d = r.dir;
        d.remaining[TrafficDirector.SRAM] = 4; d.remaining[TrafficDirector.CM] = 3; d.remaining[TrafficDirector.REPLY] = 2; d.remaining[TrafficDirector.PERIPH] = 1;
        long T0 = n.c.cycle + 2; long t = T0;
        n.at(5, t++, 9, LinkWord.M_INVALID, 0x12345, 0x800 + 7);       // mode 0: invalid: dropped, bad-mode-or-size condition, invariant 5 at the sender
        n.at(9, T0, 9, LinkWord.M_GATHER, 0, 0x0000000500000077L);     // gather request to self
        n.at(5, t++, 9, LinkWord.M_SRC, 0, 0xAAAAL);
        n.at(5, t++, 9, LinkWord.M_EXPLICIT, 0x102, 0xEEEEL);           // mode 3, 64 bits at an even addr: to the write engine
        n.at(5, t++, 9, LinkWord.M_BUCKET, 0, 0xBBBBL);
        n.at(5, t++, 9, LinkWord.M_AUG, 0, LinkWord.SIZE_32, 0xCCCCL, 0);   // mode 5 at 32 bits: augmented write, to the write engine
        n.at(5, t++, 9, LinkWord.M_CMEM, 0x010, 0x2222222211111111L);   // 64 bits: instruction memory words 0x10, 0x11
        n.at(5, t++, 9, LinkWord.M_CMEM, 0x900, LinkWord.SIZE_32, 0x33333333L, 0);   // 32 bits: data memory word 0x100 only
        n.at(5, t++, 9, LinkWord.M_CMEM, 0x7FE, LinkWord.SIZE_128, 0x6666666655555555L, 0x8888888877777777L);   // 128 bits straddling: imem 0x7FE, 0x7FF, dmem 0x000, 0x001
        n.at(5, t++, 9, LinkWord.M_CMD, 0x23, LinkWord.SIZE_32, 0xCAFE0003L, 0);   // command slot addr[3:0] = 3; the rest of addr ignored
        n.at(5, t++, 9, LinkWord.M_REPLY, 0, LinkWord.SIZE_32, 0xABCD0006L, 0);   // a 32-bit reply from src 5: reply word 10 only
        n.at(6, t, 9, LinkWord.M_REPLY, 0x55, 0x1234567800C0FFEEL);     // a 64-bit reply from src 6: reply words 12 and 13 (addr unused)
        n.at(5, t++, 9, LinkWord.M_CMD, 0, LinkWord.SIZE_32, 0x0000C0DEL, 0);   // slot 0: command landed
        n.at(5, t++, 9, LinkWord.M_PERIPH, 0x123, LinkWord.SIZE_128, 0x8888L, 0x9999L);   // mode 9 on a tile with no peripheral (this build): a bad peripheral write, counted
        n.at(5, t++, 9, 15, 0, 0xFFFFL);                                // mode 15: reserved (bad), dropped, invariant 5 at the sender
        n.at(5, t++, 9, LinkWord.M_AUG, 0, LinkWord.SIZE_128, 0xDDDDL, 0);   // mode 5 at 128 bits: a bad packet (3.1)
        n.at(5, t++, 9, LinkWord.M_EXPLICIT, 0x101, 0xDDDDL);           // mode 3, 64 bits at an odd addr: misaligned, a bad packet
        n.at(5, t++, 9, LinkWord.M_EXPLICIT, 0x102, LinkWord.SIZE_128, 0xDDDDL, 0);   // mode 3, 128 bits at addr 2 mod 4: a bad packet
        n.at(5, t++, 9, LinkWord.M_CMD, 1, 0xDDDDL);                    // mode 7 at 64 bits: a bad packet
        n.at(5, t++, 9, LinkWord.M_REPLY, 0, LinkWord.SIZE_128, 0xDDDDL, 0);   // mode 8 at 128 bits: a bad packet
        T.check(n.runAll(300), "drained");
        T.eq(7, d.droppedBad, "mode 0, mode 15, mode 5 at 128, two misaligned mode 3, mode 7 at 64 and mode 8 at 128 -> dropped (bad packets)");
        T.eq(7, d.badModeTotal, "bad-packet count");
        T.check((r.conditions() & (1 << Conditions.BAD_MODE_OR_SIZE)) != 0, "bad-mode-or-size condition set (TILE_CONDITIONS bit " + Conditions.BAD_MODE_OR_SIZE + ")");
        T.eq(1L, r.slot.badWrites, "mode 9 on a type-0 slot: a bad peripheral write (4.17.7)");
        T.check((r.conditions() & (1 << Conditions.BAD_PERIPH_WRITE)) != 0, "bad-peripheral-write condition set (bit 11)");
        T.eq(1, r.gatherStub.log.size(), "mode 1 -> GATHER queue");
        T.eq(4, r.writer.log.size(), "modes 2, 3, 4 and 5 -> write engine");
        if (r.writer.log.size() == 4) { T.eq(LinkWord.M_SRC, LinkWord.mode(r.writer.log.get(0)[2]), "mode 2 first"); T.eq(LinkWord.M_EXPLICIT, LinkWord.mode(r.writer.log.get(1)[2]), "mode 3 second");
            T.eq(LinkWord.M_BUCKET, LinkWord.mode(r.writer.log.get(2)[2]), "mode 4 third"); T.eq(LinkWord.M_AUG, LinkWord.mode(r.writer.log.get(3)[2]), "mode 5 fourth"); }
        T.eq(0xEEEE, r.sram.peek(0x102), "mode 3 written at its explicit addr (no PTR)");
        ControllerMemories m = r.cmem;
        T.eq(0x11111111, m.imem.peek(0x10), "mode 6 (64) imem word 0x10"); T.eq(0x22222222, m.imem.peek(0x11), "mode 6 (64) imem word 0x11");
        T.eq(0x33333333, m.dmem.peek(0x100), "mode 6 (32) dmem word 0x100"); T.eq(0, m.dmem.peek(0x101), "mode 6 (32) writes one word only");
        T.eq(0x55555555, m.imem.peek(0x7FE), "mode 6 (128) imem 0x7FE"); T.eq(0x66666666, m.imem.peek(0x7FF), "mode 6 (128) imem 0x7FF");
        T.eq(0x77777777, m.dmem.peek(0), "mode 6 (128) straddles: dmem 0"); T.eq(0x88888888, m.dmem.peek(1), "mode 6 (128) straddles: dmem 1");
        T.eq(0xCAFE0003, m.dmem.peek(0x7F3), "mode 7 slot 3 = data word 0x7F3 (byte 0x5FCC)");
        T.eq(0xABCD0006, m.dmem.peek(0x6F0 + 10), "mode 8 from src 5 (32 bits): reply word 10 (byte 0x5BC0 + 40)");
        T.eq(0, m.dmem.peek(0x6F0 + 11), "a 32-bit reply leaves word 1 of its pair alone");
        T.eq(0x00C0FFEE, m.dmem.peek(0x6F0 + 12), "mode 8 from src 6 (64 bits): reply word 12");
        T.eq(0x12345678, m.dmem.peek(0x6F0 + 13), "mode 8 from src 6 (64 bits): reply word 13");
        T.check(m.landedAt >= 0, "mode 7 slot 0 sets command landed");
        T.eq(0x0000C0DE, m.dmem.peek(0x7F0), "slot 0 word");
        long[] pm = d.perMode; int[] want = { 1, 1, 1, 3, 1, 2, 3, 3, 3, 1, 0, 0, 0, 0, 0, 1 };
        for (int i = 0; i < 16; i++) T.eq(want[i], pm[i], "director accepted mode " + i);
        T.eq(0, d.remaining[TrafficDirector.SRAM], "SRAM_REMAINING 4 -> 0 (modes 2, 3, 4, 5; the bad ones not counted)");
        T.eq(0, d.remaining[TrafficDirector.CM], "CM_REMAINING 3 -> 0 (three mode 6: a packet counts 1 whatever its size)");
        T.eq(0, d.remaining[TrafficDirector.REPLY], "REPLY_REMAINING 2 -> 0 (two replies)");
        T.eq(0, d.remaining[TrafficDirector.PERIPH], "PERIPH_REMAINING 1 -> 0 (the dropped mode-9 packet counts)");
        T.eq(0, r.conditions() & (1 << Conditions.UNEXPECTED), "no unexpected arrival yet (modes 1 and 7 are never counted)");
        T.eq(7, n.c.violations, "seven violations so far: the seven bad packets sent (invariant 5)");
        T.check(n.c.violationMessages.get(0).contains("invariant 5"), "it is invariant 5");
        // unexpected arrivals: one more mode-2, mode-6 and mode-8 with the counters at 0
        t = n.c.cycle + 2;
        n.at(5, t, 9, LinkWord.M_SRC, 0, 1); n.at(5, t + 1, 9, LinkWord.M_CMEM, 0x20, 1); n.at(5, t + 2, 9, LinkWord.M_REPLY, 0, 1);
        n.runAll(300);
        T.eq(3, d.unexpected, "three unexpected arrivals");
        T.check((r.conditions() & (1 << Conditions.UNEXPECTED)) != 0, "unexpected-arrival condition set");
        T.eq(0, d.remaining[TrafficDirector.SRAM], "counter stays 0, no wrap");
        T.eq(10, n.c.violations, "invariant 9 x 3, invariant 5 x 7");
        // modes 0, 1 and 7 are never counted
        n.at(5, n.c.cycle + 2, 9, LinkWord.M_INVALID, 0, 0); n.at(5, n.c.cycle + 3, 9, LinkWord.M_CMD, 9, LinkWord.SIZE_32, 0, 0); n.at(9, n.c.cycle + 2, 9, LinkWord.M_GATHER, 0, 0);
        n.runAll(300);
        T.eq(3, d.unexpected, "modes 0, 1 and 7 not counted");
        // a counted packet from C&C (src 64) counts too (the counting policy, 4.12)
        d.remaining[TrafficDirector.CM] = 1;
        n.cncSends.add(new long[] { n.c.cycle + 2, 9, LinkWord.M_CMEM, 0x30, 0x77L, LinkWord.SIZE_32, 0 });
        n.runAll(300);
        T.eq(0, d.remaining[TrafficDirector.CM], "C&C's mode-6 packet is counted (4.12: every mode from any source)");
        T.eq(3, d.unexpected, "and is expected");
        // size 11 (3.1): undefined, no hardware check; meshsim flags it at the sender and at the director, which drops it
        // uncounted with no condition (meshsim's choice for undefined behaviour)
        long v1 = n.c.violations; int cond1 = r.conditions(); long bad1 = d.badModeTotal;
        d.remaining[TrafficDirector.SRAM] = 1;
        n.at(5, n.c.cycle + 2, 9, LinkWord.M_SRC, 0, LinkWord.SIZE_UNDEF, 0x1111L, 0x2222L);
        n.runAll(300);
        T.eq(1L, d.undefinedSize, "size 11: flagged and dropped at the director");
        T.eq(1, d.remaining[TrafficDirector.SRAM], "size 11: not counted");
        T.eq(bad1, d.badModeTotal, "size 11 is not a bad packet");
        T.eq(cond1, r.conditions(), "size 11 sets no condition (the hardware has no check)");
        T.eq(2, n.c.violations - v1, "size 11 flagged twice: at the sender's injection and at the receiving director");
        d.remaining[TrafficDirector.SRAM] = 0;
        // address checks, invariants 11-13, and mode 1 to another tile (invariant 10)
        long v0 = n.c.violations;
        d.remaining[TrafficDirector.CM] = 4;
        t = n.c.cycle + 2;
        n.at(5, t++, 9, LinkWord.M_CMEM, 0xFF0, 0);     // into the command block
        n.at(5, t++, 9, LinkWord.M_CMEM, 0xFFF, 0);     // the command block, and its second word outside (64 bits)
        n.at(5, t++, 9, LinkWord.M_CMEM, 0x1000, 0);    // outside the packed map
        n.at(5, t++, 9, LinkWord.M_CMEM, 0xEEE, LinkWord.SIZE_128, 0, 0);   // 128 bits: words 2 and 3 in the reply block (checked per word)
        n.at(5, t++, 9, LinkWord.M_GATHER, 0, 0);       // mode 1 to another tile
        n.runAll(300);
        T.eq(5, n.c.violations - v0, "five address/locality violations");
        String all = String.join("\n", n.c.violationMessages);
        for (String inv : new String[] { "invariant 10", "invariant 11" }) T.check(all.contains(inv), inv + " reported");
        // port-B timing and cm_idle: accept at t, written at t + cm_latency, readable from t + cm_latency + 1
        d.remaining[TrafficDirector.REPLY] = 1;
        n.clearLogs();
        n.at(5, n.c.cycle + 2, 9, LinkWord.M_REPLY, 0, LinkWord.SIZE_32, 0x5A5A, 0);
        long accept = -1, written = -1; boolean sawIdle = false;
        for (int i = 0; i < 100; i++) {
            n.step();
            if (accept < 0 && !d.log.isEmpty()) accept = d.log.get(0)[0];
            if (accept >= 0 && n.c.cycle < accept + k.cmLatency && d.cmIdle(n.c.cycle)) sawIdle = true;
            if (written < 0 && m.dmem.peek(0x6F0 + 10) == 0x5A5A) written = n.c.cycle;
        }
        // host peeks show end-of-cycle state: written at the end of accept + cm_latency, so a load reads it from accept + cm_latency + 1
        T.eq(accept + k.cmLatency, written, "mode 8 written at port B cm_latency cycles after the director accepts it");
        T.check(!sawIdle, "cm_idle low while the write is in the port-B pipe");
        T.check(d.cmIdle(n.c.cycle), "cm_idle afterwards");
        out("director: each mode to its unit (bad packets dropped with the bad-mode sticky bit); counters per mode; invariants 9-11 and 5 fire; a mode-8 reply accepted at t is written at port B at t + " + (written - accept) + " (cm_latency " + k.cmLatency + "), readable by a load from t + " + (written - accept + 1) + " [meas: NetTest director]");
    }

    // ------------------------------------------------------------------ the injection point
    static void injection() {
        T.group("injection");
        Net n = new Net(Net.cfg(shuffle));
        Tile s = n.c.tiles[3];
        long T0 = n.c.cycle + 2;
        for (int j = 0; j < 10; j++) n.at(3, T0 + j, 4, LinkWord.M_SRC, 0, j);        // read returns, back to back
        n.step();                                                                     // feed puts read 0 at T0-1
        s.sendHold.put(n.c.cycle, 100, LinkWord.hdr(0, 4, LinkWord.M_SRC, 0, LinkWord.SIZE_64), 0);     // send port, visible at T0 too
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
        for (int j = 0; j < 20; j++) { s.sendHold.put(n.c.cycle, 200 + j, LinkWord.hdr(0, 4, LinkWord.M_SRC, 0, LinkWord.SIZE_64), 0); n.step(); }
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
            if (sent < 120 && r.sendHold.isEmpty(n.c.cycle + 1) && n.c.cycle >= T0 + 10) { r.sendHold.put(n.c.cycle + 1, sent, LinkWord.hdr(0, 9, LinkWord.M_SRC, 0, LinkWord.SIZE_32), 0); sent++; }
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
