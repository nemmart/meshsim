import java.util.*;

/**
 * The address generator without stream working registers (PROPOSAL-slot-iteration-counter.md) and the
 * PTR/HIST port rule of figure 12 (spec 4.10).
 *   worked example  the proposal's section 4: three 64-bit streams, counts 2, 0, 3 (R = 3): 9 STREAM reads, 5 element reads
 *                   in rotation order, STREAM unchanged, the run repeatable
 *   computed fields every read size and mode 5: element e at source + (e << z), the controller-memory word + 2 per packet
 *   queue bounds    the read-ahead queues never hold more than ag_lookahead entries (reads in flight included)
 *   group size      a group's streams share one read size: a stream of another size is reported (ruling Q2)
 *   figure 12       the increment's write-back on t + 1 on port B whatever ts_engine_latency; a controller write that
 *                   meets it is lost, a controller load returns the engine's value; outside that cycle both work
 * Usage: java AgTest [shuffle seed]
 */
public final class AgTest {
    static long shuffle = -1;

    public static void main(String[] a) {
        if (a.length > 0) shuffle = Long.parseLong(a[0]);
        workedExample(); computedFields(); queueBounds(); groupSize();
        for (int L : new int[] { 1, 3 }) { figure12Ptr(L); figure12Hist(L); }
        rulings();
        System.exit(T.done("AgTest" + (shuffle >= 0 ? " (shuffled, seed " + shuffle + ")" : "")));
    }

