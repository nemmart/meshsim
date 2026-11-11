package gold;

import static gold.Op.*;

import java.util.Arrays;

/**
 * BLAKE3's compression function as Jolt traces it, following upstream's builder
 * (jolt-inlines/blake3/src/sequence_builder.rs at f8fc214; jolt-cpp's inlines/blake3.inc and blake3_keyed64.inc are its
 * dumps). Words are 32-bit, kept two per 64-bit load ("dirty": a pair's low word keeps the high word in its upper
 * bits, which the 32-bit operations ignore). Registers 48-63 hold the state v, 64-79 the message block.
 *
 * BLAKE3_INLINE: rs1 the 8-word chaining value (updated in place), rs2 the 16-word block followed by the counter
 * (2 words), the block length and the flags. BLAKE3_KEYED64_INLINE: rd the key (overwritten with the result), rs1 and rs2
 * the block's two halves; counter 0, block length 64, flags CHUNK_START | CHUNK_END | ROOT | KEYED_HASH.
 */
final class Blake3Inline {
  private Blake3Inline() {}

  static final long[] IV = {0x6a09e667L, 0xbb67ae85L, 0x3c6ef372L, 0xa54ff53aL};

  static final int[][] MSG_SCHEDULE = {
      {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
      {2, 6, 3, 10, 7, 0, 4, 13, 1, 11, 12, 5, 9, 14, 15, 8},
      {3, 4, 10, 12, 13, 2, 7, 14, 6, 5, 9, 0, 11, 15, 8, 1},
      {10, 7, 12, 9, 14, 3, 13, 15, 4, 0, 11, 2, 5, 8, 1, 6},
      {12, 13, 9, 11, 15, 10, 14, 8, 7, 2, 5, 3, 0, 1, 6, 4},
      {9, 14, 11, 5, 8, 12, 15, 1, 13, 3, 0, 10, 2, 6, 4, 7},
      {11, 15, 5, 0, 1, 9, 8, 6, 14, 10, 2, 12, 3, 4, 7, 13}};

  static final long FLAGS_KEYED64 = 1 | 2 | 8 | 16;   // CHUNK_START | CHUNK_END | ROOT | KEYED_HASH

  static void expand(Expander e, boolean keyed64, int rd, int rs1, int rs2) {
    InlineAsm asm = new InlineAsm(e, rd, rs1, rs2);
    int[] regs = asm.alloc(32);
    int[] v = Arrays.copyOfRange(regs, 0, 16);
    int[] m = Arrays.copyOfRange(regs, 16, 32);
    int out;
    if (!keyed64) {                                    // build_general (:37-53)
      out = rs1;
      loadPairs(asm, rs1, 0, v, 0, 8);                 // chaining value
      loadPairs(asm, rs2, 0, m, 0, 16);                // block
      loadPairs(asm, rs2, 64, v, 12, 4);               // counter, block length, flags
      for (int i = 0; i < 4; i++) asm.lui(v[8 + i], IV[i]);
    } else {                                           // build_keyed64 (:55-84)
      out = rd;
      loadPairs(asm, rd, 0, v, 0, 8);                  // key
      loadPairs(asm, rs1, 0, m, 0, 8);
      loadPairs(asm, rs2, 0, m, 8, 8);
      for (int i = 0; i < 4; i++) asm.lui(v[8 + i], IV[i]);
      asm.lui(v[14], 64);
      asm.lui(v[15], FLAGS_KEYED64);                   // v[12], v[13] (the counter) stay 0
    }

    for (int[] s : MSG_SCHEDULE) {
      g(asm, v, m, 0, 4, 8, 12, s[0], s[1]);
      g(asm, v, m, 1, 5, 9, 13, s[2], s[3]);
      g(asm, v, m, 2, 6, 10, 14, s[4], s[5]);
      g(asm, v, m, 3, 7, 11, 15, s[6], s[7]);
      g(asm, v, m, 0, 5, 10, 15, s[8], s[9]);
      g(asm, v, m, 1, 6, 11, 12, s[10], s[11]);
      g(asm, v, m, 2, 7, 8, 13, s[12], s[13]);
      g(asm, v, m, 3, 4, 9, 14, s[14], s[15]);
    }
    for (int i = 0; i < 8; i++) asm.r(XOR, v[i], v[i], v[8 + i]);
    for (int i = 0; i < 4; i++) {                      // store_paired_u32 (sdk host.rs:387-394)
      int lo = v[2 * i], hi = v[2 * i + 1];
      asm.i(VirtualZeroExtendWord, lo, lo, 0);
      asm.i(VirtualMULI, hi, hi, 1L << 32);            // slli hi, hi, 32
      asm.r(OR, lo, hi, lo);
      asm.sd(out, lo, 8L * i);
    }
    asm.release(regs);
    asm.finish();
  }

  /** load_paired_u32_range_dirty (sdk host.rs:358-386): per pair, LD lo; SRLI hi, lo, 32. */
  static void loadPairs(InlineAsm asm, int base, long offset, int[] regs, int from, int count) {
    for (int i = 0; i < count / 2; i++) {
      int lo = regs[from + 2 * i], hi = regs[from + 2 * i + 1];
      asm.ld(lo, base, offset + 8L * i);
      asm.i(VirtualSRLI, hi, lo, Bits.maskTrailingZeros(32));
    }
  }

  /** The mixing function G on 32-bit words (rotations right by 16, 12, 8, 7, each fused with its XOR). */
  static void g(InlineAsm asm, int[] v, int[] m, int a, int b, int c, int d, int x, int y) {
    asm.r(ADD, v[a], v[a], v[b]);
    asm.r(ADD, v[a], v[a], m[x]);
    asm.r(VirtualXORROTW16, v[d], v[d], v[a]);
    asm.r(ADD, v[c], v[c], v[d]);
    asm.r(VirtualXORROTW12, v[b], v[b], v[c]);
    asm.r(ADD, v[a], v[a], v[b]);
    asm.r(ADD, v[a], v[a], m[y]);
    asm.r(VirtualXORROTW8, v[d], v[d], v[a]);
    asm.r(ADD, v[c], v[c], v[d]);
    asm.r(VirtualXORROTW7, v[b], v[b], v[c]);
  }
}
