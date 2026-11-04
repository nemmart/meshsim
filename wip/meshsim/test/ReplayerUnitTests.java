import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Random;

/** The replayer's parts in isolation and small hand-checked passes (spec 4.19). Run from ReplayerTest. */
final class ReplayerUnitTests {
    static void all() {
        decodeIndex();
        immediates();
        decompressors();
        program();
        programCompressed();
        storeLogOff();
        midStart();
        pextAnyMask();
        ldToX0();
        controllerFed();
        controllerFedRd0();
        padding();
        debugLength();
        immediateZero();
        invalid();
        writePort();
        lines();
        timing();
        v126();
    }

    // ================================================================ RISC-V encoders (independent of the decoder)
    static int r(int f7, int rs2, int rs1, int f3, int rd, int op) { return f7 << 25 | rs2 << 20 | rs1 << 15 | f3 << 12 | rd << 7 | op; }
    static int i(int imm, int rs1, int f3, int rd, int op) { return (imm & 0xFFF) << 20 | rs1 << 15 | f3 << 12 | rd << 7 | op; }
    static int s(int imm, int rs2, int rs1, int f3, int op) { return ((imm >> 5) & 0x7F) << 25 | rs2 << 20 | rs1 << 15 | f3 << 12 | (imm & 31) << 7 | op; }
    static int b(int imm, int rs2, int rs1, int f3, int op) {
        return ((imm >> 12) & 1) << 31 | ((imm >> 5) & 63) << 25 | rs2 << 20 | rs1 << 15 | f3 << 12 | ((imm >> 1) & 15) << 8 | ((imm >> 11) & 1) << 7 | op;
    }
    static int u(int imm, int rd, int op) { return (imm & 0xFFFFF000) | rd << 7 | op; }
    static int j(int imm, int rd, int op) {
        return ((imm >> 20) & 1) << 31 | ((imm >> 1) & 0x3FF) << 21 | ((imm >> 11) & 1) << 20 | ((imm >> 12) & 0xFF) << 12 | rd << 7 | op;
    }
    static final int OP_IMM = 0x13, OP = 0x33, LOAD = 0x03, STORE = 0x23, BRANCH = 0x63, JAL = 0x6F, LUI = 0x37, AMO = 0x2F, CUSTOM0 = 0x0B, SYSTEM = 0x73;

    // ================================================================ the decode index (4.19.5)
    static void decodeIndex() {
        T.group("decode index");
        int add = r(0, 11, 10, 0, 10, OP), mul = r(1, 11, 10, 0, 10, OP), sub = r(0x20, 11, 10, 0, 10, OP);
        T.eq((OP >>> 2) << 5, Replayer.decodeIndex(add), "ADD: {w[6:2], funct3 0, w30 0, w25 0}");
        T.eq((OP >>> 2) << 5 | 1, Replayer.decodeIndex(mul), "MUL: w25 = 1");
        T.eq((OP >>> 2) << 5 | 2, Replayer.decodeIndex(sub), "SUB: w30 = 1");
        T.eq(Replayer.decodeIndex(add), Replayer.decodeIndex(add & ~1), "bit 0 (the compressed flag) plays no part");
        java.util.Set<Integer> seen = new java.util.HashSet<>();
        int[] f5 = { 0x02, 0x03, 0x01, 0x00, 0x04, 0x0C, 0x08, 0x10, 0x14, 0x18, 0x1C };   // LR SC SWAP ADD XOR AND OR MIN MAX MINU MAXU
        for (int wd : new int[] { 2, 3 }) for (int f : f5) {
            int w = f << 27 | 3 << 25 | 11 << 20 | 10 << 15 | wd << 12 | 5 << 7 | AMO;   // aq/rl set: must not matter
            int x = Replayer.decodeIndex(w);
            T.eq(1 << 10 | (wd & 1) << 5 | f, x, "AMO funct5 " + f + " width " + wd);
            T.check(seen.add(x), "AMO index unique");
        }
        for (int f7 = 0; f7 <= 5; f7++) for (int f3 = 0; f3 < 8; f3++) {
            int x = Replayer.decodeIndex(r(f7, 1, 2, f3, 3, CUSTOM0));
            T.eq(1 << 10 | 1 << 8 | f7 << 3 | f3, x, "inline f7 " + f7 + " f3 " + f3);
        }
        T.eq(1 << 10 | 2 << 8 | 8, Replayer.decodeIndex(0x00000073), "ECALL");
        T.eq(1 << 10 | 2 << 8 | 9, Replayer.decodeIndex(0x00100073), "EBREAK");
        T.eq(1 << 10 | 2 << 8 | 10, Replayer.decodeIndex(0x30200073), "MRET");
        T.eq(1 << 10 | 2 << 8 | 11, Replayer.decodeIndex(0x10500073), "WFI: another funct3-0 SYSTEM word");
        T.eq(1 << 10 | 2 << 8 | 1, Replayer.decodeIndex(i(0x305, 10, 1, 0, SYSTEM)), "CSRRW: sel = funct3");
        T.eq(1 << 10 | 2 << 8 | 2, Replayer.decodeIndex(i(0x341, 0, 2, 10, SYSTEM)), "CSRRS");
    }

    // ================================================================ immediates (4.19.3, code 132)
    static void immediates() {
        T.group("immediates");
        Random rnd = new Random(1);
        boolean ok = true;
        for (int k = 0; k < 2000; k++) {
            int i12 = rnd.nextInt(4096) - 2048, b13 = (rnd.nextInt(4096) - 2048) * 2, j21 = (rnd.nextInt(1 << 20) - (1 << 19)) * 2;
            int u20 = rnd.nextInt() & 0xFFFFF000;
            ok &= Replayer.sourceImm(1, i(i12, 5, 0, 6, OP_IMM)) == i12;
            ok &= Replayer.sourceImm(2, s(i12, 5, 6, 3, STORE)) == i12;
            ok &= Replayer.sourceImm(3, b(b13, 5, 6, 0, BRANCH)) == b13;
            ok &= Replayer.sourceImm(4, u(u20, 5, LUI)) == (long) u20;
            ok &= Replayer.sourceImm(5, j(j21, 5, JAL)) == j21;
            ok &= Replayer.sourceImm(5, j(j21, 5, JAL) & ~1) == j21;
        }
        T.check(ok, "I, S, B, U, J: 2,000 random immediates each round-trip, sign-extended to 64 bits");
        T.eq(0, Replayer.sourceImm(0, r(0x7F, 31, 31, 7, 31, OP)), "R: 0");
    }

