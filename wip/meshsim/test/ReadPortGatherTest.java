import java.util.ArrayList;
import java.util.List;
import java.util.Random;

/**
 * The controller read port (spec 4.3, which replaced mode 0, the responder and probe rounds late on 2 October) and the
 * GATHER engine (4.4). Java-driven runs use M1-M3's coreless tiles with the real GATHER engine (read-port reads
 * queued through ReadPort.hostQueue); the C-driven runs boot sw/tests/rdport.c and gather.c on the machine as specified.
 * Usage: {@code java ReadPortGatherTest [shuffleSeed] [full]}.
 */
public final class ReadPortGatherTest {
    static long shuffle = -1;
    static boolean full;
    static final StringBuilder report = new StringBuilder();
    static void out(String s) { System.out.println(s); report.append(s).append('\n'); }

    public static void main(String[] args) {
        if (args.length > 0) shuffle = Long.parseLong(args[0]);
        full = args.length > 1 && args[1].equals("full");
        if (shuffle >= 0) System.out.println("shuffled child order, seed " + shuffle);
        readPortFromC();
        readPortUnderTraffic();
        readPortTolerance();
        overlapCheck();
        disjointPerRun();
        reservedStreams();
        gather();
        gatherFromC();
        System.exit(T.done("ReadPortGatherTest" + (shuffle >= 0 ? " (shuffled, seed " + shuffle + ")" : "") + (full ? " full" : "")));
    }

    static Config cfg() { Config c = Config.defaults(); c.realResponder = true; c.shuffleSeed = shuffle; return c; }
    static void noViolations(Chip c, String what) {
        T.check(c.violations == 0, what + ": no invariant violations" + (c.violations == 0 ? "" : "\n" + c.violationReport()));
    }
    static boolean phaseDone(Chip c, Tile p) { return EngineTest.phaseDone(c, p); }
    /** Start a run on every tile (arm and start in one store, + start skew); cycles from the first store to the last tile done. */
    static long runAll(Chip c, long max) {
        long t0 = c.cycle; boolean[] st = new boolean[64];
        for (long i = 0; i < max; i++) {
            for (Tile p : c.tiles) if (!st[p.idx] && c.cycle >= t0 + c.startSkew[p.idx]) { p.pulse(Fields.PULSE_START); st[p.idx] = true; }
            c.step();
            boolean all = true; for (Tile p : c.tiles) if (!st[p.idx] || !phaseDone(c, p)) { all = false; break; }
            if (all) { c.scanOverflows(); return c.cycle - t0; }
        }
        c.scanOverflows();
        return -1;
    }
    static int dword(Tile p, int packedWord) { return p.cmem.dmem.peek(packedWord & 0x7FF); }
    static long peaks(Chip c, int[] out) {   // {link, eject, -, gather-q}
        for (Tile p : c.tiles) {
            for (int o = 0; o < 4; o++) for (WordFIFO f : p.router.q[o]) out[0] = Math.max(out[0], f.highWater);
            for (WordFIFO f : p.router.q[Router.EJECT]) out[1] = Math.max(out[1], f.highWater);
            out[3] = Math.max(out[3], p.gatherIn.highWater);
        }
        return 0;
    }

    // ================================================================ the responder (4.3)

