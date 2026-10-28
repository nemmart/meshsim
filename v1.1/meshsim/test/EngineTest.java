import java.nio.file.Path;
import java.util.*;

/**
 * M3 tests: the address generator (spec 4.2), the write engine (4.9), the table and HIST stores (4.10), augmenting and
 * count-on-arrival (4.9, 4.16), and the 16-group all-to-all (schedule-4.md). Args: [shuffleSeed] [quick|full].
 * Tables, PTR and DMA_CONFIG are set from Java (the controller is M4).
 */
public final class EngineTest {
    static long shuffle = -1;
    static boolean full;
    static final StringBuilder report = new StringBuilder();
    static void out(String s) { report.append(s).append('\n'); }

    public static void main(String[] a) throws Exception {
        if (a.length > 0) shuffle = Long.parseLong(a[0]);
        full = a.length > 1 && a[1].equals("full");
        writeEngine(); localCopy(); streamRuns(); oneWordExchange(); radixSort(); lateGroups(); allToAll(); drift(); counterOverflow();
        System.out.print(report);
        System.exit(T.done("EngineTest" + (shuffle >= 0 ? " (shuffled, seed " + shuffle + ")" : "") + (full ? " full" : "")));
    }

    static Config cfg(boolean skew) { Config c = Config.defaults(); c.shuffleSeed = shuffle; c.skew = skew; return c; }
    static void noViolations(Chip c, String what) { c.scanOverflows(); T.check(c.violations == 0, what + ": no violations " + c.violationReport()); }

    /** Tile p has finished a phase: its run is over, every expected arrival landed, the stores are quiet. */
    static boolean phaseDone(Chip c, Tile p) {
        long t = c.cycle;
        return !p.ag.running && p.dir.remaining[0] == 0 && p.dir.remaining[1] == 0 && p.dir.remaining[2] == 0
                && p.memIdle(t) && p.dir.cmIdle(t) && !p.tsBusy(t);
    }
    /** Start a local run on the given tiles (+ start skew), run to the end; cycles from the store. */
    static long runLocal(Net n, int... tiles) {
        Chip c = n.c;
        long t0 = c.cycle;
        for (int i : tiles) c.tiles[i].pulse(Fields.PULSE_START);
        boolean ok = c.runUntil(() -> { for (int i : tiles) if (!phaseDone(c, c.tiles[i])) return false; return true; }, 2_000_000);
        T.check(ok, "local run finished");
        return c.cycle - t0;
    }
    static void streamRun(Tile p, int start, int count, int bucketDigit, int histDigit) {
        p.setMasterConfig(Fields.masterConfig(Fields.RUN_STREAMS, start, count, bucketDigit, histDigit, 1));
    }