    // ================================================================ the decompressors (4.19.15)
    static void decompressors() {
        T.group("decompressors");
        Random rnd = new Random(7);
        for (int trial = 0; trial < 40; trial++) {
            List<ReplayWindow.LogEvent> ev = new ArrayList<>();
            long[] pool = new long[24];
            for (int k = 0; k < pool.length; k++) pool[k] = rnd.nextBoolean() ? rnd.nextInt() & 0xFFFFFFFFL : rnd.nextLong();
            long lastLd = 0;
            for (int k = 0; k < 3000; k++) {
                int kind = rnd.nextInt(10);
                if (kind < 5) { long v = pool[rnd.nextInt(pool.length)]; if (rnd.nextInt(20) == 0) v = rnd.nextLong(); ev.add(new ReplayWindow.LogEvent(true, v, true, false, 0)); lastLd = v; }
                else if (kind < 6) ev.add(new ReplayWindow.LogEvent(true, rnd.nextInt(5000), false, false, 0));
                else { long v = rnd.nextInt(3) == 0 ? lastLd : pool[rnd.nextInt(pool.length)]; ev.add(new ReplayWindow.LogEvent(false, 0, false, true, v)); }
            }
            boolean rc = trial % 2 == 0, wc = trial % 3 != 0;
            ReplayWindow w = new ReplayWindow(); w.setLogs(ev, rc, wc);
            Replayer rp = new Replayer(0);
            rp.rlog.reset(rc, w.rlog.length / 2, w.rlogVals); rp.wlog.reset(wc, w.wlog.length / 2, w.wlogVals);
            int rn = 0, wn = 0; long t = 0; boolean ok = true; int hits15 = 0;
            for (ReplayWindow.LogEvent e : ev) {
                if (e.hasRead()) {
                    while (rp.rlog.peek() == Replayer.Stream.WAIT) { rp.rlog.push(t++, w.rlog[2 * rn], w.rlog[2 * rn + 1]); rn++; }
                    ok &= rp.rlog.peek() == Replayer.Stream.AVAIL && rp.rlog.peekValue == e.read();
                    rp.rlog.commit(); rp.rlog.releaseConsumed();
                    if (e.load()) rp.wlog.lastLoad(e.read());
                }
                if (e.hasWrite()) {
                    while (rp.wlog.peek() == Replayer.Stream.WAIT) { rp.wlog.push(t++, w.wlog[2 * wn], w.wlog[2 * wn + 1]); wn++; }
                    ok &= rp.wlog.peek() == Replayer.Stream.AVAIL && rp.wlog.peekValue == e.write();
                    if (wc && rp.wlog.peekHit == 15) hits15++;
                    rp.wlog.commit(); rp.wlog.releaseConsumed();
                }
            }
            String n = "trial " + trial + " (read " + (rc ? "LRU" : "raw") + ", write " + (wc ? "LRU+E15" : "raw") + ")";
            T.check(ok, n + ": every value decodes to what was encoded");
            T.check(rp.rlog.peek() == Replayer.Stream.END && rp.wlog.peek() == Replayer.Stream.END, n + ": both logs end exactly at their lengths");
            T.eq(0, rp.rlog.occ + rp.wlog.occ, n + ": every entry left its queue, including the one each stream ends inside");
            if (wc) T.check(hits15 > 100, n + ": the last-load entry is used (" + hits15 + " index-15 hits)");
        }
        // move to front, by hand: misses 1, 2, 3 (table 3 2 1); hit 2 → 1 (table 1 3 2); hit 1 → 3 (table 3 1 2); a 64-bit miss
        ReplayWindow.Bits bits = new ReplayWindow.Bits();
        ReplayWindow.putValue(bits, -1, 1); ReplayWindow.putValue(bits, -1, 2); ReplayWindow.putValue(bits, -1, 3);
        ReplayWindow.putValue(bits, 2, 0); ReplayWindow.putValue(bits, 1, 0); ReplayWindow.putValue(bits, -1, 0x1_0000_0000L);
        long[] ent = bits.entries();
        Replayer rp = new Replayer(0); rp.rlog.reset(true, ent.length / 2, 6);
        for (int k = 0; k < ent.length / 2; k++) rp.rlog.push(k, ent[2 * k], ent[2 * k + 1]);
        long[] expect = { 1, 2, 3, 1, 3, 0x1_0000_0000L };
        for (long e : expect) { T.check(rp.rlog.peek() == Replayer.Stream.AVAIL, "MTF: available"); T.eq(e, rp.rlog.peekValue, "MTF value"); rp.rlog.commit(); }
        T.check(Arrays.equals(Arrays.copyOf(rp.rlog.E, 5), new long[] { 0x1_0000_0000L, 3, 1, 2, 0 }), "MTF table order " + Arrays.toString(Arrays.copyOf(rp.rlog.E, 5)));
        // the write log: hit 15 reorders nothing; a miss leaves E[15] alone and drops E[14]
        Replayer rw = new Replayer(0);
        ReplayWindow.Bits wb = new ReplayWindow.Bits();
        for (int k = 1; k <= 15; k++) ReplayWindow.putValue(wb, -1, k);          // E[0..14] = 15..1
        ReplayWindow.putValue(wb, 15, 0); ReplayWindow.putValue(wb, -1, 99);
        long[] we = wb.entries(); rw.wlog.reset(true, we.length / 2, 17);
        for (int k = 0; k < we.length / 2; k++) rw.wlog.push(k, we[2 * k], we[2 * k + 1]);
        for (int k = 0; k < 15; k++) { rw.wlog.peek(); rw.wlog.commit(); }
        rw.wlog.lastLoad(0xBEEF);
        rw.wlog.peek(); T.eq(0xBEEF, rw.wlog.peekValue, "write log hit 15: the last load"); rw.wlog.commit();
        T.eq(15, rw.wlog.E[0], "hit 15 reorders nothing");
        rw.wlog.peek(); rw.wlog.commit();
        T.eq(99, rw.wlog.E[0], "a miss goes to the front");
        T.eq(2, rw.wlog.E[14], "E[14] was 1, dropped; now 2");
        T.eq(0xBEEF, rw.wlog.E[15], "E[15] untouched by a miss");
        // a 32-bit miss is zero-extended; a value straddling two entries waits for the second
        ReplayWindow.Bits b2 = new ReplayWindow.Bits(); b2.put(0, 100); ReplayWindow.putValue(b2, -1, 0xFFFF_FFFFL);
        long[] e2 = b2.entries();
        Replayer r2 = new Replayer(0); r2.rlog.reset(true, e2.length / 2, 2);
        r2.rlog.push(0, e2[0], e2[1]);
        r2.rlog.consumed = 100;
        T.eq(Replayer.Stream.WAIT, r2.rlog.peek(), "a value straddling entries waits for the second entry");
        r2.rlog.push(1, e2[2], e2[3]);
        T.eq(Replayer.Stream.AVAIL, r2.rlog.peek(), "and decodes once it lands");
        T.eq(0xFFFF_FFFFL, r2.rlog.peekValue, "a 32-bit miss is zero-extended");
    }

    // ================================================================ a hand-checked program
    static final int A0 = 10, A1 = 11, A2 = 12, A5 = 15;
    static final int T_ADD = 2, T_ADDI = 3, T_LB = 4, T_SD = 8, T_BEQ = 9, T_JAL = 10, T_MUL = 11, T_SLLI = 12;
    static final long V1 = 0x112233F5_55667788L, V2 = 0x42L;
    static final int PADDING = 8;                                // 17 real rows in a 25-row pass (v1.25: LB is 3 rows)

    /** The ROM templates the program uses, built as the tracer's would be (4.19.4's LB example included). */
    static void templates(ReplayWindow w) {
        w.fixedRom();
        w.rom(T_ADD, ReplayWindow.W.op(ReplayerAlu.ADD).rd(129).rs1(130).rs2(131).fmt(0).rd0(1).reportC());
        w.rom(T_ADDI, ReplayWindow.W.op(ReplayerAlu.ADDI).rd(129).rs1(130).rs2(132).fmt(1).rd0(3).reportC());
        // LB at jolt-cpp 821e1bd (wip/tables/replayer/rom.bin words 24-26): VirtualLoadB takes the doubleword from the log
        w.rom(T_LB,
                ReplayWindow.W.op(ReplayerAlu.LOAD_B).rd(137).rs1(130).rs2(132).fmt(1).rd0(2).virt().fromLog().sign(),
                ReplayWindow.W.op(ReplayerAlu.WMASK_B).rd(138).rs1(130).rs2(132).fmt(1).rd0(2).virt(),
                ReplayWindow.W.op(ReplayerAlu.PEXT_S).rd(129).rs1(137).rs2(138).fmt(1).rd0(2).virt().reportC());
        w.rom(T_SD, ReplayWindow.W.op(ReplayerAlu.SD).rd(132).rs1(130).rs2(131).fmt(2).fromLog().reportC().sign());
        w.rom(T_BEQ, ReplayWindow.W.op(ReplayerAlu.BEQ).rd(132).rs1(130).rs2(131).fmt(3).nfv().reportC().sign());
        w.rom(T_JAL, ReplayWindow.W.op(ReplayerAlu.JAL).rd(129).rs1(132).rs2(128).cnst(0).fmt(5).nfv().imm0pc0().rd0(2).reportC());
        w.rom(T_MUL, ReplayWindow.W.op(ReplayerAlu.MUL).rd(129).rs1(130).rs2(131).fmt(0).rd0(1).reportC());
        w.rom(T_SLLI, ReplayWindow.W.op(ReplayerAlu.MULI).rd(129).rs1(130).rs2(134).fmt(1).rd0(1).virt().reportC());
    }

    static final int[] PROG = {
        i(0x100, 0, 0, A0, OP_IMM),        // 0x1000 addi a0, x0, 0x100
        i(4, A0, 0, A5, LOAD),             // 0x1004 lb a5, 4(a0)
        i(4, A0, 0, 0, LOAD),              // 0x1008 lb x0, 4(a0): remapped, temporaries + 1
        r(0, A0, A0, 0, 0, OP),            // 0x100c add x0, a0, a0: the shared NOP row, virtual
        i(0, 0, 0, 0, OP_IMM),             // 0x1010 addi x0, x0, 0: the shared NOP row, not virtual
        s(8, A5, A0, 3, STORE),            // 0x1014 sd a5, 8(a0)
        i(3, A0, 1, A1, OP_IMM),           // 0x1018 slli a1, a0, 3: VirtualMULI
        r(1, A0, A1, 0, A2, OP),           // 0x101c mul a2, a1, a0: 5 cycles
        b(8, A0, A0, 0, BRANCH),           // 0x1020 beq a0, a0, +8: taken
        b(8, A1, A0, 0, BRANCH),           // 0x1028 beq a0, a1, +8: not taken
        b(0, A0, A0, 0, BRANCH),           // 0x102c beq a0, a0, 0: a self-jump, sequential
        i(1, A0, 0, A0, OP_IMM) & ~1,      // 0x1030 c.addi a0, 1 (stored decompressed, bit 0 clear)
        j(0, 0, JAL),                      // 0x1032 jal x0, 0: the program ends; link into v40
    };

    static ReplayWindow programWindow(boolean cc, boolean rc, boolean wc, boolean storeLog) {
        ReplayWindow w = new ReplayWindow(); w.name = "program";
        templates(w);
        for (int ins : PROG) {
            int op = (ins | 1) & 0x7F, f3 = (ins >>> 12) & 7;
            int t = op == OP ? ((ins >>> 25) == 1 ? T_MUL : T_ADD) : op == OP_IMM ? (f3 == 1 ? T_SLLI : T_ADDI) : op == LOAD ? T_LB
                    : op == STORE ? T_SD : op == BRANCH ? T_BEQ : T_JAL;
            w.decodeRom(ins, t);
        }
        w.pc = 0x1000; w.instrIdx = 1; w.storeLog = storeLog;
        w.regs[A5] = 0x55; w.regs[40] = 0x4040;
        int[] code = Arrays.copyOf(PROG, PROG.length + PADDING);  // then padding words 0
        w.setCode(code, cc);
        List<ReplayWindow.LogEvent> ev = List.of(
                new ReplayWindow.LogEvent(true, V1, true, false, 0),      // lb a5: LD
                new ReplayWindow.LogEvent(true, V2, true, false, 0),      // lb x0: LD
                new ReplayWindow.LogEvent(false, 0, false, storeLog, V2), // sd: the pre-value equals the last LD (E[15])
                new ReplayWindow.LogEvent(true, 17, false, false, 0),     // beq taken: the next instr_idx
                new ReplayWindow.LogEvent(true, 0, false, false, 0));     // jal x0, 0: the program end's log word 0
        w.setLogs(ev, rc, wc);
        return w;
    }

