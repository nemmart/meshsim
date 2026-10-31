import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.SplittableRandom;
import java.util.TreeSet;

/**
 * A synthetic replay delivery (REPLAY-INPUT-REQUIREMENTS.md, v1.25) from the universal tables, for testing the
 * acceptance kit and measuring replay speed before WORKER9's delivery writer exists. Not a substitute for it: the
 * reference rows here come from this file's own functional replay of the tables, not from jolt-cpp.
 *
 * <p>A random RV64IMAC program (ALU, M, loads and stores of every width, branches, jumps, LR/SC and the AMOs, FENCE,
 * ECALL, host I/O, CSR instructions, every inline) is expanded through the universal tables by a functional replay
 * (no queues, no timing: {@link Func}), which supplies each log word as a tracer would: a load's doubleword and a
 * store's pre-value from a memory model, a division's advice (jolt-cpp's VirtualAdvice* semantics), a transfer's next
 * instr_idx, the program end's 0. <b>Independent check:</b> every ALU, M, memory, branch, jump and atomic instruction is
 * also run by a plain RV64 interpreter ({@link Ref}), and the functional replay's x1-x31, pc and memory must equal it
 * after each instruction; a difference stops the generator. So the tables' ROM templates and the replayer's row
 * semantics are checked against RISC-V itself, on random operands.
 *
 * <p>The windows are consecutive slices of the one program, so a window can start inside an expansion; the last ends
 * the program (JAL x0, 0) and pads. Files per window: the three streams raw and LRU-coded (4.19.15), the state records
 * state (raw), state-lru (all coded) and state-pick (per stream the smaller), rows.bin, end.bin; manifest.json with
 * sizes and SHA-256; the tables under tables/.
 *
 * <p>Usage: java SyntheticDelivery OUT [--program NAME] [--windows N] [--rows R] [--first K] [--seed S]
 *        [--inline-weight W] [--no-end] [--tables DIR]. --first K writes windows K..K+N-1 of the program (the earlier
 *        ones are generated and dropped: a shard, as the acceptance kit asks of a delivery writer).
 */
public final class SyntheticDelivery {
    // ================================================================ options
    Path out; String program = "synthetic"; int windows = 4, rows = Replayer.ROWS_PER_PASS, first = 0; long seed = 1;
    int inlineWeight = 2; boolean end = true; Path tablesDir = ReplayTables.defaultDir();
    ReplayTables tb;
    /** --w9-layout: as WORKER9's delivery_writer lays a delivery out: OUT/PROGRAM.manifest.json beside OUT/PROGRAM/, file keys
     *  without the window prefix, no rows_sha256 and no tables/ (the replayer's are assumed). */
    boolean w9;

    public static void main(String[] a) throws Exception {
        SyntheticDelivery g = new SyntheticDelivery();
        g.out = Path.of(a[0]);
        for (int i = 1; i < a.length; i++) switch (a[i]) {
            case "--program" -> g.program = a[++i];
            case "--windows" -> g.windows = Integer.parseInt(a[++i]);
            case "--rows" -> g.rows = Integer.parseInt(a[++i]);
            case "--first" -> g.first = Integer.parseInt(a[++i]);
            case "--seed" -> g.seed = Long.parseLong(a[++i]);
            case "--inline-weight" -> g.inlineWeight = Integer.parseInt(a[++i]);
            case "--no-end" -> g.end = false;
            case "--tables" -> g.tablesDir = Path.of(a[++i]);
            case "--w9-layout" -> g.w9 = true;
            default -> throw new IllegalArgumentException(a[i]);
        }
        long t0 = System.nanoTime();
        g.run();
        System.out.printf("SyntheticDelivery: %d windows of %d rows (from %d) in %s, %.1f s; %d instructions checked against RV64%n",
                g.windows, g.rows, g.first, g.out, (System.nanoTime() - t0) / 1e9, g.checked);
    }

    // ================================================================ memory (doublewords; unwritten ones are a hash of the address)
    static final class Mem {
        final HashMap<Long, Long> m = new HashMap<>();
        long rd(long a) { a &= ~7L; Long v = m.get(a); return v != null ? v : mix(a * 0x9E3779B97F4A7C15L + 12345); }
        void wr(long a, long v) { m.put(a & ~7L, v); }
        long rdN(long a, int bytes) {                       // little-endian, within one doubleword (aligned accesses only)
            long d = rd(a); int sh = (int) (a & 7) * 8;
            return bytes == 8 ? d : (d >>> sh) & ((1L << (8 * bytes)) - 1);
        }
        void wrN(long a, int bytes, long v) {
            if (bytes == 8) { wr(a, v); return; }
            long d = rd(a); int sh = (int) (a & 7) * 8; long mask = ((1L << (8 * bytes)) - 1) << sh;
            wr(a, (d & ~mask) | ((v << sh) & mask));
        }
    }
    static long mix(long z) { z = (z ^ (z >>> 30)) * 0xBF58476D1CE4E5B9L; z = (z ^ (z >>> 27)) * 0x94D049BB133111EBL; return z ^ (z >>> 31); }

    // ================================================================ the functional replay (a replayer without queues or timing)
    /** One row's log traffic, for the streams. */
    record Log(boolean hasRead, long read, boolean load, boolean hasWrite, long write) {}

