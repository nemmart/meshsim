import java.util.*;

/**
 * M2 tests: the big SRAM (spec 4.1), the SRAM read and write queues (spec 4.5), mem_idle and the disjoint-buffer checker
 * (invariant 2). Arg: shuffle seed (default -1). Prints measured tables for meshsim-v4.0-M2-RESULTS.md.
 */
public final class MemTest {
    static long shuffle = -1;
    static final StringBuilder report = new StringBuilder();
    static void out(String s) { report.append(s).append('\n'); }

    public static void main(String[] a) {
        if (a.length > 0) shuffle = Long.parseLong(a[0]);
        unitDecode(); unitPartialAndTie(); unitStall(); unitCombining(); unitWrapSmall(); unitLatency();
        throughput(); priority(); packets(); returnOrder(); tokenWindow(); disjoint(); memIdle(); oversubscription();
        System.out.print(report);
        System.exit(T.done("MemTest" + (shuffle >= 0 ? " (shuffled, seed " + shuffle + ")" : "")));
    }

    static int pat(int tile, int chunk) { return (int) (chunk * 0x9E3779B1L) ^ (tile * 0x01000193); }
    static long pat64(int tile, int chunk) { return (pat(tile, chunk) & 0xFFFFFFFFL) | ((long) pat(tile, (chunk + 1) & 0xFFFFF) << 32); }
    static void fill(Tile p, int from, int n) { for (int i = 0; i < n; i++) p.sram.poke((from + i) & 0xFFFFF, pat(p.idx, (from + i) & 0xFFFFF)); }

    // ======================================================== unit level: BigSRAM at its bus ends
    static final class U {
        final BigSRAM s; final WordHold rd = new WordHold("rd"), wr = new WordHold("wr"); long t;
        final List<long[]> rets = new ArrayList<>();            // {t, token|part<<16, lo, hi, area}
        U(Config c) { s = new BigSRAM(0, c); s.readReq = rd; s.writeReq = wr; }
        void step() {
            s.runCycle(++t);
            for (int a = 0; a < 8; a++) if (s.ret[a].ready(t)) { rets.add(new long[] { t, s.ret[a].peekA(t), s.ret[a].peekB(t), s.ret[a].peekC(t), a }); s.ret[a].take(t); }
        }
        void read(int addr, int n, int tok) { rd.put(t, addr | (long) n << 20 | (long) tok << 24, 0, 0); }
        void write(int addr, int n, long lo, long hi) { wr.put(t, addr | (long) n << 20, lo, hi); }
        void run(int n) { for (int i = 0; i < n; i++) step(); }
    }
    static Config cfg() { Config c = Config.defaults(); c.shuffleSeed = shuffle; c.checkUnexpected = false; c.skew = false; c.testReadClients = true; return c; }

    static void unitDecode() {
        T.group("decode");
        T.eq(0, BigSRAM.area(0x1FFFF), "area of 0x1FFFF"); T.eq(1, BigSRAM.area(0x20000), "area of 0x20000"); T.eq(7, BigSRAM.area(0xFFFFF), "area of 0xFFFFF");
        T.eq(0x7FFF, BigSRAM.row(0x1FFFC), "row of 0x1FFFC"); T.eq(0, BigSRAM.row(0x20003), "row of 0x20003");
        T.eq(0b0011, BigSRAM.chunkMask(0x100, 2), "64-bit at offset 0"); T.eq(0b1100, BigSRAM.chunkMask(0x102, 2), "64-bit at offset 2");
        T.eq(0b1000, BigSRAM.chunkMask(0x103, 1), "32-bit at offset 3"); T.eq(0b1111, BigSRAM.chunkMask(0x104, 4), "128-bit");
        U u = new U(cfg());
        u.s.poke64(0x40010, 0x1111111100000000L); u.s.poke64(0x40012, 0x3333333322222222L);
        u.read(0x40010, 4, 7); u.run(5);
        T.eq(1, u.rets.size(), "one return"); long[] r = u.rets.get(0);
        T.eq(2, r[4], "returned by area 2"); T.eq(7, r[1] & 0xFFFF, "token"); T.eq(0x1111111100000000L, r[2], "row lo"); T.eq(0x3333333322222222L, r[3], "row hi");
        T.eq(1, u.s.reads[2], "one access in area 2");
    }