    // ================================================================ the read port from C (4.3)
    static void readPortFromC() {
        T.group("read port from C");
        Mach m = new Mach(Mach.cfg(shuffle));
        m.c.cfg.maxViolationMessages = 100;
        for (Tile p : m.c.tiles) for (int x = 0; x < 0x1100; x++) p.sram.poke(0x1000 + x, p.idx << 20 | x);
        Mach.Image im = m.boot("rdport");
        boolean ok = m.run(m::allStopped, 20000);
        T.check(ok && m.allExited(), "every tile ran rdport: " + m.haltSummary());
        for (Tile p : m.c.tiles) {
            String tl = "tile " + p.idx + ": ";
            int bad = 0;
            for (int i = 0; i < 40; i++) if (m.word(p.idx, im, "res", i) != (p.idx << 20 | (i * 37 & 0xFFF))) bad++;
            if (p.idx != 7) T.eq(0, bad, tl + "40 results at the destination + 4 i bytes");
            T.eq(64, m.word(p.idx, im, "r", 0), tl + "64 free entries at the start");
            T.check(m.word(p.idx, im, "r", 1) < 64, tl + "fewer free while reads are outstanding (" + m.word(p.idx, im, "r", 1) + ")");
            T.eq(64, m.word(p.idx, im, "r", 2), tl + "64 free after the local wake: every entry freed as its return was injected");
            T.eq(0, m.word(p.idx, im, "r", 3), tl + "WRITE_COUNTER_2 counted down to 0");
            T.eq(im.sym("wide") + 128 | Registers.HDR_SIZE.place(LinkWord.SIZE_128) | Registers.HDR_CTR.place(2), m.word(p.idx, im, "r", 6),
                    tl + "SRAM_RD_DEST_ADDR reads the next destination, advanced by the size in bytes, with the size and counter");
            boolean w64 = true, w128 = true;
            for (int i = 0; i < 16; i++) if (m.word(p.idx, im, "wide", i) != (p.idx << 20 | 0x100 + i)) w64 = false;
            for (int i = p.idx == 6 ? 4 : 0; i < 16; i++) if (m.word(p.idx, im, "wide", 16 + i) != (p.idx << 20 | 0x200 + i)) w128 = false;   // tile 6 reuses wide[16..19]
            T.check(w64, tl + "8 64-bit reads: two words each, at the destination + 8 i bytes");
            T.check(w128, tl + "4 128-bit reads: four words each, at the destination + 16 i bytes");
        }
        // v1.2 (4.3, D32): a misaligned read is not performed, its result lands with payload 0, ERRORS [1]
        T.eq(1 << Conditions.BAD_MODE_OR_SIZE, m.word(6, im, "r", 4), "tile 6: a misaligned 128-bit read set ERRORS [1], and only it");
        T.eq(1L, m.c.tiles[6].srq.misalignedReads, "tile 6: one misaligned read not performed");
        T.eq(0, m.word(6, im, "r", 7), "tile 6: its result landed as 0 (counted)");
        Tile t7 = m.c.tiles[7];
        T.eq(1 << Conditions.READ_PORT, m.word(7, im, "r", 4), "tile 7: the 65th store into a full queue set the read-port-error condition, and only it");
        T.eq(1, t7.readPort.overflows, "tile 7: one read dropped");
        T.check(m.c.violationMessages.size() == 2 && m.c.violationMessages.stream().anyMatch(v -> v.contains("tile 7") && v.contains("invariant 8"))
                && m.c.violationMessages.stream().anyMatch(v -> v.contains("tile 6") && v.contains("not aligned")), "the overflow (invariant 8) and the misaligned read, and nothing else, reported: " + m.c.violationReport());
        int bad = 0; for (int i = 0; i < 64; i++) if (m.word(7, im, "res", i) != (7 << 20 | 0x1000 + i)) bad++;
        T.eq(0, bad, "tile 7: the 64 queued reads all landed");
        out("read port from C [meas: ReadPortGatherTest read port from C]: 40 32-bit, 8 64-bit and 4 128-bit reads land at the setup register's destination + the size in bytes per read, counting on their counter; the free count reads 64, then less, then 64 after counters done; a misaligned read lands 0 with ERRORS [1]; a 65th store into a full queue is dropped with the read-port error");
    }

