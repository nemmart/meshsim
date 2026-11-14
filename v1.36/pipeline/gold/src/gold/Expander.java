package gold;

import static gold.Op.*;

import java.util.ArrayList;
import java.util.List;

/**
 * Expands one RISC-V instruction into the rows Jolt traces for it: jolt-cpp's expand_virt_seq (isa/virtual.cpp:130-1208
 * at 821e1bd), written out per instruction family. Most instructions are one row; the others become a fixed sequence
 * of RISC-V and virtual instructions using temporaries from register 40 up (and the inlines registers 48 up).
 */
public final class Expander {

  // ---- lengths (virtual.cpp:1210-1306) ----

  /** Rows for an opcode with a fixed-length expansion. */
  public static int numCycles(int op) {
    Secp.Operation secp = Secp.forOpcode(op);
    if (secp != Secp.Operation.NONE) return secp.cycles;
    switch (op) {
      case SLL: case SRL: case SRA: case SLLW: case SRLW: case SRAW: return 2;
      case LB: case LBU: case LH: case LHU: case LW: case LWU: return 3;
      case SW: case SH: case SB: return 6;
      case DIV: return 12;
      case DIVU: return 8;
      case DIVW: return 14;
      case REM: return 9;
      case REMU: return 7;
      case REMW: return 11;
      case REMUW: return 9;
      case DIVUW: return 11;
      case MULH: return 7;
      case MULHSU: return 11;
      case LR_D: return 5;
      case LR_W: return 7;
      case SC_D: return 12;
      case SC_W: return 21;
      case AMOSWAP_D: return 3;
      case AMOSWAP_W: return 8;
      case AMOADD_D: case AMOAND_D: case AMOXOR_D: case AMOOR_D: return 4;
      case AMOADD_W: case AMOAND_W: case AMOXOR_W: case AMOOR_W: return 9;
      case AMOMIN_D: case AMOMAX_D: case AMOMAXU_D: case AMOMINU_D: return 7;
      case AMOMIN_W: case AMOMAX_W: case AMOMAXU_W: case AMOMINU_W: return 13;
      case KECCAK256: return 3087;
      case KECCAK256_ABSORB_PERMUTE: return 3121;
      case SHA256_INLINE: return 1900;
      case SHA256_INIT_INLINE: return 1864;
      case BLAKE2_INLINE: return 1067;
      case BLAKE3_INLINE: return 648;
      case BLAKE3_KEYED64_INLINE: return 646;
      case BIGINT256_MUL_INLINE: return 141;
      case ECALL: return 7;
      case UNIMP: return 0;
      default: return 1;
    }
  }

  /** Rows for an instruction: rd = x0 rewrites and the CSR instructions depend on the operands. */
  public static int numCycles(Instruction in) {
    if (shouldNopForRd0(in)) return 1;
    if (in.opcode == CSRRW) return in.a == 0 ? 1 : in.a == in.b ? 3 : 2;
    if (in.opcode == CSRRS) return in.b == 0 ? 1 : in.a == 0 ? 1 : in.a == in.b ? 3 : 2;
    return numCycles(in.opcode);
  }

  // ---- the rd = x0 rule (virtual.cpp:15-128) ----

  /** Instructions whose effect goes beyond writing rd: with rd = x0 their rd is remapped, not dropped. */
  static boolean hasSideEffectsBeyondRd(int op) {
    switch (op) {
      case JAL: case JALR: case LB: case LBU: case LH: case LHU: case LW: case LWU: case LD:
      case LR_D: case LR_W: case SC_D: case SC_W:
      case AMOSWAP_D: case AMOSWAP_W: case AMOADD_D: case AMOADD_W: case AMOAND_D: case AMOAND_W:
      case AMOXOR_D: case AMOXOR_W: case AMOOR_D: case AMOOR_W: case AMOMIN_D: case AMOMIN_W:
      case AMOMAX_D: case AMOMAX_W: case AMOMAXU_D: case AMOMAXU_W: case AMOMINU_D: case AMOMINU_W:
      case CSRRW: case CSRRS: case VirtualHostIO:
        return true;
      default:
        return isInline(op);
    }
  }

