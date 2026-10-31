/**
 * What a replayer row computes (spec 4.19.7; spec/replayer-opcodes.md sections 1-5, normative): the value written to
 * rd and the new pc, for the 85 opcodes the replayer executes. Pure functions; the replayer applies the pc and
 * instr_idx rule (appendix 1.4) and forms the record (4.19.12) around them.
 *
 * Slots: {@code a} is the rs1 slot's value, {@code b} the rs2 slot's, {@code c} the rd slot's (the immediate when rd
 * is an immediate slot, else rd's value before the row). A register slot reads its register (x0 reads 0), an
 * immediate slot carries its value (appendix 1.2), so each opcode reads its immediate from its fixed slot (1.2's
 * table): rs1 for JAL, LUI and AUIPC; rd for the branches and SD; rs2 for the rest.
 */
final class ReplayerAlu {
    private ReplayerAlu() {}

    // ---- the row opcodes (the tracer's numbering, isa/riscv.inc)
    static final int NOP = 0, ADD = 1, ADDI = 2, SUB = 3, XOR = 4, XORI = 5, OR = 6, ORI = 7, AND = 8, ANDI = 9,
            SLT = 16, SLTI = 17, SLTU = 18, SLTIU = 19, BEQ = 28, BNE = 29, BLT = 30, BGE = 31, BLTU = 32, BGEU = 33,
            JAL = 34, JALR = 35, AUIPC = 36, LUI = 37, MUL = 43, MULHU = 45, FENCE = 52, SD = 53, LD = 54,
            SUBW = 62, ADDW = 63, ADDIW = 64, MULW = 65,
            REV8W = 107, ASSERT_EQ = 108, HOSTIO = 109, SEXT_W = 110, ZEXT_W = 111, ASSERT_HALF = 112, ASSERT_WORD = 113,
            MULI = 114, MULIW = 115, POW2 = 116, SRBM = 117, SRL_V = 118, SRLI_V = 119, SRAI_V = 120, POW2W = 121,
            ADVICE = 122, ASSERT_LTE = 123, ASSERT_DIV0 = 124, ASSERT_UREM = 125, NEGATE_IF = 127, MOVSIGN = 128,
            SRA_V = 129, ASSERT_MULU = 130, ROTRI = 131, ROTRIW = 132, XORROT63 = 133, XORROT32 = 134, XORROT24 = 135,
            XORROT16 = 136, XORROTW16 = 137, XORROTW12 = 138, XORROTW8 = 139, XORROTW7 = 140, WMASK_W = 141,
            PEXT_S = 142, XORROTW22 = 143, XORROTW19 = 144, XORROTW6 = 145, SRBM_W = 146, SRLW_V = 147, SRLIW_V = 148,
            SRAW_V = 149, SRAIW_V = 150, PEXT = 151, WMASK_B = 152, WMASK_H = 153, ALIGN = 154, SHDATA_B = 155,
            SHDATA_H = 156, SHDATA_W = 157, XORROTL1 = 158, ANDN = 159;

    static final String[] NAME = new String[256];
    /** Row class (4.19.7): 'S' simple, 'M' multiply, 'L' log; 0 for an opcode the replayer doesn't execute. */
    static final char[] CLASS = new char[256];
    static {
        String table = "0 NOP S;1 ADD S;2 ADDI S;3 SUB S;4 XOR S;5 XORI S;6 OR S;7 ORI S;8 AND S;9 ANDI S;16 SLT S;17 SLTI S;"
            + "18 SLTU S;19 SLTIU S;28 BEQ S;29 BNE S;30 BLT S;31 BGE S;32 BLTU S;33 BGEU S;34 JAL S;35 JALR S;36 AUIPC S;"
            + "37 LUI S;43 MUL M;45 MULHU M;52 FENCE S;53 SD L;54 LD L;62 SUBW S;63 ADDW S;64 ADDIW S;65 MULW M;"
            + "107 VirtualRev8W S;108 VirtualAssertEQ S;109 VirtualHostIO S;110 VirtualSignExtendWord S;"
            + "111 VirtualZeroExtendWord S;112 VirtualAssertHalfwordAlignment S;113 VirtualAssertWordAlignment S;"
            + "114 VirtualMULI M;115 VirtualMULIW M;116 VirtualPow2 S;117 VirtualShiftRightBitmask S;118 VirtualSRL S;"
            + "119 VirtualSRLI S;120 VirtualSRAI S;121 VirtualPow2W S;122 VirtualAdvice L;123 VirtualAssertLTE S;"
            + "124 VirtualAssertValidDiv0 S;125 VirtualAssertValidUnsignedRemainder S;127 VirtualNegateIf S;"
            + "128 VirtualMovsign S;129 VirtualSRA S;130 VirtualAssertMulUNoOverflow S;131 VirtualROTRI S;"
            + "132 VirtualROTRIW S;133 VirtualXORROT63 S;134 VirtualXORROT32 S;135 VirtualXORROT24 S;136 VirtualXORROT16 S;"
            + "137 VirtualXORROTW16 S;138 VirtualXORROTW12 S;139 VirtualXORROTW8 S;140 VirtualXORROTW7 S;"
            + "141 VirtualWindowMaskW S;142 VirtualPextSigned S;143 VirtualXORROTW22 S;144 VirtualXORROTW19 S;"
            + "145 VirtualXORROTW6 S;146 VirtualShiftRightBitmaskW S;147 VirtualSRLW S;148 VirtualSRLIW S;149 VirtualSRAW S;"
            + "150 VirtualSRAIW S;151 VirtualPext S;152 VirtualWindowMaskB S;153 VirtualWindowMaskH S;154 VirtualAlignAddr S;"
            + "155 VirtualShiftDataB S;156 VirtualShiftDataH S;157 VirtualShiftDataW S;158 VirtualXORROTL1 S;159 ANDN S;"
            // the six DIV/REM advice opcodes occur in the tracer's rows but are written as VirtualAdvice (4.19.7): not executed
            + "160 VirtualAdviceDivU -;161 VirtualAdviceRemU -;162 VirtualAdviceDiv -;163 VirtualAdviceRem -;"
            + "164 VirtualAdviceDivUW -;165 VirtualAdviceRemUW -;166 VirtualAdviceDivW -;167 VirtualAdviceRemW -";
        for (String e : table.split(";")) {
            String[] f = e.split(" ");
            int code = Integer.parseInt(f[0]);
            NAME[code] = f[1];
            CLASS[code] = f[2].equals("-") ? 0 : f[2].charAt(0);
        }
    }
    static boolean executes(int op) { return op >= 0 && op < 256 && CLASS[op] != 0; }
    static int cycles(int op) { return CLASS[op] == 'M' ? 5 : 3; }
    static String name(int op) { return op >= 0 && op < 256 && NAME[op] != null ? NAME[op] : "op" + op; }
    static int code(String name) { for (int i = 0; i < 256; i++) if (name.equals(NAME[i])) return i; return -1; }