    /**
     * Expected rows: {instr_idx, opcode, rd_reg, rs1_reg, rs2_reg, seq, flags, pc, imm, rs1, rs2, rd_prev, rd, mem_addr,
     * mem_value, sign_imm}; flags are written comp·virt·first·rdI·rs1I·rs2I, left to right. v1.25: re-derived by hand for
     * 821e1bd's 3-row LB (VirtualLoadB, WindowMaskB, PextSigned) and the renumbered opcodes; sign_imm (record word 0
     * [86]) is set on the loads, the stores and the branches (jolt-cpp signed_immediate).
     */
    static long[][] expected(boolean storeLog) {
        long M32 = 0xFFL << 32, F5 = 0xFFFF_FFFF_FFFF_FFF5L;
        return new long[][] {
            { 1, 2, A0, 0, 0, 0, 0b000001, 0x1000, 0x100, 0, 0, 0, 0x100, 0, 0, 0 },
            { 2, 110, 40, A0, 0, 2, 0b011001, 0x1004, 4, 0x100, 0, 0x4040, V1, 0x100, V1, 1 },
            { 3, 156, 41, A0, 0, 1, 0b010001, 0x1004, 4, 0x100, 0, 0, M32, 0, 0, 0 },
            { 4, 146, A5, 40, 41, 0, 0b010000, 0x1004, 0, V1, M32, 0x55, F5, 0, 0, 0 },
            { 5, 110, 41, A0, 0, 2, 0b011001, 0x1008, 4, 0x100, 0, M32, V2, 0x100, V2, 1 },
            { 6, 156, 42, A0, 0, 1, 0b010001, 0x1008, 4, 0x100, 0, 0, M32, 0, 0, 0 },
            { 7, 146, 40, 41, 42, 0, 0b010000, 0x1008, 0, V2, M32, V1, 0, 0, 0, 0 },
            { 8, 2, 0, 0, 0, 0, 0b011001, 0x100c, 0, 0, 0, 0, 0, 0, 0, 0 },
            { 9, 2, 0, 0, 0, 0, 0b000001, 0x1010, 0, 0, 0, 0, 0, 0, 0, 0 },
            { 10, 53, 0, A0, A5, 0, 0b000100, 0x1014, 8, 0x100, F5, 0, 0, 0x108, storeLog ? V2 : 0, 1 },
            { 11, 118, A1, A0, 0, 0, 0b011001, 0x1018, 8, 0x100, 0, 0, 0x800, 0, 0, 0 },
            { 12, 43, A2, A1, A0, 0, 0b000000, 0x101c, 0, 0x800, 0x100, 0, 0x80000, 0, 0, 0 },
            { 13, 28, 0, A0, A0, 0, 0b000100, 0x1020, 8, 0x100, 0x100, 0, 0, 0, 0, 1 },
            { 17, 28, 0, A0, A1, 0, 0b000100, 0x1028, 8, 0x100, 0x800, 0, 0, 0, 0, 1 },
            { 18, 28, 0, A0, A0, 0, 0b000100, 0x102c, 0, 0x100, 0x100, 0, 0, 0, 0, 1 },
            { 19, 2, A0, A0, 0, 0, 0b100001, 0x1030, 1, 0x100, 0, 0x100, 0x101, 0, 0, 0 },
            { 20, 34, 40, 0, 0, 0, 0b000011, 0x1032, 0, 0, 0, 0, 0x1036, 0, 0, 0 },
        };
    }

    static void checkRows(String what, List<long[]> rows, long[][] exp, int padding) {
        T.eq(exp.length + padding, rows.size(), what + ": rows");
        for (int k = 0; k < Math.min(rows.size(), exp.length); k++) {
            long[] g = rows.get(k), e = exp[k];
            long flags = (g[1] >>> 16) & 1 | ((g[1] >>> 17) & 1) << 1 | ((g[1] >>> 18) & 1) << 2 | ((g[1] >>> 19) & 1) << 3 | ((g[1] >>> 20) & 1) << 4 | ((g[1] >>> 21) & 1) << 5;
            long ef = Long.reverse(e[6]) >>> 58;
            long[] got = { g[0] & 0xFFFFFFFFL, (g[0] >>> 32) & 0xFF, (g[0] >>> 40) & 0xFF, (g[0] >>> 48) & 0xFF, g[0] >>> 56, g[1] & 0xFFFF, flags, g[2], g[3], g[4], g[5], g[6], g[7], g[8], g[9], (g[1] >>> Replayer.REC_SIGN_IMM) & 1 };
            long[] want = e.clone(); want[6] = ef;
            T.check(Arrays.equals(got, want), what + ": row " + (k + 1) + "\n   got  " + fmt(got) + "\n   want " + fmt(want));
            T.eq(0, g[1] >>> 23, what + ": row " + (k + 1) + " spare bits");
        }
        for (int k = exp.length; k < rows.size(); k++)
            T.check(Arrays.equals(rows.get(k), new long[10]), what + ": padding row " + (k + 1) + " is all zeros");
    }
    static String fmt(long[] v) { StringBuilder b = new StringBuilder(); for (long x : v) b.append(Long.toHexString(x)).append(' '); return b.toString(); }

    static ReplayDriver runWindow(ReplayWindow w, int rows) {
        ReplayDriver d = new ReplayDriver(w);
        d.r.rowsPerPass = rows;
        d.load();
        d.pass(200_000);
        return d;
    }

    static void program() {
        T.group("program, raw streams");
        ReplayDriver d = runWindow(programWindow(false, false, false, true), 25);
        checkRows("raw", d.rows, expected(true), PADDING);
        T.eq(0, d.r.pc(), "pc 0 after the program end");
        T.eq(0, d.r.instrIdx(), "instr_idx 0 after the program end");
        T.eq(0x1036, d.r.reg(40), "the remapped link is written into v40");
        T.eq(0x101, d.r.reg(A0), "a0");
        T.eq(0, d.r.errors(d.t), "no condition (v1.26: ERRORS [0]-[6])");
        T.check(d.r.clean(), "clean: " + d.r.errorReport());
        T.check(d.done, "done was posted");
    }

    static void programCompressed() {
        long[][] exp = expected(true);
        boolean[][] combos = { { true, true, true }, { true, false, true }, { false, true, false }, { true, true, false }, { false, false, true } };
        for (boolean[] c : combos) {
            String n = "code " + (c[0] ? "LRU" : "raw") + ", read " + (c[1] ? "LRU" : "raw") + ", write " + (c[2] ? "LRU" : "raw");
            T.group("program, " + n);
            ReplayDriver d = runWindow(programWindow(c[0], c[1], c[2], true), 25);
            checkRows(n, d.rows, exp, PADDING);
            T.check(d.r.clean(), n + ": clean " + d.r.errorReport());
            if (c[2]) T.check(d.r.wlog.hits >= 1, n + ": the SD's pre-value came from the last-load entry");
        }
    }

    static void storeLogOff() {
        T.group("includes store log = 0");
        ReplayWindow w = programWindow(false, false, false, false);
        T.eq(0, w.wlog.length, "the write log is empty");
        ReplayDriver d = runWindow(w, 25);
        checkRows("no store log", d.rows, expected(false), PADDING);
        T.eq(0, d.wlogNext, "the write-log queue was never fed (its line stayed quiet)");
        T.check(d.r.clean(), "clean: " + d.r.errorReport());
    }

    // ================================================================ mid-sequence starts (4.19.2, 4.19.4)
    /** v1.25: PEXT for any mask, timed like any row; a non-contiguous mask is counted, not flagged (4.19.7). */
    static void pextAnyMask() {
        T.group("PEXT, a non-contiguous mask");
        ReplayWindow w = new ReplayWindow(); w.fixedRom();
        int ins = r(0, A1, A0, 0, A2, OP);                                  // add a2, a0, a1, served by a PEXT_S template
        w.rom(20, ReplayWindow.W.op(ReplayerAlu.PEXT_S).rd(129).rs1(130).rs2(131).fmt(0).reportC());
        w.decodeRom(ins, 20);
        w.pc = 0x1000; w.instrIdx = 1; w.regs[A0] = 0x8000_0000_0000_0001L; w.regs[A1] = 0x8000_0000_0000_0001L;
        w.setCode(new int[] { ins, ins }, false); w.setLogs(List.of(), false, false);
        ReplayDriver d = runWindow(w, 2);
        T.eq(-1L, d.rows.get(0)[7], "two single-bit runs, top bit set: sign-extended from the last bit placed");
        T.eq(2, d.r.pextNonContiguous, "counted as a statistic");
        T.check(d.r.violations.isEmpty() && d.r.invalids.isEmpty(), "not flagged: " + d.r.violations + d.r.invalids);
        long cyc = d.r.passEndAt - d.r.passStartAt;
        T.check(cyc < 64 + 2 * 3 + 20, "timed like any S row (3 cycles): pass " + cyc);
    }

