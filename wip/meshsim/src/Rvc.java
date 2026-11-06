/**
 * The RISC-V C extension's decompression as the Tracer applies it (spec 4.21.4 step 2): a 16-bit instruction to its 32-bit
 * form, exactly as a16z's decoder does it at 7189216 (crates/jolt-riscv/src/uncompress.rs), which gold ports bit for bit
 * (wip/gold/src/gold/Decoder.java, jolt-cpp's decoder.cpp:646-1167 with a16z's D12: C.SLLI with rd = x0 is SLLI x0, x0,
 * shamt). Ported from gold unchanged (WORKER22, B1); RvcTest checks all 2^16 encodings against gold's.
 *
 * <p>{@link #uncompress} returns 0xFFFFFFFF where an encoding has no 32-bit form and 0 for the reserved forms that trap
 * (c.unimp, 0x0000, among them): both are invalid expansions, ERRORS [2], when executed (4.21.4, 4.21.8). The Tracer's
 * code-stream word is the result with bit 0 cleared (4.19.2).
 */
final class Rvc {
    private Rvc() {}

    /** The code-stream word of a 16-bit instruction (bit 0 cleared as its flag), or 0 if it has no valid 32-bit form. */
    static int codeWord(int h) {
        int w = uncompress(h & 0xFFFF);
        return w == 0xffffffff || w == 0 ? 0 : w & ~1;
    }

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
    static int uncompress(int h) {
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
            case 0: { // C.SLLI; with rd = x0 a HINT: SLLI x0, x0, shamt, as a16z (owner's D12 ruling; jolt-cpp: invalid)
              int r = (h >>> 7) & 0x1f, sh = ((h >>> 7) & 0x20) | ((h >>> 2) & 0x1f);
              return (sh << 20) | (r << 15) | (1 << 12) | (r << 7) | 0x13;
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

}
