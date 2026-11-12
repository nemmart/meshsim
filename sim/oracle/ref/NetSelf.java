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

    /** EngineTest.phaseDone, ref's own (W22-15). */
    static boolean phaseDone(Chip c, Tile p) {
        long t = c.cycle;
        return !p.ag.running && p.ctl.countersZero() && p.dir.inFlight() == 0 && p.memIdle(t) && p.dir.cmIdle(t) && !p.tsBusy(t);
    }
    /** The a2a coreless, stepped by hand: the first cycle tiles 0, 5, 40 and 63 are phase-done, and all 64. */
    static void phaseDoneProbe(long seed) {
        Config k = Config.defaults(); k.shuffleSeed = seed; k.checkErrors = false;
        Net n = new Net(k);
        A2A d = new A2A(7, 600);
        d.load(n.c, 0);
        long t0 = n.c.cycle;
        boolean[] started = new boolean[64];
        long[] first = { -1, -1, -1, -1 }; int[] watch = { 0, 5, 40, 63 }; long all = -1;
        for (int i = 0; i < 100000 && all < 0; i++) {
            for (Tile p : n.c.tiles) if (!started[p.idx] && n.c.cycle >= t0 + n.c.startSkew[p.idx]) { p.pulse(Fields.PULSE_START); started[p.idx] = true; }
            n.c.step();
            if (n.c.cycle < t0 + 3) continue;
            for (int j = 0; j < 4; j++) if (first[j] < 0 && phaseDone(n.c, n.c.tiles[watch[j]])) first[j] = n.c.cycle - t0;
            boolean a = true; for (Tile p : n.c.tiles) a &= phaseDone(n.c, p);
            if (a) all = n.c.cycle - t0;
        }
        System.out.printf("phaseDone seed %d: tiles 0 5 40 63 first at +%d +%d +%d +%d; all at +%d%n", seed, first[0], first[1], first[2], first[3], all);
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

    static String hx(Integer v) { return v == null ? "null" : Integer.toHexString(v); }
    static String q(String r) { return r == null ? "null" : r; }
    static void core() {                                           // a test's loads and stores through a tile's core
        Net n = new Net(Net.cfg(-1));
        Tile p = n.c.tiles[13];
        final int REG = 0xFFFFF800;
        StringBuilder sb = new StringBuilder("core:");
        sb.append(" store dmem ").append(q(p.core.store(n.c.cycle + 1, 0x100, 0xBEEF, -1)));
        n.step();
        sb.append("; load dmem ").append(hx(p.core.load(n.c.cycle + 1, 0x100, 4)));
        sb.append("; PARAMS ").append(hx(p.core.load(n.c.cycle + 1, REG, 4))).append(" STATUS ").append(hx(p.core.load(n.c.cycle + 1, REG + 4, 4)));
        sb.append(" ERRORS ").append(hx(p.core.load(n.c.cycle + 1, REG + 8, 4)));
        p.dir.remaining[2] = 7;
        sb.append("; WRITE_COUNTER_2 ").append(hx(p.core.load(n.c.cycle + 1, REG + 4 * 25, 4)));
        sb.append("; boot ROM [1] ").append(hx(p.core.load(n.c.cycle + 1, 0x02000004, 4)));
        sb.append("; halfword STATUS ").append(hx(p.core.load(n.c.cycle + 1, REG + 4, 2)));
        sb.append("; PTR ").append(hx(p.core.load(n.c.cycle + 1, 0xFFFF0400, 4)));
        System.out.println(sb);
        sb = new StringBuilder("  stores:");
        sb.append(" STATUS ").append(q(p.core.store(n.c.cycle + 1, REG + 4, 1, -1)));
        sb.append("; byte ROUTER_SET_W1 ").append(q(p.core.store(n.c.cycle + 1, REG + 4 * 13, 1, 0xFF)));
        sb.append("; program ").append(q(p.core.store(n.c.cycle + 1, 0x01000000, 1, -1)));
        sb.append("; boot ROM ").append(q(p.core.store(n.c.cycle + 1, 0x02000000, 1, -1)));
        sb.append("; unmapped ").append(q(p.core.store(n.c.cycle + 1, 0x0F000000, 1, -1)));
        System.out.println(sb);
        p.core.store(n.c.cycle + 1, REG + 4 * 26, 5, -1);       // WRITE_COUNTER_3 through the bus
        n.step(); n.step(); n.step();
        sb = new StringBuilder("  WRITE_COUNTER_3 after the store ").append(hx(p.core.load(n.c.cycle + 1, REG + 4 * 26, 4)));
        p.core.store(n.c.cycle + 1, REG + 4 * 10, 40 << 25, -1); n.step();   // ROUTER_HDR: dst 40, mode 0, counter 0
        p.core.store(n.c.cycle + 1, REG + 4 * 11, Net.cm(0x8E0), -1); n.step();   // ROUTER_ADDR
        long ts = n.c.cycle + 1;
        p.core.store(ts, REG + 4 * 15, 0x1234, -1);               // ROUTER_SEND_32
        n.step();
        boolean ok = n.runAll(300);
        sb.append("; a send ").append(ok ? "drained" : "NOT drained");
        for (long[] e : n.log(40)) sb.append(String.format(" t40@%d:%s:%s", e[0] - ts, hex(e[1]), hex(e[2])));
        sb.append(" dmem[0xe0] ").append(w(n, 40, 0xe0));
        System.out.println(sb);
    }

    /** Net's schedules are fed by Net.step and runAll only, not by Chip.run (W26). */
    static void feedOnlyInNet() {
        Config k = Net.cfg(-1); k.testReadClients = true;
        Net n = new Net(k);
        for (int i = 0; i < 16; i++) n.c.tiles[3].sram.poke(0x2000 + i, 0x100 * i + 7);
        for (int T = 10; T < 15; T++) n.at(4, T, 9, LinkWord.M_WRITE, Net.cm(0x900 + 2 * (T - 10)), Net.tagged(T, T));
        n.cncSends.add(new long[] { 12, 20, LinkWord.M_WRITE, 0x200, 0x77, LinkWord.SIZE_32, 0, 0 });
        n.rd(3, 0, 2, 0x2000, LinkWord.SIZE_64, LinkWord.M_WRITE, 0, 12, Net.cm(0x8A0));
        n.rd(3, 1, 30, 0x2004, LinkWord.SIZE_32, LinkWord.M_WRITE, 0, 12, Net.cm(0x8A4));
        n.step(); n.step(); n.step();                          // the first read request into its client FIFO
        n.c.run(40);                                           // Chip.run: nothing more fed; the FIFO's request is served
        StringBuilder sb = new StringBuilder("feed: after Chip.run(40) at " + n.c.cycle + ": t9 " + n.log(9).size() + " t20 " + n.log(20).size() + " t12");
        for (long[] e : n.log(12)) sb.append(" @").append(e[0]);
        sb.append("; late ").append(n.late).append("; pending ").append(n.pending());
        boolean ok = n.runAll(500);
        sb.append("; then runAll ").append(ok ? "drained" : "NOT drained").append(" at ").append(n.c.cycle).append(": late ").append(n.late).append(" t9");
        for (long[] e : n.log(9)) sb.append(" @").append(e[0]);
        sb.append(" t20"); for (long[] e : n.log(20)) sb.append(" @").append(e[0]);
        sb.append(" t12"); for (long[] e : n.log(12)) sb.append(" @").append(e[0]);
        System.out.println(sb);
    }

    /** Net on the machine as specified (HooksTest's and RequestBusTest's machine(): controllers on, the tiles waiting in
     *  their boot ROMs), periphType 0 and 4: packets into data memory and big SRAM, a broadcast (W22-9). */
    static void onMachine(int periph) {
        Config k = Config.machine(); k.periphType = periph;
        Net n = new Net(k);
        n.c.run(200);                                              // the boot ROMs reach their WFI
        long T0 = n.c.cycle + 2;
        for (int s = 0; s < 64; s += 9) n.at(s, T0, 9, LinkWord.M_WRITE, 0, Net.cm(0x900 + 2 * s), LinkWord.SIZE_64, Net.tagged(s, T0), 0);
        n.at(5, T0 + 1, LinkWord.BROADCAST, LinkWord.M_WRITE, 0, Net.cm(0x880), LinkWord.SIZE_32, 0xBEEF, 0);
        n.at(6, T0 + 2, 30, LinkWord.M_WRITE, 1, LinkWord.R_SRAM << 24 | 0x40, LinkWord.SIZE_64, 0x1234, 0);
        boolean ok = n.runAll(2000);
        StringBuilder sb = new StringBuilder("machine periph " + periph + ": " + (ok ? "quiescent" : "NOT quiescent") + " at " + n.c.cycle + "; late " + n.late);
        sb.append("; t9 dmem[0x112] ").append(w(n, 9, 0x112)).append(" t40 dmem[0x80] ").append(w(n, 40, 0x80)).append(" t30 sram 0x10 ").append(hex(n.c.tiles[30].sram.peek(0x10)));
        sb.append("; violations ").append(n.c.violations);
        System.out.println(sb);
        logs(n, 9, 30);
    }

    /** W22-10: ref's RequestBus from a test on Net(Config.machine()): a tile's request (through its core's stores), a
     *  controllerSend, the receivers' side (pending, get, receive, underflows), the credits, the drops at the root. */
    static void reqbus() {
        Config k = Config.machine(); k.periphType = 0;
        Net n = new Net(k);
        n.c.run(200);
        Tile p = n.c.tiles[5];
        final int REG = 0xFFFFF800;
        int[][] st = { { 18, (0 << 7) | (0x21 << 10) }, { 19, 0x1111 }, { 20, 0x2222 }, { 21, 0x3333 } };   // W0 (target 0) .. SEND
        for (int[] x : st) { p.core.store(n.c.cycle + 1, REG + 4 * x[0], x[1], -1); n.step(); }
        RequestBus b = n.c.reqBus;
        b.controllerSend(n.c.cycle, 1, (0 << 7) | (0x55 << 10), 1, 2, 3);
        StringBuilder sb = new StringBuilder("reqbus:");
        long first = -1;
        for (int i = 0; i < 200 && b.queue[0].size() < 2; i++) { n.step(); if (first < 0 && b.pending(0, n.c.cycle + 1)) first = n.c.cycle; }
        sb.append(" first pending at ").append(first).append(", two queued at ").append(n.c.cycle);
        sb.append("; head ").append(hex(b.get(0, n.c.cycle + 1, 0) & 0xFFFFFFFFL)).append(" ").append(hex(b.get(0, n.c.cycle + 1, 3) & 0xFFFFFFFFL));
        sb.append("; credits ").append(b.credits[0]);
        sb.append("; receive ").append(hex(b.receive(0, n.c.cycle + 1) & 0xFFFFFFFFL)).append(" size ").append(b.queue[0].size()).append(" credits ").append(b.credits[0]);
        n.step();
        sb.append("; then credits ").append(b.credits[0]).append(" head ").append(hex(b.get(0, n.c.cycle + 1, 0) & 0xFFFFFFFFL));
        sb.append("; receive ").append(hex(b.receive(0, n.c.cycle + 1) & 0xFFFFFFFFL)); n.step();
        sb.append("; receive on empty ").append(b.receive(0, n.c.cycle + 1)).append(" underflows ").append(b.underflows);
        for (int[] x : new int[][] { { 18, (3 << 7) | (0x31 << 10) }, { 21, 0x77 }, { 18, (5 << 7) | (0x32 << 10) }, { 21, 0x78 } }) { p.core.store(n.c.cycle + 1, REG + 4 * x[0], x[1], -1); n.step(); }
        n.runAll(500);
        sb.append("; dropped ").append(b.dropped.size()).append(" delivered ").append(b.delivered.size()).append(" credits ").append(b.credits[0]).append(" at ").append(n.c.cycle);
        sb.append("; violations ").append(n.c.violations);
        System.out.println(sb);
    }

    /** W22-11, W22-18, W23's (b)-(d): a tile's request port, the read port's host queue, loads with side effects and from
     *  a present peripheral's window, sticky EVENTS bits and a scheduled GO from the test. */
    static void hooks() {
        final int REG = 0xFFFFF800, EVENTS = REG + 4 * 6, PEEK = REG + 4 * 7, ERRORS = REG + 8, GET0 = REG + 4 * 18, RECV = REG + 4 * 21;
        {   // (c), (d) on the machine, periphType 0
            Config k = Config.machine(); k.periphType = 0;
            Net n = new Net(k);
            n.c.run(200);
            Tile p = n.c.tiles[3];
            StringBuilder sb = new StringBuilder("hooks:");
            int c9 = 1 << Registers.EV_COUNTERS_DONE, go = 1 << Registers.EV_GO;
            p.ctl.events |= c9;
            sb.append(" peek ").append(hx(p.core.load(n.c.cycle + 1, PEEK, 4)));
            n.step();
            sb.append(" peek ").append(hx(p.core.load(n.c.cycle + 1, PEEK, 4)));
            p.core.store(n.c.cycle + 1, REG + 4 * 8, c9, -1); n.step(); n.step(); n.step();   // EVENTS_MASK = c9
            sb.append(" EVENTS ").append(hx(p.core.load(n.c.cycle + 1, EVENTS, 4)));
            n.step(); n.step(); n.step();
            sb.append(" then peek ").append(hx(p.core.load(n.c.cycle + 1, PEEK, 4)));
            sb.append("; GET0 ").append(hx(p.core.load(n.c.cycle + 1, GET0, 4)));
            sb.append(" RECV ").append(hx(p.core.load(n.c.cycle + 1, RECV, 4)));
            for (int i = 0; i < 4; i++) { sb.append(" ERRORS@").append(n.c.cycle + 1).append(" ").append(hx(p.core.load(n.c.cycle + 1, ERRORS, 4))); n.step(); }
            long T = n.c.cycle + 5;
            p.ctl.scheduleGo(T);
            while (n.c.cycle < T + 2) { sb.append(" peek@").append(n.c.cycle + 1).append(" ").append(hx(p.core.load(n.c.cycle + 1, PEEK, 4))); n.step(); }
            System.out.println(sb);
        }
        {   // (b): window 1 of the replayer (periphType 4), words 0-7
            Config k = Config.machine(); k.periphType = 4;
            Net n = new Net(k);
            n.c.run(200);
            Tile p = n.c.tiles[9];
            StringBuilder sb = new StringBuilder("  window 1:");
            for (int w = 0; w < 8; w++) sb.append(" ").append(hx(p.core.load(n.c.cycle + 1, REG + 0x200 + 4 * w, 4)));
            System.out.println(sb);
        }
        {   // W22-11: the request port from the test, on the machine with the queues
            Config k = Config.machine(); k.periphType = 0;
            Net n = new Net(k);
            n.c.run(200);
            Tile s = n.c.tiles[12];
            StringBuilder sb = new StringBuilder("  req:");
            s.req.setW(0, (0 << 7) | (0x44 << 10)); s.req.setW(1, 0x44);
            sb.append(" canPush ").append(s.req.canPush(n.c.cycle + 1));
            s.req.send(n.c.cycle, 0x99);
            sb.append(" setW ").append(s.req.setW[0] | s.req.setW[1] | s.req.setW[2]);
            long first = -1;
            for (int i = 0; i < 100 && first < 0; i++) { n.step(); if (n.c.reqBus.pending(0, n.c.cycle + 1)) first = n.c.cycle; }
            sb.append(" pending at ").append(first).append(" head ").append(hx(n.c.reqBus.get(0, n.c.cycle + 1, 0))).append(" ").append(hx(n.c.reqBus.get(0, n.c.cycle + 1, 3)));
            System.out.println(sb);
        }
        {   // W22-18: the read port's host queue, on the coreless machine
            Net n = new Net(Net.cfg(-1));
            Tile p = n.c.tiles[6];
            for (int i = 0; i < 16; i++) p.sram.poke(0x1000 + i, 0x500 + i);
            StringBuilder sb = new StringBuilder("  rdport: idle ").append(p.readPort.idle(n.c.cycle)).append(" all ").append(p.readPort.allAccepted());
            for (int i = 0; i < 10; i++) p.readPort.hostQueue(0x1000 + i, Net.cm(0x900 + i));
            sb.append("; queued: idle ").append(p.readPort.idle(n.c.cycle)).append(" all ").append(p.readPort.allAccepted());
            long idleAt = -1, accAt = -1;
            for (int i = 0; i < 200 && accAt < 0; i++) { n.step(); if (idleAt < 0 && p.readPort.idle(n.c.cycle)) idleAt = n.c.cycle; if (p.readPort.allAccepted()) accAt = n.c.cycle; }
            sb.append("; idle at ").append(idleAt).append(", all accepted at ").append(accAt);
            n.runAll(500);
            sb.append("; dmem[0x100..0x109]"); for (int i = 0; i < 10; i++) sb.append(" ").append(w(n, 6, 0x100 + i));
            System.out.println(sb);
        }
    }

    /** W26's: ref's test peripheral on one tile (Config.testPeripheral), the others none: its window from the core. */
    static void testPeripheral() {
        Config k = Config.machine(); k.periphType = 0;
        k.testPeripheral = tile -> tile == 7 ? new TestPeripheral() : null;
        Net n = new Net(k);
        n.c.run(200);
        final int REG = 0xFFFFF800;
        StringBuilder sb = new StringBuilder("test peripheral: PARAMS t7 ").append(hx(n.c.tiles[7].core.load(n.c.cycle + 1, REG, 4)))
            .append(" t8 ").append(hx(n.c.tiles[8].core.load(n.c.cycle + 1, REG, 4))).append("; t7 window 1:");
        for (int w = 0; w < 8; w++) sb.append(" ").append(hx(n.c.tiles[7].core.load(n.c.cycle + 1, REG + 0x200 + 4 * w, 4)));
        sb.append("; t8 window 1 word 0 ").append(hx(n.c.tiles[8].core.load(n.c.cycle + 1, REG + 0x200, 4)));
        System.out.println(sb);
    }

    /** ref's drainReports: C&C 1 drains target 0's queue, a report, a result (answered) and another request. */
    static void drain() {
        Config k = Config.machine(); k.periphType = 0;
        Net n = new Net(k);
        n.c.run(200);
        final int REG = 0xFFFFF800;
        n.c.tiles[6].core.store(n.c.cycle + 1, 0x900, 0x5555, -1); n.step();
        n.c.cc.drainReports = true;
        int[][] rq = { { 5, (1 << 10), 0x11, 0x12, 0x13 }, { 6, (3 << 10) | (0 << 18), 0x900, 0x22, 0x23 }, { 7, (2 << 10), 0x31, 0x32, 0x33 }, { 5, (1 << 10), 0x41, 0x42, 0x43 } };
        for (int[] r : rq) {
            Tile p = n.c.tiles[r[0]];
            p.core.store(n.c.cycle + 1, REG + 4 * 18, r[1], -1); n.step();
            p.core.store(n.c.cycle + 1, REG + 4 * 19, r[2], -1); n.step();
            p.core.store(n.c.cycle + 1, REG + 4 * 20, r[3], -1); n.step();
            p.core.store(n.c.cycle + 1, REG + 4 * 21, r[4], -1); n.step();
        }
        for (int i = 0; i < 300; i++) n.step();
        StringBuilder sb = new StringBuilder("drain:");
        for (long[] e : n.c.cc.reports) sb.append(" report@").append(e[0]).append(" ").append(hex(e[1])).append(" ").append(hex(e[4]));
        for (long[] e : n.c.cc.requests) sb.append(" request@").append(e[0]).append(" ").append(hex(e[1]));
        for (long[] e : n.c.cc.results) sb.append(" result@").append(e[0]).append(" src ").append(e[1]).append(" ").append(hex(e[2])).append(" ").append(hex(e[3]));
        sb.append("; reportsOf(5) ").append(n.c.cc.reportsOf(5).size()).append(" reportsOf(9) ").append(n.c.cc.reportsOf(9).size());
        sb.append("; t6 dmem[0x900] ").append(hx(n.c.tiles[6].core.load(n.c.cycle + 1, 0x900, 4))).append(" at ").append(n.c.cycle);
        sb.append("; delivered");
        for (RequestBus.Req q : n.c.reqBus.delivered) sb.append(" ").append(q.deliveredAt).append(":").append(q.srcTile).append(":").append(hex(q.w[0])).append(":").append(hex(q.w[3]));
        System.out.println(sb);
    }

    /** W22-20: 128 requests pushed at once into one tile's 128-deep FIFO, then drained with the receivers'
     *  side popping one a cycle when pending. */
    static void backPressure() {
        Config k = Config.machine(); k.periphType = 0;
        Net n = new Net(k);
        n.c.run(200);
        Tile s = n.c.tiles[20];
        for (int i = 0; i < 128; i++) { s.req.setW(0, (0 << 7) | (i << 10)); s.req.setW(1, i); s.req.send(n.c.cycle, 1000 + i); }
        StringBuilder sb = new StringBuilder("back-pressure: canPush ").append(s.req.canPush(n.c.cycle + 1)).append(" stalled ").append(s.req.stalledPushes);
        RequestBus b = n.c.reqBus;
        int got = 0; long last = -1, sum = 0;
        for (int i = 0; i < 3000 && got < 128; i++) {
            if (b.pending(0, n.c.cycle + 1)) { sum += b.receive(0, n.c.cycle + 1); got++; last = n.c.cycle + 1; }
            n.step();
        }
        sb.append("; received ").append(got).append(" last at ").append(last).append(" sum ").append(sum).append(" credits ").append(b.credits[0]);
        sb.append(" delivered ").append(b.delivered.size()).append(" violations ").append(n.c.violations);
        System.out.println(sb);
    }

    /** W22-16: AgTest's figure-12 requests on the write engine's lines (weTsReq, weHistReq) from the test. */
    static void figure12(int L) {
        Config k = Config.defaults(); k.withTsLatency(L); k.checkErrors = false; k.checkUnexpected = false;
        Chip c = new Chip(k); Tile p = c.tiles[7], h = c.tiles[20];
        p.setPtr(9, 0x1000); h.hist.poke(3, 100);
        StringBuilder sb = new StringBuilder("figure 12, L = " + L + ":");
        for (int r = 0; r < 3; r++) {
            long before = p.ts.rmws; p.weTsReq.put(c.cycle + 1, TableStore.OP_ADD_RET, TableStore.PTR + 9, 2);
            long tr = -1; for (int i = 0; i < 50 && tr < 0; i++) { c.step(); if (p.ts.rmws > before) tr = c.cycle; }
            c.step(); c.step();
            sb.append(" ptr@").append(tr).append(" ").append(Integer.toHexString(p.ptr(9)));
        }
        long before = h.hist.rmws; h.weHistReq.put(c.cycle + 1, 0, 3, 1);
        long tr = -1; for (int i = 0; i < 50 && tr < 0; i++) { c.step(); if (h.hist.rmws > before) tr = c.cycle; }
        c.step(); c.step();
        sb.append("; hist@").append(tr).append(" ").append(h.hist.peek(3));
        System.out.println(sb);
    }

    /** W23's request 11: a core's misaligned load stops the run with ref's UndefinedBehavior, naming tile and reason. */
    static void ubStop() {
        Config k = Config.machine(); k.periphType = 0;
        Net n = new Net(k);
        Tile p = n.c.tiles[9];
        int B = Registers.PMEM_BASE;
        p.core.pokeLocal(B, 0x00102503);                                 // lw a0, 1(zero): misaligned
        p.core.pokeLocal(B + 4, 0x0000006F);
        n.c.cc1p.expect = 1L << 9;
        n.c.cc.command(9, Registers.CMD_JUMP << 24, B).go();
        try { n.c.run(2000); System.out.println("ub: ran on"); }
        catch (UndefinedBehavior e) { System.out.println("ub: " + e.getMessage()); }
    }

    public static void main(String[] a) {
        ubStop();
        figure12(1);
        figure12(3);
        backPressure();
        drain();
        testPeripheral();
        hooks();
        reqbus();
        onMachine(0);
        onMachine(4);
        feedOnlyInNet();
        core();
        phaseDoneProbe(-1);
        phaseDoneProbe(4);
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