    /** v1.0 spec rulings: a count-0 run ends at once with no requests; a run past its table's end wraps and is
     *  flagged; digit selectors 5-7 are treated as 0 and flagged; a start while running is ignored and flagged; a packet
     *  sent to dst 65 is flagged; the lead-in is 4 L + 2 (group) or 2 L + 1 (stream) cycles after the start takes effect. */
    static void rulings() {
        T.group("v1.0 rulings");
        Chip c = new Chip(cfg()); c.cfg.checkUnexpected = false;
        Tile p = c.tiles[3];
        // count 0
        p.setMasterConfig(Fields.masterConfig(Fields.RUN_STREAMS, 0, 0, 0, 0, 1)); p.pulse(Fields.PULSE_START); c.run(3);
        T.check(p.ag.done && !p.ag.running && p.ag.requests == 0, "a stream run of count 0 ends at once, with no requests");
        p.setMasterConfig(Fields.masterConfig(Fields.RUN_GROUPS, 0, 0, 0, 0, 1)); p.pulse(Fields.PULSE_START); c.run(3);
        T.check(p.ag.done && !p.ag.running && p.ag.requests == 0, "a group run of count 0 ends at once, with no requests");
        T.eq(0, c.violations, "count 0 is not flagged");
        // past the table's end: wraps, flagged
        p.setPtr(3, 0x40000);
        p.setStream(255, Fields.streamLo(0x100, 0), Fields.streamHi(1, 0, LinkWord.M_SRC, Fields.SIZE_64, 0));
        p.setStream(0, Fields.streamLo(0x200, 0), Fields.streamHi(1, 0, LinkWord.M_SRC, Fields.SIZE_64, 0));
        p.sram.poke64(0x100, 0x1111); p.sram.poke64(0x200, 0x2222);
        p.dir.remaining[0] = 2; c.newPhase();
        p.setMasterConfig(Fields.masterConfig(Fields.RUN_STREAMS, 255, 2, 0, 0, 1)); p.pulse(Fields.PULSE_START);
        c.runUntil(() -> p.ag.done && p.dir.remaining[0] == 0 && p.memIdle(c.cycle), 2000);
        T.eq(0x1111L, p.sram.peek64(0x40000), "a stream run 255 + 2: stream 255 first ...");
        T.eq(0x2222L, p.sram.peek64(0x40002), "... then stream 0 (the numbers wrap)");
        T.check(c.violationMessages.stream().anyMatch(v -> v.contains("past the table's end")), "and meshsim flags it");
        long v0 = c.violations;
        // digit selectors 5-7
        p.setMasterConfig(Fields.masterConfig(Fields.RUN_STREAMS, 0, 1, 5, 7, 1)); c.run(2);
        T.check(p.writer.bucketDigit == 0 && p.writer.histDigit == 0, "digit selectors 5 and 7 are treated as 0");
        T.check(c.violations > v0 && c.violationMessages.stream().anyMatch(v -> v.contains("reserved digit selector")), "and meshsim flags them");
        // a start while running
        v0 = c.violations;
        p.setStream(10, Fields.streamLo(0x100, 0), Fields.streamHi(200, 0, LinkWord.M_SRC, Fields.SIZE_64, 0));
        p.setPtr(3, 0x60000); p.dir.remaining[0] = 200; c.newPhase();
        p.setMasterConfig(Fields.masterConfig(Fields.RUN_STREAMS, 10, 1, 0, 0, 1)); p.pulse(Fields.PULSE_START); c.run(2);
        long started = p.ag.startedAt;
        p.pulse(Fields.PULSE_START); c.run(2);
        T.eq(started, p.ag.startedAt, "a start while running is ignored (the lead-in included)");
        T.check(c.violations > v0 && c.violationMessages.stream().anyMatch(v -> v.contains("a start while the address generator is running")), "and meshsim flags it");
        c.runUntil(() -> p.ag.done && p.dir.remaining[0] == 0 && p.memIdle(c.cycle), 5000);
        // rd_running is visible to a load right after the start store (wait_tables() after start_run() waits for the run)
        p.setStream(11, Fields.streamLo(0x100, 0), Fields.streamHi(50, 0, LinkWord.M_SRC, Fields.SIZE_64, 0));
        p.setPtr(3, 0x60000); p.dir.remaining[0] = 50; c.newPhase();
        p.setMasterConfig(Fields.masterConfig(Fields.RUN_STREAMS, 11, 1, 0, 0, 1));
        long ts = c.cycle + 1;
        p.core.store(ts, Registers.DMA_PULSE.addr(), Registers.P_START.place(1), -1); c.step();
        T.check(!p.ag.running && p.ag.runningForStatus(), "at the end of the start store's cycle the run has not begun, but TILE_STATUS's rd_running (computed then, read at + 1) is 1");
        T.check(!p.ag.doneForStatus(), "... and rd_done is 0");
        c.runUntil(() -> p.ag.done && p.dir.remaining[0] == 0 && p.memIdle(c.cycle), 5000);
        // dst 65
        v0 = c.violations;
        long t = c.cycle + 1;
        p.core.store(t, Registers.ROUTER_SET_HDR.addr(), Registers.H_DST.place(LinkWord.MEMSUB) | Registers.H_MODE.place(LinkWord.M_REPLY), -1); c.step();
        p.core.store(c.cycle + 1, Registers.ROUTER_SEND_LO.addr(), 1, -1); c.run(20);
        T.check(c.violations > v0 && c.violationMessages.stream().anyMatch(v -> v.contains("dst 65")), "a packet sent to dst 65 is flagged");
    }

    static Config cfg() { Config c = Net.cfg(shuffle); return c; }