    static void unitPartialAndTie() {
        T.group("partial writes, tie");
        U u = new U(cfg());
        u.s.poke64(0x500, 0xAAAAAAAAAAAAAAAAL); u.s.poke64(0x502, 0xBBBBBBBBBBBBBBBBL);
        u.write(0x502, 2, 0x2222222211111111L, 0); u.step();
        u.write(0x500, 2, 0x4444444433333333L, 0); u.step();
        u.run(3);
        T.eq(0x4444444433333333L, u.s.peek64(0x500), "64-bit write at offset 0 leaves offset 2 alone");
        T.eq(0x2222222211111111L, u.s.peek64(0x502), "64-bit write at offset 2");
        T.eq(2, u.s.writes[0], "each 64-bit write is one access (chunk enables)");
        // tie: a read and a write to the same row arrive in the same cycle: the write is queued first
        u.read(0x500, 2, 1); u.write(0x500, 2, 0x5555555555555555L, 0); u.run(6);
        T.eq(0x5555555555555555L, u.rets.get(0)[2], "same-cycle read and write: the write is served first, the read sees it");
        // 32-bit and 128-bit writes
        u.s.poke(0x506, 0xBBBBBBBB);
        u.wr.put(u.t, 0x507 | 1L << 20, 0x77777777L, 0); u.run(3);
        T.eq(0x77777777, u.s.peek(0x507), "32-bit write"); T.eq(0xBBBBBBBB, u.s.peek(0x506), "neighbouring chunk kept");
    }

    static void unitStall() {
        T.group("queue depth and stalls");
        for (int depth : new int[] { 4, 8, 16 }) {
            Config c = cfg(); c.areaQueueDepth = depth;
            U u = new U(c);
            // every cycle one read and one write to area 3: two arrivals, one access, so the queue grows by one a cycle
            int reads = 0, writes = 0, refusedR = 0, refusedW = 0;
            for (int i = 0; i < 40; i++) {
                if (u.rd.refused(u.t)) { u.rd.retry(u.t); refusedR++; } else { u.read(0x60000 + 4 * i, 4, i); reads++; }
                if (u.wr.refused(u.t)) { u.wr.retry(u.t); refusedW++; } else { u.write(0x70000 + 2 * i, 2, i, 0); writes++; }
                u.step();
            }
            T.eq(depth - 1, u.s.queuePeak[3], "depth " + depth + ": the queue holds depth - 1 after each cycle (13a: room by the count before the pop; the area serves every cycle)");
            T.check(u.s.readStallCycles > 0, "depth " + depth + ": the SRAM read queue stalls");
            T.eq(0, u.s.writeStallCycles, "depth " + depth + ": the SRAM write queue never stalls: writes take every freed slot (tie order)");
            T.check(u.s.reads[3] <= depth, "depth " + depth + ": reads wait once the queue is full (" + u.s.reads[3] + " served)");
            if (depth == 8) out("area under two queues of one request a cycle each (unit test, depth 8): the queue fills to " + (depth - 1) + " after a cycle (room is judged by the count before the pop, 13a); from then on every freed slot goes to the write (writes win the tie, ruled 2 October), so the SRAM read queue waits while the writes continue [meas: MemTest queue depth and stalls]");
        }
    }