  /** Instructions whose expansion deals with rd = x0 itself (upstream PR #1439 excludes LR/SC and the inlines). */
  static boolean rd0HandledInternally(int op) {
    // LR/SC aren't exempt: their rd = x0 is remapped to a temporary, as a16z (crates/jolt-program/src/expand/
    // operands.rs:51-60, which exempts only ECALL, MRET, EBREAK, CSRRW, CSRRS); owner's D14 ruling, DECISIONS-v1.3
    // section 17 (236a55a). jolt-cpp exempts them (isa/virtual.cpp:57-80). The inlines stay exempt, as jolt-cpp.
    switch (op) {
      case ECALL: case MRET: case EBREAK: case CSRRW: case CSRRS:
        return true;
      default:
        return isInline(op);
    }
  }

  static boolean isInline(int op) { return op >= KECCAK256 && op <= SECP256K1_GLVR_ADV_INLINE; }

  static boolean hasRd0(Instruction in) { return !in.immA && in.a == 0; }

  static boolean shouldNopForRd0(Instruction in) {
    return hasRd0(in) && !rd0HandledInternally(in.opcode) && !hasSideEffectsBeyondRd(in.opcode);
  }

  static boolean shouldRemapRd0(Instruction in) {
    return hasRd0(in) && !rd0HandledInternally(in.opcode) && hasSideEffectsBeyondRd(in.opcode);
  }

  // ---- the emitter (virtual.cpp:130-218) ----

  private final Instruction src;
  private final int length;
  private final List<VInstr> rows = new ArrayList<>();
  private int nextTemp = Isa.FIRST_TEMP;

  private Expander(Instruction src) {
    this.src = src;
    this.length = numCycles(src);
  }

  /** The rows for one source instruction (riscPc and pcIncrement are filled in by the bytecode builder). */
  public static List<VInstr> expand(Instruction src) {
    Expander e = new Expander(src);
    e.run();
    if (e.rows.size() != e.length) {
      throw new IllegalStateException(src + ": expanded to " + e.rows.size() + " rows, expected " + e.length);
    }
    return e.rows;
  }

  int temp() {
    if (nextTemp >= Isa.NUM_STATEFUL_REGS) throw new IllegalStateException("out of temporaries");
    return nextTemp++;
  }

  void free(int count) { nextTemp -= count; }

  /** emit_instr: the row is virtual if the expansion is longer than one row or changes the opcode. */
  void emit(int op, long rd, long rs1, long rs2, boolean rdImm, boolean rs1Imm, boolean rs2Imm) {
    VInstr v = new VInstr();
    v.opcode = op;
    v.rd = rd; v.rs1 = rs1; v.rs2 = rs2;
    v.rdImm = rdImm; v.rs1Imm = rs1Imm; v.rs2Imm = rs2Imm;
    v.isVirtual = length > 1 || op != src.opcode;
    v.isFirstInSequence = v.isVirtual && rows.isEmpty();
    v.seqRemaining = length - rows.size() - 1;
    rows.add(v);
  }

  void emitI(int op, int rd, int rs1, long imm) { emit(op, rd, rs1, imm, false, false, true); }
  void emitR(int op, int rd, int rs1, int rs2) { emit(op, rd, rs1, rs2, false, false, false); }
  void emitLd(int op, int rd, int rs1, long imm) { emitI(op, rd, rs1, imm); }
  void emitS(int op, int rs1, int rs2, long imm) { emit(op, imm, rs1, rs2, true, false, false); }
  void emitB(int op, long imm, int rs1, int rs2) { emit(op, imm, rs1, rs2, true, false, false); }
  void emitAdvice(int rd) { emit(VirtualAdvice, rd, 0, 0, false, true, true); }

  /** emit_u: LUI and AUIPC carry the immediate in rs1; other ops "load an immediate" as op rd, x0, imm. */
  void emitU(int op, int rd, long imm) {
    if (op == LUI || op == AUIPC) emit(op, rd, imm, 0, false, true, true);
    else emitI(op, rd, 0, imm);
  }

  void emitShiftData(int op, int rd, int value, int base, long offset) {
    emit(op, rd, value, base, false, false, false);
    rows.get(rows.size() - 1).shiftOffset = (int) offset;
  }

  // ---- shared pieces ----