    // ================================================================ the read port under an all-to-all (lowest priority)
    /** M10: 64 reads queued on every tile at an all-to-all's start break 4.3's rule (reads while packets arrive): the
     *  timetable is unaffected (lowest priority), but the results now loop back through the eject during the exchange and
     *  overflow it. Measured and reported, not required to be clean; the overlap check is off for this run. */
    static void readPortUnderTraffic() {
        T.group("read port under an all-to-all");
        StringBuilder sb = new StringBuilder("the 16-group all-to-all (uniform, Data(7, 2000), gap 0) with every tile's read port holding 64 queued reads from the start, against 4.3's rule [meas: ReadPortGatherTest read port under an all-to-all]:\n");
        long[] cyc = new long[2]; long[] late = new long[2]; long lastLat = 0, overlaps = 0, ovf = 0, lostRec = 0;
        for (int withReads = 0; withReads < 2; withReads++) {
            A2A a = new A2A(7, 2000);
            Config cf = cfg(); cf.checkReadPortOverlap = false;
            Chip c = new Chip(cf);
            a.load(c, 0);
            if (withReads == 1) for (Tile p : c.tiles) {
                p.dir.remaining[2] = 64;
                for (int i = 0; i < 64; i++) p.readPort.hostQueue(0xE0000 + 2 * i, Net.cm(0x900 + i));
            }
            cyc[withReads] = A2A.run(c, 3_000_000);
            c.runUntil(() -> { for (Tile p : c.tiles) if (!p.readPort.idle(c.cycle)) return false; return true; }, 100000);
            c.run(200);
            for (Tile p : c.tiles) late[withReads] += p.ag.rotationsLate + p.ag.groupsLatePrefetch;
            if (withReads == 0) {
                T.eq(0, a.verify(c), "without read-port traffic: every record landed");
                noViolations(c, "all-to-all without read-port traffic");
            } else {
                lostRec = a.verify(c);
                for (Tile p : c.tiles) {
                    lastLat = Math.max(lastLat, p.srq.latMax[SRAMReadQueue.READPORT]); overlaps += p.dir.readPortOverlaps;
                    for (WordFIFO[] fs : p.router.q) for (WordFIFO f : fs) ovf += f.overflowCount;
                }
            }
        }
        T.eq(cyc[0], cyc[1], "the all-to-all's length is unchanged by waiting read-port reads (lowest priority)");
        T.eq(0, late[1], "the address generator is never late with read-port reads waiting");
        T.eq(0L, ovf, "with the loopback token-protected and served last (4.5, 4.7), results looping back during the exchange overflow nothing (v1.0: they overflowed an eject FIFO; re-measured, items 24, 70)");
        T.eq(0L, lostRec, "and every record of the exchange landed");
        sb.append(String.format("  without: %d cycles; with: %d cycles (%+d); late rotations or groups: %d; the longest read-port read waited %d cycles in the SRAM read queue%n", cyc[0], cyc[1], cyc[1] - cyc[0], late[1], lastLat));
        sb.append(String.format("  with: %,d packets accepted with results outstanding; %,d packets lost to FIFO overflows (eject<loopback); records failing the all-to-all's check: %d%n", overlaps, ovf, lostRec));
        out(sb.toString().stripTrailing());
    }

