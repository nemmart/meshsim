import java.util.ArrayDeque;
import java.util.LinkedHashMap;
import java.util.Map;

/**
 * The canonical state fingerprint's M3 units, ref's side (sim/tests/AUDIT-M3.md section 6; WORKER13): the Tracer
 * (6.6: tr, tr.ic, tr.dc, tr.mem, tr.files). As StateFp (M2): read between cycles by reflection, never a method that
 * settles or moves ref (a peripheral's settle, a LinkedHashMap's get); the hash and the Out sink are StateFp's.
 *
 * M3's rules (6): a cycle-valued field is written as max(v, c); a peripheral's inputs not yet in effect are written as a
 * list of (effect cycle, kind, words), read from its deques as they are; a large memory is a sum over its non-zero
 * 64-bit words, fmix64(fmix64(region << 56 ^ index) ^ value).
 */
final class StateFpM3 {
    static final String[] TRACER_UNITS = { "tr", "tr.ic", "tr.dc", "tr.mem", "tr.files" };
    static final int R_FILES = 11, R_DECODE = 12, R_ROM = 13;
    static final long NONE = -1;

    static long term64(int region, long index, long value) { return StateFp.fmix(StateFp.fmix((long) region << 56 ^ index) ^ value); }
    static long mx(long v, long c) { return Math.max(v, c); }
    static Object o(Object x, String n) { return StateFp.o(x, n); }
    static long l(Object x, String n) { return StateFp.l(x, n); }

    static void unit(StateFp.Out o, Tracer tr, String u, long c) {
        switch (u) {
            case "tr" -> tracer(o, tr, c);
            case "tr.ic" -> cache(o, (LinkedHashMap<?, ?>) o(tr, "icache"));
            case "tr.dc" -> cache(o, (LinkedHashMap<?, ?>) o(tr, "dcache"));
            case "tr.mem" -> mem(o, tr.mem, c);
            case "tr.files" -> o.w("files", files(tr, o));
            default -> throw new IllegalArgumentException("unit " + u);
        }
    }