    // ================================================================ the write engine
    static void writeEngine() {
        T.group("write engine");
        Net n = new Net(cfg(false));
        Tile r = n.c.tiles[10];
        // mode 2 at PTR[src], count-on-arrival on digit 1
        r.setPtr(9, 0x20000); r.setMasterConfig(Fields.masterConfig(0, 0, 0, 0, 1, 1)); r.dir.remaining[0] = 20;
        for (int j = 0; j < 20; j++) n.at(9, 2 + j, 10, LinkWord.M_SRC, 0, 0xABCD0000_00000000L | (j % 5));
        n.runAll(1000);
        boolean ok = true; for (int j = 0; j < 20; j++) if (r.sram.peek64(0x20000 + 2 * j) != (0xABCD0000_00000000L | (j % 5))) ok = false;
        T.check(ok, "mode 2 lands at PTR[src], += 2");
        T.eq(0x20000 + 40, r.ptr(9), "PTR[9] advanced by 2 per packet");
        for (int k = 0; k < 5; k++) T.eq(4, r.hist.peek(k), "count-on-arrival HIST[" + k + "]");
        // mode 3 at PTR[bucket digit 2]
        for (int b = 0; b < 256; b++) r.setPtr(b, 0x60000 + 64 * b);
        r.setMasterConfig(Fields.masterConfig(0, 0, 0, 2, 0, 1)); r.dir.remaining[0] = 30;
        long[] keys = new long[30]; Random rnd = new Random(3);
        for (int j = 0; j < 30; j++) { keys[j] = rnd.nextInt() & 0xFFFFFFFFL; n.at(9, n.c.cycle + 2 + j, 10, LinkWord.M_BUCKET, 0, keys[j]); }
        n.runAll(1000);
        int[] seen = new int[256]; ok = true;
        for (long k : keys) { int b = (int) ((k >>> 8) & 0xFF); if (r.sram.peek64(0x60000 + 64 * b + 2 * seen[b]++) != k) ok = false; }
        T.check(ok, "mode 3 lands at PTR[digit 2], in arrival order per bucket");
        // all on one bucket at one a cycle: the bypass keeps every pointer update
        r.setPtr(7, 0x70000); r.setMasterConfig(Fields.masterConfig(0, 0, 0, 1, 0, 1)); r.dir.remaining[0] = 50;
        for (int j = 0; j < 50; j++) n.at(9, n.c.cycle + 2 + j, 10, LinkWord.M_BUCKET, 0, (long) j << 32 | 7);
        n.runAll(1000);
        ok = true; for (int j = 0; j < 50; j++) if (r.sram.peek64(0x70000 + 2 * j) != ((long) j << 32 | 7)) ok = false;
        T.check(ok, "50 packets to one bucket, one a cycle: consecutive (pointer bypass)");
        // mode 4 (augmented; was attach on mode 2): the high half becomes (target - base) / 2
        r.setPtr(9, 0x30010); r.setRecordBase(0x30000); r.setMasterConfig(Fields.masterConfig(0, 0, 0, 0, 0, 1)); r.dir.remaining[0] = 10;
        for (int j = 0; j < 10; j++) n.at(9, n.c.cycle + 2 + j, 10, LinkWord.M_AUG, 0, 0xFFFFFFFF00000000L | (1000 + j));
        n.runAll(1000);
        ok = true; for (int j = 0; j < 10; j++) if (r.sram.peek64(0x30010 + 2 * j) != ((long) (8 + j) << 32 | (1000 + j))) ok = false;
        T.check(ok, "mode 4: ordinals 8, 9, ... from AUGMENT_BASE_ADDRESS, key kept");
        // modes 2 and 4 mixed in one phase (4.9): two senders, each interleaving mode 2 and mode 4; both modes
        // advance PTR[src] in arrival order, and only the mode-4 payloads get an ordinal
        r.setPtr(9, 0x34000); r.setPtr(11, 0x34100); r.setRecordBase(0x34000); r.setMasterConfig(Fields.masterConfig(0, 0, 0, 0, 1, 1));
        r.dir.remaining[0] = 40; long hist0 = r.hist.peek(0x5A);
        boolean[][] aug = new boolean[2][20];
        for (int s = 0; s < 2; s++) for (int j = 0; j < 20; j++) {
            aug[s][j] = ((j * 7 + s * 3) % 5) < 2;                                   // an irregular mix, different per sender
            n.at(s == 0 ? 9 : 11, n.c.cycle + 2 + j, 10, aug[s][j] ? LinkWord.M_AUG : LinkWord.M_SRC, 0, 0xEEEE000000000000L | (long) s << 40 | (long) j << 8 | 0x5A);
        }
        n.runAll(1000);
        ok = true; int nAug = 0;
        for (int s = 0; s < 2; s++) for (int j = 0; j < 20; j++) {
            int at = (s == 0 ? 0x34000 : 0x34100) + 2 * j;
            long sent = 0xEEEE000000000000L | (long) s << 40 | (long) j << 8 | 0x5A;
            long want = aug[s][j] ? ((long) ((at - 0x34000) >>> 1) << 32 | (sent & 0xFFFFFFFFL)) : sent;
            if (aug[s][j]) nAug++;
            if (r.sram.peek64(at) != want) ok = false;
        }
        T.check(ok, "modes 2 and 4 mixed in one phase: arrival order at PTR[src] kept, ordinals only on the " + nAug + " mode-4 payloads");
        T.eq(0x34000 + 40, r.ptr(9), "PTR[9] advanced by both modes"); T.eq(0x34100 + 40, r.ptr(11), "PTR[11] advanced by both modes");
        T.eq(hist0 + 40, (long) r.hist.peek(0x5A), "count-on-arrival counts both modes");
        T.eq(0, r.dir.remaining[0], "SRAM_REMAINING counts both modes");
        // a write at chunk = 3 mod 4 spans two rows: two accesses
        r.setPtr(9, 0x50003); r.setMasterConfig(0); r.dir.remaining[0] = 4; long w0 = r.sram.writes[2];
        for (int j = 0; j < 4; j++) n.at(9, n.c.cycle + 2 + j, 10, LinkWord.M_SRC, 0, 0x1111111100000000L * (j + 1) + j);
        n.runAll(1000);
        ok = true; for (int j = 0; j < 4; j++) if (r.sram.peek64(0x50003 + 2 * j) != 0x1111111100000000L * (j + 1) + j) ok = false;
        T.check(ok, "row-spanning writes land");
        T.eq(2, r.writer.splits, "two of the four span rows (chunks 3 and 7 mod 8... 0x50003, 0x50007)");
        T.eq(6, r.sram.writes[2] - w0, "each spanning write is two accesses");
        noViolations(n.c, "write engine");
        // the pointer stage against ts_engine_latency
        StringBuilder sb = new StringBuilder("write engine pointer stage, 200 packets at one a cycle [meas: EngineTest write engine]:");
        for (int L = 1; L <= 4; L++) {
            Config k = cfg(false); k.tsEngineLatency = L;
            Net m = new Net(k); Tile q = m.c.tiles[10]; q.setPtr(9, 0x20000); q.dir.remaining[0] = 200;
            for (int j = 0; j < 200; j++) m.at(9, 2 + j, 10, LinkWord.M_SRC, 0, j);
            m.runAll(5000);
            boolean good = true; for (int j = 0; j < 200; j++) if (q.sram.peek64(0x20000 + 2 * j) != j) good = false;
            T.check(good, "L = " + L + ": every write lands");
            T.check(q.writer.stageHighWater <= 2 * L + 2, "L = " + L + ": stage within 2L + 2");
            T.eq(0, q.weIn.highWater > 1 ? 1 : 0, "L = " + L + ": the input FIFO never holds more than one (one a cycle sustained)");
            noViolations(m.c, "L = " + L);
            sb.append(String.format(" L=%d: peak %d of %d;", L, q.writer.stageHighWater, 2 * L + 2));
        }
        out(sb.toString() + " (v3.7 measured 3, 5, 7, 9)");
    }