  /** MULH (virtual.cpp:220-232): signed high product from MULHU and sign corrections. */
  void expandMulh(int rd, int rs1, int rs2) {
    int sx = temp(), sy = temp(), t = temp();
    emitI(VirtualMovsign, sx, rs1, 0);
    emitI(VirtualMovsign, sy, rs2, 0);
    emitR(MUL, sx, sx, rs2);
    emitR(MUL, sy, sy, rs1);
    emitR(MULHU, t, rs1, rs2);
    emitR(ADD, t, t, sx);
    emitR(ADD, rd, t, sy);
    free(3);
  }

  /** Narrow load (virtual.cpp:239-246): load the word, mask the lane, extract it (sign-extended or not). */
  void expandLoad(int rd, int rs1, long imm, int windowMask, boolean signed, int load) {
    int word = temp(), mask = temp();
    emitLd(load, word, rs1, imm);
    emitI(windowMask, mask, rs1, imm);
    emitR(signed ? VirtualPextSigned : VirtualPext, rd, word, mask);
    free(2);
  }

  /** Narrow store (virtual.cpp:248-262): read-modify-write of the containing word. */
  void expandNarrowStore(long imm, int rs1, int rs2, int windowMask, int shiftData, int load) {
    int word = temp(), mask = temp();
    emitLd(load, word, rs1, imm);
    emitI(windowMask, mask, rs1, imm);
    emitR(ANDN, word, word, mask);
    emitShiftData(shiftData, mask, rs2, rs1, imm);
    emitR(ADD, word, word, mask);
    int store = load == VirtualLoadB ? VirtualStoreB : load == VirtualLoadH ? VirtualStoreH : VirtualStoreW;
    emitS(store, rs1, word, imm);
    free(2);
  }

  /** A 32-bit AMO's lane update, shared by the W forms (virtual.cpp:722-987). */
  void amoWordPrologue(int word, int lane, int old, int rs1) {
    emitLd(VirtualLoadW, word, rs1, 0);
    emitI(VirtualWindowMaskW, lane, rs1, 0);
    emitR(VirtualPextSigned, old, word, lane);
  }

  void amoWordEpilogue(int rd, int rs1, int word, int lane, int old, int value) {
    emitR(ANDN, word, word, lane);
    emitShiftData(VirtualShiftDataW, lane, value, rs1, 0);
    emitR(ADD, word, word, lane);
    emitS(VirtualStoreW, rs1, word, 0);
    emitI(ADDI, rd, old, 0);
  }

  // ---- the expansion (virtual.cpp:264-1208) ----