    static void unitCombining() {
        T.group("write combining");
        for (boolean on : new boolean[] { false, true }) {
            Config c = cfg(); c.writeCombining = on;
            U u = new U(c);
            // congest area 1 with reads + writes for 6 cycles, then writes only: 64-bit halves of consecutive rows
            int w = 0;
            for (int i = 0; i < 6; i++) { u.read(0x21000 + 4 * i, 4, i); u.write(0x30000 + 2 * w, 2, 0x100 + w, 0); w++; u.step(); }
            for (int i = 0; i < 20; i++) { if (u.wr.refused(u.t)) u.wr.retry(u.t); else { u.write(0x30000 + 2 * w, 2, 0x100 + w, 0); w++; } u.step(); }
            u.run(20);
            boolean ok = true; for (int i = 0; i < w; i++) if (u.s.peek64(0x30000 + 2 * i) != 0x100 + i) ok = false;
            T.check(ok, "combining " + on + ": every write landed");
            if (!on) T.eq(w, u.s.writes[1], "off: one access per 64-bit write");
            else { T.check(u.s.combined[1] > 0, "on: writes combined (" + u.s.combined[1] + ")"); T.eq(w, u.s.writes[1] + u.s.combined[1], "on: accesses + merges = writes"); }
            if (on) out(String.format("write combining (param, off by default): after a congested stretch, %d of %d 64-bit writes merged into a queued write to the same row [meas: MemTest write combining]", u.s.combined[1], w));
        }
    }

    static void unitWrapSmall() {
        T.group("wrap and small device");
        U u = new U(cfg());
        u.s.poke(0xFFFFE, 0xE); u.s.poke(0xFFFFF, 0xF); u.s.poke(0, 0x10); u.s.poke(1, 0x11);
        T.eq(0x10, u.s.peek(0x100000 & 0xFFFFF), "address 2^20 is chunk 0");
        u.read(0xFFFFE, 2, 3); u.run(5);
        T.eq(0x0000000F0000000EL, u.rets.get(0)[3], "the top row's chunks 2-3 from area 7");
        // small device: 2 MB (areas 0-3)
        Config c = cfg(); c.memChunks = 0x80000;
        U v = new U(c);
        v.s.poke(0x7FFFF, 0x77); v.s.poke(0x80000, 0x88); v.s.poke(0xFFFFF, 0x99);
        T.eq(0x77, v.s.peek(0x7FFFF), "last real chunk"); T.eq(0, v.s.peek(0x80000), "above the device: reads 0"); T.eq(0, v.s.peek(0xFFFFF), "0xFFFFF reads 0");
        v.write(0x80000, 2, 0x1234, 0); v.step(); v.read(0x80000, 2, 0); v.run(6);
        T.eq(0, v.rets.get(0)[2], "a write above the device is dropped; the read returns 0");
        T.eq(1, v.s.writes[4], "the access still took its turn in area 4's queue");
    }

    static void unitLatency() {
        T.group("read latency");
        StringBuilder sb = new StringBuilder("read latency, request put into the SRAM's holding register at t -> row on the return line [meas: MemTest read latency]:");
        for (int L = 1; L <= 4; L++) {
            Config c = cfg(); c.sramReadLatency = L;
            U u = new U(c); long t0 = u.t; u.read(0x100, 4, 0); u.run(10);
            long lat = u.rets.get(0)[0] - t0;
            T.eq(2 + L, lat, "unqueued: taken at t + 1, served at t + 2, visible at t + 2 + L");
            sb.append(" L=").append(L).append(": ").append(lat);
        }
        out(sb.toString() + " = 2 + L: taken at t + 1, served at t + 2, row visible at t + 2 + L; the SRAM read queue releases the packet in that cycle");
    }

    // ======================================================== chip level: the SRAM read queue and the SRAM write queue
    static Net net() { return new Net(cfg()); }