    // ================================================================ tolerance: read-port results against an incoming stream
    /** One run: tile 20 receives a 256-packet mode-5 stream from tile 21 (gap 0 = one packet per cycle, 1 = half rate);
     *  its read port is given {@code reads} reads, one store per cycle, starting {@code delay} cycles after tile 21's
     *  start store. Returns {loopback FIFO peak, link-feeder peak (max over the eject's link FIFOs), overflows, overlaps}. */
    static long[] toleranceRun(int gap, int reads, int delay) {
        Config cf = cfg(); cf.checkReadPortOverlap = false;      // measuring the overlap the rule forbids
        Chip c = new Chip(cf);
        Tile x = c.tiles[20], p = c.tiles[21];
        final int pk = 256;
        for (int i = 0; i < pk; i++) p.sram.poke64(2 * i, (long) p.idx << 32 | i);
        for (int j = 0; j < pk; j++) p.setStream(j & 255, Fields.streamSrc(2 * j, gap), Net.cm(0x800 + 2 * (j & 127)), Fields.streamCount(1, Fields.SIZE_64, 2, LinkWord.M_WRITE, 63));
        x.dir.remaining[2] = pk + reads;   // read-port results are mode-6 packets too (4.3)
        EngineTest.streamRun(p, 0, pk, 0, 0);
        p.pulse(Fields.PULSE_START);
        for (int i = 0; i < delay; i++) c.step();
        for (int i = 0; i < reads; i++) x.readPort.hostQueue(0x1000 + i, 0xD00 + (i & 0xFF));
        c.runUntil(() -> x.dir.remaining[2] == 0 && x.dir.cmIdle(c.cycle), 100000);
        c.scanOverflows();
        long lb = 0, lk = 0, ovf = 0;
        int lbSlot = x.router.slotOfPort(Router.EJECT, Router.LOOPBACK);
        WordFIFO[] ej = x.router.q[Router.EJECT];
        for (int f = 0; f < ej.length; f++) { ovf += ej[f].overflowCount; if (f == lbSlot) lb = ej[f].highWater; else lk = Math.max(lk, ej[f].highWater); }
        for (Tile q : c.tiles) for (WordFIFO[] fs : q.router.q) for (WordFIFO f : fs) if (q != x || fs != ej) ovf += f.overflowCount;
        return new long[] { lb, lk, ovf, x.dir.readPortOverlaps };
    }
    static final int[] TOL_DELAYS = { 0, 4, 8, 12, 16, 24 };
    /** Worst case over the delays: {loopback peak, link peak, overflowing delays, overlaps at the worst}. */
    static long[] toleranceWorst(int gap, int reads) {
        long[] w = new long[4];
        for (int d : TOL_DELAYS) {
            long[] r = toleranceRun(gap, reads, d);
            w[0] = Math.max(w[0], r[0]); w[1] = Math.max(w[1], r[1]); if (r[2] > 0) w[2]++; w[3] = Math.max(w[3], r[3]);
        }
        return w;
    }
    /** Quick: two checks. Full: the table and the exact limit (about 5 minutes). */
    static void readPortTolerance() {
        T.group("read-port tolerance");
        if (!full) {
            T.check(toleranceWorst(0, 8)[2] == 0, "full-rate stream, 8 results: no overflow at any offset");
            T.check(toleranceWorst(0, 32)[2] == 0, "full-rate stream, 32 results: no overflow (the loopback waits on tokens; v1.0 overflowed)");
            out("read-port tolerance (quick): 8 and 32 results against a full-rate mode-6 stream never overflow (the token-protected loopback, served last, 4.5, 4.7); 'ReadPortGatherTest -1 full' measures the table");
            return;
        }
        StringBuilder sb = new StringBuilder("read-port results against an incoming stream [meas: ReadPortGatherTest read-port tolerance]: tile 20 receives a 256-packet mode-5 stream from tile 21 while R read-port reads are issued one per cycle, starting 0-24 cycles after the stream's start store (worst over " + TOL_DELAYS.length + " offsets). Results return as mode-7 packets through tile 20's loopback (4.3, M10). Eject FIFO peaks, of 16:\n");
        for (int gap : new int[] { 0, 1 }) {
            int safe = 0, bad = -1;
            for (int reads : new int[] { 0, 8, 16, 20, 24, 28, 32, 40, 48, 64 }) {
                long[] w = toleranceWorst(gap, reads);
                if (reads <= 8) T.check(w[2] == 0, "gap " + gap + ", " + reads + " results: no overflow at any offset");
                if (w[2] == 0 && bad < 0) safe = reads;
                if (w[2] > 0 && bad < 0) bad = reads;
                sb.append(String.format("  stream at %-19s R = %2d: loopback peak %2d, link peak %2d%s%n", gap == 0 ? "full rate (1/cycle)," : "half rate,", reads, w[0], w[1],
                        w[2] > 0 ? "  -> OVERFLOW at " + w[2] + " of " + TOL_DELAYS.length + " offsets" : ""));
            }
            if (bad > 0) {                                        // bisect between the last safe and the first overflowing R
                while (bad - safe > 1) { int mid = (safe + bad) / 2; if (toleranceWorst(gap, mid)[2] > 0) bad = mid; else safe = mid; }
                sb.append(String.format("  stream at %s: the largest R with no overflow at any offset is %d (first overflow at %d)%n", gap == 0 ? "full rate" : "half rate", safe, bad));
            } else sb.append(String.format("  stream at %s: no overflow up to R = 64%n", gap == 0 ? "full rate" : "half rate"));
            if (gap == 0) T.check(safe >= 8, "full rate: at least 8 results tolerated (largest safe R = " + safe + ")");
        }
        out(sb.toString().stripTrailing());
    }

