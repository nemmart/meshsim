import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.List;

/**
 * A test rig for the Tracer (WORKER22, B1, milestone 1): a hand-built guest in a {@link FileStore} (an image owning every
 * page, the Tracer's copy by cow_open with the data pages pre-copied, section 22), a jump-target map built by 4.21.4's
 * hash rule, the universal tables, and an <b>ideal controller</b> in Java standing in for C&C 3's firmware: it lands every
 * packet through the region port (one a cycle, expansion words first), answers new expansion (EXPANSION_TEMPLATE for an
 * inline, then the template from EXPANSION_OFFSET, the N3 rules as the replayer's driver), the expansion and advice
 * credits, and takes each window by {@link Tracer#idealDrain} at the state service.
 */
final class TracerRig {
    static final int SRC = 66;                         // C&C 3
    final Tracer tr = new Tracer(66);
    final FileStore fs = new FileStore();
    final MemorySystem ms = new MemorySystem(fs);
    final ReplayTables tables;
    long lowest = 0x7FF00000L; int log2K = 20;
    long t;
    final ArrayDeque<long[]> port = new ArrayDeque<>(), portExp = new ArrayDeque<>();
    final List<String> log = new ArrayList<>();

    TracerRig() throws Exception {
        tables = ReplayTables.load(ReplayTables.defaultDir());
        tr.mem = ms;
        tr.onStateWrite = ms::cold;
    }

    // ================================================================ an assembler (RV64IMAC encodings)
    static int r(int f7, int rs2, int rs1, int f3, int rd, int op) { return f7 << 25 | rs2 << 20 | rs1 << 15 | f3 << 12 | rd << 7 | op; }
    static int i(int imm, int rs1, int f3, int rd, int op) { return (imm & 0xFFF) << 20 | rs1 << 15 | f3 << 12 | rd << 7 | op; }
    static int s(int imm, int rs2, int rs1, int f3, int op) { return ((imm >> 5) & 0x7F) << 25 | rs2 << 20 | rs1 << 15 | f3 << 12 | (imm & 31) << 7 | op; }
    static int b(int imm, int rs2, int rs1, int f3) { return ((imm >> 12) & 1) << 31 | ((imm >> 5) & 63) << 25 | rs2 << 20 | rs1 << 15 | f3 << 12 | ((imm >> 1) & 15) << 8 | ((imm >> 11) & 1) << 7 | 0x63; }
    static int u(int imm20, int rd, int op) { return imm20 << 12 | rd << 7 | op; }
    static int j(int imm, int rd) { return ((imm >> 20) & 1) << 31 | ((imm >> 1) & 0x3FF) << 21 | ((imm >> 11) & 1) << 20 | ((imm >> 12) & 0xFF) << 12 | rd << 7 | 0x6F; }
    static int addi(int rd, int rs1, int imm) { return i(imm, rs1, 0, rd, 0x13); }
    static int add(int rd, int rs1, int rs2) { return r(0, rs2, rs1, 0, rd, 0x33); }
    static int mul(int rd, int rs1, int rs2) { return r(1, rs2, rs1, 0, rd, 0x33); }
    static int div(int rd, int rs1, int rs2) { return r(1, rs2, rs1, 4, rd, 0x33); }
    static int lui(int rd, int imm20) { return u(imm20, rd, 0x37); }
    static int ld(int rd, int rs1, int imm) { return i(imm, rs1, 3, rd, 0x03); }
    static int lw(int rd, int rs1, int imm) { return i(imm, rs1, 2, rd, 0x03); }
    static int lbu(int rd, int rs1, int imm) { return i(imm, rs1, 4, rd, 0x03); }
    static int sd(int rs2, int rs1, int imm) { return s(imm, rs2, rs1, 3, 0x23); }
    static int sw(int rs2, int rs1, int imm) { return s(imm, rs2, rs1, 2, 0x23); }
    static int sb(int rs2, int rs1, int imm) { return s(imm, rs2, rs1, 0, 0x23); }
    static int bne(int rs1, int rs2, int imm) { return b(imm, rs2, rs1, 1); }
    static int beq(int rs1, int rs2, int imm) { return b(imm, rs2, rs1, 0); }
    static int jal(int rd, int imm) { return j(imm, rd); }
    static int jalr(int rd, int rs1, int imm) { return i(imm, rs1, 0, rd, 0x67); }
    static final int END = 0x0000006F;                 // JAL x0, 0: the program's end

