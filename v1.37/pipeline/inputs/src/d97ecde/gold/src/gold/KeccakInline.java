package gold;

import static gold.Op.*;

import gold.InlineAsm.Value;

/**
 * Keccak-f[1600] as Jolt traces it, following upstream's builder (jolt-inlines/keccak256/src/sequence_builder.rs at
 * f8fc214; jolt-cpp's inlines/keccak256.inc and keccak256_absorb_permute.inc are its dumps). rs1 points at the 25-lane
 * state, permuted in place; KECCAK256_ABSORB_PERMUTE first xors the 17-lane rate block at rs2 into it.
 *
 * Registers: lanes 48-72 (lane (x, y) at 48 + 5y + x), C 73-77, D 78-80 (D[3] and D[4] reuse C[1] and C[2], dead by
 * then), the ρπ spill 81, χ temporaries 82-83, scratch 84. ρ and π are fused: lanes move along π's single 24-cycle,
 * each rotated as it moves, the first moved lane parked in the spill register.
 */
final class KeccakInline {
  static final long[] ROUND_CONSTANTS = {
      0x0000000000000001L, 0x0000000000008082L, 0x800000000000808aL, 0x8000000080008000L,
      0x000000000000808bL, 0x0000000080000001L, 0x8000000080008081L, 0x8000000000008009L,
      0x000000000000008aL, 0x0000000000000088L, 0x0000000080008009L, 0x000000008000000aL,
      0x000000008000808bL, 0x800000000000008bL, 0x8000000000008089L, 0x8000000000008003L,
      0x8000000000008002L, 0x8000000000000080L, 0x000000000000800aL, 0x800000008000000aL,
      0x8000000080008081L, 0x8000000000008080L, 0x0000000080000001L, 0x8000000080008008L};

  /** ρ's rotation (left) for lane (x, y), indexed [x][y]. */
  static final int[][] ROTATION_OFFSETS = {
      {0, 36, 3, 41, 18}, {1, 44, 10, 45, 2}, {62, 6, 43, 15, 61}, {28, 55, 25, 21, 56}, {27, 20, 39, 8, 14}};

  static final int RATE_LANES = 17;

  private final InlineAsm asm;
  private final int[] a, c, d;
  private final int piTemp, scratch;
  private final int[] chiTemp;

  private KeccakInline(InlineAsm asm) {
    this.asm = asm;
    a = asm.alloc(25);
    c = asm.alloc(5);
    d = asm.alloc(3);
    piTemp = asm.alloc();
    chiTemp = asm.alloc(2);
    scratch = asm.alloc();
  }

  static void expand(Expander e, boolean absorb, int rd, int rs1, int rs2) {
    InlineAsm asm = new InlineAsm(e, rd, rs1, rs2);
    new KeccakInline(asm).build(absorb);
    asm.finish();
  }

  int lane(int x, int y) { return a[5 * y + x]; }

  /** π moves lane (x, y) to (y, 2x + 3y); its first move's destination is held in the spill register. */
  static int[] piDestination(int x, int y) { return new int[] {y, (2 * x + 3 * y) % 5}; }
  static int[] piSource(int x, int y) { return new int[] {(x + 3 * y) % 5, x}; }
  static final int[] FIRST_SOURCE = {1, 0};
  static final int[] SPILLED = piDestination(1, 0);

  /** After ρπ: lane (x, y)'s value, which for the first destination sits in the spill register. */
  int rhoPiLane(int x, int y) { return x == SPILLED[0] && y == SPILLED[1] ? piTemp : lane(x, y); }

  int dLane(int x) { return x < 3 ? d[x] : x == 3 ? c[1] : c[2]; }

  void build(boolean absorb) {
    for (int i = 0; i < 25; i++) asm.ld(a[i], asm.rs1, 8L * i);
    if (absorb) {
      for (int i = 0; i < RATE_LANES; i++) {
        asm.ld(scratch, asm.rs2, 8L * i);
        asm.r(XOR, a[i], a[i], scratch);
      }
    }
    for (int round = 0; round < 24; round++) {
      theta();
      rhoPi();
      chi();
      asm.xor(Value.reg(lane(0, 0)), Value.imm(ROUND_CONSTANTS[round]), lane(0, 0));   // ι: XORI
    }
    for (int i = 0; i < 25; i++) asm.sd(asm.rs1, a[i], 8L * i);
    asm.release(a);
    asm.release(c);
    asm.release(d);
    asm.release(piTemp);
    asm.release(chiTemp);
    asm.release(scratch);
  }

  /** θ: C[x] = xor of column x; D[x] = C[x-1] ^ rotl(C[x+1], 1); every lane ^= D[x]. */
  void theta() {
    for (int x = 0; x < 5; x++) {
      asm.r(XOR, c[x], lane(x, 0), lane(x, 1));
      for (int y = 2; y < 5; y++) asm.r(XOR, c[x], c[x], lane(x, y));
    }
    for (int x = 0; x < 5; x++) asm.r(VirtualXORROTL1, dLane(x), c[(x + 4) % 5], c[(x + 1) % 5]);
    for (int x = 0; x < 5; x++) {
      for (int y = 0; y < 5; y++) asm.r(XOR, lane(x, y), lane(x, y), dLane(x));
    }
  }

  /** ρ and π together: walk π's cycle backwards from the first source, rotating each lane into its destination. */
  void rhoPi() {
    rotateInto(FIRST_SOURCE, piTemp);
    int[] dest = FIRST_SOURCE;
    for (int k = 1; k < 24; k++) {
      int[] src = piSource(dest[0], dest[1]);
      rotateInto(src, lane(dest[0], dest[1]));
      dest = src;
    }
  }

  /** rotl64 by ρ's offset: VirtualROTRI by 64 - offset (inline.rs:393-414). */
  void rotateInto(int[] src, int destination) {
    asm.rotr64(Value.reg(lane(src[0], src[1])), 64 - ROTATION_OFFSETS[src[0]][src[1]], destination);
  }

  /** χ: lane ^= ~next & next-but-one, row by row, using three temporaries so the row can be overwritten. */
  void chi() {
    int t3 = chiTemp[0], t4 = chiTemp[1];
    for (int y = 0; y < 5; y++) {
      int[] b = new int[5];
      for (int x = 0; x < 5; x++) b[x] = rhoPiLane(x, y);
      asm.r(ANDN, scratch, b[2], b[1]);
      asm.r(ANDN, t3, b[0], b[4]);
      asm.r(ANDN, t4, b[1], b[0]);
      asm.r(XOR, lane(0, y), b[0], scratch);
      asm.r(ANDN, scratch, b[3], b[2]);
      asm.r(XOR, lane(1, y), b[1], scratch);
      asm.r(ANDN, scratch, b[4], b[3]);
      asm.r(XOR, lane(2, y), b[2], scratch);
      asm.r(XOR, lane(3, y), b[3], t3);
      asm.r(XOR, lane(4, y), b[4], t4);
    }
  }
}
