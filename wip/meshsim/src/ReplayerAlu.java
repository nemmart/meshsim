/**
 * What a replayer row computes (spec 4.19.7; spec/replayer-opcodes.md sections 1-5, normative): the value written to
 * rd and the new pc, for the 88 opcodes the replayer executes (v1.25: jolt-cpp 821e1bd). Pure functions; the replayer applies the pc and
 * instr_idx rule (appendix 1.4) and forms the record (4.19.12) around them.
 *
 * Slots: {@code a} is the rs1 slot's value, {@code b} the rs2 slot's, {@code c} the rd slot's (the immediate when rd
 * is an immediate slot, else rd's value before the row). A register slot reads its register (x0 reads 0), an
 * immediate slot carries its value (appendix 1.2), so each opcode reads its immediate from its fixed slot (1.2's
 * table): rs1 for JAL, LUI and AUIPC; rd for the branches and SD; rs2 for the rest.
 */
final class ReplayerAlu {
    private ReplayerAlu() {}

    // ---- the row opcodes: jolt-cpp 821e1bd's numbering (isa/riscv.inc; wip/tables/opcode_table.txt), v1.25
    static final int NOP = 0, ADD = 1, ADDI = 2, SUB = 3, XOR = 4, XORI = 5, OR = 6, ORI = 7, AND = 8, ANDI = 9,
            SLT = 16, SLTI = 17, SLTU = 18, SLTIU = 19, BEQ = 28, BNE = 29, BLT = 30, BGE = 31, BLTU = 32, BGEU = 33,
            JAL = 34, JALR = 35, AUIPC = 36, LUI = 37, MUL = 43, MULHU = 45, FENCE = 52, SD = 53, LD = 54,
            SUBW = 62, ADDW = 63, ADDIW = 64, MULW = 65,
            REV8W = 107, ASSERT_EQ = 108, HOSTIO = 109, LOAD_B = 110, LOAD_H = 111, LOAD_W = 112, STORE_B = 113,
            STORE_H = 114, STORE_W = 115, SEXT_W = 116, ZEXT_W = 117, MULI = 118, MULIW = 119, POW2 = 120, SRBM = 121,
            SRL_V = 122, SRLI_V = 123, SRAI_V = 124, POW2W = 125, ADVICE = 126, ASSERT_LTE = 127, ASSERT_DIV0 = 128,
            ASSERT_UREM = 129, NEGATE_IF = 131, MOVSIGN = 132, SRA_V = 133, ASSERT_MULU = 134, ROTRI = 135,
            ROTRIW = 136, XORROT63 = 137, XORROT32 = 138, XORROT24 = 139, XORROT16 = 140, XORROTW16 = 141,
            XORROTW12 = 142, XORROTW8 = 143, XORROTW7 = 144, WMASK_W = 145, PEXT_S = 146, XORROTW22 = 147,
            XORROTW19 = 148, XORROTW6 = 149, SRBM_W = 150, SRLW_V = 151, SRLIW_V = 152, SRAW_V = 153, SRAIW_V = 154,
            PEXT = 155, WMASK_B = 156, WMASK_H = 157, SHDATA_B = 158, SHDATA_H = 159, SHDATA_W = 160, XORROTL1 = 161,
            ANDN = 162;