    static void throughput() {
        T.group("throughput");
        StringBuilder sb = new StringBuilder("throughput [meas: MemTest throughput], 2,000 requests, cycles from the first packet to the last at the receiver:\n");
        int N = 2000;
        String[] name = { "64-bit reads, one area", "64-bit reads, round-robin over 8 areas", "128-bit reads, one area (2 packets each)", "32-bit reads, one area",
                "64-bit reads (area 1) + 2,000 incoming writes (area 2), together", "reads every 2nd cycle (area 1) + writes every 2nd cycle (area 1, another buffer)" };
        for (int k = 0; k < name.length; k++) {
            Net n = net(); Tile src = n.c.tiles[10], dst = n.c.tiles[11];
            fill(src, 0, 0x100000);
            dst.setPtr(10, 0x40000); src.setPtr(9, k == 5 ? 0x30000 : 0x50000);
            int w = k == 2 ? 3 : k == 3 ? 1 : 2, step = k == 2 ? 4 : k == 3 ? 1 : 2, pace = k == 5 ? 2 : 1;
            for (int j = 0; j < N; j++) {
                int addr = k == 1 ? (j % 8) * 0x20000 + (j / 8) * 2 : 0x20000 + j * step;
                n.rd(10, SRAMReadQueue.AG, 2 + (long) j * pace, addr, w, LinkWord.M_SRC, 11, 0, 0);
            }
            if (k >= 4) for (int j = 0; j < N; j++) n.at(9, 2 + (long) j * pace, 10, LinkWord.M_SRC, 0, j);
            T.check(n.runAll(50000), name[k] + ": drained");
            List<long[]> l = n.log(11);
            int pk = k == 2 ? 2 * N : N;
            T.eq(pk, l.size(), name[k] + ": packets");
            boolean ok = true;
            for (int j = 0; j < N; j++) {
                int addr = k == 1 ? (j % 8) * 0x20000 + (j / 8) * 2 : 0x20000 + j * step;
                long want = w == 1 ? pat(10, addr) & 0xFFFFFFFFL : pat64(10, addr);
                long got = l.get(k == 2 ? 2 * j : j)[1];
                if (got != want) ok = false;
                if (k == 2 && l.get(2 * j + 1)[1] != pat64(10, addr + 2)) ok = false;
            }
            T.check(ok, name[k] + ": payloads, in request order");
            long span = l.get(pk - 1)[0] - l.get(0)[0] + 1;
            T.eq(0, src.srq.stallCycles, name[k] + ": no read-queue stall");
            if (k < 5) T.eq(pk, span, name[k] + ": one packet per cycle");
            if (k >= 4) { T.eq(N, src.writer.taken, name[k] + ": every incoming write taken"); T.eq(0, src.writer.stallCycles, name[k] + ": no write-queue stall"); }
            n.c.scanOverflows(); T.eq(0, n.c.violations, name[k] + ": no violations " + n.c.violationReport());
            sb.append(String.format("  %-70s %5d cycles for %d packets; read stalls %d, write stalls %d%n", name[k], span, pk, src.srq.stallCycles, src.writer.stallCycles));
        }
        out(sb.toString().stripTrailing());
    }