    // ================================================================ the overlap check (spec 4.3's software rule)
    static void overlapCheck() {
        T.group("read-port overlap check");
        // (a) an outside packet while results are outstanding: a violation
        {
            Chip c = new Chip(cfg());
            Tile x = c.tiles[20], p = c.tiles[21];
            for (int j = 0; j < 4; j++) p.setStream(j, Fields.streamSrc(2 * j), Net.cm(0x810 + 2 * j), Fields.streamCount(1, Fields.SIZE_64, 2, LinkWord.M_WRITE, 63));
            x.dir.remaining[2] = 4 + 8;
            EngineTest.streamRun(p, 0, 4, 0, 0);
            p.pulse(Fields.PULSE_START);
            for (int i = 0; i < 8; i++) x.readPort.hostQueue(0x1000 + i, Net.cm(0xE00 + i));
            c.runUntil(() -> x.dir.remaining[2] == 0 && x.dir.cmIdle(c.cycle), 10000);
            T.check(x.dir.readPortOverlaps > 0 && c.violations > 0 && c.violationReport().contains("read-port overlap"), "4 mode-5 packets arriving with 8 results outstanding: flagged (" + x.dir.readPortOverlaps + " packets)");
        }
        // (b) the same stream after every result has been accepted: no violation
        {
            Chip c = new Chip(cfg());
            Tile x = c.tiles[20], p = c.tiles[21];
            for (int j = 0; j < 4; j++) p.setStream(j, Fields.streamSrc(2 * j), Net.cm(0x810 + 2 * j), Fields.streamCount(1, Fields.SIZE_64, 2, LinkWord.M_WRITE, 63));
            x.dir.remaining[2] = 8;
            for (int i = 0; i < 8; i++) x.readPort.hostQueue(0x1000 + i, Net.cm(0xE00 + i));
            c.runUntil(() -> x.dir.remaining[2] == 0 && x.dir.cmIdle(c.cycle) && x.readPort.allAccepted(), 10000);
            x.dir.remaining[2] = 4;
            EngineTest.streamRun(p, 0, 4, 0, 0);
            p.pulse(Fields.PULSE_START);
            c.runUntil(() -> x.dir.remaining[2] == 0 && x.dir.cmIdle(c.cycle), 10000);
            T.eq(0, x.dir.readPortOverlaps, "results first, then the stream: no overlap");
            noViolations(c, "results first, then the stream");
        }
        // (c) C&C 1: a command write (uncounted mode 0 into the command block) during a search is exempt; any other
        // packet from C&C 1 (a counted data-memory write) is not
        for (int cmd = 1; cmd >= 0; cmd--) {
            Chip c = new Chip(cfg());
            Tile x = c.tiles[20];
            x.dir.remaining[2] = 32 + (cmd == 0 ? 1 : 0);
            for (int i = 0; i < 32; i++) x.readPort.hostQueue(0x1000 + i, Net.cm(0xE00 + i));
            c.run(4);
            if (cmd == 1) c.cnc.send(c.cycle, 20, LinkWord.M_WRITE, 0, ControllerMemories.COMMAND + 4, LinkWord.SIZE_32, 0x1234, 0);
            else c.cnc.send(c.cycle, 20, LinkWord.M_WRITE, 2, 0x400, LinkWord.SIZE_32, 0x1234, 0);
            c.runUntil(() -> x.dir.remaining[2] == 0 && x.dir.cmIdle(c.cycle) && c.quiescent(), 10000);
            if (cmd == 1) { T.eq(0, x.dir.readPortOverlaps, "C&C 1's command write during the results: exempt"); noViolations(c, "C&C 1's command write"); }
            else T.check(x.dir.readPortOverlaps == 1 && c.violations > 0, "C&C 1's data-memory write during the results: flagged");
        }
        out("overlap check [meas: ReadPortGatherTest read-port overlap check]: an outside packet accepted while read-port results are outstanding is a violation (Config.checkReadPortOverlap, default on); C&C 1's command writes are exempt, its other packets are not; results first and the stream after: none");
    }

