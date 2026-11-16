package gold;

import static gold.Op.*;

import gold.InlineAsm.Value;

/**
 * SHA-256's compression function as Jolt traces it, following upstream's builder
 * (jolt-inlines/sha2/src/sequence_builder.rs at f8fc214; jolt-cpp's inlines/sha256.inc and sha256_init.inc are its
 * dumps). rs1 points at the 8-word state (updated in place), rs2 at the 16 big-endian message words.
 *
 * The working variables A..H aren't moved between rounds: round r reads variable X from register
 * state[(X - r) mod 8], so each round writes only the new A and E. The message schedule lives in 16 registers,
 * updated in place from round 16 on. SHA256_INIT_INLINE starts from the standard IV, known at expansion time, so the
 * first rounds work partly on constants and fold them (InlineAsm.Value); SHA256_INLINE loads the IV from rs1.
 */
final class Sha256Inline {
  static final long[] BLOCK = {
      0x6a09e667L, 0xbb67ae85L, 0x3c6ef372L, 0xa54ff53aL, 0x510e527fL, 0x9b05688cL, 0x1f83d9abL, 0x5be0cd19L};

  static final long[] K = {
      0x428a2f98L, 0x71374491L, 0xb5c0fbcfL, 0xe9b5dba5L, 0x3956c25bL, 0x59f111f1L, 0x923f82a4L, 0xab1c5ed5L,
      0xd807aa98L, 0x12835b01L, 0x243185beL, 0x550c7dc3L, 0x72be5d74L, 0x80deb1feL, 0x9bdc06a7L, 0xc19bf174L,
      0xe49b69c1L, 0xefbe4786L, 0x0fc19dc6L, 0x240ca1ccL, 0x2de92c6fL, 0x4a7484aaL, 0x5cb0a9dcL, 0x76f988daL,
      0x983e5152L, 0xa831c66dL, 0xb00327c8L, 0xbf597fc7L, 0xc6e00bf3L, 0xd5a79147L, 0x06ca6351L, 0x14292967L,
      0x27b70a85L, 0x2e1b2138L, 0x4d2c6dfcL, 0x53380d13L, 0x650a7354L, 0x766a0abbL, 0x81c2c92eL, 0x92722c85L,
      0xa2bfe8a1L, 0xa81a664bL, 0xc24b8b70L, 0xc76c51a3L, 0xd192e819L, 0xd6990624L, 0xf40e3585L, 0x106aa070L,
      0x19a4c116L, 0x1e376c08L, 0x2748774cL, 0x34b0bcb5L, 0x391c0cb3L, 0x4ed8aa4aL, 0x5b9cca4fL, 0x682e6ff3L,
      0x748f82eeL, 0x78a5636fL, 0x84c87814L, 0x8cc70208L, 0x90befffaL, 0xa4506cebL, 0xbef9a3f7L, 0xc67178f2L};

  private final InlineAsm asm;
  private final boolean initial;
  private final int[] state, message, iv;
  private int round;

  private Sha256Inline(InlineAsm asm, boolean initial) {
    this.asm = asm;
    this.initial = initial;
    state = asm.alloc(8);
    message = asm.alloc(16);
    iv = initial ? null : asm.alloc(8);
  }

  static void expand(Expander e, boolean initial, int rd, int rs1, int rs2) {
    InlineAsm asm = new InlineAsm(e, rd, rs1, rs2);
    new Sha256Inline(asm, initial).build();
    asm.finish();
  }

  void build() {
    if (!initial) {                                   // the state, two words per load (dirty pairs)
      for (int i = 0; i < 4; i++) {
        asm.ld(iv[2 * i], asm.rs1, 8L * i);
        asm.i(VirtualSRLI, iv[2 * i + 1], iv[2 * i], Bits.maskTrailingZeros(32));
      }
    }
    for (int i = 0; i < 8; i++) {                     // the message words, byte-swapped to big-endian
      int lo = message[2 * i], hi = message[2 * i + 1];
      asm.ld(lo, asm.rs2, 8L * i);
      asm.i(VirtualRev8W, lo, lo, 0);
      asm.i(VirtualSRLI, hi, lo, Bits.maskTrailingZeros(32));
    }
    for (int r = 0; r < 64; r++) round();

    for (int x = 0; x < 8; x++) {                     // add the input state (final_add_iv)
      char c = (char) ('A' + x);
      asm.add(vri(c), initial ? Value.imm(BLOCK[x]) : Value.reg(iv[x]), vr(c));
    }
    char[][] outs = {{'A', 'B'}, {'C', 'D'}, {'E', 'F'}, {'G', 'H'}};
    for (int i = 0; i < 4; i++) {                     // store_paired_u32
      int lo = vr(outs[i][0]), hi = vr(outs[i][1]);
      asm.i(VirtualZeroExtendWord, lo, lo, 0);
      asm.i(VirtualMULI, hi, hi, 1L << 32);
      asm.r(OR, lo, hi, lo);
      asm.sd(asm.rs1, lo, 8L * i);
    }
    asm.release(state);
    asm.release(message);
    if (iv != null) asm.release(iv);
  }

  /** Whether variable c is still an input in this round (not yet produced by a round). */
  private boolean stillInput(char c) {
    return round == 0
        || (round == 1 && c != 'A' && c != 'E')
        || (round == 2 && "ABEF".indexOf(c) < 0)
        || (round == 3 && "ABCEFG".indexOf(c) < 0);
  }

