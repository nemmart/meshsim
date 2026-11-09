/**
 * The replayer's conformance, ref's side (WORKER15; the C++ side is tests/test_replayer_alu.cpp and test_replayconf.cpp).
 * Compiled into ref's default package (D5: ref's classes are package-private; nothing in sim/ref is changed).
 *
 * <pre>
 *   java -cp REF:. ReplayConf alu SEED N      every executed opcode on N seeded inputs: one line per opcode with a digest
 *                                             of exec's (rd, new pc, undefined) over them; then the RowCore sweep
 *   java -cp REF:. ReplayConf trace DELIVERY VARIANT WINDOW OUT [mutations]
 *                                             one window of a (v1.26) delivery through ReplayDriver's ideal controller, every
 *                                             call to the replayer recorded: OUT.script (the calls, cycle by cycle) and
 *                                             OUT.obs (ref's observations)
 *   java -cp REF:. ReplayConf run SCRIPT      the script replayed on ref's Replayer: the observations on stdout (equal to
 *                                             trace's: the script holds every input)
 * </pre>
 *
 * Mutations (error paths): --rows R (RUN_CONFIG's rows), --passes P (P passes, the state resent before each after the
 * first), --values S D (stream S's values out in the stream word changed by D: an underflow or a leftover), --bad-decode
 * (every decode-table entry made class 3: the first instruction is invalid), --extra-code N (N code entries more than the credits allow on the first refill:
 * an overrun and invariant 16), --flood N (N copies of the code stream's first entry pushed after the load,
 * before the pass: an overrun past 256), --cycles C (the pass's cycle limit).
 *
 * Script: a header ("# replayconf script v1", provenance), then per cycle "t op op ...", ops in call order:
 * "A:addr:lo:hi" accept (v1.1 word address; offset = addr x 4; src 0, 128 bits), "R" run, "C:v" RUN_CONFIG store,
 * "Y" a read point (events and, when any is set, words 34-35), "K:bits" a consume of EVENTS (immediate, as the driver
 * does), "S" runCycle. Values in lowercase hex. Observations: per cycle, "t" then the fields that changed after
 * runCycle(t), each "k=v" (read at t + 1: what a load at t + 1 returns), "y=..." at a read point with events set, and
 * "row=h" for each row the sink took (h: mix over its 10 words).</pre>
 *
 * The inputs come from splitmix64 (the same generator as tests/harness.h's Rng), so the C++ side regenerates them and
 * only the digests are committed. The digest: h = mix(h ^ x) for each output word x, mix splitmix64's finaliser.
 */
import java.util.List;

public final class ReplayConf {
    public static void main(String[] a) {
        switch (a.length > 0 ? a[0] : "") {
            case "alu" -> alu(Long.parseLong(a[1]), Integer.parseInt(a[2]));
            case "trace" -> { try { trace(a); } catch (java.io.IOException e) { throw new RuntimeException(e); } }
            case "run" -> { try { run(a[1]); } catch (java.io.IOException e) { throw new RuntimeException(e); } }
            default -> { System.err.println("usage: ReplayConf alu SEED N | trace DELIVERY VARIANT WINDOW OUT [mutations] | run SCRIPT"); System.exit(2); }
        }
    }

    // ================================================================ the generator and the digest (mirrored in C++)
    static final class Rng {
        long s;
        Rng(long seed) { s = seed; }
        long next() {
            long z = (s += 0x9e3779b97f4a7c15L);
            z = (z ^ (z >>> 30)) * 0xbf58476d1ce4e5b9L;
            z = (z ^ (z >>> 27)) * 0x94d049bb133111ebL;
            return z ^ (z >>> 31);
        }
        /** A value biased to the ALU's edges: small, negative, powers of two, masks, sign bits, words. */
        long value() {
            long r = next();
            int k = (int) ((r >>> 8) & 63), j = (int) ((r >>> 16) & 63);
            switch ((int) (r & 7)) {
                case 0: return next();
                case 1: return (r >>> 24) % 71;
                case 2: return -((r >>> 24) % 71);
                case 3: return 1L << k;
                case 4: return (k == 0 ? 0 : (-1L >>> (64 - k))) << j;
                case 5: return 0;
                case 6: return Long.MIN_VALUE | ((r >>> 24) % 71);
                default: return next() & 0xFFFF_FFFFL;
            }
        }
    }
    static long mix(long z) {
        z = (z ^ (z >>> 30)) * 0xbf58476d1ce4e5b9L;
        z = (z ^ (z >>> 27)) * 0x94d049bb133111ebL;
        return z ^ (z >>> 31);
    }