    // ---------------------------------------------------------------- tr
    static void tracer(StateFp.Out o, Tracer tr, long c) {
        // 1. registers
        for (String n : new String[] { "runConfig", "hashMul", "hashStep", "hashLines", "lengthCode", "lengthRlog", "lengthWlog" }) o.w(n, l(tr, n));
        // 2. the record
        for (int k = 0; k < tr.rec.length; k++) o.w("rec" + k, tr.rec[k]);
        // 3. the decode table and the ROM, as memory sums
        long sd = 0; for (int k = 0; k < tr.decodeTable.length; k++) { long v = tr.decodeTable[k] & 0xFFFFL; if (v != 0) sd += term64(R_DECODE, k, v); }
        o.w("decodeTable", sd);
        long sr = 0;
        for (int j = 0; j < tr.romLo.length; j++) { if (tr.romLo[j] != 0) sr += term64(R_ROM, 2L * j, tr.romLo[j]); if (tr.romHi[j] != 0) sr += term64(R_ROM, 2L * j + 1, tr.romHi[j]); }
        o.w("rom", sr);
        // 4. the input queues
        queue(o, "rq", (long[]) o(tr, "rqLo"), (long[]) o(tr, "rqHi"), (int) l(tr, "rqHead"), (int) l(tr, "rqOcc"), new String[] { "rqHalf", "rqPopped" }, tr);
        queue(o, "eq", (long[]) o(tr, "eqLo"), (long[]) o(tr, "eqHi"), (int) l(tr, "eqHead"), (int) l(tr, "eqOcc"), new String[] { "eqPopped" }, tr);
        // 5. the core
        for (String n : new String[] { "running", "stopped", "rowsEnded", "drained", "done", "rowsTarget", "rowsDone" }) o.w(n, l(tr, n));
        o.w("startAt", mx(l(tr, "startAt"), c)); o.w("busyUntil", mx(l(tr, "busyUntil"), c));
        long wu = l(tr, "waitUntil"); o.w("waitUntil", mx(wu, c));
        Object wc = o(tr, "waitCause"); o.w("waitCause", wu > c && wc != null ? ((Enum<?>) wc).ordinal() : NONE);
        for (String n : new String[] { "presented", "expansionOffset", "latch", "templateStored" }) o.w(n, l(tr, n));
        long[] bm = (long[]) o(tr, "bitmap"); o.w("bitmap0", bm[0]); o.w("bitmap1", bm[1]);
        o.w("expDiscard", l(tr, "expDiscard"));
        long su = l(tr, "statUntil"); o.w("statUntil", mx(su, c)); o.w("statCause", su > c ? l(tr, "statCause") : NONE);
        o.w("errors", l(tr, "errors"));
        Object cur = o(tr, "cur");
        if (cur == null) o.w("cur", 0);
        else {
            o.w("cur", 1);
            for (String n : new String[] { "w", "kind", "start", "rd0", "offset", "cls", "nextRow", "ctrlFirstSeen", "fetched", "presented" }) o.w("cur." + n, l(cur, n));
            o.w("cur.codeThisRun", l(cur, "codeRun") == l(tr, "runId") ? 1 : 0);
        }
        // 6. the events' counts
        int[] cnt = (int[]) o(tr.ev, "count"); for (int b = 0; b < tr.ev.depth.length; b++) o.w("ev" + b, cnt[b]);
        // 7. the statistics counters (words 44-51)
        for (int k = 0; k < tr.stat.length; k++) o.w("stat" + k, tr.stat[k] & 0xFFFFFFFFL);
        // 8. the jump LRU, eldest to newest
        LinkedHashMap<?, ?> lru = (LinkedHashMap<?, ?>) o(tr, "jumpLru");
        o.w("lru.n", lru.size());
        for (Map.Entry<?, ?> e : lru.entrySet()) { o.w("lru.pc", (Long) e.getKey()); o.w("lru.base", ((Integer) e.getValue()) & 0xFFFFFFFFL); }
        // 9. the FIFOs and set 1
        o.w("codeN", tr.codeN); for (int k = 0; k < tr.codeN; k++) o.w("code", tr.codeFifo[k] & 0xFFFFFFFFL);
        o.w("logN", tr.logN); for (int k = 0; k < tr.logN; k++) { o.w("log", tr.logFifo[k]); o.w("tag", tr.logTag[k]); }
        o.w("rlogValues", l(tr, "rlogValues")); o.w("wlogValues", l(tr, "wlogValues"));
        for (String n : new String[] { "set1Code", "set1Rlog", "set1Wlog" }) { LruCoder k = (LruCoder) o(tr, n); coder(o, n, k); o.w(n + ".bits", k.bits); }
        // 10. the output path
        o.w("q0ptr", l(tr, "q0ptr")); o.w("scanStart", mx(l(tr, "scanStart"), c));
        for (String n : new String[] { "scansDone", "codeScan", "logScan", "comp0", "comp1", "comp2", "codeEnded", "logEnded", "xferQueue", "xferLeft" }) o.w(n, l(tr, n));
        for (String n : new String[] { "set2Code", "set2Rlog", "set2Wlog" }) coder(o, n, (LruCoder) o(tr, n));
        for (String n : new String[] { "pkCode", "pkRlog", "pkWlog" }) {
            LruCoder.Bits b = (LruCoder.Bits) o(tr, n);
            if (b == null) o.w(n, 0); else { o.w(n, 1); o.w(n + ".lo", b.lo); o.w(n + ".hi", b.hi); o.w(n + ".fill", b.fill); o.w(n + ".words", b.words); }
        }
        for (int q = 1; q <= 3; q++) {
            Tracer.OutQueue oq = tr.oq[q]; String p = "oq" + q;
            o.w(p + ".occ", oq.occ);
            for (int k = 0; k < oq.occ; k++) { int i = (oq.head + k) % Tracer.QCAP; o.w(p + ".lo", oq.lo[i]); o.w(p + ".hi", oq.hi[i]); }
            o.w(p + ".pushed", oq.pushed); o.w(p + ".read", oq.read); o.w(p + ".granted", oq.granted);
            for (int k = 0; k < oq.granted; k++) o.w(p + ".bound", oq.bound[k]);
            o.w(p + ".ended", oq.ended ? 1 : 0);
        }
        // 11. inputs not yet in effect: the landings (e, src, offset, size, lo, hi), then the inputs (e, kind, word, value)
        ArrayDeque<?> land = (ArrayDeque<?>) o(tr, "landing"), in = (ArrayDeque<?>) o(tr, "inputs");
        o.w("landing.n", land.size());
        for (Object x : land) { o.w("e", l(x, "t")); o.w("src", l(x, "kind")); o.w("offset", l(x, "a")); o.w("size", l(x, "b")); o.w("lo", l(x, "lo")); o.w("hi", l(x, "hi")); }
        o.w("inputs.n", in.size());
        for (Object x : in) { o.w("e", l(x, "t")); o.w("kind", l(x, "kind")); o.w("word", l(x, "a")); o.w("value", l(x, "b") & 0xFFFFFFFFL); }
    }
    static void queue(StateFp.Out o, String p, long[] lo, long[] hi, int head, int occ, String[] extra, Tracer tr) {
        o.w(p + ".occ", occ);
        for (String n : extra) o.w(n, l(tr, n));
        for (int k = 0; k < occ; k++) { int i = (head + k) % lo.length; o.w(p + ".lo", lo[i]); o.w(p + ".hi", hi[i]); }
    }
    static void coder(StateFp.Out o, String p, LruCoder k) { long[] e = (long[]) o(k, "e"); for (int i = 0; i < 16; i++) o.w(p + ".e" + i, e[i]); }

