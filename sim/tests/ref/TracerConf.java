import java.io.BufferedWriter;
import java.io.FileWriter;
import java.io.PrintWriter;
import java.lang.reflect.Field;
import java.util.HashMap;
import java.util.Map;
import java.util.TreeMap;

/**
 * TracerConf (WORKER14): ref's Tracer driven through its Peripheral interface, every call and its result recorded, with
 * the Tracer's state observed after every cycle, so the C++ Tracer (tests/test_tracer.cpp) can replay the calls and must
 * reproduce every result and every observation. Compiled into ref's default package (D5); nothing in sim/ref changes.
 *
 * <h3>Cases (this file, milestone: W14 step 2)</h3>
 * {@code java TracerConf cases LOG}: TracerTest's ten cases, run by copies of TracerTest and TracerRig that tracerconf.sh
 * makes (TracerConfCases, TracerConfRig): the only change is that the rig's Tracer is a {@link Rec}, which forwards every
 * call to the real Tracer and records it. TracerTest's own assertions still run (their summary goes to stdout).
 *
 * <h3>Seeded stimulus</h3>
 * {@code java TracerConf gen LOG SEED [mix|wb]}: a generated guest on TracerConfRig ({@link Gen}): loops, calls and
 * returns through the map, loads and stores of every width, compressed instructions (fetches straddling lines), MUL,
 * DIV with its advice, a CSR and an inline template, random window lengths, compression and DMA service policies, mid-run
 * window reads, a second segment, and some seeds ending on an error that stops the Tracer; or ({@code wb}) stores to
 * 136-192 pages a page apart, which fill the memory system's write buffer far beyond its depth of 4.
 *
 * <h3>The log</h3>
 * Calls are grouped by cycle: a cycle is the calls the rig makes up to and including a runCycle(t), each written as a
 * token with its cycle offset from t; consecutive cycles with the same tokens, and the same observation apart from the
 * counters, form one group. Lines (id: the rig's Tracer):
 * <pre>
 *  N id me icacheLines           a Tracer (at its first call), with a MemorySystem made cold by each word-0 write
 *  F id k first nextFree         store dump k: then a P (data) or Z (all zero) line for each page, in address order
 *  P id addr w0 .. w511 / Z id addr
 *  c id t0 n tok;tok;...         n cycles from t0 (cycle t0 + i), each making the calls in order
 *  o id f0 f1 ...                the observation after the group's last cycle (OBS below)
 * </pre>
 * Tokens (64-bit values hex; @d: the call's cycle is t + d, omitted when 0; '!' at the end: the call raised
 * UndefinedBehavior, C++ checks it is FATAL in a child):
 * <pre>
 *  st                    runCycle            u            run
 *  a,src,off,size,lo,hi  accept              ww,w,v=r     writeWord        wr,w=v   word (v '-' for null)
 *  rn=r running   er=r errors   sy sync   ev=v events().value   co,bits events().consume (no cycle)   re=r rowsEnded
 *  wb=n writeBackDirtyUntimed (no cycle)   os,q,n outStart   or,q=r outReady   ot,q=lo,hi outTake
 *  a token ending in '%': the call raised IllegalStateException, one of ref's own fatal checks (FATAL here too)
 *  F,k   the store as dump k (C++: the pages it has must be equal, the new ones are added): at a word-0 packet after
 *        the harness allocated pages     H=h   else, at a word-0 packet: the store's hash
 * </pre>
 */
public final class TracerConf {
    static PrintWriter out;

    public static void main(String[] a) throws Exception {
        out = new PrintWriter(new BufferedWriter(new FileWriter(a[1]), 1 << 20));
        out.println("# tracerconf v1: ref's side, TracerConf " + a[0]);
        switch (a[0]) {
            case "cases" -> TracerConfCases.main(new String[0]);   // ends with System.exit: see exit()
            case "gen" -> { Gen.run(Long.parseLong(a[2]), a.length > 3 ? a[3] : "mix"); exit(0); }
            default -> throw new IllegalArgumentException(a[0]);
        }
    }
    /** TracerConfCases ends with System.exit(T.done(..)): tracerconf.sh rewrites it to this, so the log is flushed. */
    static void exit(int status) { Rec.flushAll(); out.println("# end " + status); out.close(); System.exit(status); }

