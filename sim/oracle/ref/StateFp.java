import java.io.PrintWriter;
import java.lang.reflect.Field;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/**
 * The canonical state fingerprint, ref's side (sim/tests/AUDIT-M2.md section 6; WORKER7). Between two cycles (after
 * Chip.runCycle(c) returned, before c + 1), every unit's state is read **without touching ref's clock**: private fields
 * by reflection, never a bus or memory method (those settle lazily on their first touch and would move ref's state).
 * Each unit's state is written as a defined sequence of 64-bit words (the "canonical stream", its order and encoding
 * given unit by unit below and in AUDIT-M2 section 6) and hashed; the C++ framework computes the same stream from its
 * own state. Nothing here is in sim/ref: it is compiled into ref's default package (D5) and only reads.
 *
 * Hash (both sides): h = 0x6A09E667F3BCC908; for each word w: h = fmix64(h ^ w); at the end h = fmix64(h ^ n), n the
 * number of words. fmix64 is MurmurHash3's finaliser. A memory is one word in its unit's stream: the sum mod 2^64 of
 * fmix64(region << 56 ^ index << 32 ^ value) over its non-zero 32-bit words (order-free, so it can be kept incrementally).
 *
 * Interpreting lazily settled state as at the end of cycle c: a bus's pending put made at c is listed as pending (the
 * framework holds it in a delivery bucket); one made before c (the bus not touched since) is settled here, by the bus's
 * own rule, without calling it. A memory macro's pending writes are overlaid on its array.
 */
final class StateFp {
    // ================================================================ hashing
    static long fmix(long k) { k ^= k >>> 33; k *= 0xff51afd7ed558ccdL; k ^= k >>> 33; k *= 0xc4ceb9fe1a85ec53L; k ^= k >>> 33; return k; }
    static long term(int region, long index, int value) { return fmix((long) region << 56 ^ index << 32 ^ (value & 0xFFFFFFFFL)); }

    /** A canonical stream: hashed, or dumped with names (fpdump). */
    static class Out {
        long h = 0x6A09E667F3BCC908L; long n;
        void w(String name, long v) { h = fmix(h ^ v); n++; }
        long done() { return fmix(h ^ n); }
    }
    static final class Dump extends Out {
        final PrintWriter p; final String unit;
        Dump(PrintWriter p, String unit) { this.p = p; this.unit = unit; }
        @Override void w(String name, long v) { super.w(name, v); p.printf("%s %s %x%n", unit, name, v); }
    }

    // ================================================================ reflection (cached)
    private static final Map<String, Field> FIELDS = new HashMap<>();
    static Field fld(Class<?> k, String n) {
        String key = k.getName() + "#" + n;
        Field f = FIELDS.get(key);
        if (f != null) return f;
        for (Class<?> c = k; c != null; c = c.getSuperclass())
            try { f = c.getDeclaredField(n); f.setAccessible(true); FIELDS.put(key, f); return f; } catch (NoSuchFieldException e) { }
        throw new IllegalStateException("no field " + n + " in " + k.getName());
    }
    static Object o(Object x, String n) { try { return fld(x.getClass(), n).get(x); } catch (IllegalAccessException e) { throw new RuntimeException(e); } }
    static long l(Object x, String n) {
        try {
            Field f = fld(x.getClass(), n); Class<?> t = f.getType();
            if (t == int.class) return f.getInt(x); if (t == long.class) return f.getLong(x); if (t == boolean.class) return f.getBoolean(x) ? 1 : 0;
            throw new IllegalStateException(n + ": " + t);
        } catch (IllegalAccessException e) { throw new RuntimeException(e); }
    }
    static int i(Object x, String n) { return (int) l(x, n); }
    static boolean z(Object x, String n) { return l(x, n) != 0; }