  private void run() {
    Instruction in = src;
    if (shouldNopForRd0(in)) {           // virtual.cpp:270-274: one ADDI x0, x0, 0
      emit(ADDI, 0, 0, 0, false, false, true);
      // not a virtual sequence, as a16z's noop_for (crates/jolt-program/src/expand/mod.rs:144-145; owner's D13 ruling,
      // DECISIONS-v1.3 section 17, 91ddc92); jolt-cpp flags it virtual and first when the opcode changed
      rows.get(0).isVirtual = false;
      rows.get(0).isFirstInSequence = false;
      return;
    }
    boolean remapped = false;
    if (shouldRemapRd0(in)) {            // virtual.cpp:276-283: rd = x0 becomes the first temporary
      in = in.withA(temp());
      remapped = true;
    }
    int rd = in.a, rs1 = in.b, rs2 = in.c;     // as registers (R format)
    long imm = in.c;                           // I format: sign-extended immediate
    switch (in.opcode) {
      case DIV: case REM: case DIVU: case REMU: case DIVUW: case REMUW: case DIVW: case REMW:
        expandDivision(in.opcode, rd, rs1, rs2);
        break;
      case MULH: expandMulh(rd, rs1, rs2); break;
      case MULHSU: expandMulhsu(rd, rs1, rs2); break;
      case LB: expandLoad(rd, rs1, imm, VirtualWindowMaskB, true, VirtualLoadB); break;
      case LBU: expandLoad(rd, rs1, imm, VirtualWindowMaskB, false, VirtualLoadB); break;
      case LH: expandLoad(rd, rs1, imm, VirtualWindowMaskH, true, VirtualLoadH); break;
      case LHU: expandLoad(rd, rs1, imm, VirtualWindowMaskH, false, VirtualLoadH); break;
      case LW: expandLoad(rd, rs1, imm, VirtualWindowMaskW, true, VirtualLoadW); break;
      case LWU: expandLoad(rd, rs1, imm, VirtualWindowMaskW, false, VirtualLoadW); break;
      // stores: a = imm, b = rs1, c = rs2
      case SW: expandNarrowStore(in.a, in.b, in.c, VirtualWindowMaskW, VirtualShiftDataW, VirtualLoadW); break;
      case SH: expandNarrowStore(in.a, in.b, in.c, VirtualWindowMaskH, VirtualShiftDataH, VirtualLoadH); break;
      case SB: expandNarrowStore(in.a, in.b, in.c, VirtualWindowMaskB, VirtualShiftDataB, VirtualLoadB); break;
      case SRAI: case SRLI: case SRAIW: case SRLIW: case SLLI: case SLLIW:
      case SRA: case SRL: case SLL: case SLLW: case SRLW: case SRAW:
        expandShift(in.opcode, rd, rs1, rs2, imm);
        break;
      case LR_D: case LR_W: case SC_D: case SC_W:
        expandLrSc(in.opcode, rd, rs1, rs2);
        break;
      case AMOSWAP_D: case AMOADD_D: case AMOAND_D: case AMOXOR_D: case AMOOR_D:
      case AMOMIN_D: case AMOMAX_D: case AMOMAXU_D: case AMOMINU_D:
        expandAmoDouble(in.opcode, rd, rs1, rs2);
        break;
      case AMOSWAP_W: case AMOADD_W: case AMOAND_W: case AMOXOR_W: case AMOOR_W:
      case AMOMIN_W: case AMOMAX_W: case AMOMAXU_W: case AMOMINU_W:
        expandAmoWord(in.opcode, rd, rs1, rs2);
        break;
      case FENCE: case VirtualHostIO:      // virtual.cpp:1063-1067: no register reads or writes
        emit(in.opcode, 0, 0, 0, true, true, true);
        break;
      case ECALL: expandEcall(); break;
      case CSRRW: case CSRRS: expandCsr(in); break;
      case MRET: {                          // virtual.cpp:1128-1133: return through mepc
        int t = temp();
        emitI(JALR, t, Isa.MEPC, 0);
        free(1);
        break;
      }
      case EBREAK: {                        // virtual.cpp:1134-1164: JAL-to-self (`j .`) with a throwaway rd
        int t = temp();
        emit(JAL, t, 0, 0, false, true, true);
        free(1);
        break;
      }
      case JAL: case JALR: {                // virtual.cpp:1165-1184 (rd = x0 was already remapped above)
        int link = in.a == 0 ? temp() : in.a;
        emit(in.opcode, link, in.b, in.c, false, in.immB, in.immC);
        if (in.a == 0) free(1);
        break;
      }
      default:
        if (isInline(in.opcode)) {
          Inlines.expand(this, in.opcode, rd, rs1, rs2);
        } else {                            // virtual.cpp:1185-1187: one row with the source operands
          emit(in.opcode, in.a, in.b, in.c, in.immA, in.immB, in.immC);
        }
    }
    if (remapped) free(1);

    // secp inlines mark their first row with the operation and its inputs (virtual.cpp:1194-1201)
    Secp.Operation secp = Secp.forOpcode(in.opcode);
    if (secp != Secp.Operation.NONE) {
      VInstr first = rows.get(0);
      first.adviceOp = secp;
      first.adviceRs1 = in.b;
      first.adviceRs2 = secp.hasRhs ? in.c : 0;
    }
  }

