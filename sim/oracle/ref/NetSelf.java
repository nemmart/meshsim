/**
 * NetSelf (WORKER20, P2): v1.3's Net harness and coreless machine (Config.defaults: no controller executing, the GATHER
 * engine a drain, the engines set from Java) driven through the paths the network, memory and engine suites use,
 * printing what they read back, for tests/mach.h to reproduce line for line (tests/test_mach.cpp; fixture
 * tests/mach/netself.out, written by oracle/ref/netself.sh, run in a scratch copy of ref/meshsim's layout).
 */
final class NetSelf {
    static String hex(long v) { return Long.toHexString(v); }
    static String w(Net n, int tile, int word) { return hex(n.c.tiles[tile].core.peekLocal(4 * word) & 0xFFFFFFFFL); }
    static void logs(Net n, int... tiles) {
        for (int k : tiles) {
            java.util.List<long[]> l = n.log(k);
            StringBuilder sb = new StringBuilder("  tile " + k + ": " + l.size() + " arrivals");
            for (int i = 0; i < Math.min(l.size(), 6); i++) sb.append(String.format(" [%d %s %s %s]", l.get(i)[0], hex(l.get(i)[1]), hex(l.get(i)[2]), hex(l.get(i)[3])));
            System.out.println(sb);
        }
    }

    static void packets(long seed) {
        Net n = new Net(Net.cfg(seed));
        int seq = 0;
        for (int s = 0; s < 64; s += 7)                       // fan-in: ten tiles to tile 9's data memory at once
            n.at(s, 20, 9, LinkWord.M_WRITE, Net.cm(0x800 + 2 * s), Net.tagged(seq++, 20));
        for (int T = 30; T < 40; T++) n.at(0, T, 63, LinkWord.M_WRITE, Net.cm(0x900 + 2 * (T - 30)), Net.tagged(seq++, T));   // a stream, one a cycle
        n.at(5, 50, LinkWord.BROADCAST, LinkWord.M_WRITE, 0, Net.cm(0x880), LinkWord.SIZE_128, 0x1111222233334444L, 0x5555666677778888L);
        n.at(9, 51, 9, LinkWord.M_WRITE, 0, Net.cm(0x881), LinkWord.SIZE_32, 0xABCDL, 0);   // to itself
        for (int i = 0; i < 3; i++) n.at(17, 60 + i, 17, LinkWord.M_GATHER, 0, i, LinkWord.SIZE_64, 0x40 + i, 0);   // into the drain
        boolean ok = n.runAll(100000);
        System.out.printf("packets seed %d: %s at cycle %d; late %d; router peak %d; t17 drained %d; t9 dmem[0xe] %s dmem[0x80] %s; t63 dmem[0x10e] %s; t40 dmem[0x80..0x83] %s %s %s %s%n",
                seed, ok ? "quiescent" : "NOT quiescent", n.c.cycle, n.late, n.maxRouterPeak(), n.c.tiles[17].gatherStub.taken, hex(n.c.tiles[9].core.peekLocal(4 * 0xe) & 0xFFFFFFFFL),
                hex(n.c.tiles[9].core.peekLocal(4 * 0x80) & 0xFFFFFFFFL), hex(n.c.tiles[63].core.peekLocal(4 * 0x10e) & 0xFFFFFFFFL),
                hex(n.c.tiles[40].core.peekLocal(4 * 0x80) & 0xFFFFFFFFL), hex(n.c.tiles[40].core.peekLocal(4 * 0x81) & 0xFFFFFFFFL),
                hex(n.c.tiles[40].core.peekLocal(4 * 0x82) & 0xFFFFFFFFL), hex(n.c.tiles[40].core.peekLocal(4 * 0x83) & 0xFFFFFFFFL));
        logs(n, 9, 63, 40, 5, 17);
    }

