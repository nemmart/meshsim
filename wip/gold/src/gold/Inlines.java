package gold;

/**
 * Jolt's inlines (virtual.cpp:988-1062, the inlines/*.inc sequences), written as readable loops. Each is checked row
 * for row against jolt-cpp's .inc emit lists, read as data, by InlineEmitTest on every build.
 *
 * Not yet written: an inline emits its jolt-cpp length of rows that refuse to execute, so the bytecode's shape
 * (and every jump target) is already right.
 */
final class Inlines {
  private Inlines() {}

  /** An inline whose expansion isn't written yet: executing it is an error. */
  static final int NOT_WRITTEN = -2;

  static void expand(Expander e, int op, int rd, int rs1, int rs2) {
    if (Secp.forOpcode(op) != Secp.Operation.NONE) {
      Secp256k1Inline.expand(e, op, rd, rs1, rs2);
      return;
    }
    switch (op) {
      case Op.BLAKE2_INLINE: Blake2Inline.expand(e, rd, rs1, rs2); return;
      case Op.BLAKE3_INLINE: Blake3Inline.expand(e, false, rd, rs1, rs2); return;
      case Op.BLAKE3_KEYED64_INLINE: Blake3Inline.expand(e, true, rd, rs1, rs2); return;
      case Op.BIGINT256_MUL_INLINE: BigintInline.expand(e, rd, rs1, rs2); return;
      case Op.SHA256_INLINE: Sha256Inline.expand(e, false, rd, rs1, rs2); return;
      case Op.SHA256_INIT_INLINE: Sha256Inline.expand(e, true, rd, rs1, rs2); return;
      default: break;
    }
    int n = Expander.numCycles(op);
    for (int i = 0; i < n; i++) e.emit(NOT_WRITTEN, 0, 0, 0, true, true, true);
  }
}
