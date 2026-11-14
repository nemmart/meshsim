package gold;

/**
 * jolt-cpp's opcodes, numbered in the order of its enum (isa/riscv.inc, isa/riscv.hpp:17-22 at 821e1bd).
 * The numbers are part of the row record (word 1, bits 7:0). NOP is 0, so an all-zero row is a padding row.
 * Opcodes from VirtualLoadB on are declared HANDLE_VIRTUAL there (isa::is_virtual); the ones before are not,
 * although some carry "Virtual" in their name (VirtualRev8W, VirtualAssertEQ, VirtualHostIO).
 */
public final class Op {
  private Op() {}

  public static final int NOP = 0;
  public static final int ADD = 1;
  public static final int ADDI = 2;
  public static final int SUB = 3;
  public static final int XOR = 4;
  public static final int XORI = 5;
  public static final int OR = 6;
  public static final int ORI = 7;
  public static final int AND = 8;
  public static final int ANDI = 9;
  public static final int SLL = 10;
  public static final int SLLI = 11;
  public static final int SRL = 12;
  public static final int SRLI = 13;
  public static final int SRA = 14;
  public static final int SRAI = 15;
  public static final int SLT = 16;
  public static final int SLTI = 17;
  public static final int SLTU = 18;
  public static final int SLTIU = 19;
  public static final int LB = 20;
  public static final int LH = 21;
  public static final int LW = 22;
  public static final int LBU = 23;
  public static final int LHU = 24;
  public static final int SB = 25;
  public static final int SH = 26;
  public static final int SW = 27;
  public static final int BEQ = 28;
  public static final int BNE = 29;
  public static final int BLT = 30;
  public static final int BGE = 31;
  public static final int BLTU = 32;
  public static final int BGEU = 33;
  public static final int JAL = 34;
  public static final int JALR = 35;
  public static final int AUIPC = 36;
  public static final int LUI = 37;
  public static final int ECALL = 38;
  public static final int EBREAK = 39;
  public static final int CSRRW = 40;
  public static final int CSRRS = 41;
  public static final int MRET = 42;
  public static final int MUL = 43;
  public static final int MULH = 44;
  public static final int MULHU = 45;
  public static final int MULHSU = 46;
  public static final int DIV = 47;
  public static final int DIVU = 48;
  public static final int REM = 49;
  public static final int REMU = 50;
  public static final int UNIMP = 51;
  public static final int FENCE = 52;
  public static final int SD = 53;
  public static final int LD = 54;
  public static final int LWU = 55;
  public static final int SLLW = 56;
  public static final int SLLIW = 57;
  public static final int SRLW = 58;
  public static final int SRLIW = 59;
  public static final int SRAW = 60;
  public static final int SRAIW = 61;
  public static final int SUBW = 62;
  public static final int ADDW = 63;
  public static final int ADDIW = 64;
  public static final int MULW = 65;
  public static final int DIVW = 66;
  public static final int DIVUW = 67;
  public static final int REMW = 68;
  public static final int REMUW = 69;
  public static final int LR_W = 70;
  public static final int LR_D = 71;
  public static final int SC_W = 72;
  public static final int SC_D = 73;
  public static final int AMOSWAP_W = 74;
  public static final int AMOSWAP_D = 75;
  public static final int AMOADD_W = 76;
  public static final int AMOADD_D = 77;
  public static final int AMOXOR_W = 78;
  public static final int AMOXOR_D = 79;
  public static final int AMOAND_W = 80;
  public static final int AMOAND_D = 81;
  public static final int AMOOR_W = 82;
  public static final int AMOOR_D = 83;
  public static final int AMOMIN_W = 84;
  public static final int AMOMIN_D = 85;
  public static final int AMOMAX_W = 86;
  public static final int AMOMAX_D = 87;
  public static final int AMOMINU_W = 88;
  public static final int AMOMINU_D = 89;
  public static final int AMOMAXU_W = 90;
  public static final int AMOMAXU_D = 91;
  public static final int KECCAK256 = 92;
  public static final int KECCAK256_ABSORB_PERMUTE = 93;
  public static final int SHA256_INLINE = 94;
  public static final int SHA256_INIT_INLINE = 95;
  public static final int BLAKE2_INLINE = 96;
  public static final int BLAKE3_INLINE = 97;
  public static final int BLAKE3_KEYED64_INLINE = 98;
  public static final int BIGINT256_MUL_INLINE = 99;
  public static final int SECP256K1_MULQ_INLINE = 100;
  public static final int SECP256K1_SQUAREQ_INLINE = 101;
  public static final int SECP256K1_DIVQ_INLINE = 102;
  public static final int SECP256K1_MULR_INLINE = 103;
  public static final int SECP256K1_SQUARER_INLINE = 104;
  public static final int SECP256K1_DIVR_INLINE = 105;
  public static final int SECP256K1_GLVR_ADV_INLINE = 106;
  public static final int VirtualRev8W = 107;
  public static final int VirtualAssertEQ = 108;
  public static final int VirtualHostIO = 109;
  public static final int VirtualLoadB = 110;
  public static final int VirtualLoadH = 111;
  public static final int VirtualLoadW = 112;
  public static final int VirtualStoreB = 113;
  public static final int VirtualStoreH = 114;
  public static final int VirtualStoreW = 115;
  public static final int VirtualSignExtendWord = 116;
  public static final int VirtualZeroExtendWord = 117;
  public static final int VirtualMULI = 118;
  public static final int VirtualMULIW = 119;
  public static final int VirtualPow2 = 120;
  public static final int VirtualShiftRightBitmask = 121;
  public static final int VirtualSRL = 122;
  public static final int VirtualSRLI = 123;
  public static final int VirtualSRAI = 124;
  public static final int VirtualPow2W = 125;
  public static final int VirtualAdvice = 126;
  public static final int VirtualAssertLTE = 127;
  public static final int VirtualAssertValidDiv0 = 128;
  public static final int VirtualAssertValidUnsignedRemainder = 129;
  public static final int VirtualMove = 130;
  public static final int VirtualNegateIf = 131;
  public static final int VirtualMovsign = 132;
  public static final int VirtualSRA = 133;
  public static final int VirtualAssertMulUNoOverflow = 134;
  public static final int VirtualROTRI = 135;
  public static final int VirtualROTRIW = 136;
  public static final int VirtualXORROT63 = 137;
  public static final int VirtualXORROT32 = 138;
  public static final int VirtualXORROT24 = 139;
  public static final int VirtualXORROT16 = 140;
  public static final int VirtualXORROTW16 = 141;
  public static final int VirtualXORROTW12 = 142;
  public static final int VirtualXORROTW8 = 143;
  public static final int VirtualXORROTW7 = 144;
  public static final int VirtualWindowMaskW = 145;
  public static final int VirtualPextSigned = 146;
  public static final int VirtualXORROTW22 = 147;
  public static final int VirtualXORROTW19 = 148;
  public static final int VirtualXORROTW6 = 149;
  public static final int VirtualShiftRightBitmaskW = 150;
  public static final int VirtualSRLW = 151;
  public static final int VirtualSRLIW = 152;
  public static final int VirtualSRAW = 153;
  public static final int VirtualSRAIW = 154;
  public static final int VirtualPext = 155;
  public static final int VirtualWindowMaskB = 156;
  public static final int VirtualWindowMaskH = 157;
  public static final int VirtualShiftDataB = 158;
  public static final int VirtualShiftDataH = 159;
  public static final int VirtualShiftDataW = 160;
  public static final int VirtualXORROTL1 = 161;
  public static final int ANDN = 162;
  public static final int VirtualAdviceDivU = 163;
  public static final int VirtualAdviceRemU = 164;
  public static final int VirtualAdviceDiv = 165;
  public static final int VirtualAdviceRem = 166;
  public static final int VirtualAdviceDivUW = 167;
  public static final int VirtualAdviceRemUW = 168;
  public static final int VirtualAdviceDivW = 169;
  public static final int VirtualAdviceRemW = 170;