    // ================================================================ the disjoint-buffer checker per engine run (local wake)
    static void disjointPerRun() {
        T.group("disjoint buffers per engine run");
        // run 1 copies A -> B; run 2 (after run 1's local wake, no GO between) copies B -> A: legal per engine run
        Chip c = new Chip(cfg());
        Tile p = c.tiles[12];
        final int A = 0x20000, B = 0x40000, N = 64;
        for (int i = 0; i < N; i++) p.sram.poke64(A + 2 * i, 0xA000L + i);
        p.setStream(0, Fields.streamSrc(A), 0, Fields.streamCount(N, Fields.SIZE_64, 1, LinkWord.M_SRC, 0));
        p.setPtr(12, B); p.dir.remaining[1] = N;
        EngineTest.streamRun(p, 0, 1, 0, 0);
        p.pulse(Fields.PULSE_START);
        c.runUntil(() -> p.ctl.countersDonePending(), 2000);
        T.check(p.ctl.countersDonePending(), "run 1 ended with counters done");
        p.setStream(1, Fields.streamSrc(B), 0, Fields.streamCount(N, Fields.SIZE_64, 1, LinkWord.M_SRC, 0));
        p.setPtr(12, A); p.dir.remaining[1] = N;
        EngineTest.streamRun(p, 1, 1, 0, 0);
        p.pulse(Fields.PULSE_START);
        c.runUntil(() -> EngineTest.phaseDone(c, p), 2000);
        T.eq(0xA000L + 5, p.sram.peek64(A + 10), "the data came back");
        T.eq(0, c.violations, "A -> B, local wake, B -> A: no disjoint-buffer violation " + c.violationReport());
        // one run that reads and writes the same buffer: flagged
        Chip d = new Chip(cfg());
        Tile q = d.tiles[12];
        q.setStream(0, Fields.streamSrc(A), 0, Fields.streamCount(N, Fields.SIZE_64, 1, LinkWord.M_SRC, 0));
        q.setPtr(12, A + 2 * N - 16); q.dir.remaining[1] = N;
        EngineTest.streamRun(q, 0, 1, 0, 0);
        q.pulse(Fields.PULSE_START);
        d.runUntil(() -> EngineTest.phaseDone(d, q), 2000);
        T.check(d.violationMessages.stream().anyMatch(v -> v.contains("invariant 2")), "a run that reads and writes one buffer is flagged (invariant 2)");
        out("disjoint buffers [meas: ReadPortGatherTest disjoint buffers per engine run]: a copy A -> B, then B -> A after its local wake with no GO between, passes; one run reading and writing one buffer is flagged");
    }

