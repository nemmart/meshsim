package gold;

import static gold.Op.*;

/**
 * The secp256k1 inlines' rows, following upstream's builder (jolt-inlines/secp256k1/src/sequence_builder.rs at f8fc214;
 * jolt-cpp's inlines/secp256k1_*.inc are its dump).
 *
 * MULQ/MULR, SQUAREQ/SQUARER and DIVQ/DIVR check, without computing a remainder, that
 * {@code ab + w*p == 2^256*w + c} (for division {@code c*b + w*p == 2^256*w + a}), where q is the field's modulus,
 * {@code p = 2^256 - q}, and w (the quotient) and, for division, c come as advice. The left side is built column by
 * column (product scanning) in two registers that alternate as "this column" and "carries into the next"; each column
 * k < 4 is stored as an output limb (or, for division, compared with a's limb), and each column k >= 4 is compared
 * with w's limb k - 4. For the base field p = 2^32 + 977; for the scalar field p has 129 bits: p2:p plus 2^128,
 * the last term added as w itself, two columns up.
 *
 * GLVR (the GLV decomposition) only stores six advice words.
 */
final class Secp256k1Inline {
  private Secp256k1Inline() {}

  enum Kind { MUL, SQUARE, DIV }

  static void expand(Expander e, int op, int rd, int rs1, int rs2) {
    InlineAsm asm = new InlineAsm(e, rd, rs1, rs2);
    switch (op) {
      case SECP256K1_MULQ_INLINE: new Mulq(asm, Kind.MUL, false).emit(); break;
      case SECP256K1_SQUAREQ_INLINE: new Mulq(asm, Kind.SQUARE, false).emit(); break;
      case SECP256K1_DIVQ_INLINE: new Mulq(asm, Kind.DIV, false).emit(); break;
      case SECP256K1_MULR_INLINE: new Mulq(asm, Kind.MUL, true).emit(); break;
      case SECP256K1_SQUARER_INLINE: new Mulq(asm, Kind.SQUARE, true).emit(); break;
      case SECP256K1_DIVR_INLINE: new Mulq(asm, Kind.DIV, true).emit(); break;
      default: glvr(asm); break;
    }
    asm.finish();
  }

  /** GLVR (sequence_builder.rs:11-40): six advice words stored at rd. */
  static void glvr(InlineAsm asm) {
    int v = asm.alloc();
    for (int i = 0; i < 6; i++) {          // emit_advice_stores
      asm.advice(v);
      asm.sd(asm.rs3, v, 8L * i);
    }
    asm.release(v);
  }

  /** The multiply/square/divide check (sequence_builder.rs:58-497). */
  static final class Mulq {
    final InlineAsm asm;
    final Kind kind;
    final boolean scalar;
    final int[] a, b, w, r;
    final int p, p2, aux, aux2;

    Mulq(InlineAsm asm, Kind kind, boolean scalar) {   // allocation order: sequence_builder.rs:84-117
      this.asm = asm; this.kind = kind; this.scalar = scalar;
      a = asm.alloc(4);
      b = kind == Kind.SQUARE ? null : asm.alloc(4);
      w = asm.alloc(4);
      p = asm.alloc();
      p2 = scalar ? asm.alloc() : 0;
      aux = asm.alloc();
      aux2 = kind == Kind.SQUARE ? asm.alloc() : 0;
      r = asm.alloc(2);
    }

    /** The second factor's limb j: b, or a again when squaring. */
    int bLimb(int j) { return kind == Kind.SQUARE ? a[j] : b[j]; }

