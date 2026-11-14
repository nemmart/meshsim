package gold;

/**
 * Jolt's inlines (virtual.cpp:988-1062; upstream jolt-inlines/* sequence builders at f8fc214, of which jolt-cpp's
 * inlines/*.inc are dumps), written as readable loops. Each is checked row for row against the .inc emit lists, read
 * as data, by InlineEmitTest on every build.
 */
final class Inlines {
  private Inlines() {}

  static void expand(Expander e, int op, int rd, int rs1, int rs2) {
    switch (op) {
      case Op.SECP256K1_MULQ_INLINE: case Op.SECP256K1_SQUAREQ_INLINE: case Op.SECP256K1_DIVQ_INLINE:
      case Op.SECP256K1_MULR_INLINE: case Op.SECP256K1_SQUARER_INLINE: case Op.SECP256K1_DIVR_INLINE:
      case Op.SECP256K1_GLVR_ADV_INLINE:
        Secp256k1Inline.expand(e, op, rd, rs1, rs2); return;
      case Op.BLAKE2_INLINE: Blake2Inline.expand(e, rd, rs1, rs2); return;
      case Op.BLAKE3_INLINE: Blake3Inline.expand(e, false, rd, rs1, rs2); return;
      case Op.BLAKE3_KEYED64_INLINE: Blake3Inline.expand(e, true, rd, rs1, rs2); return;
      case Op.BIGINT256_MUL_INLINE: BigintInline.expand(e, rd, rs1, rs2); return;
      case Op.SHA256_INLINE: Sha256Inline.expand(e, false, rd, rs1, rs2); return;
      case Op.SHA256_INIT_INLINE: Sha256Inline.expand(e, true, rd, rs1, rs2); return;
      case Op.KECCAK256: KeccakInline.expand(e, false, rd, rs1, rs2); return;
      case Op.KECCAK256_ABSORB_PERMUTE: KeccakInline.expand(e, true, rd, rs1, rs2); return;
      default: throw new IllegalArgumentException("not an inline: " + Op.name(op));
    }
  }
}