    // ================================================================ reserved stream encodings (4.2, invariant 5)
    static void reservedStreams() {
        T.group("reserved streams");
        Chip c = new Chip(cfg());
        Tile p = c.tiles[3];
        p.setStream(0, Fields.streamSrc(0x1000), 0, Fields.streamCount(1, Fields.SIZE_32, 1, 6, 1));            // mode 6: reserved
        p.setStream(1, Fields.streamSrc(0x1000), 0, Fields.streamCount(1, Fields.SIZE_UNDEF, 1, LinkWord.M_SRC, 1));
        p.setStream(2, Fields.streamSrc(0x1000), 0, Fields.streamCount(0, Fields.SIZE_64, 1, 7, 1));            // mode 7, count 0
        p.setStream(3, Fields.streamSrc(0x1000), 0, Fields.streamCount(1, Fields.SIZE_128, 1, LinkWord.M_AUG, 1));   // mode 3 at 128
        p.setStream(4, Fields.streamSrc(0x1000), 0x0200, Fields.streamCount(1, Fields.SIZE_64, 2, LinkWord.M_WRITE, 1));   // mode 0 at 0x200 (fine)
        EngineTest.streamRun(p, 0, 5, 0, 0);
        c.tiles[4].dir.remaining[1] = 1; c.tiles[4].dir.remaining[2] = 1; c.tiles[4].setPtr(3, 0x40000);
        p.pulse(Fields.PULSE_START);
        c.run(100);
        T.eq(1L, p.ag.sizeViolations, "a stream with read size 11 (undefined): reported");
        for (int code : new int[] { 6, 7 })
            T.check(c.violationMessages.stream().anyMatch(v -> v.contains("tile 3") && v.contains("reserved mode " + code)), "STREAM mode " + code + " rejected (4.2)" + (code == 7 ? ", even at count 0" : ""));
        T.eq(2L, p.ag.modeViolations, "two reserved modes, one report each");
        T.eq(1L, p.ag.aug128Violations, "a mode-3 stream at read size 128: reported (4.2)");
        T.eq(1, c.tiles[4].dir.droppedBad, "the 128-bit mode-3 packet is dropped at its director (a bad packet)");
        T.eq(1L, c.tiles[4].dir.reservedDiscarded, "the mode-6 packet is discarded silently at its director");
        T.eq(1L, c.tiles[4].dir.undefinedSize, "the size-11 packet: flagged and dropped at the director (meshsim's choice)");
        T.check((c.tiles[4].conditions() & (1 << Conditions.BAD_MODE_OR_SIZE)) != 0, "and the bad packet sets ERRORS [1]");
        T.eq(0, c.tiles[4].dir.remaining[2], "the good mode-0 packet landed and counted");
        // a group run reports a reserved mode on every visit of the stream, idle or not
        Chip c2 = new Chip(cfg());
        Tile q = c2.tiles[3];
        q.setStream(0, Fields.streamSrc(0x1000), 0, Fields.streamCount(2, Fields.SIZE_64, 1, LinkWord.M_SRC, 1));
        q.setStream(1, Fields.streamSrc(0x2000), 0, Fields.streamCount(0, Fields.SIZE_64, 1, 7, 2));     // mode 7, idle
        q.setGroup(0, Fields.groupLo(2), Fields.groupHi(0, 2, 0));
        c2.tiles[4].dir.remaining[1] = 2; c2.tiles[4].setPtr(3, 0x40000);
        q.setMasterConfig(Fields.masterConfig(Fields.RUN_GROUPS, 0, 1, 0, 0, 1));
        q.pulse(Fields.PULSE_START);
        c2.run(100);
        T.eq(2L, q.ag.modeViolations, "group run: mode 7 reported on both of its visits (R = 2)");
        T.eq(2L, c2.tiles[4].writer.taken, "the valid stream's two packets still land");
    }

    static long[] gatherRun(int k, int N, StringBuilder sb) {
        Chip c = new Chip(cfg());
        final int IDX = 0x60000, VAL = 0x20000, OUT = 0xA0000;
        int[][] perm = new int[64][];
        for (Tile p : c.tiles) {
            Random r = new Random(100 + p.idx);
            int[] pm = new int[N]; for (int i = 0; i < N; i++) pm[i] = i;
            for (int i = N - 1; i > 0; i--) { int j = r.nextInt(i + 1); int x = pm[i]; pm[i] = pm[j]; pm[j] = x; }
            perm[p.idx] = pm;
            for (int i = 0; i < N; i++) p.sram.poke64(IDX + 2 * i, (long) pm[i] << 32 | 0xAB000000L | i);
            for (int v = 0; v < N; v++) for (int i = 0; i < k; i++) p.sram.poke64(VAL + 2 * k * v + 2 * i, (long) p.idx << 48 | (long) v << 8 | i);
            p.setStream(195, Fields.streamSrc(IDX), 0, Fields.streamCount(N, Fields.SIZE_64, 1, LinkWord.M_GATHER, 0));
            p.setPtr(p.idx, OUT);
            p.gather.base = VAL;
            p.dir.remaining[1] = N * k;
            p.setMasterConfig(Fields.masterConfig(Fields.RUN_STREAMS, 195, 1, 0, 0, k));
        }
        long cyc = runAll(c, 10_000_000);
        long bad = 0;
        for (Tile p : c.tiles) for (int i = 0; i < N; i++) for (int e = 0; e < k; e++)
            if (p.sram.peek64(OUT + 2 * k * i + 2 * e) != ((long) p.idx << 48 | (long) perm[p.idx][i] << 8 | e)) bad++;
        T.check(cyc > 0 && bad == 0, "k = " + k + ", N = " + N + ": every value landed in sorted order (" + bad + " wrong)");
        noViolations(c, "gather k = " + k);
        int[] pk = new int[4]; peaks(c, pk);
        long agStall = 0, srqStall = 0, area = 0;
        for (Tile p : c.tiles) { agStall = Math.max(agStall, p.ag.stallCycles); srqStall = Math.max(srqStall, p.srq.stallCycles); for (int a : p.sram.queuePeak) area = Math.max(area, a); }
        sb.append(String.format("  k = %d  N = %6d  %9d cycles  %6.3f per value (k + 1 = %d)  GATHER queue peak %2d of 16  address generator stalled %d cycles, area queue peak %d%s%n",
                k, N, cyc, (double) cyc / N, k + 1, pk[3], agStall, area, pk[3] >= 16 ? "  OVERFLOW" : ""));
        return new long[] { cyc, pk[3] };
    }

