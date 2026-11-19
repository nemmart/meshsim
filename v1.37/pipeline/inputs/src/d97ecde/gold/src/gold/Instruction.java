package gold;

/**
 * A decoded RISC-V instruction, as jolt-cpp's isa::Instruction (isa/decoder.hpp:10-36): an opcode and three operands,
 * each a register number or, where its flag is set, a 32-bit immediate. Formats (decoder.cpp): R rd, rs1, rs2;
 * I rd, rs1, imm; S and B imm, rs1, rs2; LUI, AUIPC and JAL rd, imm, 0.
 */
public final class Instruction {
  public final int opcode;
  public final int a, b, c;
  public final boolean immA, immB, immC;

  public Instruction(int opcode, int a, int b, int c, boolean immA, boolean immB, boolean immC) {
    this.opcode = opcode; this.a = a; this.b = b; this.c = c; this.immA = immA; this.immB = immB; this.immC = immC;
  }

  /** The default-constructed instruction: invalid, all operands registers 0. */
  public static final Instruction INVALID = new Instruction(Op.INVALID, 0, 0, 0, false, false, false);

  /** ECALL, EBREAK, MRET, FENCE, UNIMP, VirtualHostIO: all three operands immediate 0. */
  static Instruction noOperands(int opcode) { return new Instruction(opcode, 0, 0, 0, true, true, true); }

  public Instruction withA(int newA) { return new Instruction(opcode, newA, b, c, immA, immB, immC); }

  public boolean isValid() { return opcode < Op.NUM_OPCODES; }

  @Override public String toString() {
    return "<" + Op.name(opcode) + " " + (immA ? "" + a : "x" + a) + " " + (immB ? "" + b : "x" + b) + " "
        + (immC ? "" + c : "x" + c) + ">";
  }
}