    static void priority() {
        T.group("priority");
        Net n = net(); Tile p = n.c.tiles[20];
        fill(p, 0x100, 64);
        n.c.tiles[21].writer.record();
        // five requests per client, all presented from the same cycles: gather value, the AG, and the read port (lowest;
        // its 32-bit results loop back as mode-7 packets to tile 20 itself and land in its data memory, 4.3)
        n.c.tiles[20].dir.remaining[TrafficDirector.REPLY] = 5;
        for (int j = 0; j < 5; j++) {
            n.rd(20, SRAMReadQueue.GATHER, 2, 0x100 + 2 * j, 2, LinkWord.M_SRC, 21, 0, 0);
            n.rd(20, SRAMReadQueue.AG, 2, 0x100 + 2 * (5 + j), 2, LinkWord.M_SRC, 21, 0, 0);
            n.rd(20, SRAMReadQueue.READPORT, 2, 0x100 + 2 * (10 + j), 1, LinkWord.M_REPLY, 20, 0x900 + j, 0);
        }
        n.runAll(500);
        List<long[]> l = n.c.tiles[21].writer.log;
        T.eq(10, l.size(), "10 packets");
        StringBuilder order = new StringBuilder();
        for (long[] e : l) for (int id = 0; id < 10; id++) if (e[1] == pat64(20, 0x100 + 2 * id)) order.append("GA".charAt(id / 5));
        T.eqo("GGGGGAAAAA", order.toString(), "issue (and return) order by priority");
        for (int j = 0; j < 5; j++) T.eq(pat(20, 0x100 + 2 * (10 + j)), n.c.tiles[20].cmem.dmem.peek(0x100 + j), "read-port result " + j + " in data memory");
        T.eq(0, n.c.tiles[20].dir.remaining[TrafficDirector.REPLY], "read-port results counted in REPLY_REMAINING");
        T.check(p.srq.firstIssueT[SRAMReadQueue.READPORT] > p.srq.lastIssueT[SRAMReadQueue.AG] && p.srq.firstIssueT[SRAMReadQueue.AG] > p.srq.lastIssueT[SRAMReadQueue.GATHER], "issue order gather, AG, read port (first read-port issue " + p.srq.firstIssueT[SRAMReadQueue.READPORT] + " after the last AG issue " + p.srq.lastIssueT[SRAMReadQueue.AG] + ")");
        // the AG waits while a higher-priority client has requests: 50 gather values back to back starve it
        Net m = net(); fill(m.c.tiles[22], 0x200, 512);
        for (int j = 0; j < 50; j++) m.rd(22, SRAMReadQueue.GATHER, 2 + j, 0x200 + 2 * j, 2, LinkWord.M_SRC, 23, 0, 0);
        m.rd(22, SRAMReadQueue.AG, 2, 0x300, 2, LinkWord.M_SRC, 23, 0, 0);
        m.runAll(500);
        List<long[]> l2 = m.log(23);
        T.eq(pat64(22, 0x300), l2.get(l2.size() - 1)[1], "the AG's request goes last, after 50 gather values");
        out("priority: with requests from all three clients waiting, the SRAM read queue issues gather value, address generator, read port in that order; 50 back-to-back gather values hold the address generator's request back 50 cycles [meas: MemTest priority]");
    }

    static void packets() {
        T.group("packets");
        Net n = net(); Tile p = n.c.tiles[30];
        fill(p, 0x1000, 64);
        n.rd(30, SRAMReadQueue.AG, 2, 0x1000, 3, LinkWord.M_SRC, 31, 0, 0);           // 128 bits: two packets
        n.rd(30, SRAMReadQueue.AG, 2, 0x1007, 1, LinkWord.M_SRC, 31, 0, 0);           // 32 bits
        n.rd(30, SRAMReadQueue.AG, 2, 0x1010, 3, LinkWord.M_CMEM, 31, 0x900, 0);      // 128-bit mode 5: addr then addr + 2
        n.rd(30, SRAMReadQueue.READPORT, 2, 0x1011, 1, LinkWord.M_REPLY, 30, 0x8F0, 0); // read-port read: a mode-7 packet to tile 30 itself, landing in its data memory
        n.c.tiles[31].dir.remaining[TrafficDirector.CM] = 2; p.dir.remaining[TrafficDirector.REPLY] = 1;
        n.runAll(500);
        List<long[]> l = n.log(31);
        T.eq(5, l.size(), "five packets");
        T.eq(pat(30, 0x1011), p.cmem.dmem.peek(0xF0), "read-port result: the chunk, at its data-memory word");
        T.eq(pat64(30, 0x1000), l.get(0)[1], "128-bit: low half first"); T.eq(pat64(30, 0x1002), l.get(1)[1], "then the high half");
        T.eq(1, l.get(1)[0] - l.get(0)[0], "on consecutive cycles");
        T.eq(pat(30, 0x1007) & 0xFFFFFFFFL, l.get(2)[1], "32 bits in the low half, high half 0");
        T.eq(0x900, LinkWord.addr(l.get(3)[2]), "mode 5 128-bit: first packet at addr"); T.eq(0x902, LinkWord.addr(l.get(4)[2]), "second at addr + 2");
        T.eq(pat64(30, 0x1010), n.c.tiles[31].cmem.dmem.peek64(0x100) , "mode 5 landed");
        T.eq(1, p.srq.issued[SRAMReadQueue.READPORT], "one read-port read"); T.eq(0, p.sram.reads[0] - 4, "four SRAM accesses");
        // a 128-bit read not on a row boundary and a 64-bit read across rows: two accesses each, data in order
        Net m = net(); Tile q = m.c.tiles[40]; fill(q, 0x2000, 16);
        m.rd(40, SRAMReadQueue.AG, 2, 0x2002, 3, LinkWord.M_SRC, 41, 0, 0); m.rd(40, SRAMReadQueue.AG, 2, 0x2007, 2, LinkWord.M_SRC, 41, 0, 0);
        m.runAll(500);
        T.eq(pat64(40, 0x2002), m.log(41).get(0)[1], "unaligned 128 bits, low"); T.eq(pat64(40, 0x2004), m.log(41).get(1)[1], "unaligned 128 bits, high");
        T.eq(pat64(40, 0x2007), m.log(41).get(2)[1], "64 bits across rows");
        T.eq(4, q.sram.reads[0], "each spanning read costs two accesses");
        T.eq(0, m.c.violations, "no violations");
    }