    static String hx(long v) { return Long.toHexString(v); }

    // ================================================================ reflection (the observation and the store)
    static Field f(Class<?> c, String n) {
        try { Field x = c.getDeclaredField(n); x.setAccessible(true); return x; }
        catch (NoSuchFieldException e) { throw new IllegalStateException(e); }
    }
    static final String[] PRIV = { "errors", "running", "stopped", "rowsDone", "busyUntil", "waitUntil", "statUntil", "presented",
            "expansionOffset", "latch", "rqOcc", "rqHalf", "eqOcc", "expDiscard", "q0ptr", "xferLeft" };
    static final Field[] privF = new Field[PRIV.length];
    static final Field fsPages = f(FileStore.class, "pages"), fsNext = f(FileStore.class, "nextFree");
    static { for (int i = 0; i < PRIV.length; i++) privF[i] = f(Tracer.class, PRIV[i]); }
    static long num(Object o) {
        if (o instanceof Boolean b) return b ? 1 : 0;
        return ((Number) o).longValue();
    }

    /** OBS: the private fields above in order, then codeN, logN, rows, instrs, iMiss, dMiss, writebacks, lruHits,
     *  lruMisses, probeReads, stat[0..7], stalls[0..6], oq[1..3].occ and .pushed, errorLog.size(), violations.size(),
     *  the record's hash (h = h * 31 + x over its 136 longs, from 17), events().count(b) for b 0..10. */
    static String observe(Tracer tr) {
        StringBuilder sb = new StringBuilder();
        try { for (Field x : privF) sb.append(' ').append(hx(num(x.get(tr)))); }
        catch (IllegalAccessException e) { throw new IllegalStateException(e); }
        for (long v : new long[] { tr.codeN, tr.logN, tr.rows, tr.instrs, tr.iMiss, tr.dMiss, tr.writebacks, tr.lruHits, tr.lruMisses, tr.probeReads })
            sb.append(' ').append(hx(v));
        for (int v : tr.stat) sb.append(' ').append(hx(v & 0xFFFFFFFFL));
        for (long v : tr.stalls) sb.append(' ').append(hx(v));
        for (int q = 1; q <= 3; q++) sb.append(' ').append(hx(tr.oq[q].occ)).append(' ').append(hx(tr.oq[q].pushed));
        sb.append(' ').append(hx(tr.errorLog.size())).append(' ').append(hx(tr.violations.size()));
        long h = 17; for (long x : tr.rec) h = h * 31 + x;
        sb.append(' ').append(hx(h));
        for (int b = 0; b <= 10; b++) sb.append(' ').append(tr.events().count(b));
        return sb.toString();
    }

    @SuppressWarnings("unchecked")
    static Map<Integer, long[]> pages(FileStore fs) {
        try { return (Map<Integer, long[]>) fsPages.get(fs); } catch (IllegalAccessException e) { throw new IllegalStateException(e); }
    }
    static int nextFree(FileStore fs) { try { return fsNext.getInt(fs); } catch (IllegalAccessException e) { throw new IllegalStateException(e); } }
    /** The store's hash: pages in address order, h = h * 31 + address, then h = h * 31 + each word; from 17. */
    static long storeHash(FileStore fs) {
        long h = 17;
        for (var e : new TreeMap<>(pages(fs)).entrySet()) { h = h * 31 + e.getKey(); for (long x : e.getValue()) h = h * 31 + x; }
        return h;
    }


    // ================================================================ seeded stimulus
    static final class Gen {
        static final long CODE = 0x80000000L, DATA = 0x80001000L;
        final java.util.Random r;
        final TracerConfRig g = new TracerConfRig();
        int wbMax;                                                    // the write buffer's most lines queued (statistics)
        static final Field wbCount = f(MemorySystem.class, "wbCount");
        Gen(long seed) throws Exception { r = new java.util.Random(seed); }

