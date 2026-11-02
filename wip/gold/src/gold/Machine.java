package gold;

import static gold.Op.*;

/**
 * Runs the bytecode one row at a time, as jolt-cpp's interpreter (trace_owner.cpp:104-650 at 821e1bd), and builds each
 * row's record as expand_compressed_chunk does (trace_owner.cpp:855-915).
 *
 * State: pc, the bytecode index, registers 0-127 and memory. The run ends when pc becomes 0.
 */
public final class Machine {
  /** Where secp256k1 advice comes from (stage 1: jolt-cpp's rows; stage 2: computed). */
  public interface SecpAdvice {
    /** Called at the first row of a secp inline (trace_owner.cpp:52-66, 787-791). */
    default void prepare(Machine m, VInstr first) {}
    long next();
  }

  public final long[] regs = new long[Isa.NUM_REGS];
  public long pc;
  public int index;
  public long cycle;
  public final Memory memory;
  public final Bytecode code;
  private final SecpAdvice secp;
  private int prevSeqRemaining;

  /** Executions per source opcode (counted at each expansion's first row) and per row opcode. */
  public final long[] sourceCount = new long[Op.NUM_OPCODES], opCount = new long[Op.NUM_OPCODES];

  /** The last row executed, as the ten words of the record (see Row). */
  public final long[] row = new long[Row.WORDS];

  public Machine(Program p, SecpAdvice secp) {
    this.memory = p.memory;
    this.code = p.bytecode;
    this.secp = secp;
    this.pc = p.layout.entryPoint;            // registers start at 0 (vm.cpp:56-63)
    this.index = code.indexOf(pc);
  }

  public boolean halted() { return pc == 0; }

  /** Rows run: the value read from a register operand, or the operand itself where it is an immediate (:78-83). */
  private long rdVal(VInstr v) { return v.rdImm ? v.rd : regs[(int) v.rd]; }
  private long rs1Val(VInstr v) { return v.rs1Imm ? v.rs1 : regs[(int) v.rs1]; }
  private long rs2Val(VInstr v) { return v.rs2Imm ? v.rs2 : regs[(int) v.rs2]; }

  /** Writes to x0 are dropped (:85-93). */
  private void setRd(VInstr v, long value) {
    if (v.rd != 0) regs[(int) v.rd] = value;
  }

  private static void check(boolean ok, VInstr v, String what) {
    if (!ok) throw new EmulationError(what + " failed at " + v);
  }

