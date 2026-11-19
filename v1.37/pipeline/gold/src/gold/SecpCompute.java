package gold;

import java.math.BigInteger;

/**
 * Stage 2's secp256k1 advice, computed: jolt-cpp's evaluate (inlines/secp256k1.cpp at 821e1bd) in BigInteger.
 * At a secp inline's first row the inputs are read from memory (32 bytes at rs1, and at rs2 for two-operand
 * operations) and the operation's advice words are prepared; each VirtualAdvice row then takes the next word
 * (trace_owner.cpp:52-66, 787-791).
 *
 * <ul>
 *   <li>MUL/SQUARE (mod q or r): the quotient w of a*b by the modulus, which must fit in 256 bits (4 words).
 *   <li>DIV: c = (a mod m) / (b mod m) mod m and the quotient w of b*c by m, which must leave remainder a exactly;
 *       the words interleaved c0, w0, c1, w1, ... (8 words).
 *   <li>GLV: k = a mod r split as k1 + k2*lambda with |k1|, |k2| < 2^128 (the rounded lattice decomposition); words
 *       sign(k1), |k1| low, |k1| high, sign(k2), |k2| low, |k2| high.
 * </ul>
 * A failure (a zero divisor, an oversized quotient, an invalid division witness) aborts the run, as jolt-cpp does.
 */
public final class SecpCompute implements Machine.SecpAdvice {
  static final BigInteger Q = new BigInteger("fffffffffffffffffffffffffffffffffffffffffffffffffffffffefffffc2f", 16);
  static final BigInteger R = new BigInteger("fffffffffffffffffffffffffffffffebaaedce6af48a03bbfd25e8cd0364141", 16);
  static final BigInteger A1 = new BigInteger("3086d221a7d46bcde86c90e49284eb15", 16);
  static final BigInteger B1 = new BigInteger("e4437ed6010e88286f547fa90abfe4c3", 16);
  static final BigInteger A2 = new BigInteger("114ca50f7a8e2f3f657c1108d9d44cfd8", 16);
  static final BigInteger TWO_256 = BigInteger.ONE.shiftLeft(256);
  static final BigInteger TWO_128 = BigInteger.ONE.shiftLeft(128);
  static final BigInteger TWO_512 = BigInteger.ONE.shiftLeft(512);

  private final long[] words = new long[8];
  private int next, count;
  private long prepared;

  @Override public void prepare(Machine m, VInstr first) {
    BigInteger a = read(m.memory, m.regs[first.adviceRs1]);
    BigInteger b = first.adviceOp.hasRhs ? read(m.memory, m.regs[first.adviceRs2]) : BigInteger.ZERO;
    next = 0;
    count = 0;
    switch (first.adviceOp) {
      case MUL_Q: multiply(a, b, Q); break;
      case SQUARE_Q: multiply(a, a, Q); break;
      case DIV_Q: divide(a, b, Q); break;
      case MUL_R: multiply(a, b, R); break;
      case SQUARE_R: multiply(a, a, R); break;
      case DIV_R: divide(a, b, R); break;
      case GLV_R: glv(a); break;
      default: throw new IllegalStateException();
    }
    prepared++;
  }

  @Override public long next() {
    if (next >= count) throw new EmulationError("secp advice buffer exhausted");
    return words[next++];
  }

  public long operations() { return prepared; }

  /** Four little-endian words at addr as an unsigned number (the raw memory, not reduced). */
  static BigInteger read(Memory mem, long addr) {
    BigInteger v = BigInteger.ZERO;
    for (int i = 3; i >= 0; i--) v = v.shiftLeft(64).or(unsigned(mem.load(addr + 8L * i)));
    return v;
  }

  static BigInteger unsigned(long w) { return new BigInteger(Long.toUnsignedString(w)); }

  static long limb(BigInteger v, int i) { return v.shiftRight(64 * i).longValue(); }

  private void put(long w) { words[count++] = w; }

  /** multiply_field (secp256k1.cpp): advice = the quotient of a*b by the modulus. */
  private void multiply(BigInteger a, BigInteger b, BigInteger m) {
    BigInteger[] qr = a.multiply(b).divideAndRemainder(m);
    if (qr[0].compareTo(TWO_256) >= 0) throw new EmulationError("secp: quotient overflow");
    for (int i = 0; i < 4; i++) put(limb(qr[0], i));
  }

  /** divide_field (secp256k1.cpp): c = a / b mod m, checked by b*c = w*m + a. */
  private void divide(BigInteger a, BigInteger b, BigInteger m) {
    BigInteger divisor = b.mod(m);
    if (divisor.signum() == 0) throw new EmulationError("secp: zero divisor");
    BigInteger c = a.mod(m).multiply(divisor.modInverse(m)).mod(m);
    BigInteger[] witness = b.multiply(c).divideAndRemainder(m);
    if (witness[0].compareTo(TWO_256) >= 0) throw new EmulationError("secp: quotient overflow");
    if (!witness[1].equals(a)) throw new EmulationError("secp: invalid division witness");
    for (int i = 0; i < 4; i++) {
      put(limb(c, i));
      put(limb(witness[0], i));
    }
  }

  /** The rounded quotient k*coefficient / r (half rounds down), truncated to 256 bits. */
  static BigInteger roundedDivide(BigInteger k, BigInteger coefficient) {
    BigInteger[] qr = k.multiply(coefficient).divideAndRemainder(R);
    BigInteger q = qr[0];
    if (qr[1].add(qr[1]).compareTo(R) > 0) q = q.add(BigInteger.ONE);
    return q.mod(TWO_256);
  }

  /** decompose (secp256k1.cpp): the GLV split of k = a mod r, as (sign, magnitude) pairs. */
  private void glv(BigInteger a) {
    BigInteger k = a.mod(R);
    BigInteger beta1 = roundedDivide(k, A1), beta2 = roundedDivide(k, B1);
    putSigned(k, beta1.multiply(A1).add(beta2.multiply(A2)).mod(TWO_512));
    putSigned(beta1.multiply(B1), beta2.multiply(A1));
  }

  /** signed_difference_128: x - y as (negative, magnitude low, magnitude high); the magnitude must fit 128 bits. */
  private void putSigned(BigInteger x, BigInteger y) {
    boolean negative = x.compareTo(y) < 0;
    BigInteger mag = negative ? y.subtract(x) : x.subtract(y);
    if (mag.compareTo(TWO_128) >= 0) throw new EmulationError("secp: GLV component exceeds 128 bits");
    put(negative ? 1 : 0);
    put(limb(mag, 0));
    put(limb(mag, 1));
  }
}