    static final String[] NAME = new String[256];
    /** Row class (4.19.7): 'S' simple, 'M' multiply, 'L' log; 0 for an opcode the replayer doesn't execute. */
    static final char[] CLASS = new char[256];
    /**
     * The 88 row opcodes a template can emit at 821e1bd, in wip/tables/opcode_table.txt's {@code code name class} form
     * (ReplayerTest checks this table against that file). v1.25 against v1.2: VirtualLoadB/H/W and VirtualStoreB/H/W
     * added (class L), VirtualAlignAddr and the two alignment assertions removed, every opcode from 110 on renumbered.
     */
    static final String TABLE = "0 NOP S;1 ADD S;2 ADDI S;3 SUB S;4 XOR S;5 XORI S;6 OR S;7 ORI S;8 AND S;9 ANDI S;16 SLT S;17 SLTI S;"
            + "18 SLTU S;19 SLTIU S;28 BEQ S;29 BNE S;30 BLT S;31 BGE S;32 BLTU S;33 BGEU S;34 JAL S;35 JALR S;36 AUIPC S;"
            + "37 LUI S;43 MUL M;45 MULHU M;52 FENCE S;53 SD L;54 LD L;62 SUBW S;63 ADDW S;64 ADDIW S;65 MULW M;"
            + "107 VirtualRev8W S;108 VirtualAssertEQ S;109 VirtualHostIO S;110 VirtualLoadB L;111 VirtualLoadH L;"
            + "112 VirtualLoadW L;113 VirtualStoreB L;114 VirtualStoreH L;115 VirtualStoreW L;116 VirtualSignExtendWord S;"
            + "117 VirtualZeroExtendWord S;118 VirtualMULI M;119 VirtualMULIW M;120 VirtualPow2 S;121 VirtualShiftRightBitmask S;"
            + "122 VirtualSRL S;123 VirtualSRLI S;124 VirtualSRAI S;125 VirtualPow2W S;126 VirtualAdvice L;127 VirtualAssertLTE S;"
            + "128 VirtualAssertValidDiv0 S;129 VirtualAssertValidUnsignedRemainder S;131 VirtualNegateIf S;132 VirtualMovsign S;"
            + "133 VirtualSRA S;134 VirtualAssertMulUNoOverflow S;135 VirtualROTRI S;136 VirtualROTRIW S;137 VirtualXORROT63 S;"
            + "138 VirtualXORROT32 S;139 VirtualXORROT24 S;140 VirtualXORROT16 S;141 VirtualXORROTW16 S;142 VirtualXORROTW12 S;"
            + "143 VirtualXORROTW8 S;144 VirtualXORROTW7 S;145 VirtualWindowMaskW S;146 VirtualPextSigned S;147 VirtualXORROTW22 S;"
            + "148 VirtualXORROTW19 S;149 VirtualXORROTW6 S;150 VirtualShiftRightBitmaskW S;151 VirtualSRLW S;152 VirtualSRLIW S;"
            + "153 VirtualSRAW S;154 VirtualSRAIW S;155 VirtualPext S;156 VirtualWindowMaskB S;157 VirtualWindowMaskH S;"
            + "158 VirtualShiftDataB S;159 VirtualShiftDataH S;160 VirtualShiftDataW S;161 VirtualXORROTL1 S;162 ANDN S";
    /** Opcodes that occur in the tracer's rows but never in a replayer's: VirtualMove (expanded away) and the eight DIV/REM
     *  advice opcodes, which a template writes as VirtualAdvice (4.19.7). Named for messages only. */
    static final String NOT_EXECUTED = "130 VirtualMove;163 VirtualAdviceDivU;164 VirtualAdviceRemU;165 VirtualAdviceDiv;"
            + "166 VirtualAdviceRem;167 VirtualAdviceDivUW;168 VirtualAdviceRemUW;169 VirtualAdviceDivW;170 VirtualAdviceRemW";
    static {
        for (String e : TABLE.split(";")) {
            String[] f = e.split(" ");
            int code = Integer.parseInt(f[0]);
            NAME[code] = f[1];
            CLASS[code] = f[2].charAt(0);
        }
        for (String e : NOT_EXECUTED.split(";")) { String[] f = e.split(" "); NAME[Integer.parseInt(f[0])] = f[1]; }
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
    /** The general bit extract (any mask; v1.25, DECISIONS-v1.25 s1): rs1's bits under the mask, packed from bit 0. */
    static long pext(long a, long m) {
        long v = 0; int k = 0;
        for (long r = m; r != 0; r &= r - 1, k++) if ((a & Long.lowestOneBit(r)) != 0) v |= 1L << k;
        return v;
    }
    static boolean isLoad(int op) { return op == LD || op == LOAD_B || op == LOAD_H || op == LOAD_W; }
    static boolean isStore(int op) { return op == SD || op == STORE_B || op == STORE_H || op == STORE_W; }
    static boolean isShiftData(int op) { return op == SHDATA_B || op == SHDATA_H || op == SHDATA_W; }
    /** The narrow memory rows' floor (jolt-cpp {@code floor_offset_mask}): the address bits a B/H/W access drops. */
    static long floorMask(int op) {
        return switch (op) { case LOAD_B, STORE_B -> 7; case LOAD_H, STORE_H -> 6; case LOAD_W, STORE_W -> 4; default -> 0; };
    }
    /** A memory row's address (4.19.12 mem_addr): rs1 + imm, rounded down to its doubleword for the narrow rows. */
    static long memAddr(int op, long rs1, long imm) { long a = rs1 + imm; return a - (a & floorMask(op)); }

    /**
     * Execute one row.
     * @param op       the row opcode (must be one the replayer executes)
     * @param a        the rs1 slot's value; @param b the rs2 slot's; @param c the rd slot's (immediate, or rd's old value)
     * @param pc       the source instruction's pc; @param pcInc pc_increment (0, 2 or 4)
     * @param imm0pc0  the word's imm0_pc0 flag (JAL with immediate 0 ends the program)
     * @param log      the row's log word (the loads, VirtualAdvice); ignored by other opcodes
     * @param so       the row's shift_offset (ShiftData only: the source immediate under offset_imm, else 0)
     */
    static void exec(int op, long a, long b, long c, long pc, int pcInc, boolean imm0pc0, long log, Out o) { exec(op, a, b, c, pc, pcInc, imm0pc0, log, 0, o); }
    static void exec(int op, long a, long b, long c, long pc, int pcInc, boolean imm0pc0, long log, long so, Out o) {
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
            case WMASK_B -> o.rd = 0xFFL << (8 * ((a + b) & 7));
            case WMASK_H -> o.rd = 0xFFFFL << (8 * ((a + b) & 6));
            case WMASK_W -> o.rd = 0xFFFF_FFFFL << (32 * (((a + b) >>> 2) & 1));
            // ShiftData (821e1bd): rs2 is the base register; the lane comes from base + shift_offset
            case SHDATA_B -> o.rd = (a & 0xFF) << (8 * ((b + so) & 7));
            case SHDATA_H -> o.rd = (a & 0xFFFF) << (8 * ((b + so) & 6));
            case SHDATA_W -> o.rd = (a & 0xFFFF_FFFFL) << (8 * ((b + so) & 4));
            case PEXT, PEXT_S -> {
                // v1.25: correct for any mask (DECISIONS-v1.25 s1; full speed promised only for contiguous masks). The
                // signed form extends from the last bit placed (the source bit under the mask's highest bit); mask 0 gives 0.
                int n = Long.bitCount(b);
                long v = pext(a, b);
                if (op == PEXT_S && n > 0 && n < 64 && ((a >>> (63 - Long.numberOfLeadingZeros(b))) & 1) != 0) v |= -1L << n;
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
            case LD, LOAD_B, LOAD_H, LOAD_W, ADVICE -> o.rd = log;     // the narrow loads take the whole doubleword
            case SD, STORE_B, STORE_H, STORE_W -> {}
            // H. assertions: never evaluated (4.19.7)
            case ASSERT_EQ, ASSERT_LTE, ASSERT_DIV0, ASSERT_UREM, ASSERT_MULU -> {}
            // I. miscellaneous
            case HOSTIO -> o.rd = c;    // 821e1bd: operand-free (every slot an immediate), so nothing is written
            default -> throw new IllegalArgumentException("opcode " + op + " is not executed by the replayer");
        }
    }
}