    /** A program: 16- and 32-bit parcels in order, from its pc. */
    static final class Prog {
        final long base; final List<int[]> parcels = new ArrayList<>();   // {bits, size 2 or 4}
        Prog(long base) { this.base = base; }
        Prog w(int... ws) { for (int x : ws) parcels.add(new int[] { x, 4 }); return this; }
        Prog c(int h) { parcels.add(new int[] { h & 0xFFFF, 2 }); return this; }
        long pcOf(int k) { long p = base; for (int n = 0; n < k; n++) p += parcels.get(n)[1]; return p; }
        byte[] bytes() {
            int n = 0; for (int[] q : parcels) n += q[1];
            byte[] out = new byte[n]; int o = 0;
            for (int[] q : parcels) for (int k = 0; k < q[1]; k++) out[o++] = (byte) (q[0] >>> (8 * k));
            return out;
        }
    }

    /** Each instruction's base instr_idx (section 18: 1 + the expansion lengths below it) by the universal tables. */
    long[] bases(Prog p) {
        long[] out = new long[p.parcels.size()];
        long idx = 1;
        for (int k = 0; k < out.length; k++) {
            out[k] = idx;
            int[] q = p.parcels.get(k);
            int w = q[1] == 4 ? q[0] : Rvc.codeWord(q[0]);
            idx += expansionLength(w);
        }
        return out;
    }
    int expansionLength(int w) {
        if (w == 0) return 1;
        RowCore.Decoded d = new RowCore.Decoded();
        RowCore.decode(w, tables.table, tables.romLo, d);
        return switch (d.kind) {
            case RowCore.K_ROM -> RowCore.seqRemaining(tables.romLo[d.start]) + 1;
            case RowCore.K_CTRL -> { int m = tables.lookup(w); int rd0 = m >= 0 ? tables.rd0[m] : 0; yield (((w >>> 7) & 31) == 0 && (rd0 == 1 || rd0 == 3)) ? 1 : tables.length[m]; }
            default -> 1;
        };
    }

    // ================================================================ the images
    FileStore.File image, copy, map;
    int mapK = 5, mapMul = 0x9E3779B1, mapStep = 0x00000B;   // 32 lines; the golden-ratio multiplier; a prime step

    /** The image: pages (offset in .pages -> 512 longs), the code pages shared, `owned` pre-copied in the Tracer's copy. */
    void buildImage(java.util.Map<Long, long[]> pages, java.util.Set<Long> owned) {
        image = fs.create(2);
        for (var e : pages.entrySet()) fs.putPage(image, e.getKey() / 4096, e.getValue());
        copy = fs.cowOpen(image);
        for (long o : owned) fs.makePrivate(copy, o / 4096);
    }
    static void poke(java.util.Map<Long, long[]> pages, long off, byte[] b) {
        for (int k = 0; k < b.length; k++) {
            long o = off + k; long[] pg = pages.computeIfAbsent(o / 4096 * 4096, x -> new long[512]);
            int w = (int) ((o % 4096) / 8), sh = (int) (8 * (o % 8));
            pg[w] = pg[w] & ~(0xFFL << sh) | (long) (b[k] & 0xFF) << sh;
        }
    }
    static void poke64(java.util.Map<Long, long[]> pages, long off, long v) {
        byte[] b = new byte[8]; for (int k = 0; k < 8; k++) b[k] = (byte) (v >>> (8 * k)); poke(pages, off, b);
    }