    static void midStart() {
        T.group("mid-sequence start");
        ReplayWindow w = new ReplayWindow(); templates(w);
        int lbx0 = PROG[2], jal = PROG[12];
        w.decodeRom(lbx0, T_LB); w.decodeRom(jal, T_JAL);
        w.pc = 0x1008; w.instrIdx = 8; w.nextOffset = 1;
        w.regs[A0] = 0x100; w.regs[42] = 0x77; w.regs[41] = 0x99; w.regs[40] = 0x5;
        w.setCode(new int[] { lbx0, jal, 0, 0 }, false);
        w.setLogs(List.of(new ReplayWindow.LogEvent(true, 0, false, false, 0)), false, false);
        ReplayDriver d = runWindow(w, 5);
        T.eq(5, d.rows.size(), "rows");
        long[] r0 = d.rows.get(0), r1 = d.rows.get(1);
        T.eq(ReplayerAlu.WMASK_B, (r0[0] >>> 32) & 0xFF, "the first row is the template's row 1 (WindowMaskB)");
        T.eq(42, (r0[0] >>> 40) & 0xFF, "temporary 1 resolves to 42: the offset is 1 (remap, from ROM[start])");
        T.eq(0, (r0[1] >>> 18) & 1, "not is_first: the expansion's first row ran on the previous tile");
        T.eq(1, (r0[1] >>> 17) & 1, "is_virtual");
        T.eq(8, r0[0] & 0xFFFFFFFFL, "instr_idx from the checkpoint");
        T.eq(ReplayerAlu.PEXT_S, (r1[0] >>> 32) & 0xFF, "then PextSigned");
        T.eq(40, (r1[0] >>> 40) & 0xFF, "into the remapped rd, v40");
        T.eq(0x99, r1[4], "reading v41, the image's value (its VirtualLoadB ran on the previous tile)");
        T.eq(0, d.r.condEvents[0] + d.r.condEvents[1] + d.r.condEvents[2], "no condition");
    }

    // ================================================================ LD to x0 (LR.D with rd = x0; 4.19.4, 4.19.7, appendix F4)
    static void ldToX0() {
        T.group("LD to x0");
        // LR.D x0, (a0) is exempt from the remap; its template's LD names x0. Then an SD whose pre-value equals that load.
        int lrd = 0x02 << 27 | 0 << 20 | A0 << 15 | 3 << 12 | 0 << 7 | AMO;
        int sd = s(0, A5, A0, 3, STORE);
        ReplayWindow w = new ReplayWindow(); templates(w);
        w.rom(30, ReplayWindow.W.op(ReplayerAlu.LD).rd(129).rs1(130).rs2(128).cnst(0).fmt(0).fromLog().reportC());
        w.decodeRom(lrd, 30); w.decodeRom(sd, T_SD);
        w.pc = 0x3000; w.instrIdx = 5; w.regs[A0] = 0x200;
        w.setCode(new int[] { lrd, sd }, false);
        w.setLogs(List.of(new ReplayWindow.LogEvent(true, 0xABCDEF0123L, true, false, 0),
                          new ReplayWindow.LogEvent(false, 0, false, true, 0xABCDEF0123L)), false, true);
        ReplayDriver d = runWindow(w, 2);
        long[] g = d.rows.get(0);
        T.eq(0, (g[0] >>> 40) & 0xFF, "rd_reg 0");
        T.eq(0, g[7], "rd 0: the write is dropped");
        T.eq(0x200, g[8], "mem_addr rs1 + imm");
        T.eq(0, g[9], "mem_value 0: formed from the value written to rd, not the log word (F4)");
        T.eq(0xABCDEF0123L, d.rows.get(1)[9], "the SD's pre-value: the log word coded as index 15 decodes to the popped LD value");
        T.eq(1, d.r.wlog.hits, "via the last-load entry (E[15] takes the popped value even for an LD to x0: REPLAYER-NOTES N5)");
    }

    // ================================================================ controller-fed expansions (4.19.8, 4.19.9)
    static void controllerFed() {
        T.group("controller-fed expansion");
        int inline = r(0, 2, 1, 0, 3, CUSTOM0);
        ReplayWindow w = new ReplayWindow(); w.fixedRom();
        w.decodeClass(inline, 1);
        int n = 300;                                   // longer than one expansion-queue half: the low-water line serves the rest
        ReplayWindow.W[] words = new ReplayWindow.W[n];
        for (int k = 0; k < n; k++) words[k] = ReplayWindow.W.op(ReplayerAlu.ADDI).rd(137 + (k % 8)).rs1(0).rs2(128).cnst(k + 1).fmt(0);
        words[n - 1].reportC();
        w.template(inline, words);
        w.pc = 0x2000; w.instrIdx = 100;
        w.setCode(new int[] { inline, inline }, false);
        w.setLogs(List.of(), false, false);
        ReplayDriver d = runWindow(w, 2 * n);
        T.eq(2 * n, d.rows.size(), "two 300-row inline expansions");
        boolean ok = true;
        for (int k = 0; k < Math.min(2 * n, d.rows.size()); k++) {
            long[] g = d.rows.get(k); int kk = k % n;
            ok &= g[7] == kk + 1 && ((g[0] >>> 40) & 0xFF) == 40 + kk % 8 && (g[0] & 0xFFFFFFFFL) == 100 + k
                    && (g[1] & 0xFFFF) == n - 1 - kk && ((g[1] >>> 18) & 1) == (kk == 0 ? 1 : 0) && g[2] == (k < n ? 0x2000 : 0x2004);
        }
        T.check(ok, "every row: its word's constant, its temporary, its instr_idx, seq_remaining, is_first, pc");
        T.eq(2, d.r.newExpansions, "two new-expansion events");
        T.eq(0, d.missingTemplates, "every presented instruction had a template");
        T.check(d.r.stalls[Replayer.Stall.NEWEXP.ordinal()] > 0, "the first word of each expansion was waited for");
        T.check(d.r.violations.isEmpty() && d.r.invalids.isEmpty() && d.r.overruns.isEmpty(), "clean: " + d.r.violations + d.r.invalids + d.r.overruns);

        T.group("controller-fed, mid-sequence start");
        w.nextOffset = 250; w.setCode(new int[] { inline, inline }, false);
        ReplayDriver d2 = runWindow(w, 50 + n);
        T.eq(50 + n, d2.rows.size(), "rows");
        T.eq(251, d2.rows.get(0)[7], "the first row is word 250: the controller started at PERIPH_STATUS_0's next offset");
        T.eq(49, d2.rows.get(0)[1] & 0xFFFF, "seq_remaining 49");
        T.eq(0, (d2.rows.get(0)[1] >>> 18) & 1, "not is_first");
        T.eq(1, (d2.rows.get(50)[1] >>> 18) & 1, "the next expansion's first row is is_first");
    }

    // ================================================================ N3: the rd = x0 policy on controller-fed words
    static void controllerFedRd0() {
        // A remap template served by the controller (an LB moved out of the ROM): lb x0 → temporaries + 1, rd → v40
        T.group("controller-fed, rd = x0 remap (N3)");
        ReplayWindow w = new ReplayWindow(); templates(w);
        int lbx0 = PROG[2], lba5 = PROG[1], jal = PROG[12];
        w.decodeClass(lbx0, 1); w.decodeClass(lba5, 1); w.decodeRom(jal, T_JAL);
        ReplayWindow.W[] lb = new ReplayWindow.W[3];
        for (int k = 0; k < 3; k++) { lb[k] = new ReplayWindow.W(); lb[k].lo = w.romLo[T_LB + k]; lb[k].hi = w.romHi[T_LB + k]; }
        w.template(lbx0, lb); w.template(lba5, lb);
        w.pc = 0x1004; w.instrIdx = 2; w.regs[A0] = 0x100; w.regs[40] = 0x4040; w.regs[A5] = 0x55;
        w.setCode(new int[] { lba5, lbx0, jal, 0 }, false);
        w.setLogs(List.of(new ReplayWindow.LogEvent(true, V1, true, false, 0), new ReplayWindow.LogEvent(true, V2, true, false, 0),
                new ReplayWindow.LogEvent(true, 0, false, false, 0)), false, false);
        ReplayDriver d = runWindow(w, 8);
        long[][] all = expected(true);
        long[][] exp = new long[6][];
        for (int k = 0; k < 6; k++) exp[k] = all[1 + k].clone();
        // rows 1-6 as the ROM-served program (then the JAL)
        java.util.List<long[]> got = d.rows.subList(0, Math.min(6, d.rows.size()));
        checkRows("controller-fed LB and LB x0", new java.util.ArrayList<>(got), exp, 0);
        T.check(d.r.violations.isEmpty() && d.r.invalids.isEmpty(), "clean: " + d.r.violations + d.r.invalids);

        // A NOP-policy template served by the controller (a DIV moved out of the ROM): div x0 → the shared NOP row
        for (int pol : new int[] { 1, 3 }) {
            T.group("controller-fed, rd = x0 NOP policy " + pol + " (N3)");
            int divx0 = r(1, A1, A0, 4, 0, OP), div = r(1, A1, A0, 4, A2, OP), addi = PROG[0];
            ReplayWindow v = new ReplayWindow(); templates(v);
            v.decodeClass(divx0, 1); v.decodeClass(div, 1); v.decodeRom(addi, T_ADDI);
            ReplayWindow.W[] dw = new ReplayWindow.W[12];
            for (int k = 0; k < 12; k++) dw[k] = ReplayWindow.W.op(ReplayerAlu.ADDI).rd(137 + k % 6).rs1(0).rs2(128).cnst(k).fmt(0);
            dw[0].rd0(pol); dw[11].rd(129).reportC();
            v.template(divx0, dw); v.template(div, dw);
            v.pc = 0x2000; v.instrIdx = 1;
            v.setCode(new int[] { divx0, addi, div, divx0, addi }, false);
            v.setLogs(List.of(), false, false);
            ReplayDriver e = runWindow(v, 1 + 1 + 12 + 1 + 1);
            T.eq(16, e.rows.size(), "rows: NOP, ADDI, 12 DIV rows, NOP, ADDI");
            long[] nop = e.rows.get(0);
            T.eq(2, (nop[0] >>> 32) & 0xFF, "the NOP row is ADDI");
            T.eq(pol == 1 ? 1 : 0, (nop[1] >>> 17) & 1, "is_virtual per rd0 " + pol);
            T.eq(pol == 1 ? 1 : 0, (nop[1] >>> 18) & 1, "is_first per rd0 " + pol);
            T.eq(0, nop[7], "rd 0");
            T.eq(0x100, e.rows.get(1)[7], "the next instruction runs normally");
            T.eq(11, e.rows.get(13)[3], "the full DIV expansion (rd ≠ x0) still runs: its last word's constant");
            T.eq(A2, (e.rows.get(13)[0] >>> 40) & 0xFF, "into rd");
            T.eq(2, (e.rows.get(14)[0] >>> 32) & 0xFF, "the second div x0 is a NOP row too");
            T.check(e.r.exp.occ <= 11, "only the last dropped expansion's words may remain at done (the next run clears them, 4.19.8): " + e.r.exp.occ);
            T.check(e.r.violations.isEmpty() && e.r.invalids.isEmpty() && e.r.overruns.isEmpty(), "clean: " + e.r.violations + e.r.invalids + e.r.overruns);
        }
    }