    final class Func {
        final long[] regs = new long[128]; long pc; int idx;
        final Mem mem = new Mem();
        // the current instruction
        int w, kind, offset, nextRow, tnum = -1; long[] words; long srcA, srcB;
        boolean active;
        final List<Long> written = new ArrayList<>();       // doublewords this instruction stored
        static final int PAD = 0, ROM = 1, NOP = 2, CTRL = 3;

        /** Begin instruction w at row startRow of its expansion (a window's mid-expansion start: startRow > 0). */
        void start(int w, int startRow) {
            this.w = w; nextRow = startRow; offset = 0; tnum = -1; active = true; written.clear();
            srcA = regs[(w >>> 15) & 31]; srcB = regs[(w >>> 20) & 31];
            if ((w & 3) == 0) { kind = PAD; words = new long[] { tb.romLo[0], tb.romHi[0] }; return; }
            int e = tb.table[Replayer.decodeIndex(w)] & 0xFFFF, cls = e & 3, rdf = (w >>> 7) & 31, rd0;
            if (cls == 3) throw new IllegalStateException("generated an invalid instruction 0x" + Integer.toHexString(w));
            if (cls == 0) {
                int s = (e >>> 2) & 0x1FF; rd0 = (int) ((tb.romLo[s] >>> 46) & 3);
                List<Long> l = new ArrayList<>();
                for (int a = s; ; a++) { l.add(tb.romLo[a]); l.add(tb.romHi[a]); if (((tb.romLo[a] >>> 32) & 0xFFF) == 0) break; }
                words = l.stream().mapToLong(Long::longValue).toArray(); kind = ROM;
            } else {
                tnum = tb.lookup(w);
                if (tnum < 0) throw new IllegalStateException("no template for 0x" + Integer.toHexString(w));
                words = tb.words.get(tnum); rd0 = tb.rd0[tnum]; kind = CTRL;
            }
            if (rdf == 0 && (rd0 == 1 || rd0 == 3)) {          // the shared NOP row, carrying the template's rd0 (N3)
                words = new long[] { tb.romLo[1] & ~(3L << 46) | (long) rd0 << 46, tb.romHi[1] };
                kind = NOP; nextRow = 0;
            } else offset = rdf == 0 && rd0 == 2 ? 1 : 0;
        }
        boolean done() { return !active; }

        int reg(int c) {
            if (c < 128) return c;
            if (c == 129) return offset == 1 ? 40 : (w >>> 7) & 31;
            if (c == 130) return (w >>> 15) & 31;
            if (c == 131) return (w >>> 20) & 31;
            if (c >= 137 && c <= 144) return 40 + (c - 137) + offset;
            return -1;
        }
        long imm(int c, long cnst, long si) {
            return switch (c) { case 128 -> cnst; case 145 -> 0; case 132 -> si; case 133 -> -(1L << (si & 63)); case 134 -> 1L << (si & 63);
                case 135 -> 1L << (si & 31); case 136 -> (1L << 32) - (1L << (si & 31)); default -> throw new IllegalStateException("code " + c); };
        }

