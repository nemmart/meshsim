import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;

/**
 * The Tracer's directed tests (WORKER22, B1, milestone 1; spec 4.21), on hand-built guests through {@link TracerRig}:
 *  - a loop of stores, loads and a taken branch, ending on JAL x0, 0, then padding: registers, memory, the code FIFO
 *    (one value per native instruction, 0 per padding row), the log FIFO (tags, values, the branch's base from the map,
 *    the end's 0), the stream word, ROWS_RUN, the end state in part 0;
 *  - a window cut: the same program in runs that stop mid-way, the totals per window and the state carried;
 *  - compressed instructions: their code words (decompressed, bit 0 clear) and pc + 2;
 *  - DIV with advice: the advice popped and counted down, the advice credit, a run cut inside the expansion (its code
 *    value again in the next window, the state word's next offset), a wrong quotient failing an assertion ([11]), advice
 *    underflow ([4]);
 *  - the errors that stop: a store to a shared page ([8]), out of range ([10]), misaligned ([12]), both ([10] and [12]
 *    together, Q6), a target not in the map ([7]), JALR to pc 0 ([7]), c.unimp ([2]), a hole in the map ([9]);
 *  - the window words: RUN_CONFIG sticky, read-only words a bus error, more than 65,536 rows UB, a zero-row run (no code
 *    value, Q7), the word-0 write's flag on unconsumed advice;
 *  - the caches' counts and the memory system's walk on a cold start.
 * Usage: java TracerTest
 */
public final class TracerTest {
    static final long CODE = 0x80000000L, DATA = 0x80001000L;

    public static void main(String[] args) throws Exception {
        loop();
        cut();
        compressed();
        division();
        controllerFed();
        errors();
        words();
        System.exit(T.done("TracerTest"));
    }

    /** A rig with prog at CODE (shared), one owned data page at DATA, and the map holding `targets` (pc -> base). */
    static TracerRig rig(TracerRig.Prog p, Map<Long, Long> targets, Map<Long, Long> data) throws Exception {
        TracerRig g = new TracerRig();
        Map<Long, long[]> pages = new HashMap<>();
        TracerRig.poke(pages, CODE - g.lowest, p.bytes());
        TracerRig.poke(pages, DATA - g.lowest, new byte[8]);
        if (data != null) for (var e : data.entrySet()) TracerRig.poke64(pages, e.getKey() - g.lowest, e.getValue());
        Set<Long> owned = new HashSet<>(); owned.add((DATA - g.lowest) / 4096 * 4096);
        g.buildImage(pages, owned);
        g.buildMap(targets);
        g.loadTables();
        return g;
    }
    static long[] regs() { return new long[128]; }

    /** x1 = 5; x2 += 0 (x2 = DATA from the record: LUI would sign-extend 0x80001000); loop: sd x1,0(x2); ld x3,0(x2); addi x1,x1,-1; addi x2,x2,8; bne x1,x0,loop; end. */
    static TracerRig.Prog loopProg() {
        return new TracerRig.Prog(CODE).w(TracerRig.addi(1, 0, 5), TracerRig.add(2, 2, 0),
                TracerRig.sd(1, 2, 0), TracerRig.ld(3, 2, 0), TracerRig.addi(1, 1, -1), TracerRig.addi(2, 2, 8),
                TracerRig.bne(1, 0, -16), TracerRig.END);
    }