    // ================================================================ buses (WordBuses.java), read without enter()
    /** A delay line: entries in order, each (arrival, a, b, c); the pending put last. */
    static void delay(Out o, String nm, WordDelayLine d) {
        long[] va = (long[]) o(d, "va"), vb = (long[]) o(d, "vb"), vc = (long[]) o(d, "vc"), at = (long[]) o(d, "at");
        int head = i(d, "head"), count = i(d, "count"); boolean pend = z(d, "pendPut");
        o.w(nm + ".n", count + (pend ? 1 : 0));
        for (int k = 0; k < count; k++) { int j = (head + k) % va.length; o.w(nm + ".at", at[j]); o.w(nm + ".a", va[j]); o.w(nm + ".b", vb[j]); o.w(nm + ".c", vc[j]); }
        if (pend) { o.w(nm + ".at", l(d, "pendAt")); o.w(nm + ".a", l(d, "pa")); o.w(nm + ".b", l(d, "pb")); o.w(nm + ".c", l(d, "pc")); }
    }
    /** A FIFO: its entries; then the put made at c (if any), not yet judged (the framework judges it at c + 1). A put made
     *  before c is judged here by WordFIFO.commit's rule (full at the start of its cycle: count + takes >= capacity). */
    static void fifo(Out o, String nm, WordFIFO f, long cyc) {
        long[] va = (long[]) o(f, "va"), vb = (long[]) o(f, "vb"), vc = (long[]) o(f, "vc");
        int head = i(f, "head"), count = i(f, "count"), cap = f.capacity; boolean pend = z(f, "pendPut");
        long lastT = l(f, "lastT"); int takes = i(f, "takes");
        boolean settle = pend && lastT < cyc, keep = settle && !(count == cap || WordFIFO.countBeforePop && count + takes >= cap);
        o.w(nm + ".n", count + (keep ? 1 : 0));
        for (int k = 0; k < count; k++) { int j = (head + k) % cap; o.w(nm + ".a", va[j]); o.w(nm + ".b", vb[j]); o.w(nm + ".c", vc[j]); }
        if (keep) { o.w(nm + ".a", l(f, "pa")); o.w(nm + ".b", l(f, "pb")); o.w(nm + ".c", l(f, "pc")); }
        boolean now = pend && lastT == cyc;
        o.w(nm + ".put", now ? 1 : 0);
        if (now) { o.w(nm + ".pa", l(f, "pa")); o.w(nm + ".pb", l(f, "pb")); o.w(nm + ".pc", l(f, "pc")); }
    }
    /** A holding register: occupied and its word; the put made at c (judged at c + 1: refused if still occupied). A put
     *  made before c is settled here (WordHold.commit: refused if occupied, else it occupies). */
    static void hold(Out o, String nm, WordHold hd, long cyc) {
        boolean occ = z(hd, "occ"), pend = z(hd, "pendPut"); long lastT = l(hd, "lastT");
        long a = l(hd, "a"), b = l(hd, "b"), c = l(hd, "c");
        if (pend && lastT < cyc && !occ) { occ = true; a = l(hd, "pa"); b = l(hd, "pb"); c = l(hd, "pc"); }
        o.w(nm + ".occ", occ ? 1 : 0);
        if (occ) { o.w(nm + ".a", a); o.w(nm + ".b", b); o.w(nm + ".c", c); }
        boolean now = pend && lastT == cyc;
        o.w(nm + ".put", now ? 1 : 0);
        if (now) { o.w(nm + ".pa", l(hd, "pa")); o.w(nm + ".pb", l(hd, "pb")); o.w(nm + ".pc", l(hd, "pc")); }
    }
    static void bus(Out o, String nm, Object b, long cyc) {
        if (b == null) { o.w(nm + ".none", 0); return; }
        if (b instanceof WordDelayLine d) delay(o, nm, d);
        else if (b instanceof WordFIFO f) fifo(o, nm, f, cyc);
        else if (b instanceof WordHold h) hold(o, nm, h, cyc);
        else throw new IllegalStateException(nm + ": " + b.getClass());
    }