    // ================================================================ local copies
    static void localCopy() {
        T.group("local copy");
        StringBuilder sb = new StringBuilder("local copies, one stream run on one tile, area 0 -> area 1 [meas: EngineTest local copy], cycles from the arm+start store to the local-wake condition:\n");
        int N = 4096;
        int[][] cases = { { Fields.SIZE_64, N }, { Fields.SIZE_128, N / 2 }, { Fields.SIZE_32, N } };
        String[] name = { "64-bit records", "128-bit values (one packet each)", "32-bit keys (one per packet)" };
        for (int k = 0; k < 3; k++) {
            Net n = new Net(cfg(false)); Tile p = n.c.tiles[20];
            for (int i = 0; i < 2 * N; i++) p.sram.poke(i, 0x5000_0000 + i);
            int size = cases[k][0], count = cases[k][1], packets = count;   // v1.1: one packet per element, whatever its size
            p.setStream(0, Fields.streamSrc(0), Fields.streamDest(0, 0), Fields.streamCount(count, LinkWord.M_SRC, size, 0));
            p.setPtr(20, 0x20000); p.dir.remaining[0] = packets; streamRun(p, 0, 1, 0, 0);
            long cyc = runLocal(n, 20);
            boolean ok = true;
            int chunks = count * SRAMReadQueue.chunksOf(size);           // 32 bits land packed (PTR += 1), v1.0 padded them to 64
            for (int j = 0; j < chunks; j++) if (p.sram.peek(0x20000 + j) != 0x5000_0000 + j) { ok = false; break; }
            T.check(ok, name[k] + ": copied, " + chunks + " chunks");
            T.eq(0x20000 + chunks, p.ptr(20), name[k] + ": PTR[me] advanced by the chunks written");
            noViolations(n.c, name[k]);
            sb.append(String.format("  %-34s %5d packets: %5d cycles (%d over the packet count)%n", name[k], packets, cyc, cyc - packets));
        }
        // a block copy into controller memory (spec 6.5): 1K words into instruction memory, as 256 128-bit packets (v1.1),
        // 512 64-bit packets (v1.0's only form) and 1,024 32-bit packets
        for (int size : new int[] { Fields.SIZE_128, Fields.SIZE_64, Fields.SIZE_32 }) {
            Net n = new Net(cfg(false)); Tile p = n.c.tiles[21];
            for (int i = 0; i < 1024; i++) p.sram.poke(0x10000 + i, 0x7000_0000 + i);
            int pk = 1024 >> size;
            p.setStream(5, Fields.streamSrc(0x10000), Fields.streamDest(0x000, 0), Fields.streamCount(pk, LinkWord.M_CMEM, size, 0));
            p.dir.remaining[TrafficDirector.CM] = pk; streamRun(p, 5, 1, 0, 0);
            long cyc = runLocal(n, 21);
            boolean ok = true; for (int i = 0; i < 1024; i++) if (p.cmem.imem.peek(i) != 0x7000_0000 + i) ok = false;
            T.check(ok, "mode-5 block copy, " + (32 << size) + "-bit packets: 1K words in instruction memory");
            noViolations(n.c, "block copy");
            sb.append(String.format("  %-34s %5d packets: %5d cycles%s%n", "1K-word block copy (mode 5, " + (32 << size) + ")", pk, cyc,
                    size == Fields.SIZE_128 ? " (spec 4.2 estimate: about 256 plus the stream's start)" : size == Fields.SIZE_64 ? " (v1.0: 526)" : ""));
        }
        out(sb.toString());
    }