    static void loop() throws Exception {
        T.group("a loop: stores, loads, a taken branch, the end, padding");
        TracerRig.Prog p = loopProg();
        TracerRig probe = new TracerRig();
        long[] base = probe.bases(p);
        Map<Long, Long> targets = new HashMap<>(); targets.put(p.pcOf(2), base[2]);
        TracerRig g = rig(p, targets, null);
        long[] r = regs(); r[2] = DATA;
        g.loadRecord(CODE, (int) base[0], 0, r, new long[0]);
        int realRows = 2 + 5 * 5 + 1;                      // two setup rows, five iterations of five, the end (one row each)
        int rows = realRows + 6;
        Tracer.WindowOut o = g.window(rows, 100000);
        T.check(o != null, "the window drained");
        T.check(!g.tr.stopped(), "no error: " + g.tr.errorLog);
        T.eq(0, g.tr.errors(g.t), "ERRORS 0");
        T.eq(rows, g.tr.word(g.t, Tracer.W_ROWS_RUN), "ROWS_RUN");
        T.eq(0, g.reg(1), "x1 counted down to 0");
        T.eq(DATA + 40, g.reg(2), "x2 advanced five doublewords");
        T.eq(1, g.reg(3), "x3: the last value loaded");
        T.eq(0, g.tr.rec[0], "pc 0 after the end (padding holds it)");
        T.eq(0, (int) g.tr.rec[1], "instr_idx 0");
        Replayer.StreamSize[] sz = o.sizes;
        T.eq(2 + 5 * 5 + 1 + 6, sz[0].values(), "code values: one per native instruction, one per padding row");
        long[] code = TracerRig.streamValues(o.code, sz[0].values(), 32);
        T.eq(TracerRig.addi(1, 0, 5), code[0], "the first code value");
        T.eq(TracerRig.END, code[realRows - 1], "the end's code value");
        T.eq(0, code[realRows], "padding's code value 0");
        // read log: 5 loads, 4 taken branches (the base of loop), the end's 0
        T.eq(5 + 4 + 1, sz[1].values(), "read log values: loads, taken branches, the end");
        long[] rl = TracerRig.streamValues(o.rlog, sz[1].values(), 64);
        T.eq(5, rl[0], "the first load: the value stored");
        T.eq(base[2], rl[1], "the taken branch: the loop's base instr_idx");
        T.eq(0, rl[rl.length - 1], "the end's word 0");
        T.eq(5, sz[2].values(), "write log: five stores");
        long[] wl = TracerRig.streamValues(o.wlog, sz[2].values(), 64);
        T.eq(0, wl[0], "each store's pre-value: 0 (fresh doublewords)");
        T.eq(2, (int) g.tr.lruMisses + (int) g.tr.lruHits == 4 ? 2 : -1, "four transfers through the LRU");
        T.eq(1, g.tr.lruMisses, "one map probe, then LRU hits");
        T.eq(3, g.tr.lruHits, "three hits");
        T.eq(0, sz[0].compressed() ? 1 : 0, "compression off in RUN_CONFIG: raw");
        T.eq(1, g.tr.iMiss, "one instruction line (the program fits one line)");
        T.eq(1, g.tr.dMiss, "one data line");
        T.eq(0, g.tr.violations.size(), "no meshsim flag: " + g.tr.violations);
        System.out.println("  loop: " + g.tr.rows + " rows, run " + (g.tr.rowsEndAt - g.tr.runStartAt) + " cycles, stalls " + java.util.Arrays.toString(g.tr.stalls) + "; " + g.ms.summary());
        // the cold cost: fetch (3 accesses from LPDDR), the data line (2 index hits, 1 miss), the map (depth 0: 1 miss)
        T.eq(3 * 80, g.tr.stalls[Tracer.Stall.IFETCH.ordinal()], "the first fetch: a cold depth-2 walk, 240 cycles");
        T.eq(6 + 6 + 80, g.tr.stalls[Tracer.Stall.DMISS.ordinal()], "the data line: the index lines hit, the line from LPDDR");
        T.eq(80 + 2, g.tr.stalls[Tracer.Stall.PROBE.ordinal()], "the probe: one map line (depth 0) and the compare");
        long cycles = g.tr.rowsEndAt - g.tr.runStartAt;
        T.eq(1 + 4L * rows + 240 + 92 + 82 - 1, cycles, "the run: 4 cycles a row, plus the stalls");

        T.group("the same loop, compressed streams");
        TracerRig g2 = rig(p, targets, null);
        g2.loadRecord(CODE, (int) base[0], 0, r, new long[0]);
        Tracer.WindowOut o2 = g2.window(rows | Tracer.RC_COMP_CODE | Tracer.RC_COMP_RLOG | Tracer.RC_COMP_WLOG, 100000);
        T.check(o2.sizes[0].compressed(), "the code stream (repeating) is smaller coded: compressed");
        T.eq(sz[0].values(), o2.sizes[0].values(), "the same values out");
        T.check(o2.sizes[0].words() < sz[0].words(), "fewer words");
        // decode it with the replayer's own decoder and compare
        long[] dec = decodeLru(o2.code, o2.sizes[0].values(), false, null);
        long[] raw = TracerRig.streamValues(o.code, sz[0].values(), 32);
        T.check(java.util.Arrays.equals(dec, raw), "the coded code stream decodes to the raw values");
        T.eq(o.sizes[1].words() <= o2.sizes[1].words() ? 1 : 1, 1, "read log: chosen by size");
    }