        /** Run the next row: its record (10 longs) and its log traffic. */
        long[] step(List<Log> logs) {
            int row = nextRow;
            long lo = words[2 * row], hi = words[2 * row + 1];
            int op = (int) (lo & 0xFF), cRd = (int) (lo >>> 8) & 0xFF, cRs1 = (int) (lo >>> 16) & 0xFF, cRs2 = (int) (lo >>> 24) & 0xFF;
            int seq = (int) (lo >>> 32) & 0xFFF, format = (int) (lo >>> 48) & 7;
            boolean nfv = ((lo >>> 44) & 1) != 0, hold = ((lo >>> 45) & 1) != 0, virt = ((lo >>> 51) & 1) != 0, imm0pc0 = ((lo >>> 52) & 1) != 0;
            boolean reportC = ((lo >>> 53) & 1) != 0, fromLog = ((lo >>> 54) & 1) != 0, signImm = ((lo >>> 62) & 1) != 0, offImm = ((lo >>> 63) & 1) != 0;
            if (!ReplayerAlu.executes(op) || ((lo >>> 55) & 1) != 0) throw new IllegalStateException("bad word in 0x" + Integer.toHexString(w));
            long si = Replayer.sourceImm(format, w), so = offImm ? si : 0;
            int rRd = reg(cRd), rRs1 = reg(cRs1), rRs2 = reg(cRs2);
            long vRd = rRd >= 0 ? rv(rRd) : imm(cRd, hi, si), vRs1 = rRs1 >= 0 ? rv(rRs1) : imm(cRs1, hi, si), vRs2 = rRs2 >= 0 ? rv(rRs2) : imm(cRs2, hi, si);
            long immSum = (rRd < 0 ? vRd : 0) + (rRs1 < 0 ? vRs1 : 0) + (rRs2 < 0 ? vRs2 : 0);
            // the row's log word, as the tracer writes it
            long logWord = 0; boolean rd_ = false, wr_ = false, ld = false; long addr = 0;
            if (fromLog) {
                if (ReplayerAlu.isLoad(op)) { addr = ReplayerAlu.memAddr(op, vRs1, immSum); logWord = mem.rd(addr); rd_ = true; ld = true; }
                else if (ReplayerAlu.isStore(op)) { addr = ReplayerAlu.memAddr(op, vRs1, immSum); logWord = mem.rd(addr); wr_ = true; }
                else { logWord = advice(); rd_ = true; }
            }
            boolean last = seq == 0 || kind == PAD || kind == NOP, compressed = (w & 1) == 0;
            int pcInc = !last ? 0 : compressed ? 2 : 4;
            ReplayerAlu.Out o = new ReplayerAlu.Out();
            ReplayerAlu.exec(op, vRs1, vRs2, vRd, pc, pcInc, imm0pc0, logWord, so, o);
            boolean writes = rRd >= 0 && op != ReplayerAlu.NOP;
            long rdAfter = rRd < 0 ? 0 : rRd == 0 ? 0 : writes ? o.rd : vRd;
            long newPc; int newIdx; long transferWord = -1;
            if (hold) { newPc = pc; newIdx = idx; }
            else if (o.newPc == 0) { newPc = 0; newIdx = 0; if (nfv) transferWord = 0; }
            else if (o.newPc != pc) { newPc = o.newPc; newIdx = (int) (mix(newPc) & 0x3FFFFFF) + 1; transferWord = newIdx; }
            else { newPc = pc + pcInc; newIdx = idx + 1; }
            if (transferWord >= 0) { if (rd_) throw new IllegalStateException("two read-log words in one row"); rd_ = true; logWord = transferWord; }
            if (rd_ || wr_) logs.add(new Log(rd_, rd_ ? logWord : 0, ld, wr_, wr_ ? logWord : 0));   // a store's log word: its pre-value
            long[] r = new long[10];
            if (kind != PAD) {
                boolean isVirtual = kind == NOP ? ((lo >>> 46) & 3) == 1 : (virt || seq > 0 || row > 0);
                boolean isFirst = isVirtual && (kind == NOP || row == 0);
                r[0] = (idx & 0xFFFFFFFFL) | (long) op << 32 | (long) Math.max(rRd, 0) << 40 | (long) Math.max(rRs1, 0) << 48 | (long) Math.max(rRs2, 0) << 56;
                r[1] = (seq & 0xFFFFL) | (compressed && reportC ? 1L : 0) << 16 | (isVirtual ? 1L : 0) << 17 | (isFirst ? 1L : 0) << 18
                        | (rRd < 0 ? 1L : 0) << 19 | (rRs1 < 0 ? 1L : 0) << 20 | (rRs2 < 0 ? 1L : 0) << 21 | (signImm ? 1L : 0) << Replayer.REC_SIGN_IMM;
                r[2] = pc; r[3] = ReplayerAlu.isShiftData(op) ? so : immSum;
                r[4] = rRs1 >= 0 ? vRs1 : 0; r[5] = rRs2 >= 0 ? vRs2 : 0; r[6] = rRd >= 0 ? vRd : 0; r[7] = rdAfter;
                if (ReplayerAlu.isLoad(op)) { r[8] = addr; r[9] = rdAfter; }
                else if (ReplayerAlu.isStore(op)) { r[8] = addr; r[9] = logWord; }
            }
            // commit: the store's doubleword (rs2's value, the whole doubleword: jolt-cpp), rd, pc, instr_idx
            if (ReplayerAlu.isStore(op) && fromLog) { mem.wr(addr, vRs2); written.add(addr & ~7L); }
            if (writes && rRd != 0) regs[rRd] = o.rd;
            pc = newPc; idx = newIdx;
            nextRow++;
            if (last) active = false;
            return r;
        }
        long rv(int r) { return r == 0 ? 0 : regs[r]; }

        /** jolt-cpp's division advice (trace_owner.cpp VirtualAdvice*), from the source instruction's operands. Inlines' advice: random. */
        long advice() {
            int op = (w | 1) & 0x7F, f3 = (w >>> 12) & 7, f7 = w >>> 25;
            long a = srcA, b = srcB;
            if (f7 == 1 && op == 0x33) switch (f3) {
                case 4: return b == 0 ? -1 : (a == Long.MIN_VALUE && b == -1) ? a : a / b;
                case 5: return b == 0 ? -1 : Long.divideUnsigned(a, b);
                case 6: return b == 0 ? 0 : (a == Long.MIN_VALUE && b == -1) ? Long.MIN_VALUE : Math.abs(a / b);   // unsigned_abs
                case 7: return b == 0 ? 0 : Long.divideUnsigned(a, b);
            }
            if (f7 == 1 && op == 0x3B) {
                int x = (int) a, y = (int) b;
                switch (f3) {
                    case 4: return y == 0 ? -1 : (x == Integer.MIN_VALUE && y == -1) ? 0x80000000L : (long) (x / y);
                    case 5: return y == 0 ? 0xFFFF_FFFFL : Integer.toUnsignedLong(Integer.divideUnsigned(x, y));
                    case 6: return y == 0 ? 0 : (x == Integer.MIN_VALUE && y == -1) ? 0x80000000L : Integer.toUnsignedLong(Math.abs(x / y));
                    case 7: return y == 0 ? 0 : Integer.toUnsignedLong(Integer.divideUnsigned(x, y));
                }
            }
            return rnd.nextLong();
        }
    }