        // ---- a structured program: items assembled in two passes (labels resolved after layout)
        static final class Item { int kind; int word; int half; int label = -1; int rd, rs1; long a, b; }  // kind 0 word, 1 half, 2 bne/beq to label, 3 jal to label
        final java.util.List<Item> items = new java.util.ArrayList<>();
        final java.util.List<Integer> labelAt = new java.util.ArrayList<>();
        final java.util.List<Long> advice = new java.util.ArrayList<>();
        int newLabel() { labelAt.add(-1); return labelAt.size() - 1; }
        void mark(int l) { labelAt.set(l, items.size()); }
        void w(int x) { Item i = new Item(); i.kind = 0; i.word = x; items.add(i); }
        void c(int h) { Item i = new Item(); i.kind = 1; i.half = h & 0xFFFF; items.add(i); }
        void br(int f3, int rs1, int rs2, int label) { Item i = new Item(); i.kind = 2; i.word = f3; i.rs1 = rs1; i.rd = rs2; i.label = label; items.add(i); }
        void jal(int rd, int label) { Item i = new Item(); i.kind = 3; i.rd = rd; i.label = label; items.add(i); }

        TracerConfRig.Prog assemble() {
            long[] pc = new long[items.size() + 1];
            pc[0] = CODE;
            for (int k = 0; k < items.size(); k++) pc[k + 1] = pc[k] + (items.get(k).kind == 1 ? 2 : 4);
            TracerConfRig.Prog p = new TracerConfRig.Prog(CODE);
            for (int k = 0; k < items.size(); k++) {
                Item it = items.get(k);
                switch (it.kind) {
                    case 0 -> p.w(it.word);
                    case 1 -> p.c(it.half);
                    case 2 -> p.w(TracerConfRig.b((int) (pc[labelAt.get(it.label)] - pc[k]), it.rd, it.rs1, it.word));
                    default -> p.w(TracerConfRig.jal(it.rd, (int) (pc[labelAt.get(it.label)] - pc[k])));
                }
            }
            return p;
        }