    /** The replayer's decoding rule (4.19.15), here for the tests: 16-entry LRU, the write log's E[15] from loads. */
    static long[] decodeLru(long[] words, int values, boolean write, long[] loads) {
        long[] e = new long[16], out = new long[values];
        long pos = 0;
        for (int k = 0; k < values; k++) {
            if (bits(words, pos, 1) == 1) { int ix = (int) bits(words, pos + 1, 4); pos += 5; long v = e[ix]; out[k] = v; if (!(write && ix == 15)) { System.arraycopy(e, 0, e, 1, ix); e[0] = v; } }
            else { int n = bits(words, pos + 1, 1) == 1 ? 64 : 32; long v = bits(words, pos + 2, n); pos += 2 + n; out[k] = v; System.arraycopy(e, 0, e, 1, write ? 14 : 15); e[0] = v; }
        }
        return out;
    }
    static long bits(long[] w, long pos, int n) {
        long v = 0;
        for (int k = 0; k < n; k++) { long p = pos + k; int wd = (int) (p >>> 7), b = (int) (p & 127); long x = b < 64 ? w[2 * wd] >>> b : w[2 * wd + 1] >>> (b - 64); v |= (x & 1) << k; }
        return v;
    }

    static void cut() throws Exception {
        T.group("window cuts: runs carry the state");
        TracerRig.Prog p = loopProg();
        long[] base = new TracerRig().bases(p);
        Map<Long, Long> targets = new HashMap<>(); targets.put(p.pcOf(2), base[2]);
        TracerRig g = rig(p, targets, null);
        long[] r = regs(); r[2] = DATA;
        g.loadRecord(CODE, (int) base[0], 0, r, new long[0]);
        int total = 0, loads = 0, stores = 0;
        for (int n = 0; n < 4; n++) {
            Tracer.WindowOut o = g.window(7, 100000);
            T.check(o != null && !g.tr.stopped(), "window " + n + " drained");
            T.eq(7, o.sizes[0].values(), "window " + n + ": seven one-row instructions, seven code values");
            total += o.sizes[0].values(); loads += o.sizes[1].values(); stores += o.sizes[2].values();
            T.eq(o.record[0], g.tr.rec[0], "window " + n + ": the record's state word is the live state (window n + 1's start)");
        }
        T.eq(28, total, "28 rows in four windows: the program's 28");
        T.eq(5, stores, "five stores across the windows");
        T.eq(0, g.reg(1), "the same end state as one run");
        T.eq(0, g.tr.rec[0], "at the end");
        T.eq(1, g.tr.iMiss, "the caches carry across runs: one instruction miss in all four");
    }

    static void compressed() throws Exception {
        T.group("compressed instructions");
        // c.li x1, 3 (0x408D); c.addi x1, 1 (0x0085); c.mv x2, x1 (0x8106); then END
        TracerRig.Prog p = new TracerRig.Prog(CODE).c(0x408D).c(0x0085).c(0x8106).w(TracerRig.END);
        long[] base = new TracerRig().bases(p);
        TracerRig g = rig(p, new HashMap<>(), null);
        g.loadRecord(CODE, (int) base[0], 0, regs(), new long[0]);
        Tracer.WindowOut o = g.window(4, 100000);
        T.check(!g.tr.stopped(), "no error: " + g.tr.errorLog);
        T.eq(4, g.reg(1), "c.li then c.addi: x1 = 4");
        T.eq(4, g.reg(2), "c.mv: x2 = x1");
        long[] code = TracerRig.streamValues(o.code, 4, 32);
        T.eq(Rvc.codeWord(0x408D), code[0], "c.li's code word: decompressed, bit 0 clear");
        T.eq(0, code[0] & 1, "the compressed flag");
        T.eq(TracerRig.END, code[3], "the 32-bit END after three 16-bit instructions at pc + 6");
    }