  /** The division family (virtual.cpp:285-447): advice gives the quotient; asserts and a multiply check it. */
  private void expandDivision(int op, int rd, int rs1, int rs2) {
    switch (op) {
      case DIV: {
        int q = temp(), dividendMag = temp(), divisorMag = temp(), qSign = temp(), qMag = temp(), rem = temp();
        emitR(VirtualAdviceDiv, q, rs1, rs2);
        emitB(VirtualAssertValidDiv0, 0, rs2, q);
        emitR(VirtualNegateIf, dividendMag, rs1, rs1);
        emitR(VirtualNegateIf, divisorMag, rs2, rs2);
        emitR(XOR, qSign, rs1, rs2);
        emitR(VirtualNegateIf, qMag, qSign, q);
        emitB(VirtualAssertMulUNoOverflow, 0, qMag, divisorMag);
        emitR(MUL, rem, qMag, divisorMag);
        emitB(VirtualAssertLTE, 0, rem, dividendMag);
        emitR(SUB, rem, dividendMag, rem);
        emitB(VirtualAssertValidUnsignedRemainder, 0, rem, divisorMag);
        emitI(ADDI, rd, q, 0);
        free(6);
        break;
      }
      case REM: {
        int qMag = temp(), dividendMag = temp(), divisorMag = temp(), remMag = temp();
        emitR(VirtualAdviceRem, qMag, rs1, rs2);
        emitR(VirtualNegateIf, dividendMag, rs1, rs1);
        emitR(VirtualNegateIf, divisorMag, rs2, rs2);
        emitB(VirtualAssertMulUNoOverflow, 0, qMag, divisorMag);
        emitR(MUL, remMag, qMag, divisorMag);
        emitB(VirtualAssertLTE, 0, remMag, dividendMag);
        emitR(SUB, remMag, dividendMag, remMag);
        emitB(VirtualAssertValidUnsignedRemainder, 0, remMag, divisorMag);
        emitR(VirtualNegateIf, rd, rs1, remMag);
        free(4);
        break;
      }
      case DIVU: {
        int q = temp(), t = temp();
        emitR(VirtualAdviceDivU, q, rs1, rs2);
        emitB(VirtualAssertValidDiv0, 0, rs2, q);
        emitB(VirtualAssertMulUNoOverflow, 0, q, rs2);
        emitR(MUL, t, q, rs2);
        emitB(VirtualAssertLTE, 0, t, rs1);
        emitR(SUB, t, rs1, t);
        emitB(VirtualAssertValidUnsignedRemainder, 0, t, rs2);
        emitI(ADDI, rd, q, 0);
        free(2);
        break;
      }
      case REMU: {
        int t = temp();
        emitR(VirtualAdviceDivU, t, rs1, rs2);
        emitB(VirtualAssertMulUNoOverflow, 0, t, rs2);
        emitR(MUL, t, t, rs2);
        emitB(VirtualAssertLTE, 0, t, rs1);
        emitR(SUB, t, rs1, t);
        emitB(VirtualAssertValidUnsignedRemainder, 0, t, rs2);
        emitI(ADDI, rd, t, 0);
        free(1);
        break;
      }
      case DIVUW: {
        int a = temp(), b = temp(), q = temp(), t = temp();
        emitI(VirtualZeroExtendWord, a, rs1, 0);
        emitI(VirtualZeroExtendWord, b, rs2, 0);
        emitR(VirtualAdviceDivUW, q, rs1, rs2);
        emitB(VirtualAssertMulUNoOverflow, 0, q, b);
        emitR(MUL, t, q, b);
        emitB(VirtualAssertLTE, 0, t, a);
        emitR(SUB, t, a, t);
        emitB(VirtualAssertValidUnsignedRemainder, 0, t, b);
        emitI(VirtualSignExtendWord, t, q, 0);
        emitB(VirtualAssertValidDiv0, 0, b, t);
        emitI(ADDI, rd, t, 0);
        free(4);
        break;
      }
      case REMUW: {
        int a = temp(), b = temp(), t = temp();
        emitI(VirtualZeroExtendWord, a, rs1, 0);
        emitI(VirtualZeroExtendWord, b, rs2, 0);
        emitR(VirtualAdviceDivUW, t, rs1, rs2);
        emitB(VirtualAssertMulUNoOverflow, 0, t, b);
        emitR(MUL, t, t, b);
        emitB(VirtualAssertLTE, 0, t, a);
        emitR(SUB, t, a, t);
        emitB(VirtualAssertValidUnsignedRemainder, 0, t, b);
        emitI(VirtualSignExtendWord, rd, t, 0);
        free(3);
        break;
      }
      case DIVW: {
        int dividend = temp(), divisor = temp(), q = temp(), dividendMag = temp(), divisorMag = temp(),
            qSign = temp(), qMag = temp(), rem = temp();
        emitI(VirtualSignExtendWord, dividend, rs1, 0);
        emitI(VirtualSignExtendWord, divisor, rs2, 0);
        emitR(VirtualAdviceDivW, q, rs1, rs2);
        emitB(VirtualAssertValidDiv0, 0, divisor, q);
        emitR(VirtualNegateIf, dividendMag, dividend, dividend);
        emitR(VirtualNegateIf, divisorMag, divisor, divisor);
        emitR(XOR, qSign, dividend, divisor);
        emitR(VirtualNegateIf, qMag, qSign, q);
        emitB(VirtualAssertMulUNoOverflow, 0, qMag, divisorMag);
        emitR(MUL, rem, qMag, divisorMag);
        emitB(VirtualAssertLTE, 0, rem, dividendMag);
        emitR(SUB, rem, dividendMag, rem);
        emitB(VirtualAssertValidUnsignedRemainder, 0, rem, divisorMag);
        emitI(VirtualSignExtendWord, rd, q, 0);
        free(8);
        break;
      }
      default: { // REMW
        int dividend = temp(), divisor = temp(), qMag = temp(), dividendMag = temp(), divisorMag = temp(), remMag = temp();
        emitI(VirtualSignExtendWord, dividend, rs1, 0);
        emitI(VirtualSignExtendWord, divisor, rs2, 0);
        emitR(VirtualAdviceRemW, qMag, rs1, rs2);
        emitR(VirtualNegateIf, dividendMag, dividend, dividend);
        emitR(VirtualNegateIf, divisorMag, divisor, divisor);
        emitB(VirtualAssertMulUNoOverflow, 0, qMag, divisorMag);
        emitR(MUL, remMag, qMag, divisorMag);
        emitB(VirtualAssertLTE, 0, remMag, dividendMag);
        emitR(SUB, remMag, dividendMag, remMag);
        emitB(VirtualAssertValidUnsignedRemainder, 0, remMag, divisorMag);
        emitR(VirtualNegateIf, rd, dividend, remMag);
        free(6);
      }
    }
  }