    // ================================================================ alu
    static void alu(long seed, int n) {
        StringBuilder out = new StringBuilder();
        out.append("# replayconf alu v1: seed ").append(seed).append(" n ").append(n).append('\n');
        ReplayerAlu.Out o = new ReplayerAlu.Out();
        for (int op = 0; op < 256; op++) {
            if (!ReplayerAlu.executes(op)) continue;
            Rng g = new Rng(seed * 1000 + op);
            long h = 0; int undef = 0;
            for (int i = 0; i < n; i++) {
                long va = g.value(), vb = g.value(), vc = g.value(), pc = g.value(), log = g.value(), so = g.value();
                long r = g.next();
                int inc = new int[] { 0, 2, 4 }[(int) Long.remainderUnsigned(r, 3)];
                boolean imm0pc0 = ((r >>> 8) & 1) != 0;
                ReplayerAlu.exec(op, va, vb, vc, pc, inc, imm0pc0, log, so, o);
                h = mix(h ^ o.rd); h = mix(h ^ o.newPc); h = mix(h ^ (o.undefined != null ? 1 : 0));
                if (o.undefined != null) undef++;
            }
            out.append(op).append(' ').append(ReplayerAlu.name(op)).append(' ').append(Long.toHexString(h)).append(' ').append(undef).append('\n');
        }
        // RowCore: the decode index, the six source immediates, reg() for every code and both offsets, imm_val for the
        // immediate codes, invalidWord's verdict and needsSourceImm on seeded words
        Rng g = new Rng(seed);
        long h = 0;
        for (int i = 0; i < n * 16; i++) {
            int w = (int) g.value();
            long lo = g.value(), cnst = g.value(), imm = g.value();
            h = mix(h ^ RowCore.decodeIndex(w));
            for (int f = 0; f < 6; f++) h = mix(h ^ RowCore.sourceImm(f, w));
            for (int c = 0; c < 146; c++) { h = mix(h ^ RowCore.reg(c, w, 0)); h = mix(h ^ RowCore.reg(c, w, 1)); }
            for (int c : new int[] { 128, 132, 133, 134, 135, 136, 145 }) h = mix(h ^ RowCore.immVal(c, cnst, imm));
            String inv = RowCore.invalidWord(lo);
            int verdict = inv == null ? 0 : inv.startsWith("opcode") ? 1 : inv.startsWith("a reserved") ? 2 : 3;
            h = mix(h ^ verdict); h = mix(h ^ (RowCore.needsSourceImm(lo) ? 1 : 0));
        }
        out.append("rowcore ").append(Long.toHexString(h)).append('\n');
        System.out.print(out);
    }