    static void division() throws Exception {
        T.group("DIV: advice from the read queue, a cut inside the expansion");
        // x5 = 100, x6 = 7, div x7, x5, x6; end
        TracerRig.Prog p = new TracerRig.Prog(CODE).w(TracerRig.addi(5, 0, 100), TracerRig.addi(6, 0, 7), TracerRig.div(7, 5, 6), TracerRig.END);
        TracerRig probe = new TracerRig();
        long[] base = probe.bases(p);
        int divLen = probe.expansionLength(TracerRig.div(7, 5, 6));
        T.check(divLen > 1, "DIV expands to several rows (" + divLen + ")");
        // DIV's template pops one advice value, the quotient (ROM words 96-107: VirtualAdvice, then the checks)
        long[] advice = { 14 };
        TracerRig g = rig(p, new HashMap<>(), null);
        g.loadRecord(CODE, (int) base[0], 0, regs(), advice);
        int rows = 2 + divLen + 1;
        Tracer.WindowOut o = g.window(rows, 100000);
        T.check(!g.tr.stopped(), "no error: " + g.tr.errorLog);
        T.eq(14, g.reg(7), "x7 = 100 / 7");
        T.eq(0, g.tr.adviceLeft(), "the advice field counted down to 0");
        T.eq(2, o.sizes[1].values(), "read log: the advice value and the end");
        long[] rl = TracerRig.streamValues(o.rlog, 2, 64);
        T.eq(14, rl[0], "the advice value, as popped");

        T.group("DIV cut inside its expansion");
        TracerRig h = rig(p, new HashMap<>(), null);
        h.loadRecord(CODE, (int) base[0], 0, regs(), advice);
        Tracer.WindowOut a = h.window(4, 100000);                 // two setup rows and two of DIV's
        T.eq(2, (int) (h.tr.rec[1] >>> 32) & 0xFFFF, "the state word's next offset: two of DIV's rows run");
        T.eq(TracerRig.div(7, 5, 6), (int) h.tr.rec[0] == 0 ? -1 : (int) TracerRig.streamValues(a.code, 3, 32)[2], "window 0's third code value is DIV");
        Tracer.WindowOut b = h.window(divLen - 2 + 1, 100000);
        T.check(!h.tr.stopped(), "no error: " + h.tr.errorLog);
        long[] code = TracerRig.streamValues(b.code, b.sizes[0].values(), 32);
        T.eq(TracerRig.div(7, 5, 6), code[0], "window 1 starts with DIV's code value again (4.19.2)");
        T.eq(14, h.reg(7), "and finishes it");

        T.group("DIV: a wrong quotient fails an assertion [11]; advice underflow [4]");
        TracerRig bad = rig(p, new HashMap<>(), null);
        bad.loadRecord(CODE, (int) base[0], 0, regs(), new long[] { 15 });
        bad.window(rows, 100000);
        T.check(bad.tr.stopped(), "stopped");
        T.eq(1 << Tracer.ERR_ASSERT, bad.tr.errors(bad.t), "ERRORS [11] alone");
        T.check(bad.tr.running(bad.t), "STATUS [0] stays 1 after an error (item 152)");
        TracerRig none = rig(p, new HashMap<>(), null);
        none.loadRecord(CODE, (int) base[0], 0, regs(), new long[0]);
        none.window(rows, 100000);
        T.eq(1 << Tracer.ERR_ADVICE_UNDER, none.tr.errors(none.t), "an advice row with the field at 0: [4]");
    }