    static void returnOrder() {
        T.group("return order");
        Net n = net(); Tile p = n.c.tiles[44];
        fill(p, 0, 0x100000);
        // area 5 congested by back-to-back reads from the responder; the AG alternates area 5 and area 6
        for (int j = 0; j < 40; j++) n.rd(44, SRAMReadQueue.AG, 2 + j, j % 2 == 0 ? 0xA0000 + 2 * j : 0xC0000 + 2 * j, 2, LinkWord.M_SRC, 45, 0, 0);
        n.runAll(2000);
        List<long[]> l = n.log(45);
        boolean ok = l.size() == 40;
        for (int j = 0; ok && j < 40; j++) if (l.get(j)[1] != pat64(44, j % 2 == 0 ? 0xA0000 + 2 * j : 0xC0000 + 2 * j)) ok = false;
        T.check(ok, "returns leave in request order across areas");
        // with a busy area: area 5 also gets bursts of 8 writes from tile 43 (one a cycle, every 32 cycles) while the AG
        // reads areas 5 and 6 alternately at one a cycle: area 5's queue builds during a burst, area 6's returns wait
        Net m = net(); Tile q = m.c.tiles[44]; fill(q, 0, 0x100000); q.setPtr(43, 0xB0000);
        for (int j = 0; j < 64; j++) m.at(43, 3 + (j / 8) * 32L + j % 8, 44, LinkWord.M_SRC, 0, j);   // bursts of 8 writes, every 32 cycles
        for (int j = 0; j < 200; j++) m.rd(44, SRAMReadQueue.AG, 2 + j, j % 2 == 0 ? 0xA0000 + 2 * j : 0xC0000 + 2 * j, 2, LinkWord.M_SRC, 45, 0, 0);
        m.runAll(5000);
        List<long[]> l2 = m.log(45);
        boolean ok2 = l2.size() == 200;
        for (int j = 0; ok2 && j < 200; j++) if (l2.get(j)[1] != pat64(44, j % 2 == 0 ? 0xA0000 + 2 * j : 0xC0000 + 2 * j)) ok2 = false;
        T.check(ok2, "with area 5 congested: still in request order");
        T.check(q.srq.latMax[SRAMReadQueue.AG] > 2 + m.c.cfg.sramReadLatency, "the bursts did delay reads (" + q.srq.latMax[SRAMReadQueue.AG] + ")");
        T.eq(0, m.c.violations, "no violations " + m.c.violationReport());
        out(String.format("return order: 200 reads alternating a busy area (also taking bursts of 8 writes) and a free one leave in request order; read latency max %d (unloaded %d), write stalls %d [meas: MemTest return order]",
                q.srq.latMax[SRAMReadQueue.AG], 2 + m.c.cfg.sramReadLatency, q.writer.stallCycles));
    }