    // ================================================================ the recorder: every call, and the observations
    static final String[] VIOL = { "a pass started without", "two sources feed", "unit ", "queue ", "offset_imm set",
            "a row with both from_log", "undefined result", "the program-end row", "a control transfer from a row without",
            "a misaligned" };
    static final class Rec {
        final Replayer r;
        final StringBuilder script = new StringBuilder(), obs = new StringBuilder();
        final List<String> rowHashes = new java.util.ArrayList<>();
        long lineT = Long.MIN_VALUE;
        final long[] last = new long[64];
        boolean first = true;
        Rec(Replayer r) { this.r = r; r.rowListener = row -> { long h = 0; for (long x : row) h = mix(h ^ x); rowHashes.add(Long.toHexString(h)); }; }
        void op(long t, String o) {
            if (t != lineT) { if (lineT != Long.MIN_VALUE) script.append('\n'); script.append(t); lineT = t; }
            script.append(' ').append(o);
        }
        void accept(long t, int addr, long lo, long hi) { op(t, "A:" + Integer.toHexString(addr) + ":" + Long.toHexString(lo) + ":" + Long.toHexString(hi)); r.accept(t, 0, addr << 2, LinkWord.SIZE_128, lo, hi); }
        void run(long t) { op(t, "R"); r.run(t); }
        int config(long t, int v) { op(t, "C:" + Integer.toHexString(v)); return r.writeWord(t, Replayer.W_RUN_CONFIG, v); }
        /** A read point: events, and words 34-35 if any is set; {events, w34, w35}. */
        int[] read(long t) {
            op(t, "Y");
            r.sync(t);
            int l = r.events().value(t);
            int w34 = l != 0 ? r.word(t, Replayer.W_EXP_OFFSET) : 0, w35 = l != 0 ? r.word(t, Replayer.W_EXP_OPCODE) : 0;
            if (l != 0) yObs.append(" y=").append(Integer.toHexString(l)).append(',').append(Integer.toHexString(w34)).append(',').append(Integer.toHexString(w35));
            return new int[] { l, w34, w35 };
        }
        final StringBuilder yObs = new StringBuilder();
        void consume(long t, int bits) { op(t, "K:" + Integer.toHexString(bits)); r.events().consume(bits); }
        void step(long t) {
            op(t, "S");
            rowHashes.clear();
            r.runCycle(t);
            observe(r, t, last, first, yObs, rowHashes, obs);
            first = false; yObs.setLength(0);
        }
    }
    static int violCode(String v) {
        String m = v.substring(v.indexOf(": ") + 2);
        for (int i = 0; i < VIOL.length; i++) if (m.startsWith(VIOL[i])) return i + 1;
        throw new IllegalStateException("unclassified violation: " + v);
    }
    /** The observation after runCycle(t): the fields read at t + 1 that changed (all of them the first time). */
    static void observe(Replayer r, long t, long[] last, boolean first, CharSequence y, List<String> rows, StringBuilder out) {
        long u = t + 1;
        long[] v = new long[64];
        int k = 0;
        v[k++] = r.running(u) ? 1 : 0; v[k++] = r.errors(u); r.sync(u); v[k++] = r.events().value(u);
        for (int w = 32; w <= 35; w++) v[k++] = r.word(u, w) & 0xFFFFFFFFL;
        v[k++] = r.outQueueRows(); v[k++] = r.rowsDone(); v[k++] = r.landed(t);
        for (long s : r.stalls) v[k++] = s;
        long[] vc = new long[VIOL.length + 1];
        for (String s : r.violations) vc[violCode(s)]++;
        for (int i = 1; i <= VIOL.length; i++) v[k++] = vc[i];
        v[k++] = r.invalids.size(); v[k++] = r.overruns.size(); v[k++] = r.underflows.size(); v[k++] = r.leftovers.size();
        v[k++] = r.pc(); v[k++] = r.instrIdx() & 0xFFFFFFFFL;
        StringBuilder b = new StringBuilder();
        for (int i = 0; i < k; i++) if (first || v[i] != last[i]) { b.append(' ').append(FIELD[i]).append('=').append(Long.toHexString(v[i])); last[i] = v[i]; }
        b.append(y);
        for (String h : rows) b.append(" row=").append(h);
        if (b.length() > 0) out.append(t).append(b).append('\n');
    }
    static final String[] FIELD = { "run", "err", "ev", "w32", "w33", "w34", "w35", "outq", "done", "land",
            "stI", "stC", "stN", "stE", "stR", "stW", "stO",
            "v1", "v2", "v3", "v4", "v5", "v6", "v7", "v8", "v9", "v10", "inv", "ovr", "und", "lft", "pc", "idx" };