    /** One row's results. */
    static final class Out {
        long rd;            // the value for rd (used only when rd is a register slot)
        long newPc;         // the case's new_pc (starts equal to pc); the replayer applies appendix 1.4
        String undefined;   // non-null: the result is undefined for these inputs (invariant 16); meshsim flags it
    }

    static long sx32(long x) { return (long) (int) x; }
    static long zx32(long x) { return x & 0xFFFF_FFFFL; }
    static int tz(long x) { return Long.numberOfTrailingZeros(x); }      // tz(0) = 64
    /** The high half of the unsigned 128-bit product (Java 17: no Math.unsignedMultiplyHigh). */
    static long mulhu(long x, long y) { return Math.multiplyHigh(x, y) + ((x >> 63) & y) + ((y >> 63) & x); }
    static boolean contiguous(long m) { if (m == 0) return false; long s = m >>> tz(m); return (s & (s + 1)) == 0; }

    /**
     * Execute one row.
     * @param op       the row opcode (must be one the replayer executes)
     * @param a        the rs1 slot's value; @param b the rs2 slot's; @param c the rd slot's (immediate, or rd's old value)
     * @param pc       the source instruction's pc; @param pcInc pc_increment (0, 2 or 4)
     * @param imm0pc0  the word's imm0_pc0 flag (JAL with immediate 0 ends the program)
     * @param log      the row's log word (LD, VirtualAdvice); ignored by other opcodes
     */
    static void exec(int op, long a, long b, long c, long pc, int pcInc, boolean imm0pc0, long log, Out o) {
        o.newPc = pc; o.undefined = null; o.rd = 0;
        switch (op) {
            // A. integer ALU
            case NOP, FENCE -> {}
            case ADD, ADDI -> o.rd = a + b;
            case SUB -> o.rd = a - b;
            case XOR, XORI -> o.rd = a ^ b;
            case OR, ORI -> o.rd = a | b;
            case AND, ANDI -> o.rd = a & b;
            case ANDN -> o.rd = a & ~b;
            case SLT, SLTI -> o.rd = a < b ? 1 : 0;
            case SLTU, SLTIU -> o.rd = Long.compareUnsigned(a, b) < 0 ? 1 : 0;
            // B. word forms, extension and sign helpers
            case ADDW, ADDIW -> o.rd = sx32(a + b);
            case SUBW -> o.rd = sx32(a - b);
            case SEXT_W -> o.rd = sx32(a);
            case ZEXT_W -> o.rd = zx32(a);
            case NEGATE_IF -> o.rd = a < 0 ? -b : b;
            case MOVSIGN -> o.rd = a < 0 ? -1L : 0;
            case REV8W -> o.rd = zx32(Integer.reverseBytes((int) a)) | (long) Integer.reverseBytes((int) (a >>> 32)) << 32;
            // C. multiply
            case MUL, MULI -> o.rd = a * b;
            case MULW, MULIW -> o.rd = sx32(a * b);
            case MULHU -> o.rd = mulhu(a, b);
            // D. shifts, bitmasks and rotates
            case POW2 -> o.rd = 1L << (a & 63);
            case POW2W -> o.rd = zx32(1L << (a & 31));
            case SRBM -> o.rd = -(1L << (a & 63));
            case SRBM_W -> o.rd = (1L << 32) - (1L << (a & 31));
            case SRL_V, SRLI_V -> { if (b == 0) o.undefined = "zero shift mask"; o.rd = a >>> tz(b); }
            case SRA_V, SRAI_V -> { if (b == 0) o.undefined = "zero shift mask"; o.rd = a >> tz(b); }
            case SRLW_V, SRLIW_V -> o.rd = sx32(zx32(a) >>> (tz(b) & 31));
            case SRAW_V, SRAIW_V -> o.rd = sx32((int) a >> (tz(b) & 31));
            case ROTRI -> o.rd = Long.rotateRight(a, tz(b));               // by 64 (b = 0) is the identity
            case ROTRIW -> { int k = Math.min(32, tz(b)); o.rd = zx32(k == 32 ? (int) a : Integer.rotateRight((int) a, k)); }
            case XORROTL1 -> o.rd = a ^ Long.rotateLeft(b, 1);
            case XORROT63 -> o.rd = Long.rotateRight(a ^ b, 63);
            case XORROT32 -> o.rd = Long.rotateRight(a ^ b, 32);
            case XORROT24 -> o.rd = Long.rotateRight(a ^ b, 24);
            case XORROT16 -> o.rd = Long.rotateRight(a ^ b, 16);
            case XORROTW16 -> o.rd = zx32(Integer.rotateRight((int) (a ^ b), 16));
            case XORROTW12 -> o.rd = zx32(Integer.rotateRight((int) (a ^ b), 12));
            case XORROTW8 -> o.rd = zx32(Integer.rotateRight((int) (a ^ b), 8));
            case XORROTW7 -> o.rd = zx32(Integer.rotateRight((int) (a ^ b), 7));
            case XORROTW22 -> o.rd = zx32(Integer.rotateRight((int) (a ^ b), 22));
            case XORROTW19 -> o.rd = zx32(Integer.rotateRight((int) (a ^ b), 19));
            case XORROTW6 -> o.rd = zx32(Integer.rotateRight((int) (a ^ b), 6));
            // E. sub-word memory helpers
            case ALIGN -> o.rd = (a + b) & ~7L;
            case WMASK_B -> o.rd = 0xFFL << (8 * ((a + b) & 7));
            case WMASK_H -> o.rd = 0xFFFFL << (8 * ((a + b) & 6));
            case WMASK_W -> o.rd = 0xFFFF_FFFFL << (32 * (((a + b) >>> 2) & 1));
            case SHDATA_B -> o.rd = (a & 0xFF) << (8 * (b & 7));
            case SHDATA_H -> o.rd = (a & 0xFFFF) << (8 * (b & 6));
            case SHDATA_W -> o.rd = (a & 0xFFFF_FFFFL) << (8 * (b & 4));
            case PEXT, PEXT_S -> {
                // The replayer builds only contiguous non-zero masks (item 43): (rs1 & m) >> tz(m), and its signed form.
                if (!contiguous(b)) o.undefined = b == 0 ? "zero pext mask" : "non-contiguous pext mask";
                int s = tz(b) & 63, n = Long.bitCount(b);
                long v = (a & b) >>> s;
                if (op == PEXT_S && n < 64 && b != 0 && ((a >>> (63 - Long.numberOfLeadingZeros(b))) & 1) != 0) v |= -1L << n;
                o.rd = v;
            }
            // F. control flow and pc-relative
            case BEQ -> { if (a == b) o.newPc = pc + c; }
            case BNE -> { if (a != b) o.newPc = pc + c; }
            case BLT -> { if (a < b) o.newPc = pc + c; }
            case BGE -> { if (a >= b) o.newPc = pc + c; }
            case BLTU -> { if (Long.compareUnsigned(a, b) < 0) o.newPc = pc + c; }
            case BGEU -> { if (Long.compareUnsigned(a, b) >= 0) o.newPc = pc + c; }
            case JAL -> { o.rd = pc + pcInc; o.newPc = (a == 0 && imm0pc0) ? 0 : pc + a; }
            case JALR -> { o.rd = pc + pcInc; o.newPc = (a + b) & ~1L; }
            case AUIPC -> o.rd = pc + a;
            case LUI -> o.rd = a;
            // G. memory, the log and advice
            case LD, ADVICE -> o.rd = log;
            case SD -> {}
            // H. assertions: never evaluated (4.19.7)
            case ASSERT_EQ, ASSERT_LTE, ASSERT_DIV0, ASSERT_UREM, ASSERT_MULU, ASSERT_HALF, ASSERT_WORD -> {}
            // I. miscellaneous
            case HOSTIO -> o.rd = c;                                         // rd keeps its own value
            default -> throw new IllegalArgumentException("opcode " + op + " is not executed by the replayer");
        }
    }
}