    // ================================================================ memories
    /** An SRAMMacro's words with its pending (uncommitted) port writes overlaid. */
    static int[] words(SRAMMacro m) {
        int[] w = m.w;
        if (i(m, "pendingPorts") == 0) return w;
        w = w.clone();
        boolean[] pend = (boolean[]) o(m, "pend");
        int[] pAddr = (int[]) o(m, "pAddr"), pWidth = (int[]) o(m, "pWidth"), pMask = (int[]) o(m, "pMask"),
            p0 = (int[]) o(m, "pW0"), p1 = (int[]) o(m, "pW1"), p2 = (int[]) o(m, "pW2"), p3 = (int[]) o(m, "pW3");
        for (int p = 0; p < pend.length; p++) {
            if (!pend[p]) continue;
            int a = pAddr[p];
            switch (pWidth[p]) {
                case 1 -> w[a] = (w[a] & ~pMask[p]) | (p0[p] & pMask[p]);
                case 2 -> { w[a] = p0[p]; w[a + 1] = p1[p]; }
                default -> { w[a] = p0[p]; w[a + 1] = p1[p]; w[a + 2] = p2[p]; w[a + 3] = p3[p]; }
            }
        }
        return w;
    }
    static long setHash(int region, int[] w, int stride, int offset) {
        long s = 0;
        for (int k = 0; k < w.length; k++) if (w[k] != 0) s += term(region, (long) k * stride + offset, w[k]);
        return s;
    }
    /** A controller memory (four banks, word w in bank w & 3 at row w >> 2): indexed by word. */
    static long banked(int region, BankedMemory m) { long s = 0; for (int b = 0; b < 4; b++) s += setHash(region, words(m.bank[b]), 4, b); return s; }
    /** The big SRAM's contents, indexed by chunk (BigSRAM stores writes immediately: no overlay). */
    static long sram(BigSRAM s) {
        int[][] data = (int[][]) o(s, "data"); long h = 0;
        for (int a = 0; a < data.length; a++) if (data[a] != null) h += setHash(R_SRAM, data[a], 1, a << 17);
        return h;
    }
    static final int R_DMEM = 1, R_PMEM = 2, R_SRAM = 3, R_STREAM_SRC = 4, R_STREAM_DEST = 5, R_STREAM_COUNT = 6, R_GROUP = 7, R_PTR = 8, R_HIST = 9;

    /** An RmwPipe's write-backs not yet done: (due cycle, index, value), by due cycle. */
    static void pipe(Out o, String nm, RmwPipe p, long cyc) {
        boolean[] v = (boolean[]) o(p, "wbValid"); int[] idx = (int[]) o(p, "wbIdx"), val = (int[]) o(p, "wbVal"); long[] at = (long[]) o(p, "wbAt");
        List<long[]> e = new ArrayList<>();
        for (int k = 0; k < v.length; k++) if (v[k]) e.add(new long[] { at[k] + RmwPipe.WB_DELAY, idx[k], val[k] });
        e.sort((x, y) -> Long.compare(x[0], y[0]));
        o.w(nm + ".n", e.size());
        for (long[] x : e) { o.w(nm + ".due", x[0]); o.w(nm + ".idx", x[1]); o.w(nm + ".val", x[2] & 0xFFFFFFFFL); }
    }

    // ================================================================ the units of a tile
    static final String[] TILE_UNITS = { "core", "ctl", "cmem", "dir", "router", "inj", "srq", "sram", "sram.data", "we", "ag", "ts", "hist", "gather", "rdport", "req" };

    static void unit(Out o, Chip ch, Tile p, String u, long cyc) {
        switch (u) {
            case "core" -> core(o, ch, p, cyc);
            case "ctl" -> ctl(o, p.ctl, cyc);
            case "cmem" -> { o.w("dmem", banked(R_DMEM, p.cmem.dmem)); o.w("pmem", banked(R_PMEM, p.cmem.imem)); bus(o, "portB", p.portB, cyc); }
            case "dir" -> dir(o, p, cyc);
            case "router" -> router(o, p, cyc);
            case "inj" -> { bus(o, "readReturn", p.readReturn, cyc); bus(o, "send", p.sendHold, cyc); }
            case "srq" -> srq(o, p, cyc);
            case "sram" -> sramQ(o, p, cyc);
            case "sram.data" -> o.w("data", sram(p.sram));
            case "we" -> we(o, p, cyc);
            case "ag" -> ag(o, p, cyc);
            case "ts" -> ts(o, p, cyc);
            case "hist" -> { o.w("HIST", setHash(R_HIST, words(p.hist.mem), 1, 0)); pipe(o, "wb", p.hist.pipe, cyc); bus(o, "req", p.weHistReq, cyc); bus(o, "q", p.hist.q, cyc); }
            case "gather" -> gather(o, p, cyc);
            case "rdport" -> { long st = l(p.readPort, "stores"); o.w("inUse", st - l(p.readPort, "injected")); o.w("outstanding", st - l(p.readPort, "accepted")); }
            case "req" -> req(o, p, cyc);
            default -> throw new IllegalArgumentException("unit " + u);
        }
    }