    // ================================================================ trace: ReplayDriver, recorded
    static void trace(String[] a) throws java.io.IOException {
        java.nio.file.Path dir = java.nio.file.Path.of(a[1]);
        String variant = a[2];
        int index = Integer.parseInt(a[3]);
        String out = a[4];
        int flood = 0, rowsCfg = -1, passes = 1, valuesS = -1, valuesD = 0, badDecode = -1, extraCode = 0; long cycles = -1;
        for (int i = 5; i < a.length; i++) switch (a[i]) {
            case "--rows" -> rowsCfg = Integer.parseInt(a[++i]);
            case "--passes" -> passes = Integer.parseInt(a[++i]);
            case "--values" -> { valuesS = Integer.parseInt(a[++i]); valuesD = Integer.parseInt(a[++i]); }
            case "--bad-decode" -> badDecode = 1;
            case "--extra-code" -> extraCode = Integer.parseInt(a[++i]);
            case "--cycles" -> cycles = Long.parseLong(a[++i]);
            case "--flood" -> flood = Integer.parseInt(a[++i]);
            default -> throw new IllegalArgumentException(a[i]);
        }
        java.util.Map<String, Object> m = ReplayDelivery.Json.object(java.nio.file.Files.readString(dir.resolve("manifest.json")));
        java.util.Map<String, Object> p = ReplayDelivery.Json.object(ReplayDelivery.Json.list(m.get("programs")).get(0));
        String name = (String) p.get("name");
        java.nio.file.Path pd = dir.resolve(name);
        ReplayTables prog = ReplayDelivery.tablesFor(dir, pd);
        java.util.Map<String, Object> we = null;
        for (Object wo : ReplayDelivery.Json.list(p.get("window_entries"))) { java.util.Map<String, Object> e = ReplayDelivery.Json.object(wo); if (((Number) e.get("index")).intValue() == index) we = e; }
        if (we == null) throw new IllegalArgumentException("no window " + index);
        int rows = ((Number) we.get("rows")).intValue();
        String base = String.format("window_%05d.", index);
        java.nio.ByteBuffer rec = ReplayDelivery.le(java.nio.file.Files.readAllBytes(pd.resolve(base + variant + ".bin")));
        ReplayWindow w = new ReplayWindow();
        w.rows = rowsCfg >= 0 ? rowsCfg : rows;
        w.useTables(prog);
        ReplayDelivery.checkRecord(rec, base + variant + ".bin");
        w.region0 = new long[2 * Replayer.PART0_WORDS];
        for (int i = 0; i < w.region0.length; i++) w.region0[i] = rec.getLong(8 * i);
        Replayer.StreamSize[] sz = Replayer.parseStreamWord(w.region0[2], w.region0[3]);
        w.code = ReplayDelivery.words(pd.resolve(base + ReplayDelivery.streamFile("code", sz[0].compressed())));
        w.rlog = ReplayDelivery.words(pd.resolve(base + ReplayDelivery.streamFile("rlog", sz[1].compressed())));
        w.wlog = ReplayDelivery.words(pd.resolve(base + ReplayDelivery.streamFile("wlog", sz[2].compressed())));
        // the RUN_CONFIG flags as the delivery's state record carries them (ReplayDelivery: the window's own)
        w.storeLog = true; w.rdPrev = true;
        if (valuesS >= 0) {
            Replayer.StreamSize[] s2 = sz.clone();
            s2[valuesS] = new Replayer.StreamSize(sz[valuesS].words(), Math.max(0, sz[valuesS].values() + valuesD), sz[valuesS].compressed());
            long[] sw = Replayer.streamWord(s2[0], s2[1], s2[2], Replayer.streamWordAdvice(w.region0[2], w.region0[3]));
            w.region0[2] = sw[0]; w.region0[3] = sw[1];
        }
        if (badDecode >= 0) for (int i = 0; i < 2048; i++) w.table[i] = (short) (w.table[i] | 3);
        Rec rc = new Rec(new Replayer(0));
        Driver d = new Driver(w, rc);
        d.extraCode = extraCode;
        d.load();
        for (int k = 0; k < flood; k++) d.port.add(new long[] { 0x40000, w.code[0], w.code[1] });
        while (!d.port.isEmpty()) d.step();
        long max = cycles >= 0 ? cycles : 40L * w.rows + 100_000;
        for (int k = 0; k < passes; k++) d.pass(max);
        String head = "# replayconf script v1\n# delivery " + dir.getFileName() + " " + name + " window " + index + " " + variant
                + " rows " + w.rows + " passes " + passes + " mutations " + String.join(" ", java.util.Arrays.asList(a).subList(5, a.length)) + "\n";
        java.nio.file.Files.writeString(java.nio.file.Path.of(out + ".script"), head + rc.script + "\n");
        java.nio.file.Files.writeString(java.nio.file.Path.of(out + ".obs"), "# replayconf observations v1\n" + rc.obs
                + "# end rows " + rc.r.rowsTotal + " stalls " + java.util.Arrays.toString(rc.r.stalls) + "\n");
    }

