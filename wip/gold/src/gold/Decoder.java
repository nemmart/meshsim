package gold;

import static gold.Op.*;

/**
 * Decodes RV64IMAC and Jolt's custom opcodes into {@link Instruction}s, exactly as jolt-cpp's isa/decoder.cpp at 821e1bd,
 * including what it accepts loosely (aq/rl ignored, LR's rs2 field ignored, JALR's funct3 ignored) and what it turns into
 * UNIMP. Compressed instructions are first expanded to their 32-bit encodings (uncompress, decoder.cpp:646-1167).
 */
public final class Decoder {
  private Decoder() {}

  /** A decoded instruction and its size in bytes. */
  public record Decoded(Instruction instr, int size) {}

  // ---- field extraction (decoder.cpp:62-213) ----

  static int rd(int insn) { return (insn >>> 7) & 0x1f; }
  static int rs1(int insn) { return (insn >>> 15) & 0x1f; }
  static int rs2(int insn) { return (insn >>> 20) & 0x1f; }
  static int funct3(int insn) { return (insn >>> 12) & 7; }
  static int funct7(int insn) { return (insn >>> 25) & 0x7f; }

  /** I-type immediate: bits 31:20, sign-extended (IType). */
  static int immI(int insn) { return insn >> 20; }
  /** RV64 shift-immediate: funct6 in bits 31:26, shamt the low 6 bits of the I immediate (ITypeShamt). */
  static int funct6(int insn) { return (insn >>> 26) & 0x3f; }
  static int shamt(int insn) { return immI(insn) & 0x3f; }
  static int immU(int insn) { return insn & 0xfffff000; }
  static int immJ(int insn) {
    int u = (insn & 0xff000) | ((insn & 0x100000) >>> 9) | ((insn >>> 20) & 0x7fe);
    return insn < 0 ? u - (1 << 20) : u;
  }
  static int immB(int insn) {
    int u = ((insn >>> 20) & 0x7e0) | ((insn >>> 7) & 0x1e) | ((insn & 0x80) << 4);
    return insn < 0 ? u - (1 << 12) : u;
  }
  static int immS(int insn) {
    int u = ((insn >>> 20) & 0x7e0) | ((insn >>> 7) & 0x1f);
    return insn < 0 ? u - (1 << 11) : u;
  }

  static Instruction r(int op, int insn) { return new Instruction(op, rd(insn), rs1(insn), rs2(insn), false, false, false); }
  static Instruction i(int op, int insn) { return new Instruction(op, rd(insn), rs1(insn), immI(insn), false, false, true); }
  static Instruction shift(int op, int insn) { return new Instruction(op, rd(insn), rs1(insn), shamt(insn), false, false, true); }
  static Instruction b(int op, int insn) { return new Instruction(op, immB(insn), rs1(insn), rs2(insn), true, false, false); }
  static Instruction s(int op, int insn) { return new Instruction(op, immS(insn), rs1(insn), rs2(insn), true, false, false); }

  // ---- 32-bit decoding (decoder.cpp:213-645, 1169-1270) ----

  static Instruction processOp(int insn) {
    int f7 = funct7(insn);
    switch (funct3(insn)) {
      case 0: return f7 == 0 ? r(ADD, insn) : f7 == 1 ? r(MUL, insn) : f7 == 0x20 ? r(SUB, insn) : Instruction.INVALID;
      case 1: return f7 == 0 ? r(SLL, insn) : f7 == 1 ? r(MULH, insn) : Instruction.INVALID;
      case 2: return f7 == 0 ? r(SLT, insn) : f7 == 1 ? r(MULHSU, insn) : Instruction.INVALID;
      case 3: return f7 == 0 ? r(SLTU, insn) : f7 == 1 ? r(MULHU, insn) : Instruction.INVALID;
      case 4: return f7 == 0 ? r(XOR, insn) : f7 == 1 ? r(DIV, insn) : Instruction.INVALID;
      case 5: return f7 == 0 ? r(SRL, insn) : f7 == 1 ? r(DIVU, insn) : f7 == 0x20 ? r(SRA, insn) : Instruction.INVALID;
      case 6: return f7 == 0 ? r(OR, insn) : f7 == 1 ? r(REM, insn) : Instruction.INVALID;
      default: return f7 == 0 ? r(AND, insn) : f7 == 1 ? r(REMU, insn) : Instruction.INVALID;
    }
  }