    // ================================================================ an all-padding window (4.19.2)
    static void padding() {
        T.group("all-padding window");
        ReplayWindow w = new ReplayWindow(); w.fixedRom();
        w.pc = 0; w.instrIdx = 0;
        w.setCode(new int[Replayer.ROWS_PER_PASS], false);
        w.setLogs(List.of(), false, false);
        ReplayDriver d = new ReplayDriver(w); d.load(); d.pass(400_000);
        T.eq(Replayer.ROWS_PER_PASS, d.rows.size(), "65,536 rows");
        boolean zero = true; for (long[] g : d.rows) zero &= Arrays.equals(g, new long[10]);
        T.check(zero, "every record is all zeros");
        T.check(d.done && d.r.violations.isEmpty() && d.r.invalids.isEmpty(), "done, clean");
        long cyc = d.r.passEndAt - d.r.passStartAt;
        T.check(cyc >= 3L * 65536 + 64 && cyc < 3L * 65536 + 64 + 200, "about 3 cycles per row plus the image copy: " + cyc);

        T.group("all-padding window, compressed code");
        w.setCode(new int[Replayer.ROWS_PER_PASS], true);
        T.check(w.code.length / 2 < 2600, "65,536 padding words compress to " + w.code.length / 2 + " entries");
        ReplayDriver d2 = new ReplayDriver(w); d2.load(); d2.pass(400_000);
        boolean zero2 = d2.rows.size() == Replayer.ROWS_PER_PASS; for (long[] g : d2.rows) zero2 &= Arrays.equals(g, new long[10]);
        T.check(zero2 && d2.r.violations.isEmpty(), "65,536 zero records");
    }

    // ================================================================ the pass length as a parameter (debug windows)
    static void debugLength() {
        T.group("debug window length");
        ReplayWindow w = new ReplayWindow(); w.fixedRom();
        w.rows = 256;                                  // a 2^8-row debug window, as its header would give
        w.setCode(new int[256], false);
        w.setLogs(List.of(), false, false);
        ReplayDriver d = new ReplayDriver(w); d.load(); d.pass(10_000);
        T.eq(256, d.rows.size(), "a 256-row pass");
        T.check(d.done && !d.r.isRunning(), "done after 256 rows");
        T.eq(256, d.r.status(d.t, 0) & 0xFFFF, "PERIPH_STATUS_0 counts the rows run");
        T.eq(Replayer.ROWS_PER_PASS, new Replayer(0).rowsPerPass, "the default stays 65,536");
        boolean threw = false;
        try { new Replayer(0, 65537); } catch (IllegalArgumentException e) { threw = true; }
        T.check(threw, "a length past 65,536 is refused");
    }

    // ================================================================ code 145, immediate 0 (26q, 4.19.3)
    static void immediateZero() {
        T.group("operand code 145, immediate 0");
        // secp256k1's two-immediate row: LUI rd, #const, #0, the zero slot written as 145 (26q) and as 132 (R-format source)
        int inline = r(0, 2, 1, 0, 3, CUSTOM0);
        for (int zcode : new int[] { 145, 132 }) {
            ReplayWindow w = new ReplayWindow(); w.fixedRom();
            w.decodeClass(inline, 1);
            w.template(inline, ReplayWindow.W.op(ReplayerAlu.LUI).rd(137).rs1(128).rs2(zcode).cnst(0x1000003d1L).fmt(0).reportC());
            w.pc = 0x3000; w.instrIdx = 7;
            w.setCode(new int[] { inline }, false);
            w.setLogs(List.of(), false, false);
            ReplayDriver d = runWindow(w, 1);
            long[] g = d.rows.get(0);
            T.eq(0x1000003d1L, g[7], "code " + zcode + ": LUI writes the constant");
            T.eq(0x1000003d1L, g[3], "code " + zcode + ": imm = the sum of the immediate slots, the zero adding nothing");
            T.eq(3, (g[1] >>> 20) & 3, "code " + zcode + ": rs1 and rs2 both immediate slots");
            T.eq(0, (g[0] >>> 48) & 0xFFFF, "code " + zcode + ": rs1_reg and rs2_reg 0");
            T.check(d.r.invalids.isEmpty(), "code " + zcode + ": not an invalid expansion " + d.r.invalids);
        }
    }

    // ================================================================ invalid expansions (4.19.11, condition 13)
    static void invalid() {
        // v1.26: a pop past a stream's length is an underflow (ERRORS [3]-[5], v126() below), no longer an invalid expansion
        String[] cases = { "class 3", "reserved operand code", "format 6", "opcode not executed", "past ROM word 511" };
        for (String c : cases) {
            T.group("invalid expansion: " + c);
            ReplayWindow w = new ReplayWindow(); w.fixedRom();
            int ins = i(5, 0, 0, A0, OP_IMM);
            ReplayWindow.W word = ReplayWindow.W.op(ReplayerAlu.ADDI).rd(129).rs1(130).rs2(132).fmt(1).reportC();
            int start = 20;
            switch (c) {
                case "class 3" -> w.decodeClass(ins, 3);
                case "reserved operand code" -> word.rs2(146);
                case "format 6" -> word.fmt(6);
                case "opcode not executed" -> word.lo = word.lo & ~0xFFL | 163;      // VirtualAdviceDivU: written as VirtualAdvice, never executed
                default -> {}
            }
            if (c.equals("past ROM word 511")) {
                start = 511; word.seq(1); w.romLo[511] = word.lo; w.romHi[511] = word.hi;      // seq_remaining 1 at word 511
            } else w.rom(start, word);
            if (!c.equals("class 3")) w.decodeRom(ins, start);
            if (c.equals("read log past its length")) {
                w.rom(start, ReplayWindow.W.op(ReplayerAlu.ADVICE).rd(129).rs1(128).rs2(128).cnst(0).fromLog().reportC());
                w.setLogs(List.of(new ReplayWindow.LogEvent(true, 9, false, false, 0)), false, false);
            } else w.setLogs(List.of(), false, false);
            w.setCode(new int[] { ins, ins, ins }, false);
            ReplayDriver d = runWindow(w, 10);
            long expectRows = c.equals("code past its length") ? 3 : c.equals("read log past its length") || c.equals("past ROM word 511") ? 1 : 0;
            T.eq(1, d.r.condEvents[Replayer.ERR_INVALID], "condition 13 once");
            T.check(d.done, "done posted: the pass stops as if it had ended");
            T.eq(expectRows, d.rows.size(), "rows before it");
            T.check(!d.r.isRunning(), "periph_running fell");
        }
        // v1.25 (DECISIONS-v1.25 s9): a word with a reserved bit [61:55] set (v1.25's untraced and advice_reg) is undefined
        // behavior, not an invalid expansion: meshsim stops. In the ROM and in a controller-fed template.
        for (int bit : new int[] { 55, 56, 61 }) for (boolean fed : new boolean[] { false, true }) {
            T.group("reserved bit " + bit + (fed ? ", controller-fed" : ", ROM"));
            ReplayWindow w = new ReplayWindow(); w.fixedRom();
            ReplayWindow.W word = ReplayWindow.W.op(ReplayerAlu.ADDI).rd(129).rs1(130).rs2(132).fmt(1).reportC().reserved(bit);
            int ins = fed ? r(0, 2, 1, 0, 3, CUSTOM0) : i(5, 0, 0, A0, OP_IMM);
            if (fed) { w.decodeClass(ins, 1); w.template(ins, word); }
            else { w.rom(20, word); w.decodeRom(ins, 20); }
            w.setLogs(List.of(), false, false);
            w.setCode(new int[] { ins, ins }, false);
            T.ub(() -> runWindow(w, 10), "a word with bit " + bit + " set", "replayer", "reserved bits [61:55]", fed ? "controller-fed" : "ROM");
        }
    }