        // ---- registers: x2 the data base; x1 the return address; x16-x19 loop counters; x20-x22 DIV's; the rest random
        static final int[] FREE = { 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 23, 24, 25, 26, 27, 28, 29, 30, 31 };
        int reg() { return FREE[r.nextInt(FREE.length)]; }
        int creg() { return 8 + r.nextInt(8); }                       // x8-x15, for the compressed forms with 3-bit fields
        boolean usable(int word) {                                    // no advice row in its expansion (only DIV's is supplied)
            RowCore.Decoded d = new RowCore.Decoded();
            RowCore.decode(word, g.tables.table, g.tables.romLo, d);
            if (d.kind == RowCore.K_BAD) return false;
            if (d.kind == RowCore.K_ROM) {
                for (int k = d.start; k < 512; k++) { long lo = g.tables.romLo[k]; if (RowCore.op(lo) == ReplayerAlu.ADVICE) return false; if (RowCore.seqRemaining(lo) == 0) break; }
                return true;
            }
            if (d.kind == RowCore.K_CTRL) {
                int m = g.tables.lookup(word); if (m < 0) return false;
                long[] tw = g.tables.words.get(m);
                for (int k = 0; k < tw.length; k += 2) if (RowCore.op(tw[k]) == ReplayerAlu.ADVICE) return false;
            }
            return true;
        }
        void alu() {
            int rd = reg(), a = reg(), b = reg(), imm = r.nextInt(4096) - 2048, sh = r.nextInt(64);
            int x = switch (r.nextInt(20)) {
                case 0 -> TracerConfRig.addi(rd, a, imm);
                case 1 -> TracerConfRig.add(rd, a, b);
                case 2 -> TracerConfRig.r(0x20, b, a, 0, rd, 0x33);    // sub
                case 3 -> TracerConfRig.r(0, b, a, 4, rd, 0x33);       // xor
                case 4 -> TracerConfRig.r(0, b, a, 6, rd, 0x33);       // or
                case 5 -> TracerConfRig.r(0, b, a, 7, rd, 0x33);       // and
                case 6 -> TracerConfRig.i(sh, a, 1, rd, 0x13);         // slli
                case 7 -> TracerConfRig.i(sh, a, 5, rd, 0x13);         // srli
                case 8 -> TracerConfRig.i(0x400 | sh, a, 5, rd, 0x13); // srai
                case 9 -> TracerConfRig.r(0, b, a, 2, rd, 0x33);       // slt
                case 10 -> TracerConfRig.r(0, b, a, 3, rd, 0x33);      // sltu
                case 11 -> TracerConfRig.lui(rd, r.nextInt(1 << 20));
                case 12 -> TracerConfRig.u(r.nextInt(1 << 20), rd, 0x17); // auipc
                case 13 -> TracerConfRig.mul(rd, a, b);
                case 14 -> TracerConfRig.r(1, b, a, 1, rd, 0x33);      // mulh
                case 15 -> TracerConfRig.r(1, b, a, 3, rd, 0x33);      // mulhu
                case 16 -> TracerConfRig.r(1, b, a, 0, rd, 0x3b);      // mulw
                case 17 -> TracerConfRig.r(0, b, a, 0, rd, 0x3b);      // addw
                case 18 -> TracerConfRig.i(imm, a, 0, rd, 0x1b);       // addiw
                default -> TracerConfRig.i(sh & 31, a, 1, rd, 0x1b);   // slliw
            };
            if (usable(x)) w(x); else w(TracerConfRig.addi(rd, a, imm));
        }
        void compressed() {
            int rd = reg(), rs = reg(), imm = r.nextInt(64) - 32, cr = creg(), cs = creg();
            int h = switch (r.nextInt(7)) {
                case 0 -> ((imm >> 5) & 1) << 12 | rd << 7 | (imm & 31) << 2 | 1;               // c.addi
                case 1 -> 0x4000 | ((imm >> 5) & 1) << 12 | rd << 7 | (imm & 31) << 2 | 1;      // c.li
                case 2 -> 0x8000 | rd << 7 | rs << 2 | 2;                                       // c.mv
                case 3 -> 0x9000 | rd << 7 | rs << 2 | 2;                                       // c.add
                case 4 -> 0x8C01 | (cr - 8) << 7 | (cs - 8) << 2 | (r.nextInt(4) << 5);          // c.sub/xor/or/and
                case 5 -> 0x0002 | rd << 7 | (r.nextInt(31) + 1) << 2;                           // c.slli
                default -> 0x0001;                                                              // c.nop
            };
            c(h);
        }
        /** A load or store of every width, naturally aligned, in the two owned data pages: x2 = DATA, x3 = DATA + 4096. */
        void mem() {
            int wd = r.nextInt(4), size = 1 << wd, base = r.nextBoolean() ? 2 : 3;
            int off = base == 2 ? r.nextInt(2048 / size) * size : (r.nextInt(4096 / size) - 2048 / size) * size;
            int x = r.nextBoolean() ? TracerConfRig.s(off, reg(), base, wd, 0x23)
                                    : TracerConfRig.i(off, base, wd == 3 ? 3 : wd + (r.nextBoolean() ? 4 : 0), reg(), 0x03);
            if (usable(x)) w(x);
        }
        void div() {
            int a = r.nextInt(4096) - 2048, b = r.nextInt(64) == 0 ? 0 : r.nextInt(4096) - 2048;
            w(TracerConfRig.addi(20, 0, a)); w(TracerConfRig.addi(21, 0, b)); w(TracerConfRig.div(22, 20, 21));
            divs.add(new long[] { items.size() - 1, b == 0 ? -1 : a / b });
        }
        final java.util.List<long[]> divs = new java.util.ArrayList<>();   // {item, quotient}
        void block(int depth, int n) {
            for (int k = 0; k < n; k++) {
                int x = r.nextInt(100);
                if (x < 40) alu();
                else if (x < 55) compressed();
                else if (x < 75) mem();
                else if (x < 80) div();
                else if (x < 88 && depth < 2) loop(depth);
                else if (x < 94 && depth < 1 && subs.size() < 4) call();
                else alu();
            }
        }
        void loop(int depth) {
            int cnt = 2 + r.nextInt(depth == 0 ? 12 : 4), creg = 16 + depth, top = newLabel();
            w(TracerConfRig.addi(creg, 0, cnt)); mark(top);
            int i0 = items.size();
            block(depth + 1, 2 + r.nextInt(6));
            loops.add(new int[] { i0, items.size(), cnt });
            w(TracerConfRig.addi(creg, creg, -1)); br(1, creg, 0, top);
        }
        final java.util.List<int[]> loops = new java.util.ArrayList<>();
        final java.util.List<Integer> subs = new java.util.ArrayList<>();
        void call() { int l = newLabel(); subs.add(l); jal(1, l); }