  static Instruction processOp64(int insn) {
    int f7 = funct7(insn);
    switch (funct3(insn)) {
      case 0: return f7 == 0 ? r(ADDW, insn) : f7 == 0x20 ? r(SUBW, insn) : f7 == 1 ? r(MULW, insn) : Instruction.INVALID;
      case 1: return f7 == 0 ? r(SLLW, insn) : Instruction.INVALID;
      case 4: return f7 == 1 ? r(DIVW, insn) : Instruction.INVALID;
      case 5: return f7 == 0 ? r(SRLW, insn) : f7 == 0x20 ? r(SRAW, insn) : f7 == 1 ? r(DIVUW, insn) : Instruction.INVALID;
      case 6: return f7 == 1 ? r(REMW, insn) : Instruction.INVALID;
      case 7: return f7 == 1 ? r(REMUW, insn) : Instruction.INVALID;
      default: return Instruction.INVALID;
    }
  }

  /** A extension: funct5 picks the operation, funct3 010 word / 011 doubleword; aq/rl and LR's rs2 are ignored. */
  static Instruction processAtomic(int insn) {
    int f5 = (insn >>> 27) & 0x1f, f3 = funct3(insn);
    if (f3 != 2 && f3 != 3) return Instruction.INVALID;
    boolean w = f3 == 2;
    int op;
    switch (f5) {
      case 0b00010: op = w ? LR_W : LR_D; break;
      case 0b00011: op = w ? SC_W : SC_D; break;
      case 0b00001: op = w ? AMOSWAP_W : AMOSWAP_D; break;
      case 0b00000: op = w ? AMOADD_W : AMOADD_D; break;
      case 0b00100: op = w ? AMOXOR_W : AMOXOR_D; break;
      case 0b01100: op = w ? AMOAND_W : AMOAND_D; break;
      case 0b01000: op = w ? AMOOR_W : AMOOR_D; break;
      case 0b10000: op = w ? AMOMIN_W : AMOMIN_D; break;
      case 0b10100: op = w ? AMOMAX_W : AMOMAX_D; break;
      case 0b11000: op = w ? AMOMINU_W : AMOMINU_D; break;
      case 0b11100: op = w ? AMOMAXU_W : AMOMAXU_D; break;
      default: return Instruction.INVALID;
    }
    return r(op, insn);
  }

  static Instruction processOpImm(int insn) {
    switch (funct3(insn)) {
      case 0: return i(ADDI, insn);
      case 1: return shift(SLLI, insn);                 // funct6 not checked
      case 2: return i(SLTI, insn);
      case 3: return i(SLTIU, insn);
      case 4: return i(XORI, insn);
      case 5: return funct6(insn) == 0 ? shift(SRLI, insn) : funct6(insn) == 0b010000 ? shift(SRAI, insn) : Instruction.INVALID;
      case 6: return i(ORI, insn);
      default: return i(ANDI, insn);
    }
  }

  static Instruction processOp64Imm(int insn) {
    switch (funct3(insn)) {
      case 0: return i(ADDIW, insn);
      case 1: return shift(SLLIW, insn);                // 6-bit shamt kept; the expansion masks it to 5
      case 5: return funct6(insn) == 0 ? shift(SRLIW, insn) : funct6(insn) == 0b010000 ? shift(SRAIW, insn) : Instruction.INVALID;
      default: return Instruction.INVALID;
    }
  }