    // ================================================================ the proposal's worked example (section 4)
    static void workedExample() {
        T.group("worked example");
        Net n = new Net(cfg()); Tile p = n.c.tiles[12];
        int[] count = { 2, 0, 3 }, src = { 0x100, 0x200, 0x400 };
        for (int s = 0; s < 3; s++) {
            p.setStream(40 + s, Fields.streamLo(src[s], 0), Fields.streamHi(count[s], 0, LinkWord.M_SRC, Fields.SIZE_64, 0));
            for (int e = 0; e < 4; e++) p.sram.poke64(src[s] + 2 * e, (long) (s + 0xA) << 8 | e);
        }
        p.setGroup(7, Fields.groupLo(3), Fields.groupHi(40, 3, 0));
        long[] before = new long[3]; for (int s = 0; s < 3; s++) before[s] = p.ts.stream.peek(2 * (40 + s)) & 0xFFFFFFFFL | (long) p.ts.stream.peek(2 * (40 + s) + 1) << 32;
        for (int rep = 0; rep < 2; rep++) {
            n.clearLogs();
            p.setPtr(12, 0x40000); p.dir.remaining[0] = 5;
            p.setMasterConfig(Fields.masterConfig(Fields.RUN_GROUPS, 7, 1, 0, 0, 1));
            EngineTest.runLocal(n, 12);
            List<long[]> l = n.log(12);
            T.eq(5, l.size(), "five packets (run " + rep + ")");
            long[] want = { 0xA00, 0xC00, 0xA01, 0xC01, 0xC02 };   // rotation 0: A0, C0; 1: A1, C1; 2: C2
            for (int j = 0; j < 5; j++) T.eq(want[j], p.sram.peek64(0x40000 + 2 * j), "element " + j + " in rotation order (run " + rep + ")");
            long[] rel = new long[l.size()]; for (int i = 0; i < l.size(); i++) rel[i] = l.get(i)[0] - l.get(0)[0];
            T.eqo("[0, 2, 3, 5, 8]", Arrays.toString(rel), "issue cycles: period 3, idle visits keep their time (run " + rep + ")");
            T.eq(9, p.ag.streamReads, "nine STREAM reads (three per rotation)");
            T.eq(1, p.ag.groupReads, "one GROUP read");
            T.eq(5, p.ag.requests, "five element reads");
        }
        for (int s = 0; s < 3; s++) {
            long now = p.ts.stream.peek(2 * (40 + s)) & 0xFFFFFFFFL | (long) p.ts.stream.peek(2 * (40 + s) + 1) << 32;
            T.eq(before[s], now, "STREAM[" + (40 + s) + "] as the controller wrote it");
        }
        EngineTest.noViolations(n.c, "worked example");
    }

    // ================================================================ element addresses and mode-5 words, computed
    static void computedFields() {
        T.group("computed fields");
        int[] sizes = { Fields.SIZE_32, Fields.SIZE_64, Fields.SIZE_128 };
        for (int kind : new int[] { Fields.RUN_STREAMS, Fields.RUN_GROUPS }) for (int size : sizes) {
            Net n = new Net(cfg()); Tile p = n.c.tiles[3];
            int chunks = SRAMReadQueue.chunksOf(size), N = 5, base = 0x800;
            for (int i = 0; i < N * chunks; i++) p.sram.poke(base + i, 0x5000 + i);
            p.setStream(9, Fields.streamLo(base, 0x900), Fields.streamHi(N, 0, LinkWord.M_CMEM, size, 0));
            p.setGroup(2, Fields.groupLo(N), Fields.groupHi(9, 1, 0));
            p.dir.remaining[1] = N * (size == Fields.SIZE_128 ? 2 : 1);
            p.setMasterConfig(Fields.masterConfig(kind, kind == Fields.RUN_STREAMS ? 9 : 2, 1, 0, 0, 1));
            EngineTest.runLocal(n, 3);
            List<long[]> l = n.log(3);
            String what = (kind == Fields.RUN_STREAMS ? "stream run" : "group run") + ", " + (8 << size) + "-bit";
            int packets = N * (size == Fields.SIZE_128 ? 2 : 1);
            T.eq(packets, l.size(), what + ": packets");
            boolean ok = true;
            for (int k = 0; k < l.size(); k++) {
                long[] e = l.get(k);
                int addr = LinkWord.addr(e[2]);
                if (addr != 0x900 + 2 * k) ok = false;                        // + 2 per packet before this one
                int chunk = size == Fields.SIZE_128 ? (k / 2) * 4 + (k % 2) * 2 : k * chunks;
                long lo = 0x5000 + chunk, payload = size == Fields.SIZE_32 ? lo : lo | (lo + 1) << 32;
                if (e[1] != payload) ok = false;
            }
            T.check(ok, what + ": element e at source + (e << z), controller-memory word + 2 per packet");
        }
    }