    void emit() {
      // inputs and advice (:160-189): a and b from memory, or for division b and the advised result c (kept in a)
      for (int i = 0; i < 4; i++) {
        switch (kind) {
          case MUL: asm.ld(a[i], asm.rs1, 8L * i); asm.ld(b[i], asm.rs2, 8L * i); break;
          case SQUARE: asm.ld(a[i], asm.rs1, 8L * i); break;
          case DIV: asm.ld(b[i], asm.rs2, 8L * i); asm.advice(a[i]); break;
        }
        asm.advice(w[i]);
      }
      if (scalar) {                                     // p = 2^256 - q (:190-197)
        asm.lui(p, 0x402da1732fc9bebfL);
        asm.lui(p2, 0x4551231950b75fc4L);
      } else {
        asm.lui(p, (1L << 32) + 977);
      }

      // column 0 (:198-222)
      asm.r(MUL, r[0], a[0], bLimb(0));
      macLow(r[1], r[0], w[0], p);
      outputLimb(0, r[0]);

      // columns 1..6 (:223-361)
      for (int k = 1; k < 7; k++) {
        boolean first = true;
        int rk = r[k % 2], next = r[(k + 1) % 2];
        if (k < 4) {                                    // w[k] * p, low half
          macLow(next, rk, w[k], p);
          first = false;
        }
        if (k - 1 < 4) {                                // w[k-1] * p, high half
          macHigh(!first, next, rk, w[k - 1], p);
          first = false;
        }
        if (scalar) {
          if (k - 1 < 4) {                              // w[k-1] * p2, low half
            macLow(!first, next, rk, w[k - 1], p2);
            first = false;
          }
          if (k > 1 && k - 2 < 4) {                     // w[k-2] * p2, high half, and w[k-2] * 2^128
            macHigh(!first, next, rk, w[k - 2], p2);
            first = false;
            asm.r(ADD, rk, rk, w[k - 2]);
            asm.r(SLTU, aux, rk, w[k - 2]);
            asm.r(ADD, next, next, aux);
          }
        }
        // a * b, low halves of the products landing in column k
        for (int i = 0; i <= k; i++) {
          int j = k - i;
          if (i >= 4 || j >= 4) continue;
          if (kind == Kind.SQUARE) {
            if (i > j) break;
            if (i == j) macLow(!first, next, rk, a[i], a[j]);
            else if (!first) m2acWithCarry(MUL, next, rk, a[i], a[j]);
            else m2ac(MUL, next, rk, a[i], a[j]);
          } else {
            macLow(!first, next, rk, a[i], b[j]);
          }
          first = false;
        }
        // high halves of the products from column k - 1
        for (int i = 0; i <= k - 1; i++) {
          int j = k - 1 - i;
          if (i >= 4 || j >= 4) continue;
          if (kind == Kind.SQUARE) {
            if (i > j) break;
            if (i == j) macHigh(!first, next, rk, a[i], a[j]);
            else if (!first) m2acWithCarry(MULHU, next, rk, a[i], a[j]);
            else m2ac(MULHU, next, rk, a[i], a[j]);
          } else {
            macHigh(!first, next, rk, a[i], b[j]);
          }
          first = false;
        }
        if (k < 4) outputLimb(k, rk);
        else asm.assertEq(rk, w[k - 4]);
      }

      // column 7 (:362-381): the last high product; it can't carry out
      asm.r(MULHU, aux, a[3], bLimb(3));
      asm.r(ADD, r[1], r[1], aux);
      asm.assertEq(r[1], w[3]);
      asm.assertLte(aux, r[1]);

      // division stores its result only after every check, since rd may alias rs1 (:382-391)
      if (kind == Kind.DIV) for (int i = 0; i < 4; i++) asm.sd(asm.rs3, a[i], 8L * i);

      asm.release(a);
      if (b != null) asm.release(b);
      asm.release(w);
      asm.release(p);
      if (scalar) asm.release(p2);
      asm.release(aux);
      if (kind == Kind.SQUARE) asm.release(aux2);
      asm.release(r);
    }

    /** Column k's limb: stored at rd, or for division compared with the dividend's limb. */
    void outputLimb(int k, int reg) {
      if (kind == Kind.DIV) {
        asm.ld(aux, asm.rs1, 8L * k);
        asm.assertEq(reg, aux);
      } else {
        asm.sd(asm.rs3, reg, 8L * k);
      }
    }

    // ---- multiply-accumulate steps (:398-497); c1 the column, c2 the carry ----

    /** c1 += lo(a*b); c2 = carry (no carry yet in c2). */
    void macLow(int c2, int c1, int x, int y) { mac(MUL, false, c2, c1, x, y); }

    void macLow(boolean carryExists, int c2, int c1, int x, int y) { mac(MUL, carryExists, c2, c1, x, y); }

    void macHigh(boolean carryExists, int c2, int c1, int x, int y) { mac(MULHU, carryExists, c2, c1, x, y); }

    void mac(int mul, boolean carryExists, int c2, int c1, int x, int y) {
      asm.r(mul, aux, x, y);
      asm.r(ADD, c1, c1, aux);
      if (carryExists) {
        asm.r(SLTU, aux, c1, aux);
        asm.r(ADD, c2, c2, aux);
      } else {
        asm.r(SLTU, c2, c1, aux);
      }
    }

    /** c1 += 2 * (half of a*b), the first term of a column (squaring's doubled cross products). */
    void m2ac(int mul, int c2, int c1, int x, int y) {
      asm.r(mul, aux, x, y);
      asm.r(ADD, c1, c1, aux);
      asm.r(SLTU, c2, c1, aux);
      asm.r(ADD, c1, c1, aux);
      asm.r(SLTU, aux, c1, aux);
      asm.r(ADD, c2, c2, aux);
    }

    /** The same, with carries already in c2 (uses aux2 for the carry bits). */
    void m2acWithCarry(int mul, int c2, int c1, int x, int y) {
      asm.r(mul, aux, x, y);
      asm.r(ADD, c1, c1, aux);
      asm.r(SLTU, aux2, c1, aux);
      asm.r(ADD, c2, c2, aux2);
      asm.r(ADD, c1, c1, aux);
      asm.r(SLTU, aux2, c1, aux);
      asm.r(ADD, c2, c2, aux2);
    }
  }
}