    /** The controller: pc, x1-x31, the CSRs, mcycle (a sleeping tile's caught up, as Chip.catchUp would), the run state. */
    static void core(Out o, Chip ch, Tile p, long cyc) {
        Controller k = p.core;
        o.w("pc", k.pc & 0xFFFFFFFFL);
        for (int r = 1; r < 32; r++) o.w("x" + r, k.x[r] & 0xFFFFFFFFL);
        o.w("mstatus", k.mstatus & 0xFFFFFFFFL); o.w("mie", k.mie & 0xFFFFFFFFL); o.w("mtvec", k.mtvec & 0xFFFFFFFFL); o.w("mscratch", k.mscratch & 0xFFFFFFFFL);
        o.w("mepc", k.mepc & 0xFFFFFFFFL); o.w("mcause", k.mcause & 0xFFFFFFFFL); o.w("mtval", k.mtval & 0xFFFFFFFFL);
        long lag = 0;
        if (ch.skip && k.enabled && ((boolean[]) o(ch, "asleep"))[p.idx]) lag = cyc - ((long[]) o(ch, "countedTo"))[p.idx];
        o.w("mcycle", k.mcycle + lag);
        o.w("halt", k.halt); o.w("inWfi", k.inWfi ? 1 : 0); o.w("inHandler", k.inHandler ? 1 : 0);
        o.w("stall", l(k, "stall"));
        o.w("sendRetry", l(k, "pendingT") == cyc ? 1 : 0);                 // a send store awaiting its acceptance at c + 1
        boolean lw = l(k, "loadWaitT") == cyc;                              // a register-block load's first cycle done at c
        o.w("regLoad", lw ? 1 : 0); if (lw) o.w("regLoadPc", l(k, "loadWaitPc") & 0xFFFFFFFFL);
    }

    /** The register block (TileControl): every register and the event/qualifier state; the GO line's pending arrivals. */
    static void ctl(Out o, TileControl c, long cyc) {
        o.w("routerHdr", c.routerHdr & 0xFFFFFFFFL); o.w("routerAddr", c.routerAddr & 0xFFFFFFFFL); o.w("dmaConfig", c.dmaConfig & 0xFFFFFFFFL);
        o.w("recordBase", c.recordBase & 0xFFFFFFFFL); o.w("rdSetup", c.rdSetup & 0xFFFFFFFFL);
        for (int k = 0; k < 3; k++) o.w("setW" + k, c.setW[k] & 0xFFFFFFFFL);
        o.w("done", c.done ? 1 : 0); o.w("landed", c.landed ? 1 : 0); o.w("gate", c.gate ? 1 : 0); o.w("readyLevel", l(c, "readyLevel"));
        o.w("events", c.events & 0xFFFFFFFFL); o.w("mask", c.wakeMask & 0xFFFFFFFFL); o.w("required", c.wakeRequired & 0xFFFFFFFFL);
        o.w("qAll", l(c, "qAll")); boolean[] q1 = (boolean[]) o(c, "qOne"); for (int k = 1; k < q1.length; k++) o.w("qOne" + k, q1[k] ? 1 : 0);
        o.w("condAllPrev", l(c, "condAllPrev")); boolean[] cp = (boolean[]) o(c, "condOnePrev"); for (int k = 1; k < cp.length; k++) o.w("condOnePrev" + k, cp[k] ? 1 : 0);
        o.w("rdDonePrev", l(c, "rdDonePrev")); o.w("requestBusyPrev", l(c, "requestBusyPrev"));
        o.w("conditions", c.conditions & 0xFFFFFFFFL); o.w("status", c.status & 0xFFFFFFFFL);
        o.w("simExit", c.simExitAt >= 0 ? 1 : 0); o.w("simExitValue", c.simExitValue & 0xFFFFFFFFL);
        @SuppressWarnings("unchecked") ArrayDeque<Long> goQ = (ArrayDeque<Long>) o(c, "goQ");
        o.w("go.n", goQ.size()); for (long g : goQ) o.w("go.at", g);
    }

