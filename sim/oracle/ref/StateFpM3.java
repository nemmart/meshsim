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
    /** nextFree, then the sum, rescanned at every call (about 3 ms in T2): a cache keyed on the memory system's writes
     *  went stale when a harness edited the store directly between calls (TracerTest's map-hole case; W14). */
    static long files(Tracer tr, StateFp.Out o) {
        FileStore fs = tr.mem.store;
        o.w("nextFree", l(fs, "nextFree"));
        return filesScan(fs);
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
    // ================================================================ 6.7 the slot
    static void slot(StateFp.Out o, PeripheralSlot sl, long c) {
        long[] cT = (long[]) o(sl, "consumeT"), mT = (long[]) o(sl, "pendMaskT"); int[] cB = (int[]) o(sl, "consumeBits"), pM = (int[]) o(sl, "pendMask");
        for (int n = 1; n <= 3; n++) {
            if (sl.p[n] == null) continue;
            o.w("mask" + n, sl.mask[n] & 0xFFFFFFFFL);
            if (sl.p[n].outputQueues() > 0) {
                PeripheralSlot.Dma d = sl.dma[n];
                o.w("dma.hdr", d.hdr & 0xFFFFFFFFL); o.w("dma.addr", d.addr & 0xFFFFFFFFL); o.w("dma.done", d.done);
                o.w("dma.busy", l(d, "busy")); o.w("dma.left", l(d, "left")); o.w("dma.q", l(d, "q"));
            }
            if (cT[n] == c) { o.w("consume", 1); o.w("consume.bits", cB[n] & 0xFFFFFFFFL); } else o.w("consume", 0);
            if (mT[n] == c) { o.w("maskStore", 1); o.w("maskStore.v", pM[n] & 0xFFFFFFFFL); } else o.w("maskStore", 0);
        }
    }

    // ================================================================ 6.8 the replayer
    static void replayer(StateFp.Out o, Replayer rp, long c) {
        for (int k = 0; k < rp.region0.length; k++) o.w("region0." + k, rp.region0[k]);
        long sd = 0; for (int k = 0; k < rp.decodeTable.length; k++) { long v = rp.decodeTable[k] & 0xFFFFL; if (v != 0) sd += term64(R_DECODE, k, v); }
        o.w("decodeTable", sd);
        long sr = 0;
        for (int j = 0; j < rp.romLo.length; j++) { if (rp.romLo[j] != 0) sr += term64(R_ROM, 2L * j, rp.romLo[j]); if (rp.romHi[j] != 0) sr += term64(R_ROM, 2L * j + 1, rp.romHi[j]); }
        o.w("rom", sr);
        o.w("runConfig", l(rp, "runConfig"));
        for (Replayer.Stream q : new Replayer.Stream[] { rp.code, rp.rlog, rp.wlog }) {
            o.w("q.occ", q.occ);
            for (int k = 0; k < q.occ; k++) { int i = (q.head + k) % q.cap; o.w("q.lo", q.lo[i]); o.w("q.hi", q.hi[i]); }
            o.w("q.pend", q.pend); o.w("q.poppedSince", q.poppedSince); o.w("q.base", q.base); o.w("q.consumed", q.consumed);
            o.w("q.words", q.words); o.w("q.valuesOut", q.valuesOut); o.w("q.limit", q.limit); o.w("q.taken", q.taken);
            o.w("q.endBits", q.endBits); o.w("q.landedEntries", q.landedEntries); o.w("q.comp", q.comp ? 1 : 0);
            for (int k = 0; k < 16; k++) o.w("q.E", q.E[k]);
            o.w("q.popsAtC", q.popCycle == c ? q.pops : 0);
        }
        Replayer.ExpQueue e = rp.exp;
        o.w("exp.occ", e.occ);
        for (int k = 0; k < e.occ; k++) { int i = (e.head + k) % Replayer.ExpQueue.CAP; o.w("exp.lo", e.lo[i]); o.w("exp.hi", e.hi[i]); }
        o.w("exp.pend", e.pend); o.w("exp.poppedSince", e.poppedSince); o.w("exp.newestValid", e.newestValid ? 1 : 0); o.w("exp.newestSeq", e.newestSeq);
        o.w("exp.popsAtC", e.popCycle == c ? e.pops : 0);
        for (String n : new String[] { "running", "pc", "instrIdx", "startOffset", "rowsPerPass", "fStore", "fRdPrev", "stateWritten" }) o.w(n, l(rp, n));
        o.w("copyDoneAt", mx(l(rp, "copyDoneAt"), c));
        for (String n : new String[] { "rowsStarted", "rowsDone", "firstInstr" }) o.w(n, l(rp, n));
        o.w("busyUntil", mx(l(rp, "busyUntil"), c));
        for (String n : new String[] { "presented", "expDiscard", "codeEnded", "stoppedOffset" }) o.w(n, l(rp, n));
        int[] src = (int[]) o(rp, "srcOf"); for (int k = 0; k < 4; k++) o.w("srcOf", src[k]);
        Object cur = o(rp, "cur"), nxt = o(rp, "nxt");
        instr(o, "cur", cur); instr(o, "nxt", nxt);
        Object pd = o(rp, "inflight");
        if (pd == null) o.w("inflight", 0);
        else {
            o.w("inflight", 1);
            for (String n : new String[] { "rdReg", "rdVal", "newPc", "newIdx", "hold", "lastOfInstr" }) o.w("inflight." + n, l(pd, n));
            Object in = o(pd, "instr"); o.w("inflight.instr", in == cur ? 0 : in == nxt ? 1 : 2);
            long[] rec = (long[]) o(pd, "rec"); for (int k = 0; k < 10; k++) o.w("inflight.rec", rec == null ? 0 : rec[k]);
        }
        int[] cnt = (int[]) o(rp.ev, "count"); for (int b = 0; b < rp.ev.depth.length; b++) o.w("ev" + b, cnt[b]);
        long err = 0; for (int k = 0; k < rp.condEvents.length; k++) if (rp.condEvents[k] > 0) err |= 1L << k; o.w("errors", err);
        ArrayDeque<?> oq = (ArrayDeque<?>) o(rp, "outQ"), oa = (ArrayDeque<?>) o(rp, "outAt");
        o.w("outQ.n", oq.size());
        java.util.Iterator<?> ia = oa.iterator();
        for (Object r : oq) { o.w("out.at", mx((Long) ia.next(), c)); for (long x : (long[]) r) o.w("out", x); }
        ArrayDeque<?> land = (ArrayDeque<?>) o(rp, "landing"), in = (ArrayDeque<?>) o(rp, "inputs");
        o.w("landing.n", land.size());
        for (Object x : land) { o.w("e", l(x, "t")); o.w("src", l(x, "kind")); o.w("addr", l(x, "a")); o.w("size", l(x, "b")); o.w("lo", l(x, "lo")); o.w("hi", l(x, "hi")); }
        o.w("inputs.n", in.size());
        for (Object x : in) { o.w("e", l(x, "t")); o.w("kind", l(x, "kind")); o.w("value", l(x, "a") & 0xFFFFFFFFL); }
    }
    static void instr(StateFp.Out o, String p, Object in) {
        if (in == null) { o.w(p, 0); return; }
        o.w(p, 1);
        for (String n : new String[] { "w", "kind", "start", "offset", "rd0", "nextRow", "lastStarted", "firstRow", "ctrlFirstSeen", "codeEnd" }) o.w(p + "." + n, l(in, n));
    }

    // ================================================================ 6.9 C&C 1's peripheral, the host queue
    @SuppressWarnings("unchecked")
    static void cc1p(StateFp.Out o, Cnc1Peripheral cp, long c) {
        long[] at = (long[]) o(cp, "readyAt"); long clr = l(cp, "clearedAt");
        for (int k = 0; k < 64; k++) o.w("ready" + k, at[k] == Long.MIN_VALUE || at[k] < clr ? 0 : at[k] <= c ? 1 : at[k]);
        o.w("expect", cp.expect);
        ArrayDeque<long[]> ec = (ArrayDeque<long[]>) o(cp, "errChanges"); o.w("errChanges.n", ec.size()); for (long[] e : ec) for (long x : e) o.w("errChanges", x);
        long a = 0, b = 0; for (int k = 0; k < 64; k++) { if (cp.errLine[k]) a |= 1L << k; if (cp.errLine[64 + k]) b |= 1L << k; }
        o.w("errLine0", a); o.w("errLine1", b);
        o.w("wdCount", l(cp, "wdCount")); o.w("wdN", l(cp, "wdN")); o.w("toast", cp.toast ? 1 : 0); o.w("led", cp.led);
        for (String n : new String[] { "tx", "rx" }) { ArrayDeque<Integer> f = (ArrayDeque<Integer>) o(cp, n); o.w(n + ".n", f.size()); for (int x : f) o.w(n, x); }
        o.w("divisor", l(cp, "divisor")); o.w("txDivisor", l(cp, "txDivisor")); o.w("rxDivisor", l(cp, "rxDivisor"));
        o.w("txBusyUntil", mx(l(cp, "txBusyUntil"), c)); o.w("rxOverrun", cp.rxOverrun ? 1 : 0); o.w("framing", cp.framing ? 1 : 0);
        int[] cnt = (int[]) o(cp.ev, "count"); for (int k = 0; k < cp.ev.depth.length; k++) o.w("ev" + k, cnt[k]);
    }
    @SuppressWarnings("unchecked")
    static void ccHost(StateFp.Out o, CcCores cc) {
        ArrayDeque<long[]>[] q = (ArrayDeque<long[]>[]) o(cc, "hostQ");
        for (int k = 0; k < 3; k++) { o.w("hostQ.n", q[k].size()); for (long[] e : q[k]) for (long x : e) o.w("hostQ", x); }
    }

    // ================================================================ the whole chip (6.9's chip line)
    static final String[] CC_NAMES = { "cc1", "cc2", "cc3" };
    /** Every unit's hash at the end of cycle c (= ch.cycle - 0: called between cycles as StateFp.write), then the chip's. */
    static void write(java.io.PrintWriter w, Chip ch, long c, boolean withData, long[] nanos) {
        long all = 0x6A09E667F3BCC908L; int n = 0;
        java.util.List<Object[]> units = new java.util.ArrayList<>();
        for (Tile p : ch.tiles) {
            for (String u : StateFp.TILE_UNITS) if (withData || !u.equals("sram.data")) units.add(new Object[] { "t" + p.idx + "." + u, p, u });
            units.add(new Object[] { "t" + p.idx + ".slot", p, "slot" }); units.add(new Object[] { "t" + p.idx + ".rp", p, "rp" });
        }
        for (int k = 0; k < 3; k++) {
            Tile p = ch.cores.unit(k);
            for (String u : StateFp.TILE_UNITS) if (withData || !u.equals("sram.data")) units.add(new Object[] { CC_NAMES[k] + "." + u, p, u });
            units.add(new Object[] { CC_NAMES[k] + ".slot", p, "slot" });
        }
        if (ch.cores.tracer != null) for (String u : TRACER_UNITS) units.add(new Object[] { u, null, u });
        for (String u : new String[] { "cc1p", "cc.host", "reqbus", "cnc", "ep" }) units.add(new Object[] { u, null, u });
        for (Object[] x : units) {
            long s = System.nanoTime();
            StateFp.Out o = new StateFp.Out(); one(o, ch, (Tile) x[1], (String) x[2], c);
            long h = o.done();
            if (nanos != null) nanos[kind((String) x[2])] += System.nanoTime() - s;
            w.printf("%d %s %016x%n", c, x[0], h); all = StateFp.fmix(all ^ h); n++;
        }
        w.printf("%d chip %016x%n", c, StateFp.fmix(all ^ n));
    }
    static void one(StateFp.Out o, Chip ch, Tile p, String u, long c) {
        switch (u) {
            case "slot" -> slot(o, p.slot, c);
            case "rp" -> replayer(o, (Replayer) p.slot.p[1], c);
            case "cc1p" -> cc1p(o, ch.cc1p, c);
            case "cc.host" -> ccHost(o, ch.cores);
            case "reqbus", "cnc", "ep" -> StateFp.chipUnit(o, ch, u, c);
            default -> { if (u.startsWith("tr")) unit(o, ch.cores.tracer, u, c); else StateFp.unit(o, ch, p, u, c); }
        }
    }
    /** Cost buckets: M2's 16 tile units, then slot, rp, the Tracer's five, cc1p, cc.host, the M2 chip units. */
    static final String[] KINDS;
    static { java.util.List<String> k = new java.util.ArrayList<>(java.util.List.of(StateFp.TILE_UNITS)); k.addAll(java.util.List.of("slot", "rp")); k.addAll(java.util.List.of(TRACER_UNITS)); k.addAll(java.util.List.of("cc1p", "cc.host", "reqbus", "cnc", "ep")); KINDS = k.toArray(new String[0]); }
    static int kind(String u) { for (int i = 0; i < KINDS.length; i++) if (KINDS[i].equals(u)) return i; throw new IllegalArgumentException(u); }
    /** One unit's stream named word by word (fpdump=C:UNIT, the C++ side's comparison target). */
    static void dump(java.io.PrintWriter w, Chip ch, String name, long c) {
        w.println("# fpdump cycle " + c + " unit " + name);
        int dot = name.indexOf('.');
        String head = dot < 0 ? name : name.substring(0, dot), u = dot < 0 ? name : name.substring(dot + 1);
        Tile p = null;
        if (head.startsWith("t") && !head.equals("tr")) p = ch.tiles[Integer.parseInt(head.substring(1))];
        else if (head.startsWith("cc") && !name.equals("cc.host") && !name.equals("cc1p")) p = ch.cores.unit(Integer.parseInt(head.substring(2)) - 1);
        else u = name;
        one(new StateFp.Dump(w, name), ch, p, u, c);
    }
}