    /** The jump map (4.21.4): 2^mapK lines of 16 (key u32, base u32) entries, by the probe's own rule; depth 0 here. */
    void buildMap(java.util.Map<Long, Long> targets) {
        int lines = 1 << mapK;
        long[][] l = new long[lines][16];
        for (long[] x : l) java.util.Arrays.fill(x, 0xFFFFFFFFFFFFFFFFL);
        for (var e : targets.entrySet()) {
            int key = (int) (e.getKey() - lowest);
            long h = mapK == 0 ? 0 : ((key & 0xFFFFFFFFL) * (mapMul & 0xFFFFFFFFL) & 0xFFFFFFFFL) >>> (32 - mapK);
            boolean put = false;
            for (int n = 0; n < 3 && !put; n++) {
                for (int s = 0; s < 16; s++) if ((int) l[(int) h][s] == 0xFFFFFFFF) { l[(int) h][s] = (key & 0xFFFFFFFFL) | e.getValue() << 32; put = true; break; }
                h = (h + mapStep) & (lines - 1);
            }
            if (!put) throw new IllegalStateException("map too full");
        }
        int pagesN = Math.max(1, lines / 32);
        map = fs.create(FileStore.depthFor((long) lines * 128));
        for (int p = 0; p < pagesN; p++) {
            long[] pg = new long[512];
            for (int q = 0; q < 32 && p * 32 + q < lines; q++) System.arraycopy(l[p * 32 + q], 0, pg, 16 * q, 16);
            fs.putPage(map, map.depth() == 0 ? 0 : p, pg);
        }
    }

    // ================================================================ the controller
    void step() {
        if (!port.isEmpty() || !portExp.isEmpty()) {
            long[] p = !portExp.isEmpty() ? portExp.poll() : port.poll();
            tr.accept(t, SRC, (int) p[0], LinkWord.SIZE_128, p[1], p[2]);
        }
        tr.runCycle(t);
        t++;
    }
    void drainPort() { while (!port.isEmpty() || !portExp.isEmpty()) step(); for (int k = 0; k < Peripheral.RP_LATENCY + 1; k++) step(); }

    /** Startup: the decode table and the ROM, the hash constants and RUN_CONFIG. */
    void loadTables() {
        for (int p = 0; p < 256; p++) {
            long lo = 0, hi = 0;
            for (int k = 0; k < 4; k++) lo |= (tables.table[8 * p + k] & 0xFFFFL) << (16 * k);
            for (int k = 0; k < 4; k++) hi |= (tables.table[8 * p + 4 + k] & 0xFFFFL) << (16 * k);
            port.add(new long[] { 0x1000 + 16 * p, lo, hi });
        }
        for (int q = 0; q < 512; q++) port.add(new long[] { 0x2000 + 16 * q, tables.romLo[q], tables.romHi[q] });
        drainPort();
        word(Tracer.W_HASH_MUL, mapMul); word(Tracer.W_HASH_STEP, mapStep); word(Tracer.W_HASH_LINES, mapK);
    }
    void word(int w, int v) { if (tr.writeWord(t, w, v) != 1) throw new IllegalStateException("word " + w + " refused"); step(); }

    long[] advice = new long[0]; int adviceNext;

    /** The segment's record, word 0 first: the state, the advice count, the registers, the files word, the bitmap 0. */
    void loadRecord(long pc, int instrIdx, int offset, long[] regs, long[] adviceVals) {
        advice = adviceVals; adviceNext = 0;
        port.add(new long[] { 0x000, pc, instrIdx & 0xFFFFFFFFL | (long) (offset & 0xFFFF) << 32 });
        port.add(new long[] { 0x010, 0, (long) adviceVals.length << 38 });
        for (int k = 0; k < 64; k++) port.add(new long[] { 0x020 + 16 * k, regs[2 * k], regs[2 * k + 1] });
        long[] fw = Tracer.filesWord(copy.root(), copy.depth(), map.root(), map.depth(), log2K, lowest);
        port.add(new long[] { 0x420, fw[0], fw[1] });
        port.add(new long[] { 0x430, 0, 0 });
        drainPort();
        sendAdvice(); sendAdvice();                    // preload: two half-queue refills on the word-0 write's credits
        tr.events().consume(1 << Tracer.EV_ADVICE); tr.events().consume(1 << Tracer.EV_ADVICE);
        drainPort();
    }
    void sendAdvice() {
        int n = Math.min(256, advice.length - adviceNext);
        for (int k = 0; k < n; k += 2, adviceNext += 2)
            port.add(new long[] { 0x200000, advice[adviceNext], adviceNext + 1 < advice.length ? advice[adviceNext + 1] : 0 });
        adviceNext = Math.min(adviceNext, advice.length);
    }