    // ================================================================ the RV64 reference (checked instructions only)
    final class Ref {
        final long[] x = new long[32]; long pc; final Mem mem = new Mem(); long resv = -1;
        /** Execute w; false if it isn't one the reference checks (the caller then copies the functional state). */
        boolean exec(int w) {
            int op = (w | 1) & 0x7F, rd = (w >>> 7) & 31, rs1 = (w >>> 15) & 31, rs2 = (w >>> 20) & 31, f3 = (w >>> 12) & 7, f7 = w >>> 25;
            long a = x[rs1], b = x[rs2], inc = (w & 1) == 0 ? 2 : 4, npc = pc + inc, v = 0; boolean wr = true;
            long iI = Replayer.sourceImm(1, w), iS = Replayer.sourceImm(2, w), iB = Replayer.sourceImm(3, w), iU = Replayer.sourceImm(4, w), iJ = Replayer.sourceImm(5, w);
            switch (op) {
                case 0x37 -> v = iU;
                case 0x17 -> v = pc + iU;
                case 0x6F -> { if (iJ == 0) return false; v = pc + inc; npc = pc + iJ; }
                case 0x67 -> { v = pc + inc; npc = (a + iI) & ~1L; }
                case 0x63 -> { wr = false; boolean t = switch (f3) { case 0 -> a == b; case 1 -> a != b; case 4 -> a < b; case 5 -> a >= b;
                        case 6 -> Long.compareUnsigned(a, b) < 0; case 7 -> Long.compareUnsigned(a, b) >= 0; default -> throw new IllegalStateException(); };
                    if (t) npc = pc + iB; }
                case 0x03 -> { long ad = a + iI; v = switch (f3) { case 0 -> (byte) mem.rdN(ad, 1); case 1 -> (short) mem.rdN(ad, 2); case 2 -> (int) mem.rdN(ad, 4);
                        case 3 -> mem.rdN(ad, 8); case 4 -> mem.rdN(ad, 1); case 5 -> mem.rdN(ad, 2); case 6 -> mem.rdN(ad, 4); default -> throw new IllegalStateException(); }; }
                case 0x23 -> { wr = false; mem.wrN(a + iS, 1 << f3, b); }
                case 0x13 -> { int sh = (int) (iI & 63); v = switch (f3) { case 0 -> a + iI; case 1 -> a << sh; case 2 -> a < iI ? 1 : 0; case 3 -> Long.compareUnsigned(a, iI) < 0 ? 1 : 0;
                        case 4 -> a ^ iI; case 5 -> (w & 1 << 30) != 0 ? a >> sh : a >>> sh; case 6 -> a | iI; default -> a & iI; }; }
                case 0x1B -> { int sh = (int) (iI & 31); v = switch (f3) { case 0 -> (int) (a + iI); case 1 -> (int) a << sh;
                        case 5 -> (w & 1 << 30) != 0 ? (int) a >> sh : (int) ((int) a >>> sh); default -> throw new IllegalStateException(); }; }
                case 0x33 -> {
                    if (f7 == 1) v = switch (f3) { case 0 -> a * b; case 1 -> Math.multiplyHigh(a, b); case 2 -> Math.multiplyHigh(a, b) + mulhsuFix(a, b);
                        case 3 -> ReplayerAlu.mulhu(a, b); case 4 -> b == 0 ? -1 : (a == Long.MIN_VALUE && b == -1) ? a : a / b; case 5 -> b == 0 ? -1 : Long.divideUnsigned(a, b);
                        case 6 -> b == 0 ? a : (a == Long.MIN_VALUE && b == -1) ? 0 : a % b; default -> b == 0 ? a : Long.remainderUnsigned(a, b); };
                    else v = switch (f3) { case 0 -> f7 == 0x20 ? a - b : a + b; case 1 -> a << (b & 63); case 2 -> a < b ? 1 : 0; case 3 -> Long.compareUnsigned(a, b) < 0 ? 1 : 0;
                        case 4 -> a ^ b; case 5 -> f7 == 0x20 ? a >> (b & 63) : a >>> (b & 63); case 6 -> a | b; default -> a & b; };
                }
                case 0x3B -> {
                    int p = (int) a, q = (int) b;
                    if (f7 == 1) v = switch (f3) { case 0 -> p * q; case 4 -> q == 0 ? -1 : (p == Integer.MIN_VALUE && q == -1) ? p : p / q;
                        case 5 -> q == 0 ? -1 : Integer.divideUnsigned(p, q); case 6 -> q == 0 ? p : (p == Integer.MIN_VALUE && q == -1) ? 0 : p % q;
                        case 7 -> q == 0 ? p : Integer.remainderUnsigned(p, q); default -> throw new IllegalStateException(); };
                    else v = switch (f3) { case 0 -> f7 == 0x20 ? p - q : p + q; case 1 -> p << (q & 31); case 5 -> f7 == 0x20 ? p >> (q & 31) : p >>> (q & 31); default -> throw new IllegalStateException(); };
                }
                case 0x2F -> {
                    int f5 = w >>> 27; boolean dw = f3 == 3; int n = dw ? 8 : 4;
                    long old = dw ? mem.rdN(a, 8) : (int) mem.rdN(a, 4);
                    if (f5 == 2) { v = old; resv = a; }
                    else if (f5 == 3) { if (resv == a) { mem.wrN(a, n, b); v = 0; } else v = 1; resv = -1; }
                    else {
                        long bb = dw ? b : (int) b, nv = switch (f5) { case 1 -> bb; case 0 -> old + bb; case 4 -> old ^ bb; case 12 -> old & bb; case 8 -> old | bb;
                            case 16 -> Math.min(old, bb); case 20 -> Math.max(old, bb);
                            case 24 -> dw ? (Long.compareUnsigned(old, bb) < 0 ? old : bb) : (Integer.compareUnsigned((int) old, (int) bb) < 0 ? old : bb);
                            case 28 -> dw ? (Long.compareUnsigned(old, bb) > 0 ? old : bb) : (Integer.compareUnsigned((int) old, (int) bb) > 0 ? old : bb);
                            default -> throw new IllegalStateException("amo " + f5); };
                        mem.wrN(a, n, nv); v = old;
                    }
                }
                case 0x0F -> wr = false;
                default -> { return false; }
            }
            if (wr && rd != 0) x[rd] = v;
            if (npc == pc) npc = pc + inc;      // jolt-cpp's rule (appendix 1.4): a transfer to its own pc is sequential
            pc = npc;
            return true;
        }
        long mulhsuFix(long a, long b) { return (b >> 63) & a; }   // multiplyHigh(a, b) treats b as signed: add a back when b's top bit is set
    }

