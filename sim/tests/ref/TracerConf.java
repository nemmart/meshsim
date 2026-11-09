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