    // ================================================================ the queues hold at most ag_lookahead entries
    static void queueBounds() {
        T.group("queue bounds");
        for (int L = 1; L <= 4; L++) for (int la : new int[] { 1, 2, 2 * L + 2, 9 }) {
            Config k = cfg(); k.tsEngineLatency = L; k.agLookahead = la;
            Net n = new Net(k); Tile p = n.c.tiles[0];
            // groups t = 1 .. 16, R = 1 .. 3, with R = 0 groups between
            int stream = 0, g = 0, total = 0;
            for (int t = 1; t <= 16; t += 3) for (int R = 0; R <= 3; R++) {
                for (int s = 0; s < t; s++) p.setStream(stream + s, Fields.streamLo(0x100 * g + 2 * s, 0), Fields.streamHi(R, 0, LinkWord.M_SRC, Fields.SIZE_64, 0));
                p.setGroup(g, Fields.groupLo(R), Fields.groupHi(stream, t, 0));
                stream += t; g++; total += R * t;
            }
            p.setPtr(0, 0x40000); p.dir.remaining[0] = total;
            p.setMasterConfig(Fields.masterConfig(Fields.RUN_GROUPS, 0, g, 0, 0, 1));
            EngineTest.runLocal(n, 0);
            T.check(p.ag.streamQPeak <= la && p.ag.groupQPeak <= la, "L " + L + ", ag_lookahead " + la + ": peaks " + p.ag.streamQPeak + ", " + p.ag.groupQPeak);
            T.eq(total, p.ag.requests, "L " + L + ", ag_lookahead " + la + ": every element sent");
            EngineTest.noViolations(n.c, "queue bounds L " + L + " la " + la);
        }
    }

    // ================================================================ a group's streams share one read size (ruling Q2)
    static void groupSize() {
        T.group("group read size");
        for (int bad = 0; bad < 3; bad++) {                 // the odd stream: none, a counted one, an idle one (count 0)
            Net n = new Net(cfg()); Tile p = n.c.tiles[5];
            int el = 0;
            for (int s = 0; s < 3; s++) {
                int size = (bad == 1 && s == 1) || (bad == 2 && s == 2) ? Fields.SIZE_32 : Fields.SIZE_64;
                int cnt = bad == 2 && s == 2 ? 0 : 2;
                p.setStream(60 + s, Fields.streamLo(0x200 + 8 * s, 0), Fields.streamHi(cnt, 0, LinkWord.M_SRC, size, 0));
                el += cnt;
            }
            p.setGroup(1, Fields.groupLo(2), Fields.groupHi(60, 3, 0));
            p.setPtr(5, 0x40000); p.dir.remaining[0] = el;
            p.setMasterConfig(Fields.masterConfig(Fields.RUN_GROUPS, 1, 1, 0, 0, 1));
            EngineTest.runLocal(n, 5);
            String what = bad == 0 ? "streams of one size" : bad == 1 ? "a counted stream of another size" : "an idle stream (count 0) of another size";
            T.eq(bad == 0 ? 0 : 2, p.ag.groupSizeViolations, what + ": reported on each of its visits (R = 2)");
            T.check((n.c.violations > 0) == (bad != 0), what + ": " + (bad == 0 ? "no violation" : "a violation: " + p.ag.lastViolation));
        }
    }

    // ================================================================ figure 12: PTR and HIST ports
    /** Put an RMW request on the engine's line now; return the cycle the memory takes it (port A's read). */
    static long rmw(Chip c, WordDelayLine line, long op, int idx, int add, java.util.function.LongSupplier takes) {
        long before = takes.getAsLong();
        line.put(c.cycle + 1, op, idx, add);
        for (int i = 0; i < 50; i++) { c.step(); if (takes.getAsLong() > before) return c.cycle; }
        throw new IllegalStateException("RMW never taken");
    }
    static int ptrAddr(int i) { return Registers.PTR_BASE + 4 * i; }
    static int histAddr(int i) { return Registers.HIST_BASE + 4 * i; }