  /** MULHSU (virtual.cpp:453-473): rs1's magnitude times rs2, then the sign applied to the 128-bit product. */
  private void expandMulhsu(int rd, int rs1, int rs2) {
    int v0 = temp(), v1 = temp(), v2 = temp(), v3 = temp();
    emitI(VirtualMovsign, v0, rs1, 0);
    emitI(ANDI, v1, v0, 1);
    emitR(XOR, v2, rs1, v0);
    emitR(ADD, v2, v2, v1);
    emitR(MULHU, v3, v2, rs2);
    emitR(MUL, v2, v2, rs2);
    emitR(XOR, v3, v3, v0);
    emitR(XOR, v2, v2, v0);
    emitR(ADD, v0, v2, v1);
    emitR(SLTU, v0, v0, v2);
    emitR(ADD, rd, v3, v0);
    free(4);
  }

  /** Shifts (virtual.cpp:528-611): immediate right shifts become a bitmask immediate, left shifts a multiply. */
  private void expandShift(int op, int rd, int rs1, int rs2, long imm) {
    switch (op) {
      case SRAI: emitI(VirtualSRAI, rd, rs1, Bits.maskTrailingZeros(imm & 0x3f)); break;
      case SRLI: emitI(VirtualSRLI, rd, rs1, Bits.maskTrailingZeros(imm & 0x3f)); break;
      case SRAIW: emitI(VirtualSRAIW, rd, rs1, (1L << 32) - (1L << (imm & 0x1f))); break;
      case SRLIW: emitI(VirtualSRLIW, rd, rs1, (1L << 32) - (1L << (imm & 0x1f))); break;
      case SLLI: emitI(VirtualMULI, rd, rs1, 1L << (imm & 0x3f)); break;
      case SLLIW: emitI(VirtualMULIW, rd, rs1, 1L << (imm & 0x1f)); break;  // (u32)1 << n, zero-extended
      case SRAW: case SRLW: {
        int mask = temp();
        emitI(VirtualShiftRightBitmaskW, mask, rs2, 0);
        emitR(op == SRAW ? VirtualSRAW : VirtualSRLW, rd, rs1, mask);
        free(1);
        break;
      }
      case SRL: case SRA: {
        int mask = temp();
        emitI(VirtualShiftRightBitmask, mask, rs2, 0);
        emitR(op == SRL ? VirtualSRL : VirtualSRA, rd, rs1, mask);
        break;
      }
      case SLL: {
        int pow2 = temp();
        emitI(VirtualPow2, pow2, rs2, 0);
        emitR(MUL, rd, rs1, pow2);
        break;
      }
      default: { // SLLW
        int pow2 = temp();
        emitI(VirtualPow2W, pow2, rs2, 0);
        emitR(MULW, rd, rs1, pow2);
      }
    }
  }