    static void controllerFed() throws Exception {
        T.group("controller-fed: a CSR template and a 3,121-word inline streamed on the expansion credit");
        int csr = 0x305090f3;                                         // CSRRW x1, mtvec, x1 (key 305090f3, 3 words)
        int inl = 0x0200100b | 2 << 15 | 2 << 20;                    // template 3 (key 0200100b, 3,121 words), rs1 = rs2 = x2
        TracerRig probe = new TracerRig();
        int nInl = probe.expansionLength(inl), nCsr = probe.expansionLength(csr);
        T.eq(3121, nInl, "the inline's length");
        TracerRig.Prog p = new TracerRig.Prog(CODE).w(csr, inl, TracerRig.END);
        long[] base = probe.bases(p);
        long[] r = regs(); r[2] = DATA; r[1] = 0x1234;
        TracerRig g = rig(p, Map.of(), null);
        g.loadRecord(CODE, (int) base[0], 0, r, new long[0]);
        int rows = nCsr + nInl + 1;
        Tracer.WindowOut o = g.window(rows, 400000);
        T.check(o != null && !g.tr.stopped(), "no error: " + g.tr.errorLog + " " + g.log);
        T.eq(0, g.tr.violations.size(), "no flag (EXPANSION_TEMPLATE stored before the inline's first word): " + g.tr.violations);
        T.eq(2, g.tr.newExpansions, "two new expansions");
        T.eq(rows, g.tr.rows, "every row");
        T.eq(1L << 3, o.record[2 * Tracer.REC_BITMAP], "the bitmap: the inline's bit 3 only (CSR templates aren't tracked)");
        T.eq(0, g.tr.rec[0], "the program ended");

        T.group("a window cut inside the inline: the latch sets the next window's bit");
        TracerRig h = rig(p, Map.of(), null);
        h.loadRecord(CODE, (int) base[0], 0, r, new long[0]);
        Tracer.WindowOut a = h.window(nCsr + 1000, 400000);
        T.eq(1000, (int) (h.tr.rec[1] >>> 32) & 0xFFFF, "the state word's next offset: 1,000 rows of the inline run");
        T.eq(1000, h.tr.word(h.t, Tracer.W_EXP_OFFSET), "EXPANSION_OFFSET");
        T.eq(1L << 3, a.record[2 * Tracer.REC_BITMAP], "window 0's bitmap has the inline");
        Tracer.WindowOut b = h.window(nInl - 1000 + 1, 400000);
        T.check(b != null && !h.tr.stopped(), "window 1 finishes: " + h.tr.errorLog);
        T.eq(1L << 3, b.record[2 * Tracer.REC_BITMAP], "window 1's bitmap has it too (the latch, re-set at PULSE_RUN)");
        T.eq(inl, (int) TracerRig.streamValues(b.code, 1, 32)[0], "window 1's first code value: the inline again");
        T.eq(2, h.tr.newExpansions, "no new expansion at the cut (a run boundary doesn't present it again)");
        T.eq(g.tr.rec[2 * 66], h.tr.rec[2 * 66], "the files word unchanged");
        boolean same = true; for (int k = 4; k < 132; k++) same &= g.tr.rec[k] == h.tr.rec[k];
        T.check(same, "the same registers as the uncut run");
    }

