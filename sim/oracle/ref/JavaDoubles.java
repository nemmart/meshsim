/**
 * JavaDoubles (WORKER19, P1, D42): Double.toString on a fixed set of doubles, the fixture for convert_delivery's JSON
 * writer (tools/delivery/json.h java_double; tests/delivery/java_doubles.txt, tests/test_delivery_json.cpp). One line per
 * value: the bits in hex, then Java's string. The edge cases, every power of ten from 1e-330 to 1e310 and its
 * neighbours, then 8,000 values from a fixed SplittableRandom: random bits, and random decimals of a few digits.
 *   java JavaDoubles > java_doubles.txt
 */
public final class JavaDoubles {
    public static void main(String[] a) {
        StringBuilder s = new StringBuilder();
        java.util.List<Double> v = new java.util.ArrayList<>(java.util.List.of(0.0, -0.0, 1.0, -1.0, 0.1, 0.5, 1e-3, 9.99e-4,
            1e7, 9999999.999, 1e-5, 123.456, Double.MIN_VALUE, Double.MAX_VALUE, Double.MIN_NORMAL, 2e-3, 1.0E22, 4.35, 0.3));
        for (int e = -330; e <= 310; e++) { double d = Double.parseDouble("1e" + e); v.add(d); v.add(Math.nextUp(d)); v.add(Math.nextDown(d)); }
        java.util.SplittableRandom r = new java.util.SplittableRandom(42);
        for (int i = 0; i < 4000; i++) { double d = Double.longBitsToDouble(r.nextLong()); if (!Double.isNaN(d) && !Double.isInfinite(d)) v.add(d); }
        for (int i = 0; i < 4000; i++) v.add(Double.parseDouble(r.nextInt(100000) + "." + r.nextInt(1000) + "e" + (r.nextInt(30) - 15)));
        for (double d : v) s.append(String.format("%016x %s%n", Double.doubleToRawLongBits(d), Double.toString(d)));
        System.out.print(s);
    }
}