    // ================================================================ stream runs: timing, count 0, gap-after, relative destinations, wrap
    static void streamRuns() {
        T.group("stream runs");
        Net n = new Net(cfg(false)); Tile p = n.c.tiles[30];
        for (int i = 0; i < 64; i++) p.sram.poke64(2 * i, i);
        int[] counts = { 3, 0, 2, 0, 1 }, gaps = { 2, 3, 0, 1, 0 }; int src = 0;
        for (int s = 0; s < 5; s++) { p.setStream(254 + s & 255, Fields.streamSrc(2 * src), Fields.streamDest(0, 0), Fields.streamCount(counts[s], LinkWord.M_SRC, Fields.SIZE_64, gaps[s])); src += counts[s]; }
        p.setPtr(30, 0x20000); p.dir.remaining[0] = 6; streamRun(p, 254, 5, 0, 0);
        runLocal(n, 30);
        List<long[]> l = n.log(30);
        T.eq(6, l.size(), "six packets");
        long[] rel = new long[l.size()]; for (int i = 0; i < l.size(); i++) rel[i] = l.get(i)[0] - l.get(0)[0];
        T.eqo("[0, 1, 2, 8, 9, 11]", Arrays.toString(rel), "issue cycles: 3 elements + gap 2, count 0 with gap 3, 2 elements, count 0 with gap 1, 1 element (streams 254, 255, 0, 1, 2)");
        for (int j = 0; j < 6; j++) T.eq(j, p.sram.peek64(0x20000 + 2 * j), "data " + j);
        // relative destinations: d = 5 from tile 30 reaches tile 35; d = 40 reaches tile 6
        p.setStream(10, Fields.streamSrc(0), Fields.streamDest(0, 5), Fields.streamCount(2, LinkWord.M_SRC, Fields.SIZE_64, 0));
        p.setStream(11, Fields.streamSrc(4), Fields.streamDest(0, 40), Fields.streamCount(2, LinkWord.M_SRC, Fields.SIZE_64, 0));
        n.c.tiles[35].setPtr(30, 0x30000); n.c.tiles[6].setPtr(30, 0x30000);
        n.c.tiles[35].dir.remaining[0] = 2; n.c.tiles[6].dir.remaining[0] = 2;
        streamRun(p, 10, 2, 0, 0);
        runLocal(n, 30);
        n.c.runUntil(() -> phaseDone(n.c, n.c.tiles[35]) && phaseDone(n.c, n.c.tiles[6]), 1000);
        T.eq(1, n.c.tiles[35].sram.peek64(0x30002), "d = 5 lands at tile 35"); T.eq(3, n.c.tiles[6].sram.peek64(0x30002), "d = 40 lands at tile 6 (30 + 40 mod 64)");
        T.eq(1L, (long) n.c.violations, "the run past STREAM's end (254 + 5) wraps, and meshsim flags it once (spec 8.5)");
        T.check(n.c.violationMessages.stream().allMatch(v -> v.contains("past the table's end")), "and nothing else: " + n.c.violationReport());
        out("stream runs: a count-0 stream takes exactly its gap-after cycles and a stream its elements plus its gap (issue offsets 0, 1, 2, 8, 9, 11 for counts 3, 0, 2, 0, 1 and gaps 2, 3, 0, 1, 0, streams 254 to 2: the numbers wrap, flagged); relative destinations wrap mod 64 [meas: EngineTest stream runs]");
    }