    static void figure12Ptr(int L) {
        T.group("figure 12, PTR, ts_engine_latency " + L);
        Config k = cfg(); k.tsEngineLatency = L;
        Chip c = new Chip(k); Tile p = c.tiles[7];
        p.setPtr(9, 0x1000); p.setPtr(5, 0x2000);
        // 1. a controller store on the write-back cycle (t + 1) is lost; the increment lands
        long tr = rmw(c, p.weTsReq, TableStore.OP_ADD_RET, TableStore.PTR + 9, 2, () -> p.ts.rmws);
        long v0 = c.violations;
        p.core.store(tr + 1, ptrAddr(5), 0x7777, -1);
        c.step(); c.step();
        T.eq(0x1002, p.ptr(9), "the increment's write-back lands (t + 1)");
        T.eq(0x2000, p.ptr(5), "the controller's store on the write-back cycle is lost");
        T.eq(1, p.ts.pipe.ctrlWritesLost, "one controller write lost");
        T.check(c.violations > v0 || p.ctl.violations > 0, "the window check still reports the access");
        // 2. PTR is write-only (v1.0, V1.0-CHANGES item 7): a controller load from it is a bus error
        tr = rmw(c, p.weTsReq, TableStore.OP_ADD_RET, TableStore.PTR + 9, 2, () -> p.ts.rmws);
        T.check(p.core.load(tr + 1, ptrAddr(5), 4) == null, "a controller load from PTR is a bus error (write-only)");
        // 3. the read cycle (t) and the cycle after the write-back (t + 2) are free on port B
        c.run(4);
        tr = rmw(c, p.weTsReq, TableStore.OP_ADD_RET, TableStore.PTR + 9, 2, () -> p.ts.rmws);
        p.core.store(tr, ptrAddr(5), 0x3000, -1);
        c.step(); c.step();
        p.core.store(c.cycle + 1, ptrAddr(5), 0x3004, -1);        // well after
        c.step();
        T.eq(0x3004, p.ptr(5), "stores away from the write-back cycle land");
        T.eq(0x1006, p.ptr(9), "PTR[9] = + 2 three times");
        // 4. a controller store to the same entry on the read cycle: the increment's write-back wins
        tr = rmw(c, p.weTsReq, TableStore.OP_ADD_RET, TableStore.PTR + 9, 2, () -> p.ts.rmws);
        p.core.store(tr, ptrAddr(9), 0x5000, -1);
        c.step(); c.step();
        T.eq(0x1008, p.ptr(9), "a store to the entry being incremented is overwritten by the write-back");
        // 5. the write-back is t + 1 whatever ts_engine_latency: the store on t + 2 is not lost
        tr = rmw(c, p.weTsReq, TableStore.OP_ADD_RET, TableStore.PTR + 9, 2, () -> p.ts.rmws);
        long lost = p.ts.pipe.ctrlWritesLost;
        c.step();
        p.core.store(tr + 2, ptrAddr(5), 0x6000, -1);
        c.step(); c.step();
        T.eq(lost, p.ts.pipe.ctrlWritesLost, "t + 2 is past the write-back (ts_engine_latency " + L + ")");
        T.eq(0x6000, p.ptr(5), "and the store lands");
        // back-to-back increments of one entry: no update lost
        long t0 = c.cycle;
        for (int i = 0; i < 6; i++) { p.weTsReq.put(c.cycle + 1, TableStore.OP_ADD_RET, TableStore.PTR + 9, 2); c.step(); }
        c.run(20 + 2 * L);
        T.eq(0x100A + 12, p.ptr(9), "six back-to-back increments of one entry");
    }

    static void figure12Hist(int L) {
        T.group("figure 12, HIST, ts_engine_latency " + L);
        Config k = cfg(); k.tsEngineLatency = L;
        Chip c = new Chip(k); Tile p = c.tiles[20];
        p.hist.poke(3, 100); p.hist.poke(4, 7);
        long tr = rmw(c, p.weHistReq, 0, 3, 1, () -> p.hist.rmws);
        p.core.store(tr + 1, histAddr(4), 55, -1);
        c.step(); c.step();
        T.eq(101, p.hist.peek(3), "the count-on-arrival increment lands");
        T.eq(7, p.hist.peek(4), "the controller's store on the write-back cycle is lost");
        T.eq(1, p.hist.pipe.ctrlWritesLost, "one controller write lost");
        tr = rmw(c, p.weHistReq, 0, 3, 1, () -> p.hist.rmws);
        Integer got = p.core.load(tr + 1, histAddr(4), 4);
        T.eq(102, got, "a controller load on the write-back cycle returns the engine's value (HIST[3] + 1)");
        c.run(3);
        T.eq(7, p.core.load(c.cycle + 1, histAddr(4), 4), "a load away from the write-back reads the entry");
        c.step();
        p.core.store(c.cycle + 1, histAddr(4), 55, -1); c.step();
        T.eq(55, p.hist.peek(4), "a store away from the write-back lands");
    }
}