  static Instruction processBranch(int insn) {
    switch (funct3(insn)) {
      case 0: return b(BEQ, insn);
      case 1: return b(BNE, insn);
      case 4: return b(BLT, insn);
      case 5: return b(BGE, insn);
      case 6: return b(BLTU, insn);
      case 7: return b(BGEU, insn);
      default: return Instruction.INVALID;
    }
  }

  static Instruction processLoad(int insn) {
    final int[] ops = {LB, LH, LW, LD, LBU, LHU, LWU};
    int f3 = funct3(insn);
    return f3 < 7 ? i(ops[f3], insn) : Instruction.INVALID;
  }

  static Instruction processStore(int insn) {
    final int[] ops = {SB, SH, SW, SD};
    int f3 = funct3(insn);
    return f3 < 4 ? s(ops[f3], insn) : Instruction.INVALID;
  }

  /** SYSTEM (decoder.cpp:598-644): MRET by exact encoding; ECALL, EBREAK; WFI and the bare 0x302 as UNIMP; two CSR ops. */
  static Instruction processSystem(int insn) {
    int csr = (insn >>> 20) & 0xfff;
    switch (funct3(insn)) {
      case 0:
        if (insn == 0x30200073) return Instruction.noOperands(MRET);
        if (rd(insn) != 0 || rs1(insn) != 0) return Instruction.INVALID;
        switch (csr) {
          case 0x000: return Instruction.noOperands(ECALL);
          case 0x001: return Instruction.noOperands(EBREAK);
          case 0x105: case 0x302: return Instruction.noOperands(UNIMP);
          default: return Instruction.INVALID;
        }
      case 1:
        return Isa.csrToReg(csr) == 0 ? Instruction.INVALID : new Instruction(CSRRW, rd(insn), rs1(insn), csr, false, false, true);
      case 2:
        return Isa.csrToReg(csr) == 0 ? Instruction.INVALID : new Instruction(CSRRS, rd(insn), rs1(insn), csr, false, false, true);
      case 3: case 5: case 6: case 7:
        return Instruction.noOperands(UNIMP);
      default:
        return Instruction.INVALID;
    }
  }

  /** Custom opcode 0x0B: Jolt's inlines, R format (decoder.cpp:1210-1250). */
  static Instruction processInline(int insn) {
    int f3 = funct3(insn);
    switch (funct7(insn)) {
      case 0: return f3 == 0 ? r(SHA256_INLINE, insn) : f3 == 1 ? r(SHA256_INIT_INLINE, insn) : Instruction.INVALID;
      case 1: return f3 == 0 ? r(KECCAK256, insn) : f3 == 1 ? r(KECCAK256_ABSORB_PERMUTE, insn) : Instruction.INVALID;
      case 2: return f3 == 0 ? r(BLAKE2_INLINE, insn) : Instruction.INVALID;
      case 3: return f3 == 0 ? r(BLAKE3_INLINE, insn) : f3 == 1 ? r(BLAKE3_KEYED64_INLINE, insn) : Instruction.INVALID;
      case 4: return f3 == 0 ? r(BIGINT256_MUL_INLINE, insn) : Instruction.INVALID;
      case 5:
        switch (f3) {
          case 0: return r(SECP256K1_MULQ_INLINE, insn);
          case 1: return r(SECP256K1_SQUAREQ_INLINE, insn);
          case 2: return r(SECP256K1_DIVQ_INLINE, insn);
          case 4: return r(SECP256K1_MULR_INLINE, insn);
          case 5: return r(SECP256K1_SQUARER_INLINE, insn);
          case 6: return r(SECP256K1_DIVR_INLINE, insn);
          case 7: return r(SECP256K1_GLVR_ADV_INLINE, insn);
          default: return Instruction.INVALID;
        }
      default: return Instruction.INVALID;
    }
  }

