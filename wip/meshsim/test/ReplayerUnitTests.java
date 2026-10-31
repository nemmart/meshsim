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
            rp.rlog.reset(rc, (int) w.rlogLen); rp.wlog.reset(wc, (int) w.wlogLen);
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
        Replayer rp = new Replayer(0); rp.rlog.reset(true, (int) bits.n);
        for (int k = 0; k < ent.length / 2; k++) rp.rlog.push(k, ent[2 * k], ent[2 * k + 1]);
        long[] expect = { 1, 2, 3, 1, 3, 0x1_0000_0000L };
        for (long e : expect) { T.check(rp.rlog.peek() == Replayer.Stream.AVAIL, "MTF: available"); T.eq(e, rp.rlog.peekValue, "MTF value"); rp.rlog.commit(); }
        T.check(Arrays.equals(Arrays.copyOf(rp.rlog.E, 5), new long[] { 0x1_0000_0000L, 3, 1, 2, 0 }), "MTF table order " + Arrays.toString(Arrays.copyOf(rp.rlog.E, 5)));
        // the write log: hit 15 reorders nothing; a miss leaves E[15] alone and drops E[14]
        Replayer rw = new Replayer(0);
        ReplayWindow.Bits wb = new ReplayWindow.Bits();
        for (int k = 1; k <= 15; k++) ReplayWindow.putValue(wb, -1, k);          // E[0..14] = 15..1
        ReplayWindow.putValue(wb, 15, 0); ReplayWindow.putValue(wb, -1, 99);
        long[] we = wb.entries(); rw.wlog.reset(true, (int) wb.n);
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
        Replayer r2 = new Replayer(0); r2.rlog.reset(true, (int) b2.n);
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
        T.eq(0, d.r.condEvents[0] + d.r.condEvents[1] + d.r.condEvents[2], "no condition");
        T.check(d.r.violations.isEmpty(), "no violation: " + d.r.violations);
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
            T.check(d.r.violations.isEmpty() && d.r.invalids.isEmpty(), n + ": clean " + d.r.violations + d.r.invalids);
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
    }

    // ================================================================ mid-sequence starts (4.19.2, 4.19.4)
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
        String[] cases = { "class 3", "reserved operand code", "format 6", "opcode not executed", "untraced", "past ROM word 511", "code past its length", "read log past its length" };
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
                case "untraced" -> word.untraced();
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
    }

    // ================================================================ the write port (4.19.10)
    static void writePort() {
        T.group("write port");
        Replayer r = new Replayer(0);
        long t = 0;
        // v1.2: byte offsets in region 0x04 (4.19.10); the part is offset [22:20]
        r.accept(t, 3, 0x0, LinkWord.SIZE_128, 0x1234, 0); r.runCycle(t++);
        r.accept(t, 3, 0x8, LinkWord.SIZE_128, 1, 1); r.runCycle(t++);            // part 0 off a multiple of 16 (the director drops such a packet; the port refuses it too)
        r.accept(t, 3, 0x0, LinkWord.SIZE_64, 1, 0); r.runCycle(t++);             // a 64-bit packet
        r.accept(t, 3, 0x100000, LinkWord.SIZE_32, 1, 0); r.runCycle(t++);        // a queue takes 128 bits only
        r.accept(t, 3, 0x800, LinkWord.SIZE_128, 1, 0); r.runCycle(t++);          // unmapped in part 0
        r.accept(t, 3, 0x400000, LinkWord.SIZE_128, 1, 0); r.runCycle(t++);       // part 4
        for (int k = 0; k < 4; k++) r.runCycle(t++);
        T.eq(0x1234, r.region0[0], "the state word landed");
        T.eq(5, r.condEvents[Replayer.ERR_BAD_WRITE], "five bad writes");
        T.eq(1 << Replayer.ERR_BAD_WRITE, r.errors(t), "ERRORS [0], bad write, sticky");
        T.eq(1, r.landed(2), "landed(t): a packet accepted at t − 2");
        T.eq(0, r.landed(101), "no packet");
        r.region0[0] = 0;
        r.accept(t, 3, 0x0, LinkWord.SIZE_128, 0x99, 0); r.runCycle(t++);
        r.runCycle(t++);
        T.eq(0, r.region0[0], "not landed at t + 1");
        r.runCycle(t++); r.runCycle(t++);
        T.eq(0x99, r.region0[0], "landed by t + 3 (at the end of t + 2)");
        // ownership: running → region 0 and the table are bad writes, the ROM isn't
        ReplayWindow w = programWindow(false, false, false, true);
        ReplayDriver d = new ReplayDriver(w); d.r.rowsPerPass = 25; d.load();
        d.r.run(d.t); d.step(); d.step();
        long bad0 = d.r.condEvents[Replayer.ERR_BAD_WRITE];
        d.r.accept(d.t, 3, 0x0, LinkWord.SIZE_128, 5, 5); d.step();
        d.r.accept(d.t, 3, 0x1000, LinkWord.SIZE_128, 5, 5); d.step();
        d.r.accept(d.t, 3, 0x2000 + 16 * 500, LinkWord.SIZE_128, 7, 7); d.step();
        for (int k = 0; k < 4; k++) d.step();
        T.eq(bad0 + 2, d.r.condEvents[Replayer.ERR_BAD_WRITE], "running: the state word and the decode table are bad writes");
        T.eq(7, d.r.romLo[500], "running: the ROM takes writes (its second port)");
        // HALT
        d.r.setHalted(d.t, true); d.step();
        T.check(!d.r.running(d.t), "HALT: periph_running 0");
        T.eq(0, d.r.events().value(d.t), "HALT: no event");
        long bad1 = d.r.condEvents[Replayer.ERR_BAD_WRITE];
        d.r.accept(d.t, 3, 0x100000, LinkWord.SIZE_128, 1, 1); d.step();
        d.r.accept(d.t, 3, 0x0, LinkWord.SIZE_128, 0xAB, 0); d.step();
        for (int k = 0; k < 4; k++) d.step();
        T.eq(bad1 + 1, d.r.condEvents[Replayer.ERR_BAD_WRITE], "HALT: a queue push is a bad write");
        T.eq(0xAB, d.r.region0[0], "HALT: region 0 takes a boot load");
        // overrun
        Replayer r2 = new Replayer(0); long t2 = 0;
        for (int k = 0; k < 257; k++) { r2.accept(t2, 3, 0x100000, LinkWord.SIZE_128, k, 0); r2.runCycle(t2++); }
        for (int k = 0; k < 4; k++) r2.runCycle(t2++);
        T.eq(1, r2.condEvents[Replayer.ERR_OVERRUN], "queue overrun on the 257th push");
        T.eq(1 << Replayer.ERR_OVERRUN, r2.errors(t2), "ERRORS [1], queue overrun");
        T.eq(256, r2.code.occ, "256 entries held");
        T.eq(0, r2.condEvents[Replayer.ERR_BAD_WRITE], "a push while stopped is accepted (no condition)");
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
        T.eq(0, r.events().value(t), "idle: no event");
        r.run(t); d.step(); r.sync(d.t);
        T.check(r.running(t + 1), "running from the end of the run's cycle");
        T.eq(code | rlog | wlog | exp, r.events().value(d.t), "PULSE_RUN: every queue's credits to 2 (the write log's too: the loop masks it without the store log)");
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
}
