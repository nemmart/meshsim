/**
 * What the replayer (4.19) and the Tracer (4.21) share as rules, not as design (DECISIONS-v1.3 section 25; spec 4.21.1):
 * the decode table's index (4.19.5), the expansion word's fields and checks (4.19.3, 4.19.11), the operand codes
 * (4.19.3, 4.19.4) and the source instruction's immediate. Pure functions only, extracted from {@link Replayer} without
 * change (WORKER22, owner's ruling A, 2026-11-05): the replayer stays cycle-identical, and the Tracer expands each
 * instruction by exactly the same rules, so it knows which log words the replayer will pop.
 */
final class RowCore {
    private RowCore() {}

    /** The decode table's 11-bit index (4.19.5); bit 0 of w (the compressed flag) plays no part. */
    static int decodeIndex(int w) {
        w |= 1;
        int op = w & 0x7F, f3 = (w >>> 12) & 7;
        if (op == 0x2F) return 1 << 10 | ((w >>> 12) & 1) << 5 | (w >>> 27) & 31;                        // AMO
        if (op == 0x0B) return 1 << 10 | 1 << 8 | ((w >>> 25) & 31) << 3 | f3;                          // custom-0
        if (op == 0x73) {                                                                                // SYSTEM
            int sel = f3 != 0 ? f3 : w == 0x00000073 ? 8 : w == 0x00100073 ? 9 : w == 0x30200073 ? 10 : 11;
            return 1 <<10 | 2 << 8 | sel;
        }
        return ((w >>> 2) & 31) << 5 | f3 << 2 | ((w >>> 30) & 1) << 1 | (w >>> 25) & 1;
    }

    /** The source instruction's immediate by format (4.19.3, code 132): 0 R, 1 I, 2 S, 3 B, 4 U, 5 J. */
    static long sourceImm(int format, int w) {
        w |= 1;
        return switch (format) {
            case 0 -> 0;
            case 1 -> (long) (w >> 20);
            case 2 -> (long) ((w >> 25) << 5 | (w >>> 7) & 31);
            case 3 -> (long) ((w >> 31) << 12 | ((w >>> 7) & 1) << 11 | ((w >>> 25) & 63) << 5 | ((w >>> 8) & 15) << 1);
            case 4 -> (long) (w & 0xFFFFF000);
            case 5 -> (long) ((w >> 31) << 20 | ((w >>> 12) & 0xFF) << 12 | ((w >>> 20) & 1) << 11 | ((w >>> 21) & 0x3FF) << 1);
            default -> throw new IllegalArgumentException("format " + format);
        };
    }

    // ---------------------------------------------------------------- the expansion word (4.19.3), low half
    static int op(long lo)          { return (int) (lo & 0xFF); }
    static int cRd(long lo)         { return (int) (lo >>> 8) & 0xFF; }
    static int cRs1(long lo)        { return (int) (lo >>> 16) & 0xFF; }
    static int cRs2(long lo)        { return (int) (lo >>> 24) & 0xFF; }
    static int seqRemaining(long lo){ return (int) (lo >>> 32) & 0xFFF; }
    static boolean nextFromValue(long lo) { return ((lo >>> 44) & 1) != 0; }
    static boolean hold(long lo)    { return ((lo >>> 45) & 1) != 0; }
    static int rd0(long lo)         { return (int) (lo >>> 46) & 3; }
    static int format(long lo)      { return (int) (lo >>> 48) & 7; }
    static boolean virt(long lo)    { return ((lo >>> 51) & 1) != 0; }
    static boolean imm0pc0(long lo) { return ((lo >>> 52) & 1) != 0; }
    static boolean reportC(long lo) { return ((lo >>> 53) & 1) != 0; }
    static boolean fromLog(long lo) { return ((lo >>> 54) & 1) != 0; }
    static int reserved(long lo)    { return (int) (lo >>> 55) & 0x7F; }        // [61:55], reserved since v1.25 s9
    static boolean signImm(long lo) { return ((lo >>> 62) & 1) != 0; }
    static boolean offsetImm(long lo) { return ((lo >>> 63) & 1) != 0; }

    /** 4.19.11's invalid-expansion checks on a word (reserved bits apart, which are UB): null if the word is good. */
    static String invalidWord(long lo) {
        int op = op(lo);
        if (!ReplayerAlu.executes(op)) return "opcode " + op + " (" + ReplayerAlu.name(op) + ") is not executed by the replayer";
        if (cRd(lo) >= 146 || cRs1(lo) >= 146 || cRs2(lo) >= 146) return "a reserved operand code (146-255)";
        if (format(lo) >= 6) return "format " + format(lo);
        return null;
    }

    /** Whether any slot or the shift offset reads the source immediate (codes 132-136, offset_imm). */
    static boolean needsSourceImm(long lo) {
        int d = cRd(lo), s1 = cRs1(lo), s2 = cRs2(lo);
        return (s1 >= 132 && s1 <= 136) || (s2 >= 132 && s2 <= 136) || (d >= 132 && d <= 136) || offsetImm(lo);
    }

    /** A register operand code's register, or −1 for an immediate code (4.19.3, 4.19.4); w the code-stream word, offset
     *  the temporary offset bit. */
    static int reg(int c, int w, int offset) {
        if (c < 128) return c;
        if (c == 129) return offset == 1 ? 40 : (w >>> 7) & 31;
        if (c == 130) return (w >>> 15) & 31;
        if (c == 131) return (w >>> 20) & 31;
        if (c >= 137 && c <= 144) return 40 + (c - 137) + offset;
        return -1;
    }

    /** An immediate operand code's value (4.19.3). */
    static long immVal(int c, long cnst, long imm) {
        return switch (c) {
            case 128 -> cnst;
            case 145 -> 0;                                 // immediate 0 (26q): a zero slot beside a non-zero constant
            case 132 -> imm;
            case 133 -> -(1L << (imm & 63));
            case 134 -> 1L << (imm & 63);
            case 135 -> 1L << (imm & 31);
            case 136 -> (1L << 32) - (1L << (imm & 31));
            default -> throw new IllegalStateException("operand code " + c);
        };
    }

    // ---------------------------------------------------------------- the decode (4.19.4, 4.19.5)
    /** The template an instruction takes: K_ROM (start, offset), K_NOP (the shared NOP row, rd0 1 or 3), K_CTRL
     *  (controller-fed, class 1 or 2) or K_BAD (class 3). The padding word (bits [1:0] = 00) is the caller's. */
    static final int K_PAD = 0, K_ROM = 1, K_NOP = 2, K_CTRL = 3, K_BAD = 4;
    static final class Decoded { int kind, start, rd0, offset, cls; }
    static void decode(int w, short[] decodeTable, long[] romLo, Decoded d) {
        int e = decodeTable[decodeIndex(w)] & 0xFFFF, cls = e & 3, rdField = (w >>> 7) & 31;
        d.cls = cls; d.start = 0; d.rd0 = 0; d.offset = 0;
        if (cls == 3) d.kind = K_BAD;
        else if (cls == 0) {
            d.start = (e >>> 2) & 0x1FF;
            d.rd0 = (int) ((romLo[d.start] >>> 46) & 3);
            if (rdField == 0 && (d.rd0 == 1 || d.rd0 == 3)) d.kind = K_NOP;
            else { d.kind = K_ROM; d.offset = rdField == 0 && d.rd0 == 2 ? 1 : 0; }
        } else d.kind = K_CTRL;
    }
}