  /** Custom opcode 0x5B (decoder.cpp:1251-1268): rev8w (immediate 0), VirtualAssertEQ (B format), VirtualHostIO. */
  static Instruction processVirtualI(int insn) {
    int f3 = funct3(insn);
    if (f3 == 0 && funct7(insn) == 5) return new Instruction(VirtualRev8W, rd(insn), rs1(insn), 0, false, false, true);
    if (f3 == 1) return b(VirtualAssertEQ, insn);
    if (f3 == 2 && rd(insn) == 0) return Instruction.noOperands(VirtualHostIO);
    return Instruction.INVALID;
  }

  /** decode_instr (decoder.cpp:1169-1270). */
  public static Instruction decode32(int insn) {
    switch (insn & 0x7f) {
      case 0x33: return processOp(insn);
      case 0x3b: return processOp64(insn);
      case 0x2f: return processAtomic(insn);
      case 0x13: return processOpImm(insn);
      case 0x1b: return processOp64Imm(insn);
      case 0x37: return new Instruction(LUI, rd(insn), immU(insn), 0, false, true, true);
      case 0x17: return new Instruction(AUIPC, rd(insn), immU(insn), 0, false, true, true);
      case 0x63: return processBranch(insn);
      case 0x03: return processLoad(insn);
      case 0x23: return processStore(insn);
      case 0x6f: return new Instruction(JAL, rd(insn), immJ(insn), 0, false, true, true);
      case 0x67: return new Instruction(JALR, rd(insn), rs1(insn), immI(insn), false, false, true);
      case 0x0f: return funct3(insn) == 0 ? Instruction.noOperands(FENCE) : Instruction.noOperands(UNIMP);
      case 0x73: return processSystem(insn);
      case 0x0b: return processInline(insn);
      case 0x5b: return processVirtualI(insn);
      default: return Instruction.INVALID;
    }
  }

  // ---- compressed instructions (decoder.cpp:646-1167) ----

  /** Sign-extended 6-bit immediate of C.ADDI, C.ADDIW, C.LI, C.ANDI: bit 12 and bits 6:2. */
  static int cImm6(int h) { return ((h & 0x1000) != 0 ? 0xffffffc0 : 0) | ((h >>> 7) & 0x20) | ((h >>> 2) & 0x1f); }

  /** C.J and C.JAL's jump immediate placed in a JAL encoding's bits 31:12. */
  static int cJumpField(int h) {
    int offset = ((h & 0x1000) != 0 ? 0xfffff000 : 0) | ((h >>> 1) & 0x800) | ((h >>> 7) & 0x10) | ((h >>> 1) & 0x300)
        | ((h << 2) & 0x400) | ((h >>> 1) & 0x40) | ((h << 1) & 0x80) | ((h >>> 2) & 0xe) | ((h << 3) & 0x20);
    int imm = ((offset >>> 1) & 0x80000) | ((offset << 8) & 0x7fe00) | ((offset >>> 3) & 0x100) | ((offset >>> 12) & 0xff);
    return imm << 12;
  }

  /** C.BEQZ and C.BNEZ: a branch encoding's immediate bits, with rs1 = r+8 and rs2 = x0. */
  static int cBranch(int h, int funct3) {
    int r = (h >>> 7) & 7;
    int offset = ((h & 0x1000) != 0 ? 0xfffffe00 : 0) | ((h >>> 4) & 0x100) | ((h >>> 7) & 0x18) | ((h << 1) & 0xc0)
        | ((h >>> 2) & 0x6) | ((h << 3) & 0x20);
    int imm2 = ((offset >>> 6) & 0x40) | ((offset >>> 5) & 0x3f);
    int imm1 = (offset & 0x1e) | ((offset >>> 11) & 1);
    // as jolt-cpp: the register goes in bits 24:20 (the rs2 field), rs1 = x0
    return (imm2 << 25) | ((r + 8) << 20) | (funct3 << 12) | (imm1 << 7) | 0x63;
  }

