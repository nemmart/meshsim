package gold;

/**
 * The secp256k1 inlines' operations and shapes (jolt-cpp inlines/secp256k1_shapes.hpp, secp256k1_instruction.hpp at
 * 821e1bd). Stage 1 takes their advice words from jolt-cpp's rows; stage 2 computes them (BigInteger).
 */
public final class Secp {
  private Secp() {}

  /** cycles: rows in the expansion; adviceWords: VirtualAdvice rows it consumes. */
  public enum Operation {
    NONE(0, 0, false),
    MUL_Q(186, 4, true), SQUARE_Q(167, 4, false), DIV_Q(194, 8, true),
    MUL_R(232, 4, true), SQUARE_R(213, 4, false), DIV_R(240, 8, true),
    GLV_R(13, 6, false);

    public final int cycles, adviceWords;
    /** Takes a second operand (rs2) (secp256k1_instruction.hpp:21-24). */
    public final boolean hasRhs;

    Operation(int cycles, int adviceWords, boolean hasRhs) {
      this.cycles = cycles; this.adviceWords = adviceWords; this.hasRhs = hasRhs;
    }
  }

  /** secp256k1_instruction.hpp:8-19. */
  public static Operation forOpcode(int op) {
    switch (op) {
      case Op.SECP256K1_MULQ_INLINE: return Operation.MUL_Q;
      case Op.SECP256K1_SQUAREQ_INLINE: return Operation.SQUARE_Q;
      case Op.SECP256K1_DIVQ_INLINE: return Operation.DIV_Q;
      case Op.SECP256K1_MULR_INLINE: return Operation.MUL_R;
      case Op.SECP256K1_SQUARER_INLINE: return Operation.SQUARE_R;
      case Op.SECP256K1_DIVR_INLINE: return Operation.DIV_R;
      case Op.SECP256K1_GLVR_ADV_INLINE: return Operation.GLV_R;
      default: return Operation.NONE;
    }
  }
}