    // ================================================================ the write port (4.19.10)
    static void writePort() {
        T.group("write port");
        Replayer r = new Replayer(0);
        long t = 0;
        // v1.2: byte offsets in region 0x04 (4.19.10); the part is offset [22:20]
        r.accept(t, 3, 0x0, LinkWord.SIZE_128, 0x1234, 0); r.runCycle(t++);
        for (int k = 0; k < 4; k++) r.runCycle(t++);
        T.eq(0x1234, r.region0[0], "the state word landed");
        // v1.25 (DECISIONS-v1.25 s8e): a bad write is undefined behavior, meshsim stops (v1.2 dropped it, counted it and
        // set ERRORS [0], now reserved); and (s8b) a part-0 write off a 128-bit boundary is UB too
        Object[][] badw = { { 0x0, LinkWord.SIZE_64, "a 64-bit packet", "size 64" }, { 0x100000, LinkWord.SIZE_32, "a queue takes 128 bits only", "size 32" },
                { 0x800, LinkWord.SIZE_128, "unmapped in part 0", "unmapped" }, { 0x500000, LinkWord.SIZE_128, "part 5 (v1.25 CL1: 1-4 the queues)", "unmapped" },
                { 0x700000, LinkWord.SIZE_128, "part 7", "unmapped" }, { 0x8, LinkWord.SIZE_128, "part 0 off a 128-bit boundary", "item 47" } };
        for (Object[] bw : badw) {
            Replayer rb = new Replayer(0);
            rb.accept(0, 3, (int) bw[0], (int) bw[1], 1, 1);
            T.ub(() -> { for (long u = 0; u < 8; u++) rb.runCycle(u); }, "a bad write: " + bw[2], "replayer", (String) bw[3]);
        }
        T.eq(0, r.errors(t), "ERRORS [0] is reserved: never set");
        T.eq(1, r.landed(2), "landed(t): a packet accepted at t − 2");
        T.eq(0, r.landed(101), "no packet");
        r.region0[0] = 0;
        r.accept(t, 3, 0x0, LinkWord.SIZE_128, 0x99, 0); r.runCycle(t++);
        r.runCycle(t++);
        T.eq(0, r.region0[0], "not landed at t + 1");
        r.runCycle(t++); r.runCycle(t++);
        T.eq(0x99, r.region0[0], "landed by t + 3 (at the end of t + 2)");
        // ownership: running → region 0 and the table are single-ported, so a write during a pass is a bad write, UB
        // (v1.25, s8e); the ROM isn't (its second port)
        for (int at : new int[] { 0x0, 0x1000 }) {
            ReplayWindow w = programWindow(false, false, false, true);
            ReplayDriver d = new ReplayDriver(w); d.r.rowsPerPass = 25; d.load();
            d.r.run(d.t); d.step(); d.step();
            d.r.accept(d.t, 3, 0x2000 + 16 * 500, LinkWord.SIZE_128, 7, 7); d.step();
            for (int k = 0; k < 4; k++) d.step();
            T.eq(7, d.r.romLo[500], "running: the ROM takes writes (its second port)");
            d.r.accept(d.t, 3, at, LinkWord.SIZE_128, 5, 5);
            T.ub(() -> { for (int k = 0; k < 6; k++) d.step(); }, "running: " + (at == 0 ? "the state word" : "the decode table") + " written during a pass", "replayer", "while a pass runs");
        }
        // v1.25 (DECISIONS-v1.25 s8e): PULSE_RUN while a pass runs is undefined behavior (v1.2 ignored it)
        {
            ReplayWindow w = programWindow(false, false, false, true);
            ReplayDriver d = new ReplayDriver(w); d.r.rowsPerPass = 25; d.load();
            d.r.run(d.t); d.step(); d.step();
            T.check(d.r.running(d.t), "a pass runs");
            d.r.run(d.t);
            T.ub(() -> { for (int k = 0; k < 3; k++) d.step(); }, "PULSE_RUN while a pass runs", "replayer", "PULSE_RUN while a pass runs");
        }
        // one part per queue (v1.25, CL1): offset [22:20] alone names it, anywhere in its 1 MB
        Replayer r3 = new Replayer(0); long t3 = 0;
        int[] at = { 0x100000, 0x1FFFF0, 0x200000, 0x280000, 0x2FFFF0, 0x300000, 0x3ABCD0, 0x400000, 0x4FFFF0, 0x5FFFF0, 0x700000 };
        for (int a : at) if (a < 0x500000) { r3.accept(t3, 3, a, LinkWord.SIZE_128, a, 0); r3.runCycle(t3++); }
        for (int k = 0; k < 4; k++) r3.runCycle(t3++);
        T.eq(2, r3.code.occ, "part 1 (0x100000-0x1FFFFF): the code queue");
        T.eq(3, r3.rlog.occ, "part 2 (0x200000-0x2FFFFF): the read-log queue, 0x280000 included");
        T.eq(2, r3.wlog.occ, "part 3 (0x300000-0x3FFFFF): the write-log queue");
        T.eq(2, r3.exp.occ, "part 4 (0x400000-0x4FFFFF): the expansion queue");
        T.eq(0x280000L, r3.rlog.lo[(r3.rlog.head + 1) % r3.rlog.cap], "0x280000 went to the read log, in order");
        T.eq(0x400000L, r3.exp.lo[r3.exp.head], "0x400000 went to the expansion queue");
        // overrun
        Replayer r2 = new Replayer(0); long t2 = 0;
        for (int k = 0; k < 257; k++) { r2.accept(t2, 3, 0x100000, LinkWord.SIZE_128, k, 0); r2.runCycle(t2++); }
        for (int k = 0; k < 4; k++) r2.runCycle(t2++);
        T.eq(1, r2.condEvents[Replayer.ERR_OVERRUN], "queue overrun on the 257th push");
        T.eq(1 << Replayer.ERR_OVERRUN, r2.errors(t2), "ERRORS [1], queue overrun");
        T.eq(256, r2.code.occ, "256 entries held");
        T.eq(1 << Replayer.ERR_OVERRUN, r2.errors(t2), "a push while stopped is accepted: only the overrun is set");
    }

    // ================================================================ events and credits (4.19.8, 4.19.9, 4.17.3)
    static void lines() {
        T.group("events and credits");
        int code = 1 << Replayer.EV_CODE, rlog = 1 << Replayer.EV_RLOG, wlog = 1 << Replayer.EV_WLOG, exp = 1 << Replayer.EV_EXP, done = 1 << Replayer.EV_RUN_DONE;
        ReplayWindow w = programWindow(false, false, false, true);
        Replayer r = new Replayer(0); r.rowsPerPass = 25;
        ReplayDriver d = new ReplayDriver(w, r); d.load();
        long t = d.t;
        T.check(!r.running(t), "idle: STATUS [0] = 0");
        T.eq(code | rlog | wlog | exp, r.events().value(t), "idle, after the load: the state word's write set every queue's credits to 2 (v1.26; the write log's too: the loop masks it without the store log)");
        r.run(t); d.step(); r.sync(d.t);
        T.check(r.running(t + 1), "running from the end of the run's cycle");
        T.eq(code | rlog | wlog | exp, r.events().value(d.t), "PULSE_RUN leaves them (v1.26)");
        for (int b = Replayer.EV_CODE; b <= Replayer.EV_EXP; b++) T.eq(2, r.events().count(b), "credit bit " + b + ": 2");
        r.events().consume(code); d.step(); r.sync(d.t);
        T.eq(code | rlog | wlog | exp, r.events().value(d.t), "one consuming load: code still has a credit");
        r.events().consume(code); d.step(); r.sync(d.t);
        T.eq(rlog | wlog | exp, r.events().value(d.t), "two: code's bit falls (two refills may be outstanding)");
        T.eq(4, r.params(), "PARAMS: type 4 (4.17.1, D46), version 0");
        T.eq(0, r.status(d.t, 1), "EXPANSION_OPCODE: nothing presented yet");
        T.eq((int) r.word(d.t, 34), r.status(d.t, 1), "word 34 is EXPANSION_OPCODE");
        T.check(r.word(d.t, 35) == null, "word 35 undefined");
        ReplayDriver d3 = runWindow(programWindow(false, false, false, true), 25);
        T.check(d3.done, "the driver saw run done");
        d3.r.sync(d3.t);
        T.eq(0, d3.r.events().value(d3.t) & done, "and its consuming load took it");
        // credits come back: a pass that pops more than half of each log returns credits as it goes
        T.check(d3.rows.size() == 25, "the pass ran its 25 rows on returned credits");
    }

    // ================================================================ timing (4.19.7, 4.19.11)
    static void timing() {
        T.group("timing");
        ReplayDriver d = runWindow(programWindow(false, false, false, true), 25);
        long cyc = d.r.passEndAt - d.r.passStartAt;
        long rowsCycles = 23 * 3 + 2 * 5;                 // MUL and VirtualMULI (slli) are class M
        long stalls = 0; for (long s : d.r.stalls) stalls += s;
        System.out.println("  program pass: " + cyc + " cycles from the run to the last row: rows " + rowsCycles + ", stalls "
                + Arrays.toString(d.r.stalls) + " " + Arrays.toString(Replayer.Stall.values()));
        T.eq(rowsCycles + stalls, cyc, "cycles = row schedules (3, or 5 for class M) + stall cycles (the image copy among them)");
        T.eq(64, d.r.stalls[Replayer.Stall.IMAGE.ordinal()], "the image copy takes 64 cycles");
        T.eq(2, d.r.rowsM, "two class-M rows");
        T.eq(25, d.r.status(d.t, 0) & 0xFFFF, "PERIPH_STATUS_0 [15:0]: rows run");
    }

    // ================================================================ v1.26 (DECISIONS-v1.26)
    static void v126() {
        streamWordLayout();
        underflows();
        leftovers();
        paddingAnything();
        flushes();
        rdPrevFlag();
        recordWords();
        queueSizes();
    }