    // ================================================================ the program
    final SplittableRandom rnd = new SplittableRandom();
    SplittableRandom gen;
    static final long BASE2 = 0x8010_0000L, BASE3 = 0x8020_0000L, BASE4 = 0x8030_0000L, MTVEC = 0x8000_1000L;
    int lrPending = -1;                                      // after an LR: the SC to emit next (same address)
    static final int[] CSRS = { 0x300, 0x305, 0x340, 0x341, 0x342, 0x343 };

    int reg() { return gen.nextInt(32); }
    int rdReg() { int r; do r = gen.nextInt(32); while (r >= 2 && r <= 4); return gen.nextInt(10) == 0 ? 0 : r; }
    static int R(int f7, int rs2, int rs1, int f3, int rd, int op) { return f7 << 25 | rs2 << 20 | rs1 << 15 | f3 << 12 | rd << 7 | op; }
    static int I(int imm, int rs1, int f3, int rd, int op) { return (imm & 0xFFF) << 20 | rs1 << 15 | f3 << 12 | rd << 7 | op; }
    static int S(int imm, int rs2, int rs1, int f3, int op) { return (imm >> 5 & 0x7F) << 25 | rs2 << 20 | rs1 << 15 | f3 << 12 | (imm & 31) << 7 | op; }
    static int B(int imm, int rs2, int rs1, int f3) { return (imm >> 12 & 1) << 31 | (imm >> 5 & 0x3F) << 25 | rs2 << 20 | rs1 << 15 | f3 << 12 | (imm >> 1 & 15) << 8 | (imm >> 11 & 1) << 7 | 0x63; }
    static int J(int imm, int rd) { return (imm >> 20 & 1) << 31 | (imm >> 1 & 0x3FF) << 21 | (imm >> 11 & 1) << 20 | (imm >> 12 & 0xFF) << 12 | rd << 7 | 0x6F; }