        /** The DIVs' quotients in execution order: the structure walked (loops run their counts, calls their bodies). */
        void walk(int from, int to, java.util.Map<Integer, int[]> loopAt, java.util.Map<Integer, Integer> subStart, java.util.Map<Integer, Integer> subEnd) {
            for (int k = from; k < to; k++) {
                int[] lp = loopAt.get(k);
                if (lp != null) { for (int n = 0; n < lp[2]; n++) walk(lp[0], lp[1], loopAt, subStart, subEnd); k = lp[1] + 1; continue; }
                Item it = items.get(k);
                if (it.kind == 3) { int s0 = labelAt.get(it.label); walk(s0, subEnd.get(it.label), loopAt, subStart, subEnd); continue; }
                for (long[] d : divs) if (d[0] == k) advice.add(d[1]);
            }
        }

        static void run(long seed, String family) throws Exception {
            Gen x = new Gen(seed);
            try { x.go(family); }
            catch (IllegalStateException e) { System.out.println("gen " + family + " " + seed + ": ref stopped: " + e.getMessage()); }
            Tracer t = x.g.tr.x;
            System.out.println("gen " + family + " " + seed + ": " + x.items.size() + " instructions, " + x.advice.size() + " advice, " + t.rows + " rows, "
                    + t.runs + " runs, stopped " + t.stopped() + " " + t.errorLog + ", flags " + t.violations.size() + ", writebacks " + t.writebacks
                    + ", newExp " + t.newExpansions + ", straddles " + t.straddles + ", early evictions " + x.g.ms.earlyEvictions + ", write buffer up to " + x.wbMax);
        }