    // ---------------------------------------------------------------- tr.ic, tr.dc: eldest to newest (iteration doesn't reorder)
    static void cache(StateFp.Out o, LinkedHashMap<?, ?> m) {
        o.w("lines", m.size());
        for (Map.Entry<?, ?> e : m.entrySet()) {
            Tracer.Line ln = (Tracer.Line) e.getValue();
            o.w("line", (Long) e.getKey());
            for (int k = 0; k < 16; k++) o.w("d" + k, ln.d[k]);
            o.w("dirty", ln.dirty ? 1 : 0); o.w("writable", ln.writable ? 1 : 0);
        }
    }

    // ---------------------------------------------------------------- tr.mem
    static void mem(StateFp.Out o, MemorySystem ms, long c) {
        o.w("busyUntil", mx(l(ms, "busyUntil"), c));
        long[] wb = (long[]) o(ms, "wbDone"); int head = (int) l(ms, "wbHead"), n = (int) l(ms, "wbCount");
        int live = 0; for (int k = 0; k < n; k++) if (wb[(head + k) % wb.length] > c) live++;
        o.w("wb.n", live);
        for (int k = 0; k < n; k++) { long d = wb[(head + k) % wb.length]; if (d > c) o.w("wb", d); }
        int[] tag = (int[]) o(ms, "tag"); boolean[] dirty = (boolean[]) o(ms, "dirty"); long[] stamp = (long[]) o(ms, "stamp");
        int W = MemorySystem.WAYS;
        for (int s = 0; s < MemorySystem.SETS; s++) {
            int b = s * W; boolean any = false;
            for (int w = 0; w < W; w++) if (tag[b + w] >= 0) { any = true; break; }
            if (!any) continue;
            o.w("set", s);
            for (int w = 0; w < W; w++) {
                if (tag[b + w] < 0) { o.w("tag", NONE); o.w("dirty", 0); o.w("rank", NONE); continue; }
                int rank = 0; for (int v = 0; v < W; v++) if (v != w && tag[b + v] >= 0 && stamp[b + v] < stamp[b + w]) rank++;
                o.w("tag", tag[b + w]); o.w("dirty", dirty[b + w] ? 1 : 0); o.w("rank", rank);
            }
        }
    }

    // ---------------------------------------------------------------- tr.files: nextFree, then one sum; rescanned only after a write
    private static final java.util.IdentityHashMap<FileStore, long[]> FILES = new java.util.IdentityHashMap<>();   // store -> {key, sum}
    /** The store changes only by the memory system's writes (a write-back), whose count `cold()` clears at every state
     *  write in T2: the key is (state writes, writes since), so a rescan follows any write. */
    static long files(Tracer tr, StateFp.Out o) {
        FileStore fs = tr.mem.store;
        o.w("nextFree", l(fs, "nextFree"));
        long key = l(tr, "stateWrites") << 40 ^ tr.mem.writes;
        long[] k = FILES.get(fs);
        if (k != null && k[0] == key) return k[1];
        long s = filesScan(fs);
        FILES.put(fs, new long[] { key, s });
        return s;
    }
    static long filesScan(FileStore fs) {
        @SuppressWarnings("unchecked") Map<Integer, long[]> pages = (Map<Integer, long[]>) o(fs, "pages");
        long s = 0;
        for (Map.Entry<Integer, long[]> e : pages.entrySet()) {
            long base = (long) (e.getKey() / FileStore.PAGE_UNITS) * 512; long[] p = e.getValue();
            for (int i = 0; i < p.length; i++) if (p[i] != 0) s += term64(R_FILES, base + i, p[i]);
        }
        return s;
    }
}