    // ================================================================ one-word exchanges (63 streams of count 1, 16-group order)
    static int[] exchangeOrder() { int[] o = new int[63]; int i = 0; for (int[] g : A2A.G4) for (int k : g) if (k != 0) o[i++] = k; return o; }

    static void oneWordExchange() {
        T.group("one-word exchange");
        StringBuilder sb = new StringBuilder("one-word exchange, every tile sends one record to each of the other 63 in the 16-group offset order, stream runs, 0/+1 start skew [meas: EngineTest one-word exchange]:\n");
        int[] order = exchangeOrder();
        for (int gap : new int[] { 4, 2, 1, 0 }) {
            Net n = new Net(cfg(true));
            for (Tile p : n.c.tiles) {
                for (int s = 0; s < 63; s++) {
                    int d = (p.idx + order[s]) & 63;
                    p.sram.poke64(2 * s, (long) p.idx << 32 | d);
                    p.setStream(s, Fields.streamSrc(2 * s), Fields.streamDest(0, order[s]), Fields.streamCount(1, LinkWord.M_SRC, Fields.SIZE_64, gap));
                }
                for (int s = 0; s < 64; s++) p.setPtr(s, 0x20000 + 2 * s);
                p.dir.remaining[0] = 63; streamRun(p, 0, 63, 0, 0);
            }
            long t0 = n.c.cycle;
            boolean[] st = new boolean[64];
            for (int i = 0; i < 2000; i++) {
                for (Tile p : n.c.tiles) if (!st[p.idx] && n.c.cycle >= t0 + n.c.startSkew[p.idx]) { p.pulse(Fields.PULSE_START); st[p.idx] = true; }
                n.step();
                boolean all = true; for (Tile p : n.c.tiles) if (!phaseDone(n.c, p) || !st[p.idx]) { all = false; break; }
                if (all) break;
            }
            long cyc = n.c.cycle - t0;
            boolean ok = true;
            for (Tile p : n.c.tiles) for (int s = 0; s < 64; s++) if (s != p.idx && p.sram.peek64(0x20000 + 2 * s) != ((long) s << 32 | p.idx)) ok = false;
            T.check(ok, "gap " + gap + ": every word landed");
            n.c.scanOverflows();
            long agReads = 0; int ptrQ = 0; for (Tile p : n.c.tiles) { agReads = Math.max(agReads, p.ts.tableReads); ptrQ = Math.max(ptrQ, p.ts.qWe.highWater); }
            int link = 0, ej = 0;
            for (Tile p : n.c.tiles) { for (int o = 0; o < 4; o++) for (WordFIFO f : p.router.q[o]) link = Math.max(link, f.highWater); for (WordFIFO f : p.router.q[Router.EJECT]) ej = Math.max(ej, f.highWater); }
            noViolations(n.c, "gap " + gap + " (separate table macros: no we-in overflow)");
            sb.append(String.format("  gap after %d: %4d cycles; link FIFO peak %2d of 16, eject peak %2d of 16; STREAM reads %d, PTR request FIFO peak %d of 16; %s%n",
                    gap, cyc, link, ej, agReads, ptrQ, n.c.violations == 0 ? "no overflow" : "OVERFLOW " + n.c.violationMessages.get(0).replaceAll("^cycle \\d+ ", "")));
        }
        out(sb.toString().stripTrailing());
    }