    static void gather() {
        T.group("gather");
        StringBuilder sb = new StringBuilder("local gather on every tile: the index stream (mode 1, d = 0, 64-bit) from area 3, values from area 1, output at PTR[me] in area 5; 0/+1 skew [meas: ReadPortGatherTest gather]:\n");
        int N = 4096;
        for (int k : new int[] { 1, 2, 3, 4, 8 }) gatherRun(k, N, sb);
        if (full) gatherRun(2, 65536, sb);
        out(sb.toString().stripTrailing());
    }

    // ================================================================ from C: a probe round and gather 1 (two programs)

    static void gatherFromC() {
        T.group("gather from C");
        int N = full ? 65536 : 4096, k = 2;
        Mach m = new Mach(Mach.cfg(shuffle));
        final int IDX = 0x60000, VAL = 0x20000, OUT = 0xA0000;
        int[][] perm = new int[64][];
        for (Tile p : m.c.tiles) {
            Random r = new Random(100 + p.idx);
            int[] pm = new int[N]; for (int i = 0; i < N; i++) pm[i] = i;
            for (int i = N - 1; i > 0; i--) { int j = r.nextInt(i + 1); int x = pm[i]; pm[i] = pm[j]; pm[j] = x; }
            perm[p.idx] = pm;
            for (int i = 0; i < N; i++) p.sram.poke64(IDX + 2 * i, (long) pm[i] << 32 | i);
            for (int v = 0; v < N; v++) for (int i = 0; i < k; i++) p.sram.poke64(VAL + 2 * k * v + 2 * i, (long) p.idx << 48 | (long) v << 8 | i);
        }
        Mach.Image im = m.boot("gather");
        m.c.cc.command(LinkWord.BROADCAST, 1 << 24 | N).go().command(LinkWord.BROADCAST, 2 << 24).go();
        boolean ok = m.run(m::allStopped, 5_000_000);
        T.check(ok && m.allExited(), "every tile ran gather to its exit: " + m.haltSummary());
        long bad = 0;
        for (Tile p : m.c.tiles) for (int i = 0; i < N; i++) for (int e = 0; e < k; e++)
            if (p.sram.peek64(OUT + 2 * k * i + 2 * e) != ((long) p.idx << 48 | (long) perm[p.idx][i] << 8 | e)) bad++;
        T.eq(0, bad, "every value landed in sorted order");
        noViolations(m.c, "gather from C");
        List<long[]> ph = m.c.cc.phaseLog;
        long cyc = ph.get(1)[0] - ph.get(0)[1];
        out(String.format("gather 1 from C (sw/tests/gather.c, one-command phase, %,d values, k = 2) [meas: ReadPortGatherTest gather from C]: C&C's GO -> all 64 READY %,d cycles (%.3f per value)%s",
                N, cyc, (double) cyc / N, full ? "; the walk-through estimates 196,648" : ""));
    }
}