    /** The next instruction word (bit 0 clear for a compressed one, as the code stream stores it). */
    int next() {
        if (lrPending != -1) { int sc = lrPending; lrPending = -1; return sc; }
        int w, pick = gen.nextInt(100 + inlineWeight);
        boolean canC = false;
        if (pick < 30) {                                                    // ALU, register and immediate, 64- and 32-bit
            int k = gen.nextInt(4);
            if (k == 0) { int f3 = gen.nextInt(8); int f7 = (f3 == 0 || f3 == 5) && gen.nextBoolean() ? 0x20 : 0; w = R(f7, reg(), reg(), f3, rdReg(), 0x33); }
            else if (k == 1) { int f3 = gen.nextInt(8); int imm = f3 == 1 ? gen.nextInt(64) : f3 == 5 ? gen.nextInt(64) | (gen.nextBoolean() ? 0x400 : 0) : gen.nextInt(4096) - 2048; w = I(imm, reg(), f3, rdReg(), 0x13); }
            else if (k == 2) { int f3 = new int[] { 0, 1, 5 }[gen.nextInt(3)]; int f7 = (f3 == 0 || f3 == 5) && gen.nextBoolean() ? 0x20 : 0; w = R(f7, reg(), reg(), f3, rdReg(), 0x3B); }
            else { int f3 = new int[] { 0, 1, 5 }[gen.nextInt(3)]; int imm = f3 == 0 ? gen.nextInt(4096) - 2048 : gen.nextInt(32) | (f3 == 5 && gen.nextBoolean() ? 0x400 : 0); w = I(imm, reg(), f3, rdReg(), 0x1B); }
            canC = true;
        } else if (pick < 34) w = (gen.nextInt(1 << 20) << 12) | rdReg() << 7 | (gen.nextBoolean() ? 0x37 : 0x17);
        else if (pick < 44) { int f3 = gen.nextInt(8); w = R(1, reg(), reg(), f3, rdReg(), 0x33); }    // M
        else if (pick < 47) { int f3 = new int[] { 0, 4, 5, 6, 7 }[gen.nextInt(5)]; w = R(1, reg(), reg(), f3, rdReg(), 0x3B); }
        else if (pick < 60) { int f3 = gen.nextInt(7), n = 1 << (f3 & 3); int base = 2 + gen.nextInt(3);
            w = I((gen.nextInt(256) - 128) * n, base, f3, rdReg(), 0x03); canC = true; }                // loads, every width
        else if (pick < 70) { int f3 = gen.nextInt(4), n = 1 << f3; int base = 2 + gen.nextInt(3);
            w = S((gen.nextInt(256) - 128) * n, reg(), base, f3, 0x23); canC = true; }                  // stores
        else if (pick < 80) { int f3 = new int[] { 0, 1, 4, 5, 6, 7 }[gen.nextInt(6)]; w = B((gen.nextInt(2048) - 1024) * 2, reg(), reg(), f3); canC = true; }
        else if (pick < 83) { int imm; do imm = (gen.nextInt(1 << 19) - (1 << 18)) * 2; while (imm == 0); w = J(imm, rdReg()); canC = true; }
        else if (pick < 85) { w = I(gen.nextInt(4096) - 2048, 2 + gen.nextInt(3), 0, rdReg(), 0x67); canC = true; }   // JALR off a base: pc stays far from 0 (the program end)
        else if (pick < 93) {                                                // atomics, .W and .D
            boolean dw = gen.nextBoolean(); int f3 = dw ? 3 : 2, base = 2 + gen.nextInt(3), rd = rdReg();
            int[] f5s = { 2, 1, 0, 4, 12, 8, 16, 20, 24, 28 };
            int f5 = f5s[gen.nextInt(f5s.length)];
            int aq = gen.nextInt(4);
            if (f5 == 2) { w = (2 << 27) | aq << 25 | base << 15 | f3 << 12 | rd << 7 | 0x2F; lrPending = gen.nextInt(4) == 0 ? -1 : (3 << 27) | gen.nextInt(4) << 25 | reg() << 20 | base << 15 | f3 << 12 | rdReg() << 7 | 0x2F; }
            else w = (f5 << 27) | aq << 25 | reg() << 20 | base << 15 | f3 << 12 | rd << 7 | 0x2F;
        } else if (pick < 94) w = 0x0FF0000F;                                // FENCE
        else if (pick < 95) w = 0x0000205B;                                  // VirtualHostIO, the host-I/O word (4.19.5)
        else if (pick < 96) w = 0x00000073;                                  // ECALL: to mtvec
        else if (pick < 98) { int csr = CSRS[gen.nextInt(CSRS.length)]; int f3 = 1 + gen.nextInt(2);
            int rd = gen.nextInt(3) == 0 ? 0 : rdReg(), rs1 = gen.nextInt(3) == 0 ? 0 : gen.nextInt(3) == 0 && rd != 0 ? rd : reg();
            if (csr == 0x305) { f3 = 2; rs1 = 0; }                           // mtvec: read only, so ECALL keeps its target
            w = csr << 20 | rs1 << 15 | f3 << 12 | rd << 7 | 0x73; }
        else if (pick < 100) w = 0x0FF0000F;
        else {                                                               // an inline: every one of the index's class-1 keys
            List<Integer> inl = new ArrayList<>(); for (int k = 0; k < tb.count(); k++) if (tb.cls[k] == 1) inl.add(tb.key[k]);
            int key = inl.get(gen.nextInt(inl.size()));
            w = key | 4 << 7 | 2 << 15 | 3 << 20;                            // rd x4, rs1 x2, rs2 x3: the arenas
        }
        if (canC && gen.nextInt(5) == 0) w &= ~1;                            // stored decompressed, bit 0 clear
        return w;
    }

    // ================================================================ the run
    long checked;

