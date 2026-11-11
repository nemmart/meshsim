package gold;

import static gold.Op.*;

/**
 * BLAKE2b's compression function (RFC 7693, section 3.2) as Jolt traces it, following upstream's builder
 * (jolt-inlines/blake2/src/sequence_builder.rs at f8fc214; jolt-cpp's inlines/blake2b.inc is its dump).
 *
 * rs1 points at the 8-word hash state, updated in place; rs2 at the 16-word message block followed by the counter t
 * and the final-block flag (0 or 1). Registers 48-63 hold the working vector v, 64-79 the message, 80-87 the state.
 */
final class Blake2Inline {
  private Blake2Inline() {}

  static final long[] IV = {
      0x6a09e667f3bcc908L, 0xbb67ae8584caa73bL, 0x3c6ef372fe94f82bL, 0xa54ff53a5f1d36f1L,
      0x510e527fade682d1L, 0x9b05688c2b3e6c1fL, 0x1f83d9abfb41bd6bL, 0x5be0cd19137e2179L};

  static final int[][] SIGMA = {
      {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
      {14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3},
      {11, 8, 12, 0, 5, 2, 15, 13, 10, 14, 3, 6, 7, 1, 9, 4},
      {7, 9, 3, 1, 13, 12, 11, 14, 2, 6, 5, 10, 4, 0, 15, 8},
      {9, 0, 5, 7, 2, 4, 10, 15, 14, 1, 11, 12, 6, 8, 3, 13},
      {2, 12, 6, 10, 0, 11, 8, 3, 4, 13, 7, 5, 15, 14, 1, 9},
      {12, 5, 1, 15, 14, 13, 4, 10, 0, 7, 6, 3, 9, 2, 8, 11},
      {13, 11, 7, 14, 12, 1, 3, 9, 5, 0, 15, 4, 8, 6, 2, 10},
      {6, 15, 14, 9, 11, 3, 0, 8, 12, 2, 13, 7, 1, 4, 10, 5},
      {10, 2, 8, 4, 7, 6, 1, 5, 15, 11, 9, 14, 3, 12, 13, 0},
      {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
      {14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3}};

  static void expand(Expander e, int rd, int rs1, int rs2) {
    InlineAsm asm = new InlineAsm(e, rd, rs1, rs2);
    int[] regs = asm.alloc(40);
    int[] v = java.util.Arrays.copyOfRange(regs, 0, 16);
    int[] m = java.util.Arrays.copyOfRange(regs, 16, 32);
    int[] h = java.util.Arrays.copyOfRange(regs, 32, 40);

    for (int i = 0; i < 8; i++) asm.ld(h[i], rs1, 8L * i);           // the hash state
    for (int i = 0; i < 16; i++) asm.ld(m[i], rs2, 8L * i);          // the message block
    asm.ld(v[12], rs2, 16 * 8);                                        // the counter t
    asm.ld(v[14], rs2, 17 * 8);                                        // the final-block flag

    // v[0..8] = h; v[8..16] = IV, with v[12] ^= t and v[14] inverted on the final block
    for (int i = 0; i < 8; i++) asm.i(XORI, v[i], h[i], 0);
    for (int i : new int[] {0, 1, 2, 3, 5, 7}) asm.lui(v[8 + i], IV[i]);
    asm.i(XORI, v[12], v[12], IV[4]);
    asm.r(SUB, v[14], 0, v[14]);                                       // 0 or all ones
    asm.i(XORI, v[14], v[14], IV[6]);

    for (int round = 0; round < 12; round++) {
      int[] s = SIGMA[round];
      g(asm, v, m, 0, 4, 8, 12, s[0], s[1]);
      g(asm, v, m, 1, 5, 9, 13, s[2], s[3]);
      g(asm, v, m, 2, 6, 10, 14, s[4], s[5]);
      g(asm, v, m, 3, 7, 11, 15, s[6], s[7]);
      g(asm, v, m, 0, 5, 10, 15, s[8], s[9]);
      g(asm, v, m, 1, 6, 11, 12, s[10], s[11]);
      g(asm, v, m, 2, 7, 8, 13, s[12], s[13]);
      g(asm, v, m, 3, 4, 9, 14, s[14], s[15]);
    }

    for (int i = 0; i < 8; i++) {                                      // h ^= v[i] ^ v[i + 8]
      asm.r(XOR, v[i], v[i], v[8 + i]);
      asm.r(XOR, h[i], h[i], v[i]);
    }
    for (int i = 0; i < 8; i++) asm.sd(rs1, h[i], 8L * i);
    asm.release(regs);
    asm.finish();
  }

  /** The mixing function G (rotations right by 32, 24, 16, 63, each fused with its XOR). */
  static void g(InlineAsm asm, int[] v, int[] m, int a, int b, int c, int d, int x, int y) {
    asm.r(ADD, v[a], v[a], v[b]);
    asm.r(ADD, v[a], v[a], m[x]);
    asm.r(VirtualXORROT32, v[d], v[d], v[a]);
    asm.r(ADD, v[c], v[c], v[d]);
    asm.r(VirtualXORROT24, v[b], v[b], v[c]);
    asm.r(ADD, v[a], v[a], v[b]);
    asm.r(ADD, v[a], v[a], m[y]);
    asm.r(VirtualXORROT16, v[d], v[d], v[a]);
    asm.r(ADD, v[c], v[c], v[d]);
    asm.r(VirtualXORROT63, v[b], v[b], v[c]);
  }
}