    static void tokenWindow() {
        T.group("token window");
        Config c = cfg(); c.readTokens = 64;
        Net n = new Net(c); Tile p = n.c.tiles[12]; fill(p, 0, 0x40000);
        // the first read waits behind a full area queue that the SRAM write queue keeps busy? simpler: a slow head --
        // hold area 0 with many 128-bit reads, so returns (2 packets each) are slower than issue
        for (int j = 0; j < 300; j++) n.rd(12, SRAMReadQueue.AG, 2 + j, 4 * j, 3, LinkWord.M_SRC, 13, 0, 0);
        int maxIn = 0;
        for (int i = 0; i < 3000 && (n.pending() || !n.c.quiescent()); i++) { n.step(); maxIn = Math.max(maxIn, p.srq.inFlight()); }
        T.check(maxIn <= 64, "never more than 64 requests in flight (" + maxIn + ")");
        T.check(p.srq.windowFullCycles > 0, "the window fills when returns (2 packets per 128-bit read) are slower than issue");
        T.eq(600, n.log(13).size(), "every packet");
        out(String.format("token window: 128-bit reads issued at one a cycle return at one per 2 cycles; in flight peaks at %d of 64, then issue waits (%d cycles) [meas: MemTest token window]", maxIn, p.srq.windowFullCycles));
    }

    static void disjoint() {
        T.group("disjoint-buffer check");
        Net n = net(); Tile p = n.c.tiles[50]; fill(p, 0, 0x2000);
        // good phase: read buffer A (0x0..), receive writes into buffer B (0x40000..)
        p.setPtr(49, 0x40000);
        for (int j = 0; j < 100; j++) { n.rd(50, SRAMReadQueue.AG, 2 + j, 2 * j, 2, LinkWord.M_SRC, 51, 0, 0); n.at(49, 2 + j, 50, LinkWord.M_SRC, 0, j); }
        n.runAll(2000);
        T.eq(0, n.c.violations, "ping-pong phase: no violation");
        // next phase: read what was written last phase (allowed), and a bad program: writes land in the buffer being read
        n.c.newPhase();
        p.setPtr(49, 0x60000);
        for (int j = 0; j < 100; j++) { n.rd(50, SRAMReadQueue.AG, n.c.cycle + 2 + j, 0x40000 + 2 * j, 2, LinkWord.M_SRC, 51, 0, 0); }
        for (int j = 0; j < 100; j++) n.at(49, n.c.cycle + 2 + j, 50, LinkWord.M_SRC, 0, j);
        n.runAll(2000);
        T.eq(0, n.c.violations, "reading last phase's output, writing a disjoint buffer: no violation");
        n.c.newPhase();
        p.setPtr(49, 0x40000);                            // overwrite the buffer while it is read (area demand 1.0)
        for (int j = 0; j < 100; j++) n.rd(50, SRAMReadQueue.AG, n.c.cycle + 2 + 2L * j, 0x40000 + 2 * j, 2, LinkWord.M_SRC, 51, 0, 0);
        for (int j = 0; j < 100; j++) n.at(49, n.c.cycle + 31 + 2L * j, 50, LinkWord.M_SRC, 0, j);
        n.runAll(2000);
        T.eq(1, n.c.violations, "bad program: one violation reported for the tile and phase");
        T.check(n.c.violationMessages.get(0).contains("invariant 2"), "it is invariant 2: " + n.c.violationMessages.get(0));
        T.check(p.sram.disjointViolations > 1, "every offending chunk access is counted (" + p.sram.disjointViolations + ")");
        out("disjoint-buffer check: a phase that reads one buffer while another tile's writes land in it is reported once per tile and phase, e.g. \"" + n.c.violationMessages.get(0).replaceAll("^cycle \\d+ ", "") + "\" [meas: MemTest disjoint-buffer check]");
    }