    static void reads() {
        Config k = Net.cfg(-1); k.testReadClients = true;
        Net n = new Net(k);
        for (int i = 0; i < 16; i++) n.c.tiles[3].sram.poke(0x2000 + i, 0x1000 * i + 3);
        n.rd(3, 2, 10, 0x2000, LinkWord.SIZE_64, LinkWord.M_WRITE, 0, 12, Net.cm(0x8A0));
        n.rd(3, 0, 10, 0x2004, LinkWord.SIZE_128, LinkWord.M_WRITE, 0, 12, Net.cm(0x8A4));
        n.rd(3, 1, 11, 0x2008, LinkWord.SIZE_32, LinkWord.M_WRITE, 0, 3, Net.cm(0x8A8));    // to itself (the loopback)
        for (int i = 0; i < 70; i++) n.rd(3, 2, 12, 0x2000 + (i & 15), LinkWord.SIZE_32, LinkWord.M_WRITE, 0, 20 + (i & 7), Net.cm(0x8C0 + (i & 31)));
        boolean ok = n.runAll(100000);
        System.out.printf("reads: %s at cycle %d; late %d; t12 dmem[0xa0..0xa7] %s %s %s %s %s %s; t3 dmem[0xa8] %s%n", ok ? "quiescent" : "NOT quiescent", n.c.cycle, n.late,
                w(n, 12, 0xa0), w(n, 12, 0xa1), w(n, 12, 0xa4), w(n, 12, 0xa5), w(n, 12, 0xa6), w(n, 12, 0xa7), w(n, 3, 0xa8));
        logs(n, 12, 3, 21);
    }

    static void a2a(long seed, int gap) {
        Config k = Config.defaults(); k.shuffleSeed = seed; k.checkErrors = false;
        Net n = new Net(k);
        A2A d = new A2A(7, 2000);
        d.load(n.c, gap);
        long cyc = A2A.run(n.c, 2_000_000);
        Tile t5 = n.c.tiles[5];
        System.out.printf("a2a seed %d gap %d: %d cycles (at cycle %d); misplaced %d; t5 ag requests %d done %b; t5 remaining[1] %d memIdle %b; t5 ptr[9] %s%n",
                seed, gap, cyc, n.c.cycle, d.verify(n.c), t5.ag.requests, t5.ag.done, t5.dir.remaining[1], t5.memIdle(n.c.cycle), hex(t5.ptr(9)));
        boolean ok = n.runAll(10000);
        System.out.printf("  then %s at cycle %d; t5 gather drained %d%n", ok ? "quiescent" : "NOT quiescent", n.c.cycle, t5.gatherStub.taken);
    }

    static String recv(Net n, int c) {
        StringBuilder sb = new StringBuilder(n.c.ccs[c].received.size() + " [");
        for (long[] e : n.c.ccs[c].received) sb.append(String.format(" %d:%s:%s", e[0], hex(e[1]), hex(e[2])));
        return sb.append(" ]").toString();
    }

