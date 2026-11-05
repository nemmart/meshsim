package gold;

/**
 * One instruction of the expanded ("virtual") bytecode, as jolt-cpp's VirtualInstruction (isa/virtual.hpp:83-133):
 * an opcode, three operands each a register number or (where its flag is set) a 64-bit immediate, and the expansion's
 * bookkeeping. Each trace row executes one of these.
 */
public final class VInstr {
  /** The source RISC-V instruction's address (every row of an expansion carries it). */
  public long riscPc;
  /** The source RISC-V instruction's opcode (after the host I/O encoding), for coverage reports. */
  public int srcOpcode;
  public int opcode;
  public long rd, rs1, rs2;
  public boolean rdImm, rs1Imm, rs2Imm;
  /** Shift-data rows: the offset added to the base register (virtual.cpp:234-237; riscv.hpp's imm for these rows). */
  public int shiftOffset;
  /** Part of a virtual sequence: the expansion has more than one row, or its opcode differs from the source's. */
  public boolean isVirtual;
  public boolean isFirstInSequence;
  /** Rows left in the expansion after this one (0 on the last). */
  public int seqRemaining;
  /** On an expansion's last row, the source instruction's size (2 or 4); 0 otherwise. */
  public int pcIncrement;
  /** secp256k1 inlines, first row only: the operation and its input registers (virtual.cpp:1194-1201). */
  public Secp.Operation adviceOp = Secp.Operation.NONE;
  public int adviceRs1, adviceRs2;
  /** Not provable: executing this row throws (the standard UNIMP 0xC0001073, loaded as an unreachable marker; owner's
   *  D4 ruling, DECISIONS-v1.3 section 17; or an unprovable instruction the ACT4 runner decoded at run time). */
  public String unprovable;
  /** An LR.D x0 or LR.W x0 expansion: the word it loads must be 0 (DECISIONS-v1.3 section 17; WORKER17). */
  public boolean lrToX0;

  /** The row's immediate: the sum of the immediate fields, the shift offset for shift-data, 0 for advice
   *  (trace_owner.hpp:168-177). */
  public long immRaw() {
    if (Isa.isVirtualAdvice(opcode)) return 0;
    if (opcode == Op.VirtualShiftDataB || opcode == Op.VirtualShiftDataH || opcode == Op.VirtualShiftDataW) return shiftOffset;
    return (rdImm ? rd : 0) + (rs1Imm ? rs1 : 0) + (rs2Imm ? rs2 : 0);
  }

  @Override public String toString() {
    return String.format("%s %s %s %s%s pc=%#x rem=%d%s%s", Op.name(opcode), rdImm ? "" + rd : "x" + rd,
        rs1Imm ? "" + rs1 : "x" + rs1, rs2Imm ? "" + rs2 : "x" + rs2,
        shiftOffset != 0 ? " off=" + shiftOffset : "", riscPc, seqRemaining, isVirtual ? " virt" : "",
        isFirstInSequence ? " first" : "");
  }
}