  /**
   * LR/SC (virtual.cpp:612-712): word and doubleword reservations in registers 32 and 33; LR.D sets both (it covers
   * SC.W), LR.W sets the word one and clears the other; SC succeeds iff its address equals the reservation, then clears
   * both. Both first assert the address is in RAM (at or above 0x8000_0000).
   */
  private void expandLrSc(int op, int rd, int rs1, int rs2) {
    final int resW = Isa.RESERVATION_W, resD = Isa.RESERVATION_D;
    switch (op) {
      case LR_D: {
        int ramStart = temp();
        emitU(LUI, ramStart, 0x80000000L);
        emitB(VirtualAssertLTE, 0, ramStart, rs1);
        free(1);
        emitI(ADDI, resD, rs1, 0);
        emitI(ADDI, resW, rs1, 0);
        emitLd(LD, rd, rs1, 0);
        break;
      }
      case LR_W: {
        int ramStart = temp();
        emitU(LUI, ramStart, 0x80000000L);
        emitB(VirtualAssertLTE, 0, ramStart, rs1);
        free(1);
        emitI(ADDI, resW, rs1, 0);
        emitI(ADDI, resD, 0, 0);
        expandLoad(rd, rs1, 0, VirtualWindowMaskW, true, VirtualLoadW);
        break;
      }
      case SC_D: {
        int success = temp();
        emitU(LUI, success, 0x80000000L);
        emitB(VirtualAssertLTE, 0, success, rs1);
        emitR(XOR, success, resD, rs1);
        emitI(SLTIU, success, success, 1);
        int mem = temp();
        emitLd(LD, mem, rs1, 0);
        int diff = temp();
        emitR(SUB, diff, rs2, mem);
        emitR(MUL, diff, diff, success);
        emitR(ADD, diff, mem, diff);
        free(1);
        emitS(SD, rs1, diff, 0);           // diff's register number, now freed but still holding the value
        free(1);
        emitI(ADDI, resD, 0, 0);
        emitI(ADDI, resW, 0, 0);
        emitI(XORI, rd, success, 1);
        free(1);
        break;
      }
      default: { // SC_W
        int success = temp();
        emitU(LUI, success, 0x80000000L);
        emitB(VirtualAssertLTE, 0, success, rs1);
        emitR(XOR, success, resW, rs1);
        emitI(SLTIU, success, success, 1);
        emitI(ADDI, resW, success, 0);     // spill success: the nested load/store need the temporaries
        free(1);
        int mem = temp();
        expandLoad(mem, rs1, 0, VirtualWindowMaskW, true, VirtualLoadW);
        int diff = temp();
        emitR(SUB, diff, rs2, mem);
        emitR(MUL, diff, diff, resW);
        emitR(ADD, diff, mem, diff);
        free(1);
        emitI(ADDI, resD, diff, 0);
        free(1);
        expandNarrowStore(0, rs1, resD, VirtualWindowMaskW, VirtualShiftDataW, VirtualLoadW);
        emitI(XORI, rd, resW, 1);
        emitI(ADDI, resW, 0, 0);
        emitI(ADDI, resD, 0, 0);
      }
    }
  }

  /** 64-bit AMOs (virtual.cpp:713-987, the _D cases): load, combine, store, return the old value. */
  private void expandAmoDouble(int op, int rd, int rs1, int rs2) {
    if (op == AMOSWAP_D) {
      int old = temp();
      emitLd(LD, old, rs1, 0);
      emitS(SD, rs1, rs2, 0);
      emitI(ADDI, rd, old, 0);
      return;
    }
    if (op == AMOADD_D || op == AMOAND_D || op == AMOXOR_D || op == AMOOR_D) {
      int value = temp(), old = temp();
      emitLd(LD, old, rs1, 0);
      emitR(op == AMOADD_D ? ADD : op == AMOAND_D ? AND : op == AMOXOR_D ? XOR : OR, value, old, rs2);
      emitS(SD, rs1, value, 0);
      emitI(ADDI, rd, old, 0);
      return;
    }
    // min/max: select by a comparison, then old + (rs2 - old) * take
    int old = temp(), sel = temp(), delta = temp();
    emitLd(LD, old, rs1, 0);
    switch (op) {
      case AMOMIN_D: emitR(SLT, sel, rs2, old); break;
      case AMOMAX_D: emitR(SLT, sel, old, rs2); break;
      case AMOMAXU_D: emitR(SLTU, sel, old, rs2); break;
      default: emitR(SLTU, sel, rs2, old); break;      // AMOMINU_D
    }
    emitR(SUB, delta, rs2, old);
    emitR(MUL, delta, delta, sel);
    emitR(ADD, sel, old, delta);
    emitS(SD, rs1, sel, 0);
    emitI(ADDI, rd, old, 0);
  }