  public static final int NUM_OPCODES = 171;
  /** The default-constructed Instruction's opcode: an undecodable instruction. */
  public static final int INVALID = 255;

  /** First opcode declared HANDLE_VIRTUAL. */
  public static final int FIRST_VIRTUAL = VirtualLoadB;

  private static final String[] NAMES = {
    "NOP", "ADD", "ADDI", "SUB", "XOR", "XORI",
    "OR", "ORI", "AND", "ANDI", "SLL", "SLLI",
    "SRL", "SRLI", "SRA", "SRAI", "SLT", "SLTI",
    "SLTU", "SLTIU", "LB", "LH", "LW", "LBU",
    "LHU", "SB", "SH", "SW", "BEQ", "BNE",
    "BLT", "BGE", "BLTU", "BGEU", "JAL", "JALR",
    "AUIPC", "LUI", "ECALL", "EBREAK", "CSRRW", "CSRRS",
    "MRET", "MUL", "MULH", "MULHU", "MULHSU", "DIV",
    "DIVU", "REM", "REMU", "UNIMP", "FENCE", "SD",
    "LD", "LWU", "SLLW", "SLLIW", "SRLW", "SRLIW",
    "SRAW", "SRAIW", "SUBW", "ADDW", "ADDIW", "MULW",
    "DIVW", "DIVUW", "REMW", "REMUW", "LR_W", "LR_D",
    "SC_W", "SC_D", "AMOSWAP_W", "AMOSWAP_D", "AMOADD_W", "AMOADD_D",
    "AMOXOR_W", "AMOXOR_D", "AMOAND_W", "AMOAND_D", "AMOOR_W", "AMOOR_D",
    "AMOMIN_W", "AMOMIN_D", "AMOMAX_W", "AMOMAX_D", "AMOMINU_W", "AMOMINU_D",
    "AMOMAXU_W", "AMOMAXU_D", "KECCAK256", "KECCAK256_ABSORB_PERMUTE", "SHA256_INLINE", "SHA256_INIT_INLINE",
    "BLAKE2_INLINE", "BLAKE3_INLINE", "BLAKE3_KEYED64_INLINE", "BIGINT256_MUL_INLINE", "SECP256K1_MULQ_INLINE", "SECP256K1_SQUAREQ_INLINE",
    "SECP256K1_DIVQ_INLINE", "SECP256K1_MULR_INLINE", "SECP256K1_SQUARER_INLINE", "SECP256K1_DIVR_INLINE", "SECP256K1_GLVR_ADV_INLINE", "VirtualRev8W",
    "VirtualAssertEQ", "VirtualHostIO", "VirtualLoadB", "VirtualLoadH", "VirtualLoadW", "VirtualStoreB",
    "VirtualStoreH", "VirtualStoreW", "VirtualSignExtendWord", "VirtualZeroExtendWord", "VirtualMULI", "VirtualMULIW",
    "VirtualPow2", "VirtualShiftRightBitmask", "VirtualSRL", "VirtualSRLI", "VirtualSRAI", "VirtualPow2W",
    "VirtualAdvice", "VirtualAssertLTE", "VirtualAssertValidDiv0", "VirtualAssertValidUnsignedRemainder", "VirtualMove", "VirtualNegateIf",
    "VirtualMovsign", "VirtualSRA", "VirtualAssertMulUNoOverflow", "VirtualROTRI", "VirtualROTRIW", "VirtualXORROT63",
    "VirtualXORROT32", "VirtualXORROT24", "VirtualXORROT16", "VirtualXORROTW16", "VirtualXORROTW12", "VirtualXORROTW8",
    "VirtualXORROTW7", "VirtualWindowMaskW", "VirtualPextSigned", "VirtualXORROTW22", "VirtualXORROTW19", "VirtualXORROTW6",
    "VirtualShiftRightBitmaskW", "VirtualSRLW", "VirtualSRLIW", "VirtualSRAW", "VirtualSRAIW", "VirtualPext",
    "VirtualWindowMaskB", "VirtualWindowMaskH", "VirtualShiftDataB", "VirtualShiftDataH", "VirtualShiftDataW", "VirtualXORROTL1",
    "ANDN", "VirtualAdviceDivU", "VirtualAdviceRemU", "VirtualAdviceDiv", "VirtualAdviceRem", "VirtualAdviceDivUW",
    "VirtualAdviceRemUW", "VirtualAdviceDivW", "VirtualAdviceRemW",
  };

  public static String name(int op) {
    return op >= 0 && op < NUM_OPCODES ? NAMES[op] : (op == INVALID ? "INVALID" : "op" + op);
  }

  public static int byName(String name) {
    for (int i = 0; i < NUM_OPCODES; i++) if (NAMES[i].equals(name)) return i;
    throw new IllegalArgumentException("unknown opcode " + name);
  }
}