  /** Executes one row and fills {@link #row}. */
  public void step() {
    VInstr v = code.instrs[index];
    if (prevSeqRemaining == 0 && v.seqRemaining != 0 && v.adviceOp != Secp.Operation.NONE) secp.prepare(this, v);
    prevSeqRemaining = v.seqRemaining;
    if (!v.isVirtual || v.isFirstInSequence) {
      sourceCount[v.srcOpcode]++;
    }
    opCount[v.opcode]++;

    // the row, before execution (trace_owner.cpp:874-886)
    final int op = v.opcode;
    final long rs1Before = v.rs1Imm ? 0 : regs[(int) v.rs1];
    final long rs2Before = v.rs2Imm ? 0 : regs[(int) v.rs2];
    row[0] = pc;
    row[2] = v.immRaw();
    row[4] = rs1Before;
    row[5] = rs2Before;
    row[6] = v.rdImm ? 0 : regs[(int) v.rd];
    row[7] = row[8] = row[9] = 0;
    int rdReg = v.rdImm ? 0 : (int) v.rd, rs1Reg = v.rs1Imm ? 0 : (int) v.rs1, rs2Reg = v.rs2Imm ? 0 : (int) v.rs2;
    int rowOp = op;

    long newPc = pc;
    switch (op) {
      case LUI: setRd(v, rs1Val(v)); break;
      case AUIPC: setRd(v, pc + rs1Val(v)); break;
      case ADD: case ADDI: setRd(v, rs1Val(v) + rs2Val(v)); break;
      case ADDW: case ADDIW: setRd(v, (int) (rs1Val(v) + rs2Val(v))); break;
      case SUB: setRd(v, rs1Val(v) - rs2Val(v)); break;
      case SUBW: setRd(v, (int) (rs1Val(v) - rs2Val(v))); break;
      case AND: case ANDI: setRd(v, rs1Val(v) & rs2Val(v)); break;
      case XOR: case XORI: setRd(v, rs1Val(v) ^ rs2Val(v)); break;
      case OR: case ORI: setRd(v, rs1Val(v) | rs2Val(v)); break;
      case SLL: case SLLI: setRd(v, rs1Val(v) << (rs2Val(v) & 0x3f)); break;
      case SRL: case SRLI: setRd(v, rs1Val(v) >>> (rs2Val(v) & 0x3f)); break;
      case VirtualMULI: case MUL: setRd(v, rs1Val(v) * rs2Val(v)); break;
      case VirtualMULIW: case MULW: setRd(v, (int) (rs1Val(v) * rs2Val(v))); break;
      case MULHU: setRd(v, Math.unsignedMultiplyHigh(rs1Val(v), rs2Val(v))); break;
      case SLTU: case SLTIU: setRd(v, Long.compareUnsigned(rs1Val(v), rs2Val(v)) < 0 ? 1 : 0); break;
      case SLT: case SLTI: setRd(v, rs1Val(v) < rs2Val(v) ? 1 : 0); break;

      // branches: the immediate is in the rd field (:247-276)
      case BEQ: if (rs1Val(v) == rs2Val(v)) newPc = pc + v.rd; break;
      case BNE: if (rs1Val(v) != rs2Val(v)) newPc = pc + v.rd; break;
      case BGEU: if (Long.compareUnsigned(rs1Val(v), rs2Val(v)) >= 0) newPc = pc + v.rd; break;
      case BGE: if (rs1Val(v) >= rs2Val(v)) newPc = pc + v.rd; break;
      case BLT: if (rs1Val(v) < rs2Val(v)) newPc = pc + v.rd; break;
      case BLTU: if (Long.compareUnsigned(rs1Val(v), rs2Val(v)) < 0) newPc = pc + v.rd; break;

      case JAL: {                         // :277-283: an immediate of 0 ends the run
        long imm = rs1Val(v);
        setRd(v, pc + v.pcIncrement);
        newPc = imm == 0 ? 0 : pc + imm;
        break;
      }
      case JALR: {                        // :284-290
        long link = pc + v.pcIncrement;
        newPc = (rs1Val(v) + rs2Val(v)) & ~1L;
        setRd(v, link);
        break;
      }
      case ECALL: case VirtualHostIO: case FENCE: break;

      // memory: every access is a whole aligned word (:298-325, row fields :896-905)
      case VirtualStoreB: case VirtualStoreH: case VirtualStoreW: case SD: {
        long addr = Isa.wordAddress(op, rs1Val(v), v.rd);
        row[7] = addr;
        row[8] = memory.load(addr);
        row[9] = rs2Before;
        memory.store(addr, rs2Val(v));
        break;
      }
      case VirtualLoadB: case VirtualLoadH: case VirtualLoadW: case LD: {
        long addr = Isa.wordAddress(op, rs1Val(v), v.rs2);
        row[7] = addr;
        setRd(v, memory.load(addr));
        break;                            // pre and post = the row's rd, filled below
      }

      case VirtualSignExtendWord: setRd(v, (int) rs1Val(v)); break;
      case VirtualZeroExtendWord: setRd(v, rs1Val(v) & 0xffffffffL); break;
      case VirtualAssertMulUNoOverflow:
        check(Math.unsignedMultiplyHigh(rs1Val(v), rs2Val(v)) == 0, v, "VirtualAssertMulUNoOverflow");
        break;
      case VirtualPow2: setRd(v, 1L << (rs1Val(v) & 0x3f)); break;
      case VirtualPow2W: setRd(v, 1L << (rs1Val(v) & 0x1f)); break;
      // shifts by a bitmask's trailing zeros (:355-379); a zero mask shifts by 64, which x86 (and Java) take mod 64
      case VirtualSRAI: case VirtualSRA: setRd(v, rs1Val(v) >> Bits.trailingZeros(rs2Val(v))); break;
      case VirtualSRLI: case VirtualSRL: setRd(v, rs1Val(v) >>> Bits.trailingZeros(rs2Val(v))); break;
      case VirtualROTRI: setRd(v, Long.rotateRight(rs1Val(v), Bits.trailingZeros(rs2Val(v)))); break;
      case VirtualROTRIW: {
        int shift = Math.min(32, Bits.trailingZeros(rs2Val(v)));
        setRd(v, Integer.rotateRight((int) rs1Val(v), shift) & 0xffffffffL);
        break;
      }
      case VirtualRev8W: {
        long x = rs1Val(v);
        long lo = Integer.reverseBytes((int) x) & 0xffffffffL, hi = Integer.reverseBytes((int) (x >>> 32)) & 0xffffffffL;
        setRd(v, lo + (hi << 32));
        break;
      }
      case VirtualXORROT63: setRd(v, Long.rotateRight(rs1Val(v) ^ rs2Val(v), 63)); break;
      case VirtualXORROT32: setRd(v, Long.rotateRight(rs1Val(v) ^ rs2Val(v), 32)); break;
      case VirtualXORROT24: setRd(v, Long.rotateRight(rs1Val(v) ^ rs2Val(v), 24)); break;
      case VirtualXORROT16: setRd(v, Long.rotateRight(rs1Val(v) ^ rs2Val(v), 16)); break;
      case VirtualXORROTW16: setRd(v, Bits.xorRotW(rs1Val(v), rs2Val(v), 16)); break;
      case VirtualXORROTW12: setRd(v, Bits.xorRotW(rs1Val(v), rs2Val(v), 12)); break;
      case VirtualXORROTW8: setRd(v, Bits.xorRotW(rs1Val(v), rs2Val(v), 8)); break;
      case VirtualXORROTW7: setRd(v, Bits.xorRotW(rs1Val(v), rs2Val(v), 7)); break;
      case VirtualXORROTW22: setRd(v, Bits.xorRotW(rs1Val(v), rs2Val(v), 22)); break;
      case VirtualXORROTW19: setRd(v, Bits.xorRotW(rs1Val(v), rs2Val(v), 19)); break;
      case VirtualXORROTW6: setRd(v, Bits.xorRotW(rs1Val(v), rs2Val(v), 6)); break;
      case ANDN: setRd(v, rs1Val(v) & ~rs2Val(v)); break;
      case VirtualShiftRightBitmask: setRd(v, Bits.maskTrailingZeros(rs1Val(v) & 0x3f)); break;
      case VirtualShiftRightBitmaskW: setRd(v, (1L << 32) - (1L << (rs1Val(v) & 0x1f))); break;
      // lane masks and shifted data for narrow accesses (:448-485)
      case VirtualWindowMaskW: setRd(v, 0xffffffffL << (32 * (((rs1Val(v) + rs2Val(v)) >>> 2) & 1))); break;
      case VirtualWindowMaskB: setRd(v, 0xffL << (8 * ((rs1Val(v) + rs2Val(v)) & 7))); break;
      case VirtualWindowMaskH: setRd(v, 0xffffL << (8 * ((rs1Val(v) + rs2Val(v)) & 6))); break;
      case VirtualShiftDataB: setRd(v, (rs1Val(v) & 0xff) << (8 * ((rs2Val(v) + v.shiftOffset) & 7))); break;
      case VirtualShiftDataH: setRd(v, (rs1Val(v) & 0xffff) << (8 * ((rs2Val(v) + v.shiftOffset) & 6))); break;
      case VirtualShiftDataW: setRd(v, (rs1Val(v) & 0xffffffffL) << (8 * ((rs2Val(v) + v.shiftOffset) & 4))); break;
      case VirtualXORROTL1: setRd(v, rs1Val(v) ^ Long.rotateLeft(rs2Val(v), 1)); break;
      case VirtualPext: setRd(v, Bits.pext(rs1Val(v), rs2Val(v))); break;
      case VirtualPextSigned: setRd(v, Bits.pextSigned(rs1Val(v), rs2Val(v))); break;
      case VirtualSRLW: case VirtualSRLIW: setRd(v, Bits.virtualSrlw(rs1Val(v), rs2Val(v))); break;
      case VirtualSRAW: case VirtualSRAIW: setRd(v, Bits.virtualSraw(rs1Val(v), rs2Val(v))); break;
      case VirtualNegateIf: setRd(v, rs1Val(v) < 0 ? -rs2Val(v) : rs2Val(v)); break;
      case VirtualMovsign: setRd(v, rs1Val(v) < 0 ? -1L : 0); break;
      case VirtualMove: setRd(v, rs1Val(v)); break;

      // advice (:527-618): secp words from the advice source; the division family computes its own
      case VirtualAdvice: setRd(v, secp.next()); break;
      case VirtualAdviceDivU: case VirtualAdviceRemU: {     // RemU returns the quotient too
        long a = rs1Val(v), b = rs2Val(v);
        setRd(v, b == 0 ? (op == VirtualAdviceDivU ? -1L : 0) : Long.divideUnsigned(a, b));
        break;
      }
      case VirtualAdviceDiv: {
        long a = rs1Val(v), b = rs2Val(v);
        setRd(v, b == 0 ? -1L : (a == Long.MIN_VALUE && b == -1) ? a : a / b);
        break;
      }
      case VirtualAdviceRem: {                              // |quotient|
        long a = rs1Val(v), b = rs2Val(v);
        setRd(v, b == 0 ? 0 : (a == Long.MIN_VALUE && b == -1) ? Bits.unsignedAbs(a) : Bits.unsignedAbs(a / b));
        break;
      }
      case VirtualAdviceDivW: {
        int a = (int) rs1Val(v), b = (int) rs2Val(v);
        setRd(v, b == 0 ? -1L : (a == Integer.MIN_VALUE && b == -1) ? 0x80000000L : (long) (a / b));
        break;
      }
      case VirtualAdviceRemW: {
        int a = (int) rs1Val(v), b = (int) rs2Val(v);
        setRd(v, b == 0 ? 0 : (a == Integer.MIN_VALUE && b == -1) ? Bits.unsignedAbs32(a) : Bits.unsignedAbs32(a / b));
        break;
      }
      case VirtualAdviceDivUW: case VirtualAdviceRemUW: {
        int a = (int) rs1Val(v), b = (int) rs2Val(v);
        setRd(v, b == 0 ? (op == VirtualAdviceDivUW ? 0xffffffffL : 0) : Integer.divideUnsigned(a, b) & 0xffffffffL);
        break;
      }

      case VirtualAssertEQ: check(rs1Val(v) == rs2Val(v), v, "VirtualAssertEQ"); break;
      case VirtualAssertLTE: check(Long.compareUnsigned(rs1Val(v), rs2Val(v)) <= 0, v, "VirtualAssertLTE"); break;
      case VirtualAssertValidDiv0: check(rs1Val(v) != 0 || rs2Val(v) == -1L, v, "VirtualAssertValidDiv0"); break;
      case VirtualAssertValidUnsignedRemainder:
        check(rs2Val(v) == 0 || Long.compareUnsigned(rs1Val(v), rs2Val(v)) < 0, v, "VirtualAssertValidUnsignedRemainder");
        break;
      case UNIMP: throw new EmulationError("guest executed UNIMP at " + v);
      default: throw new EmulationError("unimplemented instruction " + v);
    }

    // the row, after execution (:888-905)
    long rdAfter = v.rdImm ? 0 : regs[(int) v.rd];
    row[3] = rdAfter;
    if (Isa.isVirtualAdvice(op)) {        // every advice row is traced as VirtualAdvice, without its operands
      rowOp = VirtualAdvice;
      rs1Reg = rs2Reg = 0;
      row[4] = row[5] = 0;
      row[2] = 0;
    } else if (Isa.isLoad(op)) {
      row[8] = row[9] = rdAfter;
    }
    row[1] = (rowOp & 0xffL) | (long) rdReg << 8 | (long) rs1Reg << 16 | (long) rs2Reg << 24
        | (long) (v.seqRemaining & 0xffff) << 32 | (v.pcIncrement == 2 ? 1L : 0) << 48
        | (long) ((v.isVirtual ? 1 : 0) | (v.isFirstInSequence ? 2 : 0)) << 56;

    finish(v, newPc);
  }

  /** finish_instruction (:111-126): a new pc equal to the current one advances, as if there were no jump. */
  private void finish(VInstr v, long newPc) {
    cycle++;
    if (newPc == 0) {
      pc = 0;
      index = 0;
    } else if (newPc != pc) {
      pc = newPc;
      index = code.indexOf(newPc);
      if (index == 0) throw new EmulationError(String.format("jump to %#x, where no instruction starts", newPc));
    } else {
      pc += v.pcIncrement;
      index++;
    }
  }
}