    /** The traffic director: the write counters, the K-stage line's pending decrements (due, counter), its input bus. */
    static void dir(Out o, Tile p, long cyc) {
        TrafficDirector d = p.dir;
        for (int c = 1; c <= TrafficDirector.N_COUNTERS; c++) o.w("remaining" + c, d.remaining[c]);
        int[] ctr = (int[]) o(d, "lineCtr"); long[] at = (long[]) o(d, "lineT");
        List<long[]> e = new ArrayList<>();
        for (int k = 0; k < at.length; k++) if (at[k] > cyc) e.add(new long[] { at[k], ctr[k] });
        e.sort((x, y) -> Long.compare(x[0], y[0]));
        o.w("line.n", e.size()); for (long[] x : e) { o.w("line.due", x[0]); o.w("line.ctr", x[1]); }
        bus(o, "in", p.ejectBus, cyc);
    }

    /** The router: the round-robin pointers, every output FIFO (outputs 0-5, feeders in Router.feeders order), and its
     *  inputs: the four incoming links, the injection bus, the from-controller holding register (tiles 0-2). */
    static void router(Out o, Tile p, long cyc) {
        Router r = p.router;
        int[] rr = (int[]) o(r, "rr"); for (int k = 0; k < rr.length; k++) o.w("rr" + k, rr[k]);
        for (int out = 0; out < r.q.length; out++) for (int f = 0; f < r.q[out].length; f++) fifo(o, "q" + out + "." + f, r.q[out][f], cyc);
        for (int d = 0; d < 4; d++) bus(o, "link" + d, r.linkIn[d], cyc);
        bus(o, "inj", p.injBus, cyc);
        if (r.fromCnc != null) bus(o, "fromCnc", r.fromCnc, cyc);
    }

    /** The SRAM read queue: the token free list (its order decides which token a read takes, and two read-port returns
     *  ready in one cycle go out by token), each held read, the in-order return queues, the read port's ready set, the
     *  tokens held (as at the start of c + 1); its inputs (the three clients, the 16 area returns). */
    @SuppressWarnings("unchecked")
    static void srq(Out o, Tile p, long cyc) {
        SRAMReadQueue s = p.srq;
        ArrayDeque<Integer> free = (ArrayDeque<Integer>) o(s, "free");
        o.w("free.n", free.size()); for (int k : free) o.w("free", k);
        long[] reqA = (long[]) o(s, "reqA"), reqB = (long[]) o(s, "reqB"), issueT = (long[]) o(s, "issueT"), readyT = (long[]) o(s, "readyT"), lo = (long[]) o(s, "lo"), hi = (long[]) o(s, "hi");
        int[] parts = (int[]) o(s, "partsLeft"), cl = (int[]) o(s, "cl"); boolean[] zero = (boolean[]) o(s, "zero");
        boolean[] isFree = new boolean[reqA.length]; for (int k : free) isFree[k] = true;
        for (int tok = 0; tok < reqA.length; tok++) {
            if (isFree[tok]) continue;
            o.w("tok", tok); o.w("reqA", reqA[tok]); o.w("reqB", reqB[tok]); o.w("issueT", issueT[tok]); o.w("readyT", readyT[tok]);
            o.w("partsLeft", parts[tok]); o.w("client", cl[tok]); o.w("lo", lo[tok]); o.w("hi", hi[tok]); o.w("zero", zero[tok] ? 1 : 0);
        }
        ArrayDeque<Integer>[] order = (ArrayDeque<Integer>[]) o(s, "order");
        for (int c = 0; c < 2; c++) { o.w("order" + c + ".n", order[c].size()); for (int k : order[c]) o.w("order" + c, k); }
        java.util.TreeSet<long[]> rp = (java.util.TreeSet<long[]>) o(s, "rpReady");
        o.w("rpReady.n", rp.size()); for (long[] x : rp) { o.w("rpReady.t", x[0]); o.w("rpReady.tok", x[1]); }
        o.w("held", l(s, "reads") + l(s, "loopHeld") - l(s, "freesPending"));
        for (int c = 0; c < 3; c++) bus(o, "client" + c, s.client[c], cyc);
        for (int a = 0; a < BigSRAM.AREAS; a++) bus(o, "ret" + a, s.fromSram[a], cyc);
    }