    // ================================================================ a 4-pass local radix sort (step 1's augmenting pass, then 4 bucket passes)
    static void radixSort() {
        T.group("radix sort");
        int N = full ? 16384 : 4096;
        Net n = new Net(cfg(true));
        final int A = 0x00000, B = 0x20000, C = 0x40000;
        long[][] keys = new long[64][N];
        Random rnd = new Random(11);
        for (Tile p : n.c.tiles) for (int j = 0; j < N; j++) { keys[p.idx][j] = rnd.nextInt() & 0xFFFFFFFFL; p.sram.poke(A + j, (int) keys[p.idx][j]); }
        long[] passCycles = new long[5];
        // pass 0: raw 32-bit keys, one per packet, mode 4 (augmented) to the tile itself, histogram digit 1
        for (Tile p : n.c.tiles) {
            p.setStream(0, Fields.streamSrc(A), Fields.streamDest(0, 0), Fields.streamCount(N, LinkWord.M_AUG, Fields.SIZE_32, 0));
            p.setPtr(p.idx, B); p.setRecordBase(B); p.dir.remaining[0] = N; streamRun(p, 0, 1, 0, 1);
        }
        passCycles[0] = runAll(n);
        int src = B, dst = C;
        for (int d = 1; d <= 4; d++) {
            for (Tile p : n.c.tiles) {
                int pre = 0;
                for (int b = 0; b < 256; b++) { int c = p.hist.peek(b); p.setPtr(b, dst + 2 * pre); pre += c; p.hist.poke(b, 0); }
                T.eq(N, pre, "tile " + p.idx + " pass " + d + ": counts sum to N");
                p.setStream(0, Fields.streamSrc(src), Fields.streamDest(0, 0), Fields.streamCount(N, LinkWord.M_BUCKET, Fields.SIZE_64, 0));
                p.dir.remaining[0] = N; streamRun(p, 0, 1, d, d < 4 ? d + 1 : 0);
            }
            n.c.newPhase();
            passCycles[d] = runAll(n);
            int x = src; src = dst; dst = x;
        }
        int bad = 0;
        for (Tile p : n.c.tiles) {
            long prevKey = -1, prevOrd = -1;
            for (int j = 0; j < N; j++) {
                long r = p.sram.peek64(src + 2 * j), k = r & 0xFFFFFFFFL, ord = r >>> 32;
                if (k < prevKey || (k == prevKey && ord < prevOrd) || keys[p.idx][(int) ord] != k) bad++;
                prevKey = k; prevOrd = ord;
            }
        }
        T.eq(0, bad, "64 tiles sorted, stable, each (key, ordinal) matching the input");
        noViolations(n.c, "radix sort");
        out(String.format("local radix sort, 64 tiles x %d keys, 0/+1 skew [meas: EngineTest radix sort]: augmenting pass %d cycles, bucket passes %d, %d, %d, %d (N + %d to N + %d each); total %d",
                N, passCycles[0], passCycles[1], passCycles[2], passCycles[3], passCycles[4],
                Math.min(Math.min(passCycles[1], passCycles[2]), Math.min(passCycles[3], passCycles[4])) - N, Math.max(Math.max(passCycles[1], passCycles[2]), Math.max(passCycles[3], passCycles[4])) - N,
                passCycles[0] + passCycles[1] + passCycles[2] + passCycles[3] + passCycles[4]));
    }
    /** Arm + start every tile's run (+ skew), run until every tile is done. */
    static long runAll(Net n) {
        Chip c = n.c; long t0 = c.cycle; boolean[] st = new boolean[64];
        for (int i = 0; i < 5_000_000; i++) {
            for (Tile p : c.tiles) if (!st[p.idx] && c.cycle >= t0 + c.startSkew[p.idx]) { p.pulse(Fields.PULSE_START); st[p.idx] = true; }
            c.step();
            boolean all = true; for (Tile p : c.tiles) if (!st[p.idx] || !phaseDone(c, p)) { all = false; break; }
            if (all) return c.cycle - t0;
        }
        T.check(false, "run finished"); return -1;
    }