    /** Item 2: the stream word carries words, values and the compression flag for each stream (parseStreamWord's layout). */
    static void streamWordLayout() {
        T.group("v1.26: the stream word");
        Random g = new Random(26);
        boolean ok = true;
        for (int k = 0; k < 2000; k++) {
            Replayer.StreamSize[] a = new Replayer.StreamSize[3];
            for (int s = 0; s < 3; s++) a[s] = new Replayer.StreamSize(g.nextInt(0x10000), g.nextInt(0x20000), g.nextBoolean());
            long[] w = Replayer.streamWord(a[0], a[1], a[2]);
            Replayer.StreamSize[] b = Replayer.parseStreamWord(w[0], w[1]);
            ok &= Arrays.equals(a, b) && (w[1] >>> 56) == 0;
        }
        T.check(ok, "2,000 random sizes round-trip, [127:120] spare");
        long[] mx = Replayer.streamWord(new Replayer.StreamSize(33_800, 65_536, true), new Replayer.StreamSize(0xFFFF, 0x1FFFF, true), new Replayer.StreamSize(0, 0, false));
        Replayer.StreamSize[] m = Replayer.parseStreamWord(mx[0], mx[1]);
        T.check(m[0].words() == 33_800 && m[0].values() == 65_536 && m[0].compressed() && m[1].words() == 0xFFFF && m[1].values() == 0x1FFFF && !m[2].compressed(),
                "the largest sizes fit (a stream at most about 33,800 words and 65,536 values): " + Arrays.toString(m));
        T.eq(1L << 51, Replayer.stateHi(0, 0, true, false), "the state word: use the store log at [115]");
        T.eq(1L << 52, Replayer.stateHi(0, 0, false, true), "the state word: output rd_prev at [116] (Proposed)");
        boolean thrown = false;
        try { new Replayer.StreamSize(0x10000, 0, false); } catch (IllegalArgumentException e) { thrown = true; }
        T.check(thrown, "a size past its field is refused");
    }

    /** Items 6, 7: a pop past a stream's values, or a decode past its words, is an underflow at once: [3], [4], [5]. */
    static void underflows() {
        String[] cases = { "code past its values", "code past its words", "read log past its values", "read log past its words",
                "write log past its values", "write log past its words", "a transfer past the read log", "write log off: no [5]" };
        for (String c : cases) {
            T.group("v1.26 underflow: " + c);
            ReplayWindow w = new ReplayWindow(); w.fixedRom();
            int ins = i(5, 0, 0, A0, OP_IMM);
            int nIns = 3, rows = 3;
            int expectBit = -1; long expectRows = 0;
            if (c.startsWith("code")) {
                w.rom(20, ReplayWindow.W.op(ReplayerAlu.ADDI).rd(129).rs1(130).rs2(132).fmt(1).reportC());
                w.setLogs(List.of(), false, false);
                if (c.endsWith("values")) { expectRows = 3; rows = 4; }                     // a fourth row needs a fourth instruction
                else { nIns = 5; rows = 5; w.wordsAdj[0] = -1; expectRows = 3; }             // the fifth value's bits are in word 2: at its decode, the fourth row in flight
                expectBit = Replayer.ERR_CODE_UNDER;
            } else if (c.startsWith("read log") || c.startsWith("a transfer")) {
                if (c.startsWith("a transfer")) {
                    ins = j(8, 0, JAL);                                             // jal x0, 8: a transfer takes the next instr_idx from the read log
                    w.rom(20, ReplayWindow.W.op(ReplayerAlu.JAL).rd(129).rs1(132).rs2(128).cnst(0).fmt(5).nfv().imm0pc0().rd0(2).reportC());
                    w.setLogs(List.of(new ReplayWindow.LogEvent(true, 2, false, false, 0)), false, false);
                    expectRows = 1;
                } else {
                    w.rom(20, ReplayWindow.W.op(ReplayerAlu.ADVICE).rd(129).rs1(128).rs2(128).cnst(0).fromLog().reportC());
                    List<ReplayWindow.LogEvent> ev = new ArrayList<>();
                    for (int k = 0; k < (c.endsWith("values") ? 1 : 3); k++) ev.add(new ReplayWindow.LogEvent(true, 9 + k, false, false, 0));
                    w.setLogs(ev, false, false);
                    if (c.endsWith("words")) { w.wordsAdj[1] = -1; expectRows = 2; } else expectRows = 1;   // the third value is in word 2
                }
                expectBit = Replayer.ERR_RLOG_UNDER;
            } else {
                ins = s(8, A1, A0, 3, STORE);                                       // sd: a write-log pop
                w.rom(20, ReplayWindow.W.op(ReplayerAlu.SD).rd(132).rs1(130).rs2(131).fmt(2).fromLog().reportC().sign());
                List<ReplayWindow.LogEvent> ev = new ArrayList<>();
                int n = c.endsWith("values") ? 1 : c.endsWith("words") ? 3 : 0;
                for (int k = 0; k < n; k++) ev.add(new ReplayWindow.LogEvent(false, 0, false, true, 7 + k));
                w.setLogs(ev, false, false);
                if (c.endsWith("words")) { w.wordsAdj[2] = -1; expectRows = 2; } else expectRows = c.endsWith("values") ? 1 : 3;
                w.storeLog = !c.startsWith("write log off");
                if (w.storeLog) expectBit = Replayer.ERR_WLOG_UNDER;
                w.regs[A0] = 0x2000;
            }
            w.decodeRom(ins, 20);
            w.pc = 0x1000; w.instrIdx = 1;
            int[] code = new int[nIns]; Arrays.fill(code, ins);
            w.setCode(code, false);
            ReplayDriver d = runWindow(w, rows);
            if (expectBit < 0) {
                T.eq(0, d.r.errors(d.t), "no error: the write log's sizes are ignored without the store log");
                T.eq(3L, d.rows.size(), "all three rows");
                T.check(d.r.clean(), "clean: " + d.r.errorReport());
                continue;
            }
            T.eq(1 << expectBit, d.r.errors(d.t), "ERRORS [" + expectBit + "] alone");
            T.eq(1, d.r.condEvents[expectBit], "once");
            T.check(d.done && !d.r.isRunning(), "the pass stopped, done posted");
            T.eq(expectRows, d.rows.size(), "rows before it");
            T.check(d.r.underflows.size() == 1 && d.r.invalids.isEmpty() && d.r.leftovers.isEmpty(), "named as an underflow: " + d.r.underflows);
        }
    }

    /** Items 6, 7: done with input left over, [6], one bit for any stream; not for the write log without the store log. */
    static void leftovers() {
        String[] cases = { "a code value left", "a read-log value left", "a write-log value left", "a code word after its last value",
                "a read-log word after its last value", "an entry pushed past the words", "write log off, values left: no [6]", "exact: no [6]" };
        for (String c : cases) {
            T.group("v1.26 leftover: " + c);
            ReplayWindow w = programWindow(false, false, false, !c.startsWith("write log off"));
            int rows = 25;
            switch (c) {
                case "a code value left" -> { int[] code = Arrays.copyOf(PROG, PROG.length + PADDING + 1); w.setCode(code, false); }
                case "a read-log value left", "a write-log value left" -> {
                    List<ReplayWindow.LogEvent> ev = new ArrayList<>(List.of(
                            new ReplayWindow.LogEvent(true, V1, true, false, 0), new ReplayWindow.LogEvent(true, V2, true, false, 0),
                            new ReplayWindow.LogEvent(false, 0, false, true, V2), new ReplayWindow.LogEvent(true, 17, false, false, 0),
                            new ReplayWindow.LogEvent(true, 0, false, false, 0)));
                    ev.add(c.startsWith("a read") ? new ReplayWindow.LogEvent(true, 5, false, false, 0) : new ReplayWindow.LogEvent(false, 0, false, true, 5));
                    w.setLogs(ev, false, false);
                }
                case "a code word after its last value" -> { w.code = Arrays.copyOf(w.code, w.code.length + 2); }
                case "a read-log word after its last value" -> { w.rlog = Arrays.copyOf(w.rlog, w.rlog.length + 2); }
                case "an entry pushed past the words" -> { w.rlog = Arrays.copyOf(w.rlog, w.rlog.length + 2); w.wordsAdj[1] = -1; }
                case "write log off, values left: no [6]" -> {
                    w.setLogs(List.of(new ReplayWindow.LogEvent(true, V1, true, false, 0), new ReplayWindow.LogEvent(true, V2, true, false, 0),
                            new ReplayWindow.LogEvent(false, 0, false, true, V2), new ReplayWindow.LogEvent(true, 17, false, false, 0),
                            new ReplayWindow.LogEvent(true, 0, false, false, 0)), false, false);   // a write-log value the pass never pops
                }
                default -> {}
            }
            ReplayDriver d = runWindow(w, rows);
            T.eq(25L, d.rows.size(), "the pass ran its rows");
            boolean none = c.endsWith("no [6]");
            T.eq(none ? 0 : 1 << Replayer.ERR_LEFTOVER, d.r.errors(d.t), none ? "no error" : "ERRORS [6] alone, at done");
            T.check(d.done, "done posted");
            if (!none) T.check(d.r.leftovers.size() == 1, "named: " + d.r.leftovers);
        }
        // the expansion queue isn't checked (it ends by seq_remaining): a window ending mid-expansion is clean
        T.group("v1.26 leftover: a window ending inside a controller-fed expansion");
        int inline = r(0, 2, 1, 0, 3, CUSTOM0), len = 20;
        ReplayWindow w = new ReplayWindow(); w.fixedRom(); w.decodeClass(inline, 1);
        ReplayWindow.W[] words = new ReplayWindow.W[len];
        for (int k = 0; k < len; k++) words[k] = ReplayWindow.W.op(ReplayerAlu.ADDI).rd(137).rs1(0).rs2(128).cnst(k + 1).fmt(0);
        words[len - 1].reportC();
        w.template(inline, words);
        w.pc = 0x2000; w.instrIdx = 100;
        w.setCode(new int[] { inline }, false); w.setLogs(List.of(), false, false);
        ReplayDriver d = runWindow(w, 10);
        T.eq(10L, d.rows.size(), "10 of its 20 rows");
        T.eq(0, d.r.errors(d.t), "no [6]: the code stream's one value was popped; the expansion queue isn't counted");
    }