    static void controllers(long seed) {
        Net n = new Net(Net.cfg(seed));
        long T0 = n.c.cycle + 2;                                   // C&C 1's broadcast (NetTest's broadcast group, s = 64)
        n.cncSends.add(new long[] { T0, LinkWord.BROADCAST, LinkWord.M_SRC, 0, Net.tagged(64, T0) });
        boolean ok = n.runAll(500);
        long lo = Long.MAX_VALUE, hi = 0, copies = 0;
        for (int d = 0; d < 64; d++) for (long[] e : n.log(d)) { lo = Math.min(lo, e[0]); hi = Math.max(hi, e[0]); copies++; }
        System.out.printf("controllers seed %d: broadcast %s at cycle %d; %d copies, first %d last %d; C&C 1 received %s%n", seed,
                ok ? "drained" : "NOT drained", n.c.cycle, copies, lo - T0, hi - T0, recv(n, 0));
        n.clearLogs();
        T0 = n.c.cycle + 2;                                        // a command from C&C 1 (an argument, then slot 0)
        n.cncSends.add(new long[] { T0, 9, LinkWord.M_WRITE, 0x80 + 12, 0xCAFE0003L, LinkWord.SIZE_32, 0, 0 });
        n.cncSends.add(new long[] { T0 + 1, 9, LinkWord.M_WRITE, 0x80, 0x0000C0DEL, LinkWord.SIZE_32, 0, 0 });
        for (int k = 0; k < 5; k++) n.cncSends.add(new long[] { T0 + 1, 20 + k, LinkWord.M_WRITE, 0x100, k, LinkWord.SIZE_64, 0, 2 });   // back to back
        ok = n.runAll(500);
        System.out.printf("  command %s at cycle %d; t9 dmem[0x20] %s dmem[0x23] %s%n", ok ? "drained" : "NOT drained", n.c.cycle, w(n, 9, 0x20), w(n, 9, 0x23));
        logs(n, 9, 20, 24);
        n.clearLogs();
        T0 = n.c.cycle + 2;                                        // tiles to C&C 1-3
        for (int s : new int[] { 0, 2, 37, 63 }) for (int c = 0; c < 3; c++) n.at(s, T0 + c, 64 + c, LinkWord.M_WRITE, 3, 0x100, LinkWord.SIZE_32, s, 0);
        ok = n.runAll(500);
        System.out.printf("  to the controllers %s at cycle %d%n", ok ? "drained" : "NOT drained", n.c.cycle);
        for (int c = 0; c < 3; c++) System.out.println("  C&C " + (c + 1) + " received " + recv(n, c));
        n.clearLogs();                                             // direct sends between cycles (ref's ccs[c].send(c.cycle, ...)): their cycles
        long t0 = n.c.cycle;
        n.c.ccs[1].send(t0, 37, LinkWord.M_WRITE, 2, 0x100, LinkWord.SIZE_32, 0x55, 0);
        n.c.ccs[0].send(t0, 5, LinkWord.M_WRITE, 2, 0x104, LinkWord.SIZE_32, 0x56, 0);
        n.step();
        n.c.ccs[1].send(n.c.cycle, 37, LinkWord.M_WRITE, 2, 0x108, LinkWord.SIZE_32, 0x57, 0);   // the next cycle, back to back
        n.c.ccs[2].send(n.c.cycle, 64, LinkWord.M_WRITE, 2, 0x10C, LinkWord.SIZE_64, 0x58, 0);   // C&C 3 -> C&C 1
        ok = n.runAll(300);
        StringBuilder sb = new StringBuilder("  direct sends " + (ok ? "drained" : "NOT drained") + " at cycle " + (n.c.cycle - t0) + " after:");
        for (int d : new int[] { 37, 5 }) for (long[] e : n.log(d)) sb.append(String.format(" t%d@%d:%s", d, e[0] - t0, hex(e[1])));
        for (long[] e : n.c.ccs[0].received) sb.append(String.format(" cc1@%d:%s:%s", e[0] - t0, hex(e[1]), hex(e[2])));
        System.out.println(sb);
        n.clearLogs();                                             // C&C 2 -> C&C 3, C&C 2's broadcast (sizes and sources only)
        n.c.ccs[1].send(n.c.cycle, 66, LinkWord.M_WRITE, 2, 0x200, LinkWord.SIZE_64, 0x77, 0);
        n.runAll(300);
        int src = n.c.ccs[2].received.size() == 1 ? LinkWord.src(n.c.ccs[2].received.get(0)[2]) : -1;
        n.clearLogs();
        n.c.ccs[1].send(n.c.cycle, LinkWord.BROADCAST, LinkWord.M_COUNT, 1, 0, LinkWord.SIZE_32, 9, 0);
        n.runAll(300);
        int reached = 0; for (int d = 0; d < 64; d++) if (n.log(d).size() == 1 && LinkWord.src(n.log(d).get(0)[2]) == 65) reached++;
        System.out.printf("  C&C 2 -> C&C 3 from %d; C&C 2's broadcast reaches %d tiles once, controllers %d %d %d%n", src, reached,
                n.c.ccs[0].received.size(), n.c.ccs[1].received.size(), n.c.ccs[2].received.size());
    }

    public static void main(String[] a) {
        controllers(-1);
        controllers(3);
        packets(-1);
        packets(5);
        reads();
        a2a(-1, 0);
        a2a(4, 0);
        a2a(-1, 8);
    }
}