    /** The big SRAM's area queues (per area, in order: write, row, chunk mask, token, part, the four data chunks) and its
     *  two holding registers. The contents are the separate unit sram.data. */
    static void sramQ(Out o, Tile p, long cyc) {
        BigSRAM s = p.sram;
        boolean[][] qw = (boolean[][]) o(s, "qWrite"); int[][] row = (int[][]) o(s, "qRow"), mask = (int[][]) o(s, "qMask"), tok = (int[][]) o(s, "qToken"), part = (int[][]) o(s, "qPart");
        int[][][] data = (int[][][]) o(s, "qData"); int[] head = (int[]) o(s, "qHead"), count = (int[]) o(s, "qCount");
        for (int a = 0; a < BigSRAM.AREAS; a++) {
            o.w("area" + a + ".n", count[a]);
            for (int k = 0; k < count[a]; k++) {
                int j = (head[a] + k) % qw[a].length;
                o.w("w", qw[a][j] ? 1 : 0); o.w("row", row[a][j]); o.w("mask", mask[a][j]);
                if (qw[a][j]) for (int c = 0; c < 4; c++) o.w("d" + c, data[a][j][c] & 0xFFFFFFFFL);
                else { o.w("tok", tok[a][j]); o.w("part", part[a][j]); }
            }
        }
        bus(o, "rd", p.sramRd, cyc); bus(o, "wr", p.sramWr, cyc);
    }

    /** The write engine: its configuration, the pointer stage (in order: the packet, PTR's value if returned), the order
     *  of entries awaiting their PTR return; its inputs (we-in, the PTR returns). */
    @SuppressWarnings("unchecked")
    static void we(Out o, Tile p, long cyc) {
        WriteEngine w = p.writer;
        o.w("bucketDigit", w.bucketDigit); o.w("histDigit", w.histDigit); o.w("augmentBase", w.augmentBase);
        long[] d = (long[]) o(w, "stD"), h = (long[]) o(w, "stH"), c = (long[]) o(w, "stC"); int[] ptr = (int[]) o(w, "stPtr"); boolean[] have = (boolean[]) o(w, "stHave");
        int head = i(w, "head"), count = i(w, "count"), cap = d.length;
        o.w("stage.n", count);
        for (int k = 0; k < count; k++) { int j = (head + k) % cap; o.w("d", d[j]); o.w("h", h[j]); o.w("c", c[j]); o.w("have", have[j] ? 1 : 0); if (have[j]) o.w("ptr", ptr[j]); }
        ArrayDeque<Integer> aw = (ArrayDeque<Integer>) o(w, "awaiting");
        o.w("awaiting.n", aw.size()); for (int j : aw) o.w("awaiting", ((j - head) % cap + cap) % cap);   // as a stage position
        bus(o, "in", p.weIn, cyc); bus(o, "tsRet", p.weTsRet, cyc);
    }

    /** The address generator: the run's configuration and status, the read-ahead queues' entries and reads in flight,
     *  the STREAM reader's position, the stream-run and group-run state machines; its two table-return lines. */
    static void ag(Out o, Tile p, long cyc) {
        AddressGenerator a = p.ag;
        for (String f : new String[] { "kind", "runStart", "runCount", "pStart", "running", "done", "leadIn", "finishedIssuing", "gatherChecked",
                "sqOut", "cqOut", "streamDiscard", "groupDiscard", "sReads", "cReads", "rg", "rr", "rv",
                "slRun", "cur", "cSrc", "cDest", "cCount", "cJ", "cD", "cMode", "cSize", "cGap", "cCtr", "gapLeft",
                "g", "r", "v", "gR", "gT", "gGap", "groupS", "rotSched", "inRot", "stalledSinceGroup" }) o.w(f, l(a, f));
        long[] sq = (long[]) o(a, "sq"), sq1 = (long[]) o(a, "sq1"), cq = (long[]) o(a, "cq"); int n = sq.length;
        int sb = i(a, "sqBuf"), sh = i(a, "sqHead"), cb = i(a, "cqBuf"), chd = i(a, "cqHead");
        o.w("sq.n", sb); for (int k = 0; k < sb; k++) { int j = (sh + k) % n; o.w("sq.e0", sq[j]); o.w("sq.e1", sq1[j]); }
        o.w("cq.n", cb); for (int k = 0; k < cb; k++) o.w("cq.e", cq[(chd + k) % n]);
        bus(o, "streamRet", p.agStreamRet, cyc); bus(o, "groupRet", p.agGroupRet, cyc);
    }