    /** ReplayDriver (ref's test), every call to the replayer through the recorder. Kept line for line with it; the
     *  additions: extraCode (a mutation). */
    static final class Driver {
        final Rec rc; final Replayer r; final ReplayWindow w;
        long t;
        final java.util.ArrayDeque<long[]> port = new java.util.ArrayDeque<>(), portExp = new java.util.ArrayDeque<>();
        int codeNext, rlogNext, wlogNext; long[] expWords; int expNext; boolean done;
        int lastConfig, passesRun, extraCode;
        Driver(ReplayWindow w, Rec rc) { this.w = w; this.rc = rc; this.r = rc.r; }
        void writeConfig() {
            int c = w.runConfig();
            if (c != lastConfig) { if (rc.config(t, c) != 1) throw new IllegalStateException("RUN_CONFIG refused"); lastConfig = c; step(); }
        }
        void step() {
            if (!port.isEmpty() || !portExp.isEmpty()) {
                long[] q = !portExp.isEmpty() ? portExp.poll() : port.poll();
                rc.accept(t, (int) q[0], q[1], q[2]);
            }
            rc.step(t);
            t++;
        }
        void load() {
            addPart0();
            for (int q = 0; q < 256; q++) {
                long lo = 0, hi = 0;
                for (int k = 0; k < 4; k++) lo |= (w.table[8 * q + k] & 0xFFFFL) << (16 * k);
                for (int k = 0; k < 4; k++) hi |= (w.table[8 * q + 4 + k] & 0xFFFFL) << (16 * k);
                port.add(new long[] { 0x400 + 4 * q, lo, hi });
            }
            for (int j = 0; j < 512; j++) port.add(new long[] { 0x800 + 4 * j, w.romLo[j], w.romHi[j] });
            while (!port.isEmpty()) step();
            for (int i = 0; i < Peripheral.RP_LATENCY + 1; i++) step();
        }
        void resendState() { addPart0(); while (!port.isEmpty()) step(); for (int i = 0; i < Peripheral.RP_LATENCY + 1; i++) step(); }
        void addPart0() { for (int i = 0; i < Replayer.PART0_WORDS; i++) port.add(new long[] { 4 * i, w.region0[2 * i], w.region0[2 * i + 1] }); }
        void pass(long maxCycles) {
            if (passesRun++ > 0) resendState();
            done = false; codeNext = 0; rlogNext = wlogNext = 0; expWords = null;
            writeConfig();
            rc.run(t);
            step();
            long end = t + maxCycles;
            boolean extra = extraCode > 0;
            while (t < end) {
                int[] y = rc.read(t);
                int l = y[0];
                if (l != 0) {
                    int st1 = y[2];
                    rc.consume(t, l);
                    if ((l & 1 << Replayer.EV_CODE) != 0) { codeNext = refill(w.code, codeNext, 0x40000, 128 + (extra ? extraCode : 0)); extra = false; }
                    if ((l & 1 << Replayer.EV_RLOG) != 0) rlogNext = refill(w.rlog, rlogNext, 0x80000, r.rlog.half);
                    if ((l & 1 << Replayer.EV_WLOG) != 0) wlogNext = refill(w.wlog, wlogNext, 0xC0000, r.wlog.half);
                    if ((l & 1 << Replayer.EV_NEWEXP) != 0) {
                        int key = ReplayTables.key(st1);
                        long[] tpl = w.templates.get(key);
                        if (tpl == null) tpl = new long[0];
                        int off = y[1], rd0 = w.templateRd0.getOrDefault(key, (int) (tpl.length > 0 ? (tpl[0] >>> 46) & 3 : 0));
                        if (((st1 >>> 7) & 31) == 0 && (rd0 == 1 || rd0 == 3)) {
                            expWords = new long[] { w.romLo[1] & ~(3L << 46) | (long) rd0 << 46, w.romHi[1] }; expNext = 0;
                        } else if (off > 0 && 2 * off < tpl.length) {
                            expWords = tpl.clone(); expWords[2 * off] = expWords[2 * off] & ~(3L << 46) | (long) rd0 << 46; expNext = 2 * off;
                        } else { expWords = tpl; expNext = 2 * off; }
                        if ((l & 1 << Replayer.EV_EXP) == 0) rc.consume(t, 1 << Replayer.EV_EXP);
                        l &= ~(1 << Replayer.EV_EXP);
                        sendExp();
                    }
                    if ((l & 1 << Replayer.EV_EXP) != 0) sendExp();
                    if ((l & 1 << Replayer.EV_RUN_DONE) != 0) done = true;
                }
                step();
                if (done && r.outQueueRows() == 0) break;
            }
            for (int i = 0; i < 4; i++) step();
        }
        int refill(long[] s, int next, int base, int half) {
            int n = Math.min(half, s.length / 2 - next);
            for (int i = 0; i < n; i++, next++) port.add(new long[] { base, s[2 * next], s[2 * next + 1] });
            return next;
        }
        void sendExp() {
            if (expWords == null) return;
            int n = Math.min(128, (expWords.length - expNext) / 2);
            for (int i = 0; i < n; i++, expNext += 2) portExp.add(new long[] { 0x100000, expWords[expNext], expWords[expNext + 1] });
        }
    }