  /**
   * The 32-bit encoding of a compressed instruction (RV64C): 0xffffffff where it can't be expanded, 0 for the reserved
   * forms that trap (UNIMP). Bit for bit as decoder.cpp's uncompress_instruction.
   */
  public static int uncompress(int h) {
    int op = h & 3, f3 = (h >>> 13) & 7;
    switch (op) {
      case 0:
        switch (f3) {
          case 0: { // C.ADDI4SPN
            int rd = (h >>> 2) & 7;
            int nzuimm = ((h >>> 7) & 0x30) | ((h >>> 1) & 0x3c0) | ((h >>> 4) & 0x4) | ((h >>> 2) & 0x8);
            return nzuimm != 0 ? (nzuimm << 20) | (2 << 15) | ((rd + 8) << 7) | 0x13 : 0;
          }
          case 1: { // C.FLD
            int rd = (h >>> 2) & 7, rs1 = (h >>> 7) & 7;
            int offset = ((h >>> 7) & 0x38) | ((h << 1) & 0xc0);
            return (offset << 20) | ((rs1 + 8) << 15) | (3 << 12) | ((rd + 8) << 7) | 0x7;
          }
          case 2: { // C.LW
            int rs1 = (h >>> 7) & 7, rd = (h >>> 2) & 7;
            int offset = ((h >>> 7) & 0x38) | ((h >>> 4) & 0x4) | ((h << 1) & 0x40);
            return (offset << 20) | ((rs1 + 8) << 15) | (2 << 12) | ((rd + 8) << 7) | 0x3;
          }
          case 3: { // C.LD
            int rs1 = (h >>> 7) & 7, rd = (h >>> 2) & 7;
            int offset = ((h >>> 7) & 0x38) | ((h << 1) & 0xc0);
            return (offset << 20) | ((rs1 + 8) << 15) | (3 << 12) | ((rd + 8) << 7) | 0x3;
          }
          case 5: { // C.FSD
            int rs1 = (h >>> 7) & 7, rs2 = (h >>> 2) & 7;
            int offset = ((h >>> 7) & 0x38) | ((h << 1) & 0xc0);
            return (((offset >>> 5) & 0x7f) << 25) | ((rs2 + 8) << 20) | ((rs1 + 8) << 15) | (3 << 12) | ((offset & 0x1f) << 7) | 0x27;
          }
          case 6: { // C.SW
            int rs1 = (h >>> 7) & 7, rs2 = (h >>> 2) & 7;
            int offset = ((h >>> 7) & 0x38) | ((h << 1) & 0x40) | ((h >>> 4) & 0x4);
            return (((offset >>> 5) & 0x7f) << 25) | ((rs2 + 8) << 20) | ((rs1 + 8) << 15) | (2 << 12) | ((offset & 0x1f) << 7) | 0x23;
          }
          case 7: { // C.SD
            int rs1 = (h >>> 7) & 7, rs2 = (h >>> 2) & 7;
            int offset = ((h >>> 7) & 0x38) | ((h << 1) & 0xc0);
            return (((offset >>> 5) & 0x7f) << 25) | ((rs2 + 8) << 20) | ((rs1 + 8) << 15) | (3 << 12) | ((offset & 0x1f) << 7) | 0x23;
          }
          default: return 0xffffffff; // 4: reserved
        }
      case 1:
        switch (f3) {
          case 0: { // C.ADDI (r = 0 or imm = 0: NOP)
            int r = (h >>> 7) & 0x1f, imm = cImm6(h);
            if (r == 0 || imm == 0) return 0x13;
            return (imm << 20) | (r << 15) | (r << 7) | 0x13;
          }
          case 1: { // C.ADDIW (imm 0: sext.w)
            int r = (h >>> 7) & 0x1f, imm = cImm6(h);
            if (r == 0) return 0xffffffff;
            if (imm == 0) return (r << 15) | (r << 7) | 0x1b;
            return (imm << 20) | (r << 15) | (r << 7) | 0x1b;
          }
          case 2: { // C.LI (r = 0: HINT, NOP)
            int r = (h >>> 7) & 0x1f, imm = cImm6(h);
            return r != 0 ? (imm << 20) | (r << 7) | 0x13 : 0x13;
          }
          case 3: {
            int r = (h >>> 7) & 0x1f;
            if (r == 2) { // C.ADDI16SP
              int imm = ((h & 0x1000) != 0 ? 0xfffffc00 : 0) | ((h >>> 3) & 0x200) | ((h >>> 2) & 0x10)
                  | ((h << 1) & 0x40) | ((h << 4) & 0x180) | ((h << 3) & 0x20);
              if (imm != 0) return (imm << 20) | (r << 15) | (r << 7) | 0x13;
            }
            if (r != 0 && r != 2) { // C.LUI
              int nzimm = ((h & 0x1000) != 0 ? 0xfffc0000 : 0) | ((h << 5) & 0x20000) | ((h << 10) & 0x1f000);
              if (nzimm != 0) return nzimm | (r << 7) | 0x37;
            }
            if (r == 0) return 0x13;
            return 0xffffffff;
          }
          case 4: {
            int f2 = (h >>> 10) & 3;
            switch (f2) {
              case 0: { // C.SRLI
                int sh = ((h >>> 7) & 0x20) | ((h >>> 2) & 0x1f), rs1 = (h >>> 7) & 7;
                return (sh << 20) | ((rs1 + 8) << 15) | (5 << 12) | ((rs1 + 8) << 7) | 0x13;
              }
              case 1: { // C.SRAI
                int sh = ((h >>> 7) & 0x20) | ((h >>> 2) & 0x1f), rs1 = (h >>> 7) & 7;
                return (0x20 << 25) | (sh << 20) | ((rs1 + 8) << 15) | (5 << 12) | ((rs1 + 8) << 7) | 0x13;
              }
              case 2: { // C.ANDI
                int r = (h >>> 7) & 7, imm = cImm6(h);
                return (imm << 20) | ((r + 8) << 15) | (7 << 12) | ((r + 8) << 7) | 0x13;
              }
              default: {
                int f1 = (h >>> 12) & 1, f22 = (h >>> 5) & 3, rs1 = (h >>> 7) & 7, rs2 = (h >>> 2) & 7;
                int base = ((rs2 + 8) << 20) | ((rs1 + 8) << 15) | ((rs1 + 8) << 7);
                if (f1 == 0) {
                  switch (f22) {
                    case 0: return (0x20 << 25) | base | 0x33;           // C.SUB
                    case 1: return base | (4 << 12) | 0x33;              // C.XOR
                    case 2: return base | (6 << 12) | 0x33;              // C.OR
                    default: return base | (7 << 12) | 0x33;             // C.AND
                  }
                }
                if (f22 == 0) return (0x20 << 25) | base | 0x3b;        // C.SUBW
                if (f22 == 1) return base | 0x3b;                        // C.ADDW
                return 0xffffffff;
              }
            }
          }
          case 5: return cJumpField(h) | 0x6f;                         // C.J: jal x0
          case 6: return cBranch(h, 0);                                // C.BEQZ
          default: return cBranch(h, 1);                               // C.BNEZ
        }
      case 2:
        switch (f3) {
          case 0: { // C.SLLI
            int r = (h >>> 7) & 0x1f, sh = ((h >>> 7) & 0x20) | ((h >>> 2) & 0x1f);
            return r != 0 ? (sh << 20) | (r << 15) | (1 << 12) | (r << 7) | 0x13 : 0xffffffff;
          }
          case 1: { // C.FLDSP
            int rd = (h >>> 7) & 0x1f, offset = ((h >>> 7) & 0x20) | ((h >>> 2) & 0x18) | ((h << 4) & 0x1c0);
            return rd != 0 ? (offset << 20) | (2 << 15) | (3 << 12) | (rd << 7) | 0x7 : 0xffffffff;
          }
          case 2: { // C.LWSP
            int r = (h >>> 7) & 0x1f, offset = ((h >>> 7) & 0x20) | ((h >>> 2) & 0x1c) | ((h << 4) & 0xc0);
            return r != 0 ? (offset << 20) | (2 << 15) | (2 << 12) | (r << 7) | 0x3 : 0xffffffff;
          }
          case 3: { // C.LDSP
            int rd = (h >>> 7) & 0x1f, offset = ((h >>> 7) & 0x20) | ((h >>> 2) & 0x18) | ((h << 4) & 0x1c0);
            return rd != 0 ? (offset << 20) | (2 << 15) | (3 << 12) | (rd << 7) | 0x3 : 0xffffffff;
          }
          case 4: {
            int f1 = (h >>> 12) & 1, rs1 = (h >>> 7) & 0x1f, rs2 = (h >>> 2) & 0x1f;
            if (f1 == 0) {
              if (rs1 != 0 && rs2 == 0) return (rs1 << 15) | 0x67;               // C.JR
              if (rs1 == 0 && rs2 != 0) return 0x13;                             // HINT
              if (rs1 != 0) return (rs2 << 20) | (rs1 << 7) | 0x33;               // C.MV: add rd, x0, rs2
              return 0xffffffff;
            }
            if (rs1 == 0 && rs2 == 0) return 0x00100073;                         // C.EBREAK
            if (rs1 != 0 && rs2 == 0) return (rs1 << 15) | (1 << 7) | 0x67;       // C.JALR
            if (rs1 == 0) return 0x13;                                           // HINT
            return (rs2 << 20) | (rs1 << 15) | (rs1 << 7) | 0x33;                // C.ADD
          }
          case 5: { // C.FSDSP
            int rs2 = (h >>> 2) & 0x1f, offset = ((h >>> 7) & 0x38) | ((h >>> 1) & 0x1c0);
            return (((offset >>> 5) & 0x3f) << 25) | (rs2 << 20) | (2 << 15) | (3 << 12) | ((offset & 0x1f) << 7) | 0x27;
          }
          case 6: { // C.SWSP
            int rs2 = (h >>> 2) & 0x1f, offset = ((h >>> 7) & 0x3c) | ((h >>> 1) & 0xc0);
            return (((offset >>> 5) & 0x3f) << 25) | (rs2 << 20) | (2 << 15) | (2 << 12) | ((offset & 0x1f) << 7) | 0x23;
          }
          default: { // C.SDSP
            int rs2 = (h >>> 2) & 0x1f, offset = ((h >>> 7) & 0x38) | ((h >>> 1) & 0x1c0);
            return (((offset >>> 5) & 0x3f) << 25) | (rs2 << 20) | (2 << 15) | (3 << 12) | ((offset & 0x1f) << 7) | 0x23;
          }
        }
      default:
        return 0xffffffff;
    }
  }

  /**
   * decode_next_instr (decoder.cpp:1273-1297): compressed if the low two bits aren't 11. An expansion of 0xffffffff is
   * an invalid instruction of size 2, of 0 an UNIMP of size 2. (jolt-cpp only asserts the compressed case, so a 32-bit
   * word 0xffffffff also comes out as invalid with size 2 in its release build.)
   */
  public static Decoded decodeNext(byte[] code, int offset) {
    int h0 = (code[offset] & 0xff) | (code[offset + 1] & 0xff) << 8;
    boolean compressed = (h0 & 3) != 3;
    int word;
    if (compressed) {
      word = uncompress(h0);
    } else {
      int h1 = offset + 3 < code.length ? (code[offset + 2] & 0xff) | (code[offset + 3] & 0xff) << 8 : 0;
      word = h0 | h1 << 16;
    }
    if (word == 0xffffffff) return new Decoded(Instruction.INVALID, 2);
    if (word == 0) return new Decoded(Instruction.noOperands(UNIMP), 2);
    return new Decoded(decode32(word), compressed ? 2 : 4);
  }
}
