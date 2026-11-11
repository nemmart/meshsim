package gold;

/** Bit helpers with jolt-cpp's exact behaviour (utils/math.hpp at 821e1bd). */
public final class Bits {
  private Bits() {}

  /** Bits a..63 set, for a in 0..63 (math.hpp:134-138). */
  public static long maskTrailingZeros(long a) { return -1L << a; }

  /** std::countr_zero: 64 for 0. */
  public static int trailingZeros(long a) { return Long.numberOfTrailingZeros(a); }

  /** Gather x's bits at mask's set bits into the low bits (math.hpp:71-87). */
  public static long pext(long x, long mask) {
    long out = 0;
    int bit = 0;
    while (mask != 0) {
      int in = Long.numberOfTrailingZeros(mask);
      out |= ((x >>> in) & 1) << bit;
      bit++;
      mask &= mask - 1;
    }
    return out;
  }

  /** pext, then sign-extended from the highest selected bit (math.hpp:101-131). */
  public static long pextSigned(long x, long mask) {
    if (mask == 0) return 0;
    long out = 0;
    int bit = 0, signPos = 0;
    while (mask != 0) {
      int in = Long.numberOfTrailingZeros(mask);
      out |= ((x >>> in) & 1) << bit;
      signPos = in;
      bit++;
      mask &= mask - 1;
    }
    if (((x >>> signPos) & 1) != 0 && bit < 64) out |= -1L << bit;
    return out;
  }

  /** 32-bit logical right shift by the mask's trailing zeros (mod 32), sign-extended (math.hpp:89-93). */
  public static long virtualSrlw(long x, long bitmask) {
    int shift = Long.numberOfTrailingZeros(bitmask) & 31;
    return (long) ((int) x >>> shift);
  }

  /** 32-bit arithmetic right shift by the mask's trailing zeros (mod 32), sign-extended (math.hpp:95-99). */
  public static long virtualSraw(long x, long bitmask) {
    int shift = Long.numberOfTrailingZeros(bitmask) & 31;
    return (long) ((int) x >> shift);
  }

  /** (x ^ y) on 32 bits, rotated right, zero-extended (math.hpp:64-69). */
  public static long xorRotW(long x, long y, int rotation) {
    return Integer.rotateRight((int) x ^ (int) y, rotation) & 0xffffffffL;
  }

  /** |a| as unsigned; the minimum maps to itself (math.hpp:203-215). */
  public static long unsignedAbs(long a) { return a < 0 ? -a : a; }
  public static long unsignedAbs32(int a) { return (a < 0 ? -a : a) & 0xffffffffL; }
}