    // ================================================================ run: a script on ref's Replayer
    static void run(String script) throws java.io.IOException {
        Rec rc = new Rec(new Replayer(0));
        for (String line : java.nio.file.Files.readAllLines(java.nio.file.Path.of(script))) {
            if (line.isEmpty() || line.startsWith("#")) continue;
            String[] f = line.split(" ");
            long t = Long.parseLong(f[0]);
            for (int i = 1; i < f.length; i++) {
                String o = f[i];
                switch (o.charAt(0)) {
                    case 'A' -> { String[] g = o.split(":"); rc.accept(t, Integer.parseInt(g[1], 16), Long.parseUnsignedLong(g[2], 16), Long.parseUnsignedLong(g[3], 16)); }
                    case 'R' -> rc.run(t);
                    case 'C' -> rc.config(t, (int) Long.parseLong(o.substring(2), 16));
                    case 'Y' -> rc.read(t);
                    case 'K' -> rc.consume(t, Integer.parseInt(o.substring(2), 16));
                    case 'S' -> rc.step(t);
                    default -> throw new IllegalArgumentException(o);
                }
            }
        }
        System.out.print("# replayconf observations v1\n" + rc.obs + "# end rows " + rc.r.rowsTotal + " stalls " + java.util.Arrays.toString(rc.r.stalls) + "\n");
    }
}