    void run() throws Exception {
        tb = ReplayTables.load(tablesDir);
        gen = new SplittableRandom(seed);
        Func f = new Func(); Ref ref = new Ref();
        for (int i = 1; i < 32; i++) f.regs[i] = gen.nextInt(3) == 0 ? gen.nextInt(64) - 32 : gen.nextLong();
        f.regs[2] = BASE2; f.regs[3] = BASE3; f.regs[4] = BASE4; f.regs[34] = MTVEC; f.regs[39] = 0x1800;
        f.pc = 0x8000_0000L; f.idx = 1;
        System.arraycopy(f.regs, 0, ref.x, 0, 32); ref.pc = f.pc;
        Path pd = out.resolve(program); Files.createDirectories(pd);
        if (!w9) {
            Files.createDirectories(out.resolve("tables"));
            for (String n : new String[] { "decode_table.bin", "rom.bin", "templates.bin", "template_index.bin" })
                Files.write(out.resolve("tables").resolve(n), Files.readAllBytes(tablesDir.resolve(n)));
        }
        int total = first + windows;
        List<Map<String, Object>> entries = new ArrayList<>();
        boolean ended = false; int afterEnd = 0;
        // the program ends (JAL x0, 0) half a window before the last window's end, which then pads (an inline is <= 3,121 rows)
        long endAt = (long) total * rows - rows / 2;
        int instrs = 0;
        for (int win = 0; win < total; win++) {
            boolean keep = win >= first;
            // the state at the window's start
            long[] startRegs = f.regs.clone(); long startPc = f.pc; int startIdx = f.idx, startOff = f.active ? f.nextRow : 0;
            List<Integer> code = new ArrayList<>(); List<Log> logs = new ArrayList<>(); List<long[]> rowsOut = new ArrayList<>();
            TreeSet<Integer> used = new TreeSet<>();
            if (f.active) { code.add(f.w); if (f.tnum >= 0) used.add(f.tnum); }
            for (int r = 0; r < rows; r++) {
                if (!f.active) {
                    int w;
                    if (ended) w = 0;
                    else if (end && rowCount >= endAt) { w = J(0, 0); ended = true; }
                    else { w = next(); instrs++; }
                    code.add(w);
                    // the reference runs the instruction whole, before the functional replay does it row by row
                    boolean chk = (w & 3) != 0 && !ended && ref.exec(w);
                    hist[histN++ & 7] = String.format("%08x@%x", w, f.pc);
                    f.start(w, 0);
                    if (f.tnum >= 0) used.add(f.tnum);
                    refCheck = chk;
                }
                List<Log> l = new ArrayList<>();
                long[] row = f.step(l);
                logs.addAll(l); rowCount++;
                rowsOut.add(row);
                if (!f.active) afterInstr(f, ref);
            }
            if (ended) afterEnd++;
            if (keep) entries.add(writeWindow(pd, win, startRegs, startPc, startIdx, startOff, code, logs, rowsOut, used, f));
        }
        writeManifest(entries);
    }
    boolean refCheck; long rowCount;

    void afterInstr(Func f, Ref ref) {
        if (refCheck) {
            checked++;
            for (int i = 1; i < 32; i++) if (f.regs[i] != ref.x[i]) fail(f, "x" + i + ": replay 0x" + Long.toHexString(f.regs[i]) + ", RV64 0x" + Long.toHexString(ref.x[i]));
            if (f.pc != ref.pc) fail(f, "pc: replay 0x" + Long.toHexString(f.pc) + ", RV64 0x" + Long.toHexString(ref.pc));
            for (long a : f.written) if (f.mem.rd(a) != ref.mem.rd(a)) fail(f, "memory 0x" + Long.toHexString(a) + ": replay 0x" + Long.toHexString(f.mem.rd(a)) + ", RV64 0x" + Long.toHexString(ref.mem.rd(a)));
            for (Map.Entry<Long, Long> e : ref.mem.m.entrySet()) if (f.mem.rd(e.getKey()) != e.getValue()) fail(f, "memory 0x" + Long.toHexString(e.getKey()) + " (RV64 wrote it)");
        } else {                                                  // not checked: the reference takes the replay's state
            System.arraycopy(f.regs, 0, ref.x, 0, 32); ref.x[0] = 0; ref.pc = f.pc;
            for (long a : f.written) ref.mem.wr(a, f.mem.rd(a));
            if (f.regs[2] != BASE2 || f.regs[3] != BASE3 || f.regs[4] != BASE4) fail(f, "an unchecked instruction moved a base register (x2-x4)");
        }
        refCheck = false;
    }
    final String[] hist = new String[8]; int histN;
    void fail(Func f, String m) {
        StringBuilder h = new StringBuilder(); for (int i = 8; i >= 1; i--) if (histN - i >= 0) h.append(' ').append(hist[(histN - i) & 7]);
        throw new IllegalStateException("RV64 check failed after 0x" + String.format("%08x", f.w) + " (now pc 0x" + Long.toHexString(f.pc) + "): " + m + "; last instructions (word@pc):" + h);
    }