        void go(String family) throws Exception {
            boolean wb = family.startsWith("wb");
            int dataPages = 2;
            int stride = 4096, n = 0, R = 0;
            if (wb) {
                // wb: within the ring (up to 63 lines queued); wbover: a third pass that overflows ref's 64-line ring
                if (family.equals("wbover")) { n = 136 + r.nextInt(41); R = 3; } else { n = 136 + r.nextInt(57); R = n <= 176 ? 2 : 3; }
                int top = newLabel(), inner = newLabel();
                w(TracerConfRig.addi(5, 0, R)); mark(top);
                w(TracerConfRig.add(6, 2, 0)); w(TracerConfRig.addi(4, 0, n)); mark(inner);
                w(TracerConfRig.sd(1, 6, 0)); w(TracerConfRig.add(6, 6, 3)); w(TracerConfRig.addi(4, 4, -1)); br(1, 4, 0, inner);
                w(TracerConfRig.addi(5, 5, -1)); br(1, 5, 0, top);
                w(TracerConfRig.END);
                dataPages = n;
            } else {
                block(0, 20 + r.nextInt(60));
                if (r.nextInt(4) == 0) w(0x305090f3);                                   // a CSR template (class 2)
                if (r.nextInt(6) == 0) w(0x0200100b | 2 << 15 | 2 << 20);                // the 3,121-row inline (class 1)
                int err = r.nextInt(5) == 0 ? 1 + r.nextInt(6) : 0;
                switch (err) {
                    case 1 -> { w(TracerConfRig.lui(26, 2)); w(TracerConfRig.add(26, 26, 2)); w(TracerConfRig.sd(5, 26, 0)); }  // past the owned pages: a hole, [8]
                    case 2 -> w(TracerConfRig.ld(5, 2, 3));                                  // misaligned, [12]
                    case 3 -> { w(TracerConfRig.lui(26, 0x10000)); w(TracerConfRig.add(26, 26, 2)); w(TracerConfRig.ld(5, 26, 0)); }  // out of range, [10]
                    case 4 -> c(0x0000);                                                     // c.unimp, [2]
                    case 5 -> { div(); divs.get(divs.size() - 1)[1] += 1; }                  // a wrong quotient, [11]
                    default -> { }
                }
                w(TracerConfRig.END);
                for (int s : subs) { mark(s); block(1, 3 + r.nextInt(8)); w(TracerConfRig.jalr(0, 1, 0)); }
            }
            for (int l = 0; l < labelAt.size(); l++) if (labelAt.get(l) < 0) throw new IllegalStateException("label " + l);
            TracerConfRig.Prog p = assemble();
            // the advice: the DIVs in execution order (main to END; the subroutines through their calls)
            java.util.Map<Integer, int[]> loopAt = new java.util.HashMap<>();
            for (int[] lp : loops) loopAt.put(lp[0] - 1, lp);        // keyed by the counter's set-up, before the body
            java.util.Map<Integer, Integer> subEnd = new java.util.HashMap<>();
            for (int s : subs) { int k = labelAt.get(s); while (items.get(k).kind != 0 || items.get(k).word != TracerConfRig.jalr(0, 1, 0)) k++; subEnd.put(s, k); }
            int endAt = 0; while (!(items.get(endAt).kind == 0 && items.get(endAt).word == TracerConfRig.END)) endAt++;
            walkMain(endAt, loopAt, subEnd);

            // the image: the code, the owned data pages (a hole after them), the map holding every instruction start
            long[] base = g.bases(p);
            java.util.Map<Long, long[]> pages = new java.util.HashMap<>();
            TracerConfRig.poke(pages, CODE - g.lowest, p.bytes());
            java.util.Set<Long> owned = new java.util.HashSet<>();
            for (int k = 0; k < dataPages; k++) { long o = DATA - g.lowest + (long) stride * k; long[] pg = new long[512]; if (!wb) for (int q = 0; q < 512; q++) pg[q] = r.nextInt(4) == 0 ? r.nextLong() : 0; pages.put(o, pg); owned.add(o); }
            g.buildImage(pages, owned);
            java.util.Map<Long, Long> targets = new java.util.HashMap<>();
            for (int k = 0; k < p.parcels.size(); k++) targets.put(p.pcOf(k), base[k]);
            int need = targets.size(); g.mapK = 5; while ((16L << g.mapK) < 3L * need) g.mapK++;
            g.buildMap(targets); g.loadTables();
            // the controller: service order, hold, compression; reads of the window words during the runs
            if (r.nextBoolean()) { int[] o = { 1, 2, 3 }; for (int k = 2; k > 0; k--) { int j = r.nextInt(k + 1), x = o[k]; o[k] = o[j]; o[j] = x; } g.order = o; }
            g.stateLast = r.nextInt(4) == 0; g.q0One = r.nextInt(4) == 0; g.hold = r.nextInt(3) == 0 ? 1 + r.nextInt(300) : 0;
            int comp = r.nextInt(8) << 26;
            final boolean reads = r.nextBoolean();
            g.onStep = () -> {
                if (reads) { if (r.nextInt(8) == 0) g.tr.word(g.t, 32 + r.nextInt(21)); if (r.nextInt(64) == 0) g.tr.errors(g.t); }
                try { wbMax = Math.max(wbMax, wbCount.getInt(g.ms)); } catch (IllegalAccessException e) { throw new IllegalStateException(e); }
            };
            long[] regs = new long[128]; regs[2] = DATA; regs[3] = wb ? stride : DATA + 4096; regs[1] = 7;
            for (int k : FREE) regs[k] = r.nextLong();
            long[] adv = new long[advice.size()]; for (int k = 0; k < adv.length; k++) adv[k] = advice.get(k);
            int segments = wb ? 1 : 1 + r.nextInt(2);
            for (int s = 0; s < segments; s++) {
                g.loadRecord(CODE, (int) base[0], 0, regs, adv);
                for (int win = 0; win < 400; win++) {
                    int rows = switch (r.nextInt(4)) { case 0 -> 1 + r.nextInt(16); case 1 -> 16 + r.nextInt(500); case 2 -> 500 + r.nextInt(5000); default -> r.nextInt(16) == 0 ? 65536 : 2000 + r.nextInt(8000); };
                    if (g.window(rows | comp, 4_000_000) == null || g.tr.stopped()) return;
                    if (g.tr.rec[0] == 0) { g.window(1 + r.nextInt(8) | comp, 100_000); break; }  // a padding window after the end
                }
            }
        }
        void walkMain(int endAt, java.util.Map<Integer, int[]> loopAt, java.util.Map<Integer, Integer> subEnd) {
            walk(0, endAt, loopAt, null, subEnd);
        }
    }