  /** Variable c as a value: a constant from the IV while it is still an input in SHA256_INIT, else its register. */
  private Value vri(char c) {
    if (initial && stillInput(c)) return Value.imm(BLOCK[Math.floorMod(c - 'A' - round, 8)]);
    return Value.reg(vr(c));
  }

  /** Variable c's register: the loaded IV while still an input in SHA256_INLINE, else the rotating state. */
  private int vr(char c) {
    if (!initial && stillInput(c)) return iv[Math.floorMod(c - 'A' - round, 8)];
    return state[Math.floorMod(c - 'A' - round, 8)];
  }

  private int w(int shift) { return message[Math.floorMod(round + shift, 16)]; }

  private void round() {
    int t1 = asm.alloc(), t2 = asm.alloc(), ss = asm.alloc(), ss2 = asm.alloc();
    // T1 = H + K[r] + Sigma1(E) + Ch(E, F, G) + W[r]
    Value v = asm.add(Value.imm(K[round]), vri('H'), t1);
    v = asm.add(v, sigma1(vri('E'), ss, ss2), t1);
    v = asm.add(v, ch(vri('E'), vri('F'), vri('G'), ss, ss2), t1);
    updateW(ss, ss2);
    Value tt1 = asm.add(v, Value.reg(w(0)), t1);
    // T2 = Sigma0(A) + Maj(A, B, C)
    Value s0 = sigma0(vri('A'), t2, ss);
    Value tt2 = asm.add(s0, maj(vri('A'), vri('B'), vri('C'), ss, ss2), t2);
    Value oldD = vri('D');
    round++;
    asm.add(tt1, tt2, vr('A'));                       // the new A and E, in the next round's registers
    asm.add(tt1, oldD, vr('E'));
    asm.release(t1, t2, ss, ss2);
  }

  /** W[r] = W[r-16] + sigma0(W[r-15]) + W[r-7] + sigma1(W[r-2]), in W[r-16]'s register (rounds 16 on). */
  private void updateW(int ss, int ss2) {
    if (round < 16) return;
    wordSigma(w(-15), 11, VirtualXORROTW7, 3, ss, ss2);   // sigma0 = rotr 7 ^ rotr 18 ^ shr 3
    asm.add(Value.reg(w(-16)), Value.reg(ss), w(-16));
    asm.add(Value.reg(w(-7)), Value.reg(w(-16)), w(-16));
    wordSigma(w(-2), 30, VirtualXORROTW19, 10, ss, ss2);  // sigma1 = rotr 17 ^ rotr 19 ^ shr 10
    asm.add(Value.reg(w(-16)), Value.reg(ss), w(-16));
  }

  /** Into rd: rotr(x, rot1) xored and rotated again (fused), then xored with x >> shr. */
  private void wordSigma(int x, int rot1, int xorRot, int shr, int rd, int ss) {
    asm.rotri32(Value.reg(x), rot1, ss);
    asm.r(xorRot, rd, x, ss);
    asm.srli32(Value.reg(x), shr, ss);
    asm.xor(Value.reg(rd), Value.reg(ss), rd);
  }

  /** Sigma0(A) = rotr 2 ^ rotr 13 ^ rotr 22. */
  private Value sigma0(Value a, int rd, int ss) {
    if (a.isImm()) {
      int x = (int) a.v();
      return Value.imm((Integer.rotateRight(x, 2) ^ Integer.rotateRight(x, 13) ^ Integer.rotateRight(x, 22)) & 0xffffffffL);
    }
    asm.rotri32(a, 11, ss);
    asm.r(VirtualXORROTW12, rd, a.reg(), ss);
    asm.r(VirtualXORROTW22, rd, a.reg(), rd);
    return Value.reg(rd);
  }

  /** Sigma1(E) = rotr 6 ^ rotr 11 ^ rotr 25. */
  private Value sigma1(Value e, int rd, int ss) {
    if (e.isImm()) {
      int x = (int) e.v();
      return Value.imm((Integer.rotateRight(x, 6) ^ Integer.rotateRight(x, 11) ^ Integer.rotateRight(x, 25)) & 0xffffffffL);
    }
    asm.rotri32(e, 18, ss);
    asm.r(VirtualXORROTW19, rd, e.reg(), ss);
    asm.r(VirtualXORROTW6, rd, e.reg(), rd);
    return Value.reg(rd);
  }

  /** Ch(E, F, G) = (E & F) ^ (~E & G). */
  private Value ch(Value e, Value f, Value g, int rd, int ss) {
    Value ef = asm.and(e, f, ss);
    if (!e.isImm() && !g.isImm()) {
      asm.r(ANDN, rd, g.reg(), e.reg());
      return asm.xor(ef, Value.reg(rd), rd);
    }
    Value notE = asm.xor(e, Value.imm(0xffffffffL), rd);
    Value notEg = asm.and(notE, g, rd);
    return asm.xor(ef, notEg, rd);
  }

  /** Maj(A, B, C) = (B & C) ^ (A & (B ^ C)). */
  private Value maj(Value a, Value b, Value c, int rd, int ss) {
    Value bc = asm.and(b, c, ss);
    Value bxc = asm.xor(b, c, rd);
    Value abxc = asm.and(a, bxc, rd);
    return asm.xor(bc, abxc, rd);
  }
}