    // ================================================================ the files
    Map<String, Object> writeWindow(Path pd, int win, long[] regs, long pc, int idx, int off, List<Integer> code, List<Log> logs, List<long[]> rowsOut,
                                    TreeSet<Integer> used, Func f) throws Exception {
        String base = String.format("window_%05d.", win);
        Map<String, Object> e = new LinkedHashMap<>(), files = new LinkedHashMap<>();
        e.put("index", (long) win); e.put("first_row", (long) win * rows); e.put("rows", (long) rows);
        int[] cw = code.stream().mapToInt(Integer::intValue).toArray();
        List<ReplayWindow.LogEvent> ev = new ArrayList<>();
        for (Log l : logs) ev.add(new ReplayWindow.LogEvent(l.hasRead(), l.read(), l.load(), l.hasWrite(), l.write()));
        ReplayWindow raw = new ReplayWindow(), lru = new ReplayWindow();
        raw.setCode(cw, false); raw.setLogs(ev, false, false);
        lru.setCode(cw, true); lru.setLogs(ev, true, true);
        put(pd, base + "code.bin", raw.code, files); put(pd, base + "code-lru.bin", lru.code, files);
        put(pd, base + "rlog.bin", raw.rlog, files); put(pd, base + "rlog-lru.bin", lru.rlog, files);
        put(pd, base + "wlog.bin", raw.wlog, files); put(pd, base + "wlog-lru.bin", lru.wlog, files);
        long[] bm = new long[2]; for (int t : used) bm[t >>> 6] |= 1L << (t & 63);
        boolean pc_ = lru.codeLen < raw.codeLen, pr = lru.rlogLen < raw.rlogLen, pw = lru.wlogLen < raw.wlogLen;
        put(pd, base + "state.bin", record(bm, pc, idx, off, false, false, false, raw.codeLen, raw.rlogLen, raw.wlogLen, regs), files);
        put(pd, base + "state-lru.bin", record(bm, pc, idx, off, true, true, true, lru.codeLen, lru.rlogLen, lru.wlogLen, regs), files);
        put(pd, base + "state-pick.bin", record(bm, pc, idx, off, pc_, pr, pw, pc_ ? lru.codeLen : raw.codeLen, pr ? lru.rlogLen : raw.rlogLen, pw ? lru.wlogLen : raw.wlogLen, regs), files);
        put(pd, base + "end.bin", record(new long[2], f.pc, f.idx, f.active ? f.nextRow : 0, false, false, false, 0, 0, 0, f.regs), files);
        long[] rr = new long[10 * rowsOut.size()]; for (int i = 0; i < rowsOut.size(); i++) System.arraycopy(rowsOut.get(i), 0, rr, 10 * i, 10);
        byte[] rb = bytes(rr); Files.write(pd.resolve(base + "rows.bin"), rb);
        files.put(base + "rows.bin", Map.of("bytes", (long) rb.length, "sha256", ReplayTables.sha256(rb)));
        if (w9) { Map<String, Object> f2 = new LinkedHashMap<>(); for (var x : files.entrySet()) f2.put(x.getKey().substring(base.length()), x.getValue()); files = f2; }
        e.put("files", files); if (!w9) e.put("rows_sha256", ReplayTables.sha256(rb));
        e.put("templates", new ArrayList<>(used));
        long reads = logs.stream().filter(Log::hasRead).count(), writes = logs.stream().filter(Log::hasWrite).count();
        e.put("counts", Map.of("instructions", (long) code.size(), "read_log_words", reads, "write_log_words", writes,
                "padding_rows", rowsOut.stream().filter(r -> r[0] == 0 && r[1] == 0).count()));
        e.put("next_offset", (long) off);
        return e;
    }
    static long[] record(long[] bm, long pc, int idx, int off, boolean cc, boolean rc, boolean wc, long cl, long rl, long wl, long[] regs) {
        long[] r = new long[2 * 67];
        r[0] = bm[0]; r[1] = bm[1]; r[2] = pc;
        r[3] = (idx & 0xFFFFFFFFL) | (long) (off & 0xFFFF) << 32 | (cc ? 1L : 0) << 48 | (rc ? 1L : 0) << 49 | (wc ? 1L : 0) << 50 | 1L << 51;
        r[4] = (cl & 0xFFFFFF) | (rl & 0xFFFFFF) << 32; r[5] = wl & 0xFFFFFF;
        System.arraycopy(regs, 0, r, 6, 128); r[6] = 0;
        return r;
    }
    void put(Path pd, String name, long[] v, Map<String, Object> files) throws Exception {
        byte[] b = bytes(v); Files.write(pd.resolve(name), b);
        files.put(name, Map.of("bytes", (long) b.length, "sha256", ReplayTables.sha256(b)));
    }
    static byte[] bytes(long[] v) { ByteBuffer b = ReplayTables.le(new byte[8 * v.length]); for (int i = 0; i < v.length; i++) b.putLong(8 * i, v[i]); return b.array(); }

    void writeManifest(List<Map<String, Object>> entries) throws IOException {
        StringBuilder s = new StringBuilder();
        s.append("{\n \"format\": \"replay-inputs/v1.25 (synthetic: SyntheticDelivery, not jolt-cpp)\",\n");
        s.append(" \"generator\": {\"seed\": ").append(seed).append(", \"rows\": ").append(rows).append(", \"first\": ").append(first).append("},\n");
        s.append(" \"tables\": \"tables\",\n \"programs\": [{\"name\": \"").append(program).append("\", \"window_rows\": ").append(rows).append(", \"window_entries\": [\n");
        for (int i = 0; i < entries.size(); i++) s.append("  ").append(json(entries.get(i))).append(i + 1 < entries.size() ? ",\n" : "\n");
        s.append(" ]}]\n}\n");
        Files.writeString(out.resolve(w9 ? program + ".manifest.json" : "manifest.json"), s.toString());
    }
    @SuppressWarnings("unchecked")
    static String json(Object o) {
        if (o instanceof Map<?, ?> m) { StringBuilder s = new StringBuilder("{"); boolean f = true;
            for (Map.Entry<?, ?> e : m.entrySet()) { if (!f) s.append(", "); f = false; s.append('"').append(e.getKey()).append("\": ").append(json(e.getValue())); }
            return s.append('}').toString(); }
        if (o instanceof List<?> l) { StringBuilder s = new StringBuilder("["); for (int i = 0; i < l.size(); i++) { if (i > 0) s.append(", "); s.append(json(l.get(i))); } return s.append(']').toString(); }
        if (o instanceof String str) return '"' + str + '"';
        return String.valueOf(o);
    }
}