    // ================================================================ group runs: R = 0, late groups, start before rd_ready
    static void lateGroups() {
        T.group("late groups");
        Net n = new Net(cfg(false));
        // group 0: t = 4, R = 1; group 1: t = 16, R = 2 (its 17 table reads cannot finish during group 0); group 2: R = 0,
        // gap 6; group 3: t = 4, R = 3. Every stream sends R records to offset k.
        int[][] grp = { { 1, 2, 3, 5 }, { 6, 7, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22 }, { 23 }, { 24, 25, 26, 27 } };
        int[] R = { 1, 2, 0, 3 }, gap = { 4, 4, 6, 4 };
        int[] expect = new int[64];
        for (Tile p : n.c.tiles) {
            int stream = 0, src = 0;
            for (int g = 0; g < 4; g++) {
                p.setGroup(g, Fields.groupLo(R[g]), Fields.groupHi(stream, grp[g].length, gap[g]));
                for (int k : grp[g]) {
                    p.setStream(stream++, Fields.streamSrc(src), Fields.streamDest(0, k), Fields.streamCount(R[g], LinkWord.M_SRC, Fields.SIZE_64, 0));
                    for (int j = 0; j < R[g]; j++) p.sram.poke64(src + 2 * j, (long) p.idx << 32 | k << 8 | j);
                    src += 2 * R[g]; expect[(p.idx + k) & 63] += R[g];
                }
            }
            for (int s = 0; s < 64; s++) p.setPtr(s, 0x40000 + 0x100 * s);
            p.setMasterConfig(Fields.masterConfig(Fields.RUN_GROUPS, 0, 4, 0, 0, 1));
        }
        for (Tile p : n.c.tiles) p.dir.remaining[0] = expect[p.idx];
        long cyc = A2A.run(n.c, 10000);
        T.check(cyc > 0, "finished");
        Tile t5 = n.c.tiles[5];
        long late1 = t5.ag.groupLateBy[1];
        T.eq(0L, late1, "group 1 starts on time (M8 read-ahead; M7's bank prefetch made it 13 cycles late)");
        boolean same = true; for (Tile p : n.c.tiles) for (int g = 0; g < 4; g++) if (p.ag.groupLateBy[g] != t5.ag.groupLateBy[g] || p.ag.groupStart[g] != t5.ag.groupStart[g]) same = false;
        T.check(same, "every tile late by the same amount, every group starting on the same cycle");
        T.eq(0, t5.ag.rotationsLate, "no rotation late within a group");
        long s1 = t5.ag.groupStart[1], s2 = t5.ag.groupStart[2], s3 = t5.ag.groupStart[3];
        T.eq(s1 + 2 * 16 + 4, s2, "group 2 begins R*t + gap after group 1's actual start");
        T.eq(s2 + 1 + 6, s3, "group 2 (R = 0) takes 1 cycle plus its gap (spec 4.2, 10 October; M14, was only its gap)");
        noViolations(n.c, "late groups");
        out(String.format("group run with a 16-stream group after a 4-cycle group: group 1 starts %d cycles late on every tile (M7: 13, the bank prefetch of GROUP + 16 STREAM reads); R = 0 takes 1 cycle plus its gap; no drift, no invariant-3 violation; ag_lookahead %d, STREAM queue peak %d, GROUP queue peak %d, underrun cycles %d [meas: EngineTest late groups]", late1, t5.ag.lookahead, t5.ag.streamQPeak, t5.ag.groupQPeak, t5.ag.underrunCycles));
        // a second start (v1.0: start loads the run): group 0 begins after the lead-in, which is not a read-ahead wait
        for (Tile p : n.c.tiles) p.dir.remaining[0] = expect[p.idx];
        for (Tile p : n.c.tiles) for (int s = 0; s < 64; s++) p.setPtr(s, 0x60000 + 0x100 * s);
        n.c.newPhase();
        n.c.pulseAll(Fields.PULSE_START);
        n.c.runUntil(() -> { for (Tile p : n.c.tiles) if (!phaseDone(n.c, p)) return false; return true; }, 10000);
        T.eq(0L, n.c.tiles[0].ag.groupLateBy[0], "start: group 0 begins after the lead-in, not counted late");
        T.eq((long) (4 * n.c.cfg.tsEngineLatency + 2), n.c.tiles[0].ag.startToFirstRequest(), "start: the first request 4 ts_engine_latency + 2 cycles after the start takes effect");
        out(String.format("start: the first request %d cycles after the start takes effect (its GROUP entry, then its first STREAM entry; ts_engine_latency %d) [meas: EngineTest late groups]", n.c.tiles[0].ag.startToFirstRequest(), n.c.tiles[0].cfg.tsEngineLatency));
        noViolations(n.c, "the lead-in");
    }

    // ================================================================ the 16-group all-to-all
    static String peaks(Chip c) {
        int link = 0, ej = 0, we = 0;
        for (Tile p : c.tiles) { for (int o = 0; o < 4; o++) for (WordFIFO f : p.router.q[o]) link = Math.max(link, f.highWater); for (WordFIFO f : p.router.q[Router.EJECT]) ej = Math.max(ej, f.highWater); we = Math.max(we, p.weIn.highWater); }
        int aq = 0; for (Tile p : c.tiles) for (int q : p.sram.queuePeak) aq = Math.max(aq, q);
        return String.format("link %d, eject %d, we-in %d, area queue %d", link, ej, we, aq);
    }