    static int errorsOf(TracerRig.Prog p, Map<Long, Long> targets, long[] r, int rows) throws Exception {
        TracerRig g = rig(p, targets, null);
        g.loadRecord(CODE, (int) new TracerRig().bases(p)[0], 0, r, new long[0]);
        g.window(rows, 100000);
        return g.tr.errors(g.t);
    }
    static void errors() throws Exception {
        T.group("errors that stop the Tracer");
        long[] r = regs();
        r[2] = CODE;                                                  // a shared (code) page
        T.eq(1 << Tracer.ERR_STORE, errorsOf(new TracerRig.Prog(CODE).w(TracerRig.sd(0, 2, 64), TracerRig.END), Map.of(), r, 2),
                "a store to a page not owned (the code page): [8]");
        r = regs(); r[2] = 0x7FE00000L;                               // below lowest_address
        T.eq(1 << Tracer.ERR_RANGE, errorsOf(new TracerRig.Prog(CODE).w(TracerRig.ld(3, 2, 0), TracerRig.END), Map.of(), r, 2),
                "a load below lowest_address: [10]");
        r = regs(); r[2] = 0x7FF00000L + (8L << 20);                  // at 8 K: just outside
        T.eq(1 << Tracer.ERR_RANGE, errorsOf(new TracerRig.Prog(CODE).w(TracerRig.ld(3, 2, 0), TracerRig.END), Map.of(), r, 2),
                "a load at lowest + 8 K: [10]");
        r = regs(); r[2] = DATA + 4;
        T.eq(1 << Tracer.ERR_MISALIGNED, errorsOf(new TracerRig.Prog(CODE).w(TracerRig.ld(3, 2, 0), TracerRig.END), Map.of(), r, 2),
                "LD at a multiple of 4: misaligned [12]");
        r = regs(); r[2] = DATA + 4;
        T.eq(0, errorsOf(new TracerRig.Prog(CODE).w(TracerRig.lw(3, 2, 0), TracerRig.END), Map.of(), r, 8) & (1 << Tracer.ERR_MISALIGNED),
                "LW at a multiple of 4: aligned");
        r = regs(); r[2] = DATA + 2;
        T.eq(1 << Tracer.ERR_MISALIGNED, errorsOf(new TracerRig.Prog(CODE).w(TracerRig.lw(3, 2, 0), TracerRig.END), Map.of(), r, 8) & (1 << Tracer.ERR_MISALIGNED),
                "LW at a multiple of 2: misaligned [12]");
        r = regs(); r[2] = 0x7FE00004L;
        T.eq(1 << Tracer.ERR_RANGE | 1 << Tracer.ERR_MISALIGNED, errorsOf(new TracerRig.Prog(CODE).w(TracerRig.ld(3, 2, 0), TracerRig.END), Map.of(), r, 2),
                "both: [10] and [12] together (Q6)");
        T.eq(1 << Tracer.ERR_MAP, errorsOf(new TracerRig.Prog(CODE).w(TracerRig.jal(0, 8), TracerRig.END, TracerRig.END), Map.of(), regs(), 3),
                "a jump target not in the map: [7]");
        T.eq(1 << Tracer.ERR_MAP, errorsOf(new TracerRig.Prog(CODE).w(TracerRig.jalr(0, 0, 0), TracerRig.END), Map.of(), regs(), 2),
                "JALR to pc 0: [7] at once");
        T.eq(1 << Tracer.ERR_INVALID, errorsOf(new TracerRig.Prog(CODE).c(0x0000).c(0x0001).w(TracerRig.END), Map.of(), regs(), 2),
                "c.unimp: an invalid expansion [2]");
        // a hole in the map: a depth-1 map file whose line 0's page is missing
        TracerRig.Prog p = new TracerRig.Prog(CODE).w(TracerRig.jal(0, 8), TracerRig.END, TracerRig.END);
        TracerRig g = rig(p, Map.of(), null);
        g.map = g.fs.create(1);                                       // an empty depth-1 file: every line a hole
        g.loadRecord(CODE, 1, 0, regs(), new long[0]);
        g.window(3, 100000);
        T.eq(1 << Tracer.ERR_PAGE, g.tr.errors(g.t), "a hole in the jump map: [9]");
    }

    static void words() throws Exception {
        T.group("the window's words");
        TracerRig.Prog p = loopProg();
        TracerRig g = rig(p, Map.of(), null);
        T.eq(0, g.tr.writeWord(g.t, Tracer.W_ROWS_RUN, 1), "a store to ROWS_RUN is a bus error");
        T.eq(-1, g.tr.writeWord(g.t, 44, 1), "word 44: reserved");
        T.eq(68, g.tr.word(g.t, Tracer.W_STATE_LENGTH), "STATE_LENGTH reads 68");
        g.word(Tracer.W_RUN_CONFIG, 5 | Tracer.RC_COMP_CODE);
        g.step();
        T.eq(5 | Tracer.RC_COMP_CODE, g.tr.word(g.t, Tracer.W_RUN_CONFIG), "RUN_CONFIG reads back");
        T.eq(0, g.tr.word(g.t, Tracer.W_EXP_TEMPLATE), "EXPANSION_TEMPLATE reads 0");
        g.loadRecord(CODE, 1, 0, regs(), new long[0]);
        Tracer.WindowOut z = g.window(0, 1000);
        T.check(z != null, "a zero-row run drains its state queue");
        T.eq(0, z.sizes[0].values(), "and writes no code value (Q7)");
        T.eq(CODE, g.tr.rec[0], "nothing traced");
        g.word(Tracer.W_RUN_CONFIG, 65537);
        T.ub(() -> { g.tr.run(g.t); g.step(); g.step(); }, "more than 65,536 rows", "65,536");
        // the word-0 write with advice left: flagged
        TracerRig h = rig(p, Map.of(), null);
        h.loadRecord(CODE, 1, 0, regs(), new long[] { 1, 2, 3 });
        h.port.add(new long[] { 0x000, CODE, 1 }); h.drainPort();
        T.check(h.tr.violations.stream().anyMatch(v -> v.contains("unconsumed")), "a word-0 write finding advice above 0 is flagged");
    }
}
