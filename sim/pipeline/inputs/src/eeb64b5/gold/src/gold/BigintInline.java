package gold;

import static gold.Op.*;

/**
 * BIGINT256_MUL: the 512-bit product of two 256-bit numbers (4 limbs each, at rs1 and rs2), stored as 8 limbs at rd.
 * Follows upstream's builder (jolt-inlines/bigint/src/multiplication/sequence_builder.rs at f8fc214; jolt-cpp's
 * inlines/bigint_multiply.inc is its dump). Product scanning: column k sums the low halves of a[i]*b[j] with i + j = k
 * and the high halves with i + j = k - 1, carrying into the next column; two registers alternate as column and carry.
 */
final class BigintInline {
  private BigintInline() {}

  static void expand(Expander e, int rd, int rs1, int rs2) {
    InlineAsm asm = new InlineAsm(e, rd, rs1, rs2);
    int[] vr = asm.alloc(11);
    int[] a = {vr[0], vr[1], vr[2], vr[3]};
    int[] b = {vr[4], vr[5], vr[6], vr[7]};
    int[] s = {vr[8], vr[9]};                     // column k is s[k % 2]
    int t = vr[10];

    for (int i = 0; i < 4; i++) asm.ld(a[i], rs1, 8L * i);
    for (int i = 0; i < 4; i++) asm.ld(b[i], rs2, 8L * i);
    asm.r(MUL, s[0], a[0], b[0]);
    asm.sd(rd, s[0], 0);
    asm.r(MULHU, s[1], a[0], b[0]);

    for (int k = 1; k < 8; k++) {
      boolean overwriteCarry = true;               // the first carry into column k + 1 replaces its old contents
      for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
          if (i == 0 && j == 0) continue;          // a[0]*b[0] is done above
          if (i + j != k && i + j != k - 1) continue;
          asm.r(i + j == k ? MUL : MULHU, t, a[i], b[j]);
          asm.r(ADD, s[k % 2], s[k % 2], t);
          if (k + 1 < 8) {
            if (overwriteCarry) {
              asm.r(SLTU, s[(k + 1) % 2], s[k % 2], t);
            } else {
              asm.r(SLTU, t, s[k % 2], t);
              asm.r(ADD, s[(k + 1) % 2], t, s[(k + 1) % 2]);
            }
          }
          overwriteCarry = false;
        }
      }
      asm.sd(rd, s[k % 2], 8L * k);
    }
    asm.release(vr);
    asm.finish();
  }
}