  /** 32-bit AMOs (virtual.cpp:713-987, the _W cases): the same on the word's lane of its doubleword. */
  private void expandAmoWord(int op, int rd, int rs1, int rs2) {
    int word = temp(), lane = temp(), old = temp();
    if (op == AMOSWAP_W) {
      amoWordPrologue(word, lane, old, rs1);
      amoWordEpilogue(rd, rs1, word, lane, old, rs2);
      return;
    }
    int value = temp();
    if (op == AMOADD_W || op == AMOAND_W || op == AMOXOR_W || op == AMOOR_W) {
      amoWordPrologue(word, lane, old, rs1);
      emitR(op == AMOADD_W ? ADD : op == AMOAND_W ? AND : op == AMOXOR_W ? XOR : OR, value, old, rs2);
      amoWordEpilogue(rd, rs1, word, lane, old, value);
      return;
    }
    int take = temp();
    amoWordPrologue(word, lane, old, rs1);
    emitI(VirtualSignExtendWord, value, rs2, 0);
    switch (op) {
      case AMOMIN_W: emitR(SLT, take, value, old); break;
      case AMOMAX_W: emitR(SLT, take, old, value); break;
      case AMOMAXU_W: emitR(SLTU, take, old, value); break;
      default: emitR(SLTU, take, value, old); break;   // AMOMINU_W
    }
    emitR(SUB, value, rs2, old);
    emitR(MUL, value, value, take);
    emitR(ADD, value, value, old);
    amoWordEpilogue(rd, rs1, word, lane, old, value);
  }

  /** ECALL (virtual.cpp:1068-1085): set mepc, mcause = 11, mtval = 0, mstatus = 3 << 11, jump to mtvec. */
  private void expandEcall() {
    int addr = temp();
    emitU(AUIPC, addr, 0);
    emitI(ADDI, Isa.MEPC, addr, 0);
    free(1);
    emitI(ADDI, Isa.MCAUSE, 0, 11);
    emitI(ADDI, Isa.MTVAL, 0, 0);
    int three = temp();
    emitI(ADDI, three, 0, 3);
    emitI(VirtualMULI, Isa.MSTATUS, three, 1L << 11);
    free(1);
    int link = temp();
    emitI(JALR, link, Isa.MTVEC, 0);
    free(1);
  }

  /** CSRRW and CSRRS (virtual.cpp:1086-1127): moves between rd, rs1 and the CSR's register. */
  private void expandCsr(Instruction in) {
    int vr = Isa.csrToReg(in.c & 0xfff);
    int rd = in.a, rs1 = in.b;
    if (in.opcode == CSRRW) {
      if (rd == 0) {
        emitI(ADDI, vr, rs1, 0);
      } else if (rd == rs1) {
        int t = temp();
        emitI(ADDI, t, rs1, 0);
        emitI(ADDI, rd, vr, 0);
        emitI(ADDI, vr, t, 0);
        free(1);
      } else {
        emitI(ADDI, rd, vr, 0);
        emitI(ADDI, vr, rs1, 0);
      }
    } else {
      if (rs1 == 0) {
        emitI(ADDI, rd, vr, 0);
      } else if (rd == 0) {
        emitR(OR, vr, vr, rs1);
      } else if (rd == rs1) {
        int t = temp();
        emitI(ADDI, t, rs1, 0);
        emitI(ADDI, rd, vr, 0);
        emitR(OR, vr, vr, t);
        free(1);
      } else {
        emitI(ADDI, rd, vr, 0);
        emitR(OR, vr, vr, rs1);
      }
    }
  }
}