    long[] expWords; int expNext;
    void sendExp() {
        if (expWords == null) return;
        int n = Math.min(128, (expWords.length - expNext) / 2);
        for (int k = 0; k < n; k++, expNext += 2) portExp.add(new long[] { 0x400000, expWords[expNext], expWords[expNext + 1] });
    }

    /** One window: RUN_CONFIG, PULSE_RUN, the controller loop until run done (or the Tracer stops, or maxCycles). */
    Tracer.WindowOut window(int runConfig, long maxCycles) {
        word(Tracer.W_RUN_CONFIG, runConfig);
        tr.run(t); step();
        int mask = 1 << Tracer.EV_RUN_DONE | 1 << Tracer.EV_ADVICE | 1 << Tracer.EV_EXP | 1 << Tracer.EV_NEWEXP;
        Tracer.WindowOut out = null;
        long end = t + maxCycles;
        while (t < end) {
            tr.sync(t);
            int v = tr.events().value(t);
            if ((v & 1 << Tracer.EV_SVC_STATE) != 0 && out == null) out = tr.idealDrain(t);
            int c = v & mask;
            if (c != 0) {
                tr.events().consume(c);
                if ((c & 1 << Tracer.EV_NEWEXP) != 0) {
                    int w = tr.word(t, Tracer.W_EXP_OPCODE), off = tr.word(t, Tracer.W_EXP_OFFSET);
                    int m = tables.lookup(w);
                    if (m < 0) { log.add("no template for 0x" + Integer.toHexString(w)); expWords = new long[0]; }
                    else {
                        if (tables.cls[m] == 1) word(Tracer.W_EXP_TEMPLATE, 0x80000000 | m);
                        long[] tpl = tables.words.get(m); int rd0 = tables.rd0[m];
                        if (((w >>> 7) & 31) == 0 && (rd0 == 1 || rd0 == 3)) { expWords = new long[] { tables.romLo[1] & ~(3L << 46) | (long) rd0 << 46, tables.romHi[1] }; expNext = 0; }
                        else if (off > 0) { expWords = tpl.clone(); expWords[2 * off] = expWords[2 * off] & ~(3L << 46) | (long) rd0 << 46; expNext = 2 * off; }
                        else { expWords = tpl; expNext = 0; }
                    }
                    if ((c & 1 << Tracer.EV_EXP) == 0) tr.events().consume(1 << Tracer.EV_EXP);
                    c &= ~(1 << Tracer.EV_EXP);
                    sendExp();
                }
                if ((c & 1 << Tracer.EV_EXP) != 0) sendExp();
                if ((c & 1 << Tracer.EV_ADVICE) != 0) sendAdvice();
                if ((c & 1 << Tracer.EV_RUN_DONE) != 0) { step(); return out; }
            }
            if (tr.stopped()) { for (int k = 0; k < 3; k++) step(); return out; }
            step();
        }
        return out;
    }

    // ================================================================ reading the output
    long reg(int r) { return tr.rec[4 + r]; }
    static long[] streamValues(long[] words, int values, int bits) {
        long[] v = new long[values];
        for (int k = 0; k < values; k++) {
            long pos = (long) k * bits; int wd = (int) (pos / 128), b = (int) (pos % 128);
            long x = b < 64 ? words[2 * wd] >>> b : words[2 * wd + 1] >>> (b - 64);
            if (b < 64 && b + bits > 64) x |= words[2 * wd + 1] << (64 - b);
            v[k] = bits == 64 ? x : x & 0xFFFFFFFFL;
        }
        return v;
    }
}