    static void memIdle() {
        T.group("mem_idle");
        Net n = net(); Tile p = n.c.tiles[60]; p.setPtr(59, 0x20000);
        T.check(p.memIdle(n.c.cycle), "idle at start");
        for (int j = 0; j < 20; j++) n.at(59, 2 + j, 60, LinkWord.M_SRC, 0, 0x1000 + j);
        boolean sawBusy = false; long lastBusy = -1, lastWrite = -1;
        for (int i = 0; i < 300; i++) {
            n.step();
            if (!p.memIdle(n.c.cycle)) { sawBusy = true; lastBusy = n.c.cycle; }
        }
        T.check(sawBusy, "busy while writes are in the path");
        T.check(p.memIdle(n.c.cycle), "idle once every write is in the array");
        for (int j = 0; j < 20; j++) T.eq(0x1000 + j, p.sram.peek64(0x20000 + 2 * j), "write " + j + " in memory when mem_idle");
        // mem_idle with only reads queued
        fill(p, 0, 64);
        for (int j = 0; j < 30; j++) n.rd(60, SRAMReadQueue.AG, n.c.cycle + 2 + j, 2 * j, 2, LinkWord.M_SRC, 61, 0, 0);
        boolean idleThroughout = true;
        for (int i = 0; i < 100; i++) { n.step(); if (!p.memIdle(n.c.cycle)) idleThroughout = false; }
        T.check(idleThroughout, "queued reads do not hold mem_idle low (DEV)");
    }

    /** Over-subscribed area: the SRAM read queue reading one area at rate r while writes arrive for the same area. */
    static void oversubscription() {
        T.group("oversubscription");
        StringBuilder sb = new StringBuilder("one area shared by the SRAM read queue (reads every Pr cycles) and incoming writes from a neighbour (every Pw cycles), 400 of each; depth 8, read latency 2 [meas: MemTest oversubscription]:\n");
        sb.append(String.format("  %-4s %-4s %-7s %-14s %-14s %-14s %-12s %s%n", "Pr", "Pw", "demand", "read lat max", "read waits", "write stalls", "we-in peak", "outcome"));
        int[][] cases = { { 2, 2 }, { 1, 4 }, { 2, 1 }, { 1, 2 }, { 1, 1 } };
        for (int[] cs : cases) {
            Net n = net(); Tile p = n.c.tiles[36]; fill(p, 0x60000, 0x1000); p.setPtr(35, 0x70000);
            for (int j = 0; j < 400; j++) n.rd(36, SRAMReadQueue.AG, 2 + (long) j * cs[0], 0x60000 + 2 * j, 2, LinkWord.M_SRC, 37, 0, 0);
            for (int j = 0; j < 400; j++) n.at(35, 2 + (long) j * cs[1], 36, LinkWord.M_SRC, 0, j);
            n.runAll(20000);
            int ej = 0; for (WordFIFO f : p.router.q[Router.EJECT]) ej = Math.max(ej, f.highWater);
            boolean ovf = n.c.violations > 0;
            double demand = 1.0 / cs[0] + 1.0 / cs[1];
            long oc = p.weIn.overflowCycle;
            String outcome = ovf ? "we-in overflows " + (oc - 2) + " cycles after the queues start" : "all " + p.writer.taken + " writes landed";
            long last = n.log(37).isEmpty() ? 0 : n.log(37).get(n.log(37).size() - 1)[0];
            sb.append(String.format("  %-4d %-4d %-7.2f %-14d %-14d %-14d %-12s %s; last read packet at cycle %d (last read issued on schedule at %d)%n", cs[0], cs[1], demand, p.srq.latMax[SRAMReadQueue.AG], p.sram.readStallCycles, p.writer.stallCycles, p.weIn.highWater + " of 4", outcome, last, 2 + 399L * cs[0]));
            T.check(!ovf, "demand " + demand + ": no overflow: the SRAM read queue waits instead (writes first)");
            T.eq(400, p.writer.taken, "demand " + demand + ": every write lands");
            if (cs[0] == 1 && cs[1] == 1) out("  memReport for the full-rate case:\n" + n.c.memReport().stripTrailing());
        }
        out(sb.toString().stripTrailing());
    }
}