    /** The table store: STREAM_SRC, STREAM_DEST, STREAM_COUNT, GROUP, PTR (each a memory), PTR's pending write-backs; its
     *  request lines and request FIFOs. */
    static void ts(Out o, Tile p, long cyc) {
        TableStore s = p.ts;
        o.w("STREAM_SRC", setHash(R_STREAM_SRC, words(s.streamSrc), 1, 0)); o.w("STREAM_DEST", setHash(R_STREAM_DEST, words(s.streamDest), 1, 0));
        o.w("STREAM_COUNT", setHash(R_STREAM_COUNT, words(s.streamCount), 1, 0)); o.w("GROUP", setHash(R_GROUP, words(s.group), 1, 0));
        o.w("PTR", setHash(R_PTR, words(s.ptr), 1, 0));
        pipe(o, "wb", s.pipe, cyc);
        bus(o, "reqStream", p.agStreamReq, cyc); bus(o, "reqGroup", p.agGroupReq, cyc); bus(o, "reqWe", p.weTsReq, cyc);
        bus(o, "qStream", s.qStream, cyc); bus(o, "qGroup", s.qGroup, cyc); bus(o, "qWe", s.qWe, cyc);
    }

    static void gather(Out o, Tile p, long cyc) {
        GatherEngine g = p.gather;
        o.w("k", g.k); o.w("base", g.base);
        for (String f : new String[] { "left", "ord", "i", "ctr", "wide" }) o.w(f, l(g, f));
        bus(o, "in", p.gatherIn, cyc);
    }

    /** The request port: the FIFO's requests (four words and the store cycle), the head's beats sent, W0-W2. */
    @SuppressWarnings("unchecked")
    static void req(Out o, Tile p, long cyc) {
        RequestPort r = p.req;
        o.w("fifo.n", r.fifo.size()); for (long[] e : r.fifo) for (long x : e) o.w("fifo", x);
        o.w("beatsSent", r.beatsSent); for (int k = 0; k < 3; k++) o.w("setW" + k, r.setW[k] & 0xFFFFFFFFL);
    }