    static void allToAll() throws Exception {
        T.group("all-to-all");
        StringBuilder sb = new StringBuilder("16-group all-to-all as a group run [meas: EngineTest all-to-all]: cycles from the start store to the last tile's local-wake condition; bound = sum 4 R_g + 16 gap; 0/+1 skew\n");
        sb.append(String.format("  %-26s %-4s %10s %10s %6s  %-44s %s%n", "data set", "gap", "measured", "bound", "over", "queue peaks (of 16; we-in of 4; area of 8)", "late / drift"));
        List<Object[]> sets = new ArrayList<>();
        if (full) {
            sets.add(new Object[] { "uniform (Data(7, 65536))", new A2A(7, 65536), new int[] { 4, 0, 8 } });
            for (String m : new String[] { "zipfraw", "trace", "one99" }) sets.add(new Object[] { m, new A2A(Path.of("data/" + m + ".txt")), new int[] { 4, 0, 8 } });
        } else sets.add(new Object[] { "uniform (Data(7, 2000))", new A2A(7, 2000), new int[] { 4, 0 } });
        for (Object[] st : sets) {
            A2A d = (A2A) st[1];
            for (int gap : (int[]) st[2]) {
                Net n = new Net(cfg(true));
                d.load(n.c, gap);
                long cyc = A2A.run(n.c, 20_000_000);
                int bad = d.verify(n.c);
                T.eq(0, bad, st[0] + " gap " + gap + ": every record where it belongs");
                T.check(cyc > 0, st[0] + " gap " + gap + ": finished");
                if (gap > 0) noViolations(n.c, st[0] + " gap " + gap);
                String lr = n.c.agReport().trim().replaceAll(".*rotations late (\\d+) on (\\d+) tiles \\(max (\\d+) cycles\\); group starts waiting for read-ahead (\\d+) \\(max (\\d+), timetable\\); drift between tiles at a group start (\\d+).*", "rot $1, prefetch-late groups $4 (max $5), drift $6");
                sb.append(String.format("  %-26s %-4d %10d %10d %6d  %-44s %s%s%n", st[0], gap, cyc, d.bound(gap), cyc - d.bound(gap), peaks(n.c), lr, n.c.violations > 0 ? "; " + n.c.violationMessages.get(0) : ""));
            }
        }
        sb.append("  reference, measured on simulator v3.10 (gap 8, AllToAllTest measure, schedule-4 RESULTS 4.1): sixteen groups uniform 71,345, zipfraw 294,580, trace 706,470, one99 434,712; nine groups 73,904, 304,647, 800,218, 575,905");
        out(sb.toString());
    }

    // ================================================================ drift: an over-subscribed source area in a group run (invariant 3)
    static void drift() {
        T.group("drift");
        A2A d = new A2A(7, 500);
        Net n = new Net(cfg(false));
        d.load(n.c, 4);
        // tile 7 receives into its send buffer's area (area 0, at 0x10000: a different buffer, so no invariant-2 issue):
        // writes win every tie, so its reads wait and its rotations run late
        Tile p = n.c.tiles[7];
        for (int s = 0; s < 64; s++) p.setPtr(s, 0x10000 + 2 * d.dstoff[s][7]);
        long cyc = A2A.run(n.c, 1_000_000);
        T.check(cyc > 0, "finished");
        T.check(p.ag.rotationsLate > 0, "tile 7's rotations run late");
        T.check(String.join("\n", n.c.violationMessages).contains("invariant 3"), "reported as invariant 3");
        out("drift: a tile whose incoming writes share its send buffer's area waits at the area (writes first) and runs late: " + n.c.agReport().trim() + " [meas: EngineTest drift]");
    }

    // ================================================================ invariant 6
    static void counterOverflow() {
        T.group("invariant 6");
        Net n = new Net(cfg(false)); Tile r = n.c.tiles[3];
        r.hist.poke(5, 0x1FFFF); r.setMasterConfig(Fields.masterConfig(0, 0, 0, 0, 1, 1)); r.setPtr(2, 0x20000); r.dir.remaining[0] = 1;
        n.at(2, 2, 3, LinkWord.M_SRC, 0, 5);
        n.runAll(500);
        T.check(r.hist.histOverflow, "counter past 17 bits flagged");
        T.check(String.join("\n", n.c.violationMessages).contains("invariant 6"), "reported as invariant 6");
    }
}