    // ================================================================ the recording proxy
    /** The counters masked out of the observation's group key: stat[0..7] and stalls[0..6] (fields 26-40). */
    static String key(String obs) {
        String[] f = obs.trim().split(" ");
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < f.length; i++) if (i < 26 || i > 40) sb.append(' ').append(f[i]);
        return sb.toString();
    }

    /** A Tracer as the rig sees it: every Peripheral call forwarded and recorded; reads that settle nothing pass through. */
    static final class Rec {
        static int nextId;
        static final java.util.List<Rec> all = new java.util.ArrayList<>();
        final Tracer x;
        final java.util.function.Supplier<FileStore> fs;
        final int id;
        boolean started;
        int dumpedNext = -1, dumps;
        // the current cycle's calls: their cycles and tokens (the offset is filled in when the cycle's anchor is known)
        final java.util.List<Long> callT = new java.util.ArrayList<>();
        final java.util.List<String> callTok = new java.util.ArrayList<>();
        // the open group
        boolean gOpen; String gSig, gKey, gObs; long gT0, gN;
        final long[] rec; final long[] stalls; final Tracer.OutQueue[] oq; final java.util.List<String> errorLog, violations;
        final long[] iMissBy, iCycBy; final int[] stat;
        final Ev ev = new Ev();

        Rec(Tracer x, java.util.function.Supplier<FileStore> fs) {
            this.x = x; this.fs = fs; id = nextId++; all.add(this);
            rec = x.rec; stalls = x.stalls; oq = x.oq; errorLog = x.errorLog; violations = x.violations; iMissBy = x.iMissBy; iCycBy = x.iCycBy; stat = x.stat;
        }
        static void flushAll() { for (Rec r : all) r.close(); }

        void start() {
            if (started) return;
            started = true;
            out.println("N " + id + " 66 " + x.icacheLines);
            dump();
        }
        int dump() {
            FileStore s = fs.get();
            Map<Integer, long[]> p = pages(s);
            int first = p.isEmpty() ? nextFree(s) : java.util.Collections.min(p.keySet());
            int k = dumps++;
            out.println("F " + id + " " + k + " " + first + " " + nextFree(s));
            for (var e : new TreeMap<>(p).entrySet()) {
                boolean zero = true; for (long v : e.getValue()) if (v != 0) { zero = false; break; }
                if (zero) { out.println("Z " + id + " " + e.getKey()); continue; }
                StringBuilder sb = new StringBuilder("P " + id + " " + e.getKey());
                for (long v : e.getValue()) sb.append(' ').append(hx(v));
                out.println(sb);
            }
            dumpedNext = nextFree(s);
            return k;
        }
        void call(long t, String tok) { start(); callT.add(t); callTok.add(tok); }
        String sig(long anchor) {
            StringBuilder sb = new StringBuilder();
            for (int i = 0; i < callTok.size(); i++) {
                if (i > 0) sb.append(';');
                String tk = callTok.get(i); long t = callT.get(i);
                if (t == Long.MIN_VALUE || t == anchor) { sb.append(tk); continue; }
                int c = tk.indexOf(','), e = tk.indexOf('='), at = c < 0 ? (e < 0 ? tk.length() : e) : (e < 0 ? c : Math.min(c, e));
                sb.append(tk, 0, at).append('@').append(t - anchor).append(tk.substring(at));
            }
            return sb.toString();
        }
        long anchor() { for (long t : callT) if (t != Long.MIN_VALUE) return t; return 0; }
        void flushGroup() { if (gOpen) { out.println("c " + id + " " + gT0 + " " + gN + " " + gSig); out.println("o " + id + gObs); gOpen = false; } }
        /** The cycle ends with runCycle(t): extend the open group or start one. */
        void endCycle(long t) {
            String s = sig(t), o = observe(x), k = key(o);
            if (gOpen && s.equals(gSig) && k.equals(gKey) && t == gT0 + gN) { gN++; gObs = o; }
            else { flushGroup(); gOpen = true; gSig = s; gKey = k; gObs = o; gT0 = t; gN = 1; }
            callT.clear(); callTok.clear();
        }
        /** A call raised UndefinedBehavior, or the run ends: what is pending goes out as a group of its own. */
        void flushPartial() {
            flushGroup();
            if (callTok.isEmpty()) return;
            long a = anchor();
            out.println("c " + id + " " + a + " 1 " + sig(a)); out.println("o " + id + observe(x));
            callT.clear(); callTok.clear();
        }
        void close() { if (started) flushPartial(); }
        <R> R ub(java.util.function.Supplier<R> call) {
            try { return call.get(); }
            catch (UndefinedBehavior e) {
                int i = callTok.size() - 1; callTok.set(i, callTok.get(i) + "!");
                flushPartial(); throw e;
            } catch (IllegalStateException e) {
                int i = callTok.size() - 1; callTok.set(i, callTok.get(i) + "%");
                flushPartial(); throw e;
            }
        }

        // ---- the Peripheral calls
        void accept(long t, int src, int offset, int size, long lo, long hi) {
            start();
            if (offset == 0) { if (nextFree(fs.get()) != dumpedNext) call(t, "F," + dump()); else call(t, "H=" + hx(storeHash(fs.get()))); }
            call(t, "a," + src + "," + hx(offset) + "," + size + "," + hx(lo) + "," + hx(hi));
            ub(() -> { x.accept(t, src, offset, size, lo, hi); return null; });
        }
        void run(long t) { call(t, "u"); ub(() -> { x.run(t); return null; }); }
        int writeWord(long t, int w, int v) {
            call(t, "ww," + w + "," + hx(v & 0xFFFFFFFFL)); int r = ub(() -> x.writeWord(t, w, v)); app("=" + r); return r;
        }
        Integer word(long t, int w) {
            call(t, "wr," + w); Integer r = ub(() -> x.word(t, w)); app("=" + (r == null ? "-" : hx(r & 0xFFFFFFFFL))); return r;
        }
        boolean running(long t) { call(t, "rn"); boolean r = ub(() -> x.running(t)); app("=" + (r ? 1 : 0)); return r; }
        int errors(long t) { call(t, "er"); int r = ub(() -> x.errors(t)); app("=" + hx(r)); return r; }
        void sync(long t) { call(t, "sy"); ub(() -> { x.sync(t); return null; }); }
        boolean rowsEnded(long t) { call(t, "re"); boolean r = ub(() -> x.rowsEnded(t)); app("=" + (r ? 1 : 0)); return r; }
        int writeBackDirtyUntimed() { call(Long.MIN_VALUE, "wb"); int n = x.writeBackDirtyUntimed(); app("=" + n); return n; }
        void outStart(long t, int q, int words) { call(t, "os," + q + "," + words); ub(() -> { x.outStart(t, q, words); return null; }); }
        boolean outReady(long t, int q) { call(t, "or," + q); boolean r = x.outReady(t, q); app("=" + (r ? 1 : 0)); return r; }
        void outTake(long t, int q, long[] w) { call(t, "ot," + q); x.outTake(t, q, w); app("=" + hx(w[0]) + "," + hx(w[1])); }
        void runCycle(long t) { call(t, "st"); ub(() -> { x.runCycle(t); return null; }); endCycle(t); }
        void app(String s) { int i = callTok.size() - 1; callTok.set(i, callTok.get(i) + s); }
        Ev events() { return ev; }
        final class Ev {
            int value(long t) { call(t, "ev"); int v = x.events().value(t); app("=" + hx(v)); return v; }
            void consume(int bits) { call(Long.MIN_VALUE, "co," + hx(bits)); x.events().consume(bits); }
        }

        // ---- reads that settle nothing: passed through
        boolean stopped() { return x.stopped(); }
        int cacheLines(boolean data) { return x.cacheLines(data); }
        int adviceLeft() { return x.adviceLeft(); }
        long lowest() { return x.lowest(); }
    }
}