    // ================================================================ chip-level units
    static final String[] CHIP_UNITS = { "cnc", "cc1p", "reqbus", "ep" };
    @SuppressWarnings("unchecked")
    static void chipUnit(Out o, Chip ch, String u, long cyc) {
        switch (u) {
            case "cnc" -> {
                CnC c = ch.cc;
                o.w("steps", ((ArrayDeque<?>) o(c, "steps")).size()); o.w("lastGoAt", c.lastGoAt); o.w("lastCommandWord", c.lastCommandWord & 0xFFFFFFFFL);
                long par = 0; for (int k = 0; k < 64; k++) par |= (long) (c.commandsTo[k] & 1) << k; o.w("parity", par);
                ArrayDeque<long[]> rxq = (ArrayDeque<long[]>) o(c, "rxq"); o.w("rxq.n", rxq.size()); for (long[] e : rxq) for (long x : e) o.w("rxq", x);
                o.w("errorBit", l(c, "errorBit")); o.w("errLineSeen", l(c, "errLineSeen")); o.w("errLineLast", l(c, "errLineLast"));
                ArrayDeque<long[]> el = (ArrayDeque<long[]>) o(c, "errLine"); o.w("errLine.n", el.size()); for (long[] e : el) for (long x : e) o.w("errLine", x);
                bus(o, "rx", ch.cnc.rx, cyc);
            }
            case "cc1p" -> {                                    // READY: per tile, visible (1), pending (its arrival), none (0)
                Cnc1Peripheral c = ch.cc1p; long[] at = (long[]) o(c, "readyAt"); long clr = l(c, "clearedAt");
                for (int k = 0; k < 64; k++) o.w("ready" + k, at[k] == Long.MIN_VALUE || at[k] < clr ? 0 : at[k] <= cyc ? 1 : at[k]);
                o.w("expect", c.expect);
                ArrayDeque<long[]> ec = (ArrayDeque<long[]>) o(c, "errChanges"); o.w("errChanges.n", ec.size()); for (long[] e : ec) for (long x : e) o.w("errChanges", x);
            }
            case "reqbus" -> reqbus(o, ch.reqBus);
            case "ep" -> { bus(o, "memsub.rx", ch.memsub.rx, cyc); bus(o, "cc3.rx", ch.cc3.rx, cyc); }
            default -> throw new IllegalArgumentException("unit " + u);
        }
    }
    @SuppressWarnings("unchecked")
    static void reqbus(Out o, RequestBus b) {
        for (int r = 0; r < 8; r++) for (int c = 0; c < 8; c++) buf(o, "row" + r + "." + c, b.rowIn[r][c]);
        for (int k = 0; k < b.rootIn.length; k++) buf(o, "root" + k, b.rootIn[k]);
        int[] rc = (int[]) o(b, "rowCur"), rr = (int[]) o(b, "rowRr"); for (int r = 0; r < 8; r++) { o.w("rowCur", rc[r]); o.w("rowRr", rr[r]); }
        o.w("rootCur", l(b, "rootCur")); o.w("rootRr", l(b, "rootRr")); o.w("rootDropping", l(b, "rootDropping"));
        for (int k = 0; k < 8; k++) o.w("credits", b.credits[k]);
        for (int k = 0; k < RequestBus.TARGETS; k++) { o.w("queue.n", b.queue[k].size()); for (long[] e : b.queue[k]) for (long x : e) o.w("queue", x); }
        ArrayDeque<long[]> cr = (ArrayDeque<long[]>) o(b, "creditReturns"); o.w("creditReturns.n", cr.size()); for (long[] e : cr) for (long x : e) o.w("creditReturns", x);
        ArrayDeque<Object[]> tr = (ArrayDeque<Object[]>) o(b, "toReceiver"); o.w("toReceiver.n", tr.size());
        for (Object[] e : tr) { o.w("toReceiver.at", (long) e[0]); req(o, (RequestBus.Req) e[1]); }
    }
    static void buf(Out o, String nm, RequestBus.Buf b) {
        o.w(nm + ".occ", b.occ); o.w(nm + ".reqs", b.reqs.size()); for (RequestBus.Req q : b.reqs) req(o, q);
        o.w(nm + ".arrivals", b.arrivals.size()); for (long a : b.arrivals) o.w(nm + ".arrival", a);
    }
    static void req(Out o, RequestBus.Req q) { for (long x : q.w) o.w("w", x); o.w("storeT", q.storeT); o.w("src", q.srcTile); o.w("arrived", q.arrived); o.w("sent", q.sent); }

    // ================================================================ one fingerprint
    /** Every unit's hash at the end of cycle c (= ch.cycle): lines "c unit hash", then "c chip hash" over them all. */
    static void write(PrintWriter w, Chip ch, boolean withData, boolean[] tiles) {
        long cyc = ch.cycle, all = 0x6A09E667F3BCC908L; int n = 0;
        for (Tile p : ch.tiles) for (String u : TILE_UNITS) {
            if (!tiles[p.idx] || !withData && u.equals("sram.data")) continue;
            Out o = new Out(); unit(o, ch, p, u, cyc); long h = o.done();
            w.printf("%d t%d.%s %016x%n", cyc, p.idx, u, h); all = fmix(all ^ h); n++;
        }
        for (String u : CHIP_UNITS) { Out o = new Out(); chipUnit(o, ch, u, cyc); long h = o.done(); w.printf("%d %s %016x%n", cyc, u, h); all = fmix(all ^ h); n++; }
        w.printf("%d chip %016x%n", cyc, fmix(all ^ n));
    }
    /** One unit's canonical stream, named word by word (to compare against the C++ side's dump). */
    static void dump(PrintWriter w, Chip ch, String unit) {
        long cyc = ch.cycle;
        w.println("# fpdump cycle " + cyc + " unit " + unit);
        if (unit.startsWith("t")) { int dot = unit.indexOf('.'); Tile p = ch.tiles[Integer.parseInt(unit.substring(1, dot))]; unit(new Dump(w, unit), ch, p, unit.substring(dot + 1), cyc); }
        else chipUnit(new Dump(w, unit), ch, unit, cyc);
    }
}