    /** Item 5: a stream's padding can be anything, compressed or not: the decoder stops at its values. */
    static void paddingAnything() {
        T.group("v1.26: padding is anything");
        long[][] exp = expected(true);
        int n = 0;
        for (int v = 0; v < 8; v++) {
            boolean cc = (v & 1) != 0, rc = (v & 2) != 0, wc = (v & 4) != 0;
            ReplayWindow p = programWindow(cc, rc, wc, true);
            // the same streams with random padding bits (and the zero-padded ones differ from them)
            p.padSeed = 0x26 + v; p.setCode(Arrays.copyOf(PROG, PROG.length + PADDING), cc);
            ReplayWindow z = programWindow(cc, rc, wc, true);
            boolean differs = !Arrays.equals(z.code, p.code) || !Arrays.equals(z.rlog, p.rlog);
            p.setLogs(List.of(new ReplayWindow.LogEvent(true, V1, true, false, 0), new ReplayWindow.LogEvent(true, V2, true, false, 0),
                    new ReplayWindow.LogEvent(false, 0, false, true, V2), new ReplayWindow.LogEvent(true, 17, false, false, 0),
                    new ReplayWindow.LogEvent(true, 0, false, false, 0)), rc, wc);
            differs |= !Arrays.equals(z.code, p.code) || !Arrays.equals(z.rlog, p.rlog) || !Arrays.equals(z.wlog, p.wlog);
            String name = "code " + (cc ? "LRU" : "raw") + ", read " + (rc ? "LRU" : "raw") + ", write " + (wc ? "LRU" : "raw");
            T.check(differs, name + ": the padding bits are not zero");
            ReplayDriver d = runWindow(p, 25);
            checkRows(name + ", random padding", d.rows, exp, PADDING);
            T.check(d.r.clean() && d.r.errors(d.t) == 0, name + ": clean " + d.r.errorReport());
            n++;
        }
        T.eq(8, n, "every compression combination");
    }

    /** Item 4: the write to word 0 flushes every queue; PULSE_RUN no longer does. */
    static void flushes() {
        T.group("v1.26: the state word's write flushes the queues");
        ReplayWindow w = programWindow(false, false, false, true);
        Replayer r = new Replayer(0); r.rowsPerPass = 25;
        ReplayDriver d = new ReplayDriver(w, r); d.load();
        long f0 = r.flushes;
        // junk in every queue while stopped, then the state word: all of it goes
        for (int a : new int[] { 0x40000, 0x80000, 0xC0000, 0x100000 }) d.port.add(new long[] { a, 0xBAD, 0xBAD });
        d.port.add(new long[] { 0x8, 1, 2 });                         // a register-image write: not a flush
        while (!d.port.isEmpty()) d.step();
        for (int k = 0; k < 4; k++) d.step();
        T.check(r.code.occ == 1 && r.rlog.occ == 1 && r.wlog.occ == 1 && r.exp.occ == 1, "pushed while stopped: held (one entry each)");
        T.eq(f0, r.flushes, "an image write doesn't flush");
        r.events().consume(1 << Replayer.EV_CODE);
        d.port.add(new long[] { 0x4, w.streamLo(), w.streamHi() });   // the stream word: not a flush
        while (!d.port.isEmpty()) d.step();
        for (int k = 0; k < 4; k++) d.step();
        T.eq(1, r.code.occ, "the stream word's write doesn't flush");
        d.resendState();
        T.eq(f0 + 1, r.flushes, "the state word's write flushes");
        T.check(r.code.occ == 0 && r.rlog.occ == 0 && r.wlog.occ == 0 && r.exp.occ == 0, "every queue empty, the expansion queue too");
        for (int b = Replayer.EV_CODE; b <= Replayer.EV_EXP; b++) T.eq(2, r.events().count(b), "credit bit " + b + " set to 2 (the code credit taken above is back)");
        // pushes after the flush and before PULSE_RUN are kept: two halves of code preloaded, the pass is unchanged
        int pre = Math.min(w.code.length / 2, 2 * r.code.half);
        for (int k = 0; k < pre; k++) d.port.add(new long[] { 0x40000, w.code[2 * k], w.code[2 * k + 1] });
        while (!d.port.isEmpty()) d.step();
        for (int k = 0; k < 4; k++) d.step();
        T.eq(pre, r.code.occ, "pushed after the flush, before the run: kept");
        r.events().consume(1 << Replayer.EV_CODE); r.events().consume(1 << Replayer.EV_CODE);   // the credits those pushes used
        d.passesRun = 1; d.codeNext = pre;                             // the driver: no resend, the code already sent
        List<long[]> rows = new ArrayList<>(d.passFrom(200_000, pre));
        checkRows("preloaded code", rows, expected(true), PADDING);
        T.eq(0, r.code.occ, "and the pass used it");
        T.check(r.clean(), "clean: " + r.errorReport());
        // a second pass: the driver resends the state word; the same rows, clean
        List<long[]> again = new ArrayList<>(d.pass(200_000));
        T.check(again.size() == rows.size() && java.util.stream.IntStream.range(0, rows.size()).allMatch(k -> Arrays.equals(rows.get(k), again.get(k))), "a second pass after the state word: the same rows");
        T.check(r.clean(), "clean: " + r.errorReport());
        T.eq(f0 + 2, r.flushes, "one flush per pass");
        // a pass without the state word's write since the last: flagged, and its queues weren't flushed
        T.group("v1.26: a pass without the flush");
        Replayer r2 = new Replayer(0); r2.rowsPerPass = 25;
        ReplayDriver d2 = new ReplayDriver(programWindow(false, false, false, true), r2); d2.load(); d2.pass(200_000);
        T.check(r2.clean(), "the first pass is clean");
        d2.port.add(new long[] { 0x40000, 0xBAD, 0xBAD });
        while (!d2.port.isEmpty()) d2.step();
        for (int k = 0; k < 4; k++) d2.step();
        r2.run(d2.t); d2.step(); d2.step();
        T.eq(1, r2.code.occ, "PULSE_RUN doesn't empty the queues (v1.26)");
        T.check(r2.violations.stream().anyMatch(v -> v.contains("without a write to the state word")), "meshsim flags a pass without the flush: " + r2.violations);
    }

    /** Item 8: the output-rd_prev flag: 0 makes every row's rd_prev (word 3 [63:0]) 0; the rest is unchanged. */
    static void rdPrevFlag() {
        T.group("v1.26: output rd_prev");
        ReplayWindow w = programWindow(false, false, false, true); w.rdPrev = false;
        ReplayDriver d = runWindow(w, 25);
        long[][] e = expected(true);
        for (long[] row : e) row[11] = 0;
        checkRows("rd_prev off", d.rows, e, PADDING);
        T.check(d.r.clean(), "clean: " + d.r.errorReport());
        ReplayDriver d1 = runWindow(programWindow(false, false, false, true), 25);
        int n = 0; for (long[] row : d1.rows) if (row[6] != 0) n++;
        T.check(n > 0, "with it on, rows carry rd_prev (" + n + " non-zero)");
    }

    /** Item 1: the record's words 66 (files) and 67 (the bitmap) are ignored by the replayer. */
    static void recordWords() {
        T.group("v1.26: record words 66 and 67");
        Replayer r = new Replayer(0); long t = 0;
        long[] before = r.region0.clone();
        r.accept(t, 3, 16 * 66, LinkWord.SIZE_128, 0x66, 0x66); r.runCycle(t++);
        r.accept(t, 3, 16 * 67, LinkWord.SIZE_128, 0x67, 0x67); r.runCycle(t++);
        for (int k = 0; k < 4; k++) r.runCycle(t++);
        T.eq(2L, r.ignoredWrites, "both accepted and ignored, not UB");
        T.check(Arrays.equals(before, r.region0), "part 0's words 0-65 unchanged");
        T.eq(0, r.errors(t), "no error");
        ReplayWindow w = programWindow(false, false, false, true);
        long[] rec = w.record(new long[] { 5, 0 });
        T.eq(2 * 68, rec.length, "a record is 68 words");
        T.eq(w.stateLo(), rec[0], "word 0: the state word"); T.eq(w.streamLo(), rec[2], "word 1: the stream word");
        T.eq(w.regs[A5], rec[4 + A5], "words 2-65: the image"); T.eq(5L, rec[2 * 67], "word 67: the bitmap");
    }

    /** V3-12: every queue 4 KB; -Dreplayer.logq=512 (LOG_QUEUE_ENTRIES) gives v1.25's 8 KB logs for a control run. */
    static void queueSizes() {
        T.group("v1.26: the queues are 4 KB");
        Replayer r = new Replayer(0);
        T.check(r.code.cap == 256 && r.rlog.cap == 256 && r.wlog.cap == 256 && Replayer.ExpQueue.CAP == 256, "256 x 128-bit entries each");
        T.check(r.rlog.half == 128 && r.wlog.half == 128, "a credit is 128 entries");
        int keep = Replayer.LOG_QUEUE_ENTRIES;
        Replayer.LOG_QUEUE_ENTRIES = 512;
        Replayer r8 = new Replayer(0);
        Replayer.LOG_QUEUE_ENTRIES = keep;
        T.check(r8.rlog.cap == 512 && r8.wlog.half == 256 && r8.code.cap == 256, "the 8 KB control: logs 512 entries, the code queue unchanged");
        ReplayDriver d = new ReplayDriver(programWindow(true, true, true, true), r8); r8.rowsPerPass = 25; d.load(); d.pass(200_000);
        T.check(r8.clean() && d.rows.size() == 25, "a pass on the 8 KB control replayer: clean " + r8.errorReport());
    }
}
