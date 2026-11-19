import java.io.BufferedOutputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.math.BigInteger;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;

/**
 * The sort's input key sets without Python: a Java port of design v1's data.py catalogue
 * (design_v1/python/split-point-model/data.py, datasets(64 n, seed 0)), bit-exact with numpy 2.x:
 * numpy's SeedSequence, PCG64 (with its 32-bit half buffer), Generator.random, Generator.integers (Lemire's bounded
 * method, 32-bit for ranges below 2^32), Generator.choice, cumsum, the pairwise sum and searchsorted.
 * Writes the same files as sw/ext/sort/tools/export_keys.py: <slug>.keys, magic 0x4B455953, 64 counts, the keys in
 * generation order, 64 consecutive blocks. Verified byte-identical with export_keys.py on every set.
 *   java -cp <classes> KeyGen OUTDIR [--n PER_TILE] [--only slug,slug,...]
 */
final class KeyGen {
    // ================================================================ numpy SeedSequence (bit_generator.pyx)
    static final int INIT_A = 0x43b0d7e5, MULT_A = 0x931e8875, INIT_B = 0x8b51f9dd, MULT_B = 0x58f38ded,
        MIX_MULT_L = 0xca01f9dd, MIX_MULT_R = 0x4973f715, XSHIFT = 16, POOL = 4;

    /** generate_state(4, uint64) of SeedSequence(entropy), entropy given as uint32 words. */
    static long[] seedState(int[] entropy) {
        int[] mixer = new int[POOL];
        int[] hc = { INIT_A };
        java.util.function.IntUnaryOperator hashmix = v -> { v ^= hc[0]; hc[0] *= MULT_A; v *= hc[0]; v ^= v >>> XSHIFT; return v; };
        for (int i = 0; i < POOL; i++) mixer[i] = hashmix.applyAsInt(i < entropy.length ? entropy[i] : 0);
        for (int s = 0; s < POOL; s++) for (int d = 0; d < POOL; d++) if (s != d) mixer[d] = mix(mixer[d], hashmix.applyAsInt(mixer[s]));
        for (int s = POOL; s < entropy.length; s++) for (int d = 0; d < POOL; d++) mixer[d] = mix(mixer[d], hashmix.applyAsInt(entropy[s]));
        int h = INIT_B; int[] w = new int[8];
        for (int i = 0; i < 8; i++) { int v = mixer[i % POOL]; v ^= h; h *= MULT_B; v *= h; v ^= v >>> XSHIFT; w[i] = v; }
        long[] out = new long[4];
        for (int i = 0; i < 4; i++) out[i] = (w[2 * i] & 0xFFFFFFFFL) | (long) w[2 * i + 1] << 32;
        return out;
    }
    static int mix(int x, int y) { int r = MIX_MULT_L * x - MIX_MULT_R * y; return r ^ (r >>> XSHIFT); }
    /** A Python int (>= 0) as numpy's uint32 words, little-endian; 0 is one word. */
    static int[] words(long v) { if (v == 0) return new int[] { 0 }; List<Integer> l = new ArrayList<>(); while (v != 0) { l.add((int) v); v >>>= 32; } return l.stream().mapToInt(Integer::intValue).toArray(); }

    // ================================================================ PCG64 (XSL-RR 128/64) and Generator
    static final class Rng {
        static final BigInteger M128 = BigInteger.ONE.shiftLeft(128).subtract(BigInteger.ONE);
        static final long MUL_HI = 0x2360ED051FC65DA4L, MUL_LO = 0x4385DF649FCCF645L;
        long sHi, sLo, iHi, iLo; boolean has32; int buf32;
        /** default_rng(seed) or default_rng([seed, p]). */
        Rng(long... seeds) {
            List<Integer> e = new ArrayList<>();
            for (long s : seeds) for (int w : words(s)) e.add(w);
            long[] v = seedState(e.stream().mapToInt(Integer::intValue).toArray());
            // pcg64_set_seed: state seed = v[0] << 64 | v[1], inc seed = v[2] << 64 | v[3]; srandom: state 0,
            // inc = initseq << 1 | 1, step, state += initstate, step
            BigInteger initstate = u(v[0]).shiftLeft(64).or(u(v[1]));
            BigInteger initseq = u(v[2]).shiftLeft(64).or(u(v[3]));
            BigInteger inc = initseq.shiftLeft(1).or(BigInteger.ONE).and(M128);
            iHi = inc.shiftRight(64).longValue(); iLo = inc.longValue();
            sHi = 0; sLo = 0; step();
            BigInteger s = u(sHi).shiftLeft(64).or(u(sLo)).add(initstate).and(M128);
            sHi = s.shiftRight(64).longValue(); sLo = s.longValue();
            step();
        }
        static BigInteger u(long x) { return new BigInteger(Long.toUnsignedString(x)); }
        /** state = state * MUL + inc (mod 2^128). */
        void step() {
            long lo = sLo * MUL_LO, hi = umulh(sLo, MUL_LO) + sLo * MUL_HI + sHi * MUL_LO;
            long nlo = lo + iLo; long carry = Long.compareUnsigned(nlo, lo) < 0 ? 1 : 0;
            sHi = hi + iHi + carry; sLo = nlo;
        }
        /** the high 64 bits of an unsigned 64 x 64 product (Java 17: no Math.unsignedMultiplyHigh) */
        static long umulh(long a, long b) { return Math.multiplyHigh(a, b) + ((a >> 63) & b) + ((b >> 63) & a); }
        long next64() { step(); long v = sHi ^ sLo; int rot = (int) (sHi >>> 58); return Long.rotateRight(v, rot); }
        int next32() { if (has32) { has32 = false; return buf32; } long n = next64(); has32 = true; buf32 = (int) (n >>> 32); return (int) n; }
        double random() { return (next64() >>> 11) * (1.0 / 9007199254740992.0); }
        /** integers(low, high) in int64, for high - 1 - low < 2^32 (every call data.py makes). */
        long integer(long low, long high) {
            long rng = high - 1 - low;
            if (rng == 0) return low;
            if (rng == 0xFFFFFFFFL) return low + (next32() & 0xFFFFFFFFL);
            if (rng > 0xFFFFFFFFL) throw new IllegalArgumentException("range above 2^32: not used by data.py");
            long excl = rng + 1, m = (next32() & 0xFFFFFFFFL) * excl, left = m & 0xFFFFFFFFL;
            if (left < excl) {
                long threshold = (0xFFFFFFFFL - rng) % excl;
                while (left < threshold) { m = (next32() & 0xFFFFFFFFL) * excl; left = m & 0xFFFFFFFFL; }
            }
            return low + (m >>> 32);
        }
    }

    // ================================================================ numpy helpers
    /** numpy's pairwise summation (pairwise_sum_DOUBLE, blocks of 8, PW_BLOCKSIZE 128). */
    static double pairwise(double[] a, int off, int n) {
        if (n < 8) { double r = -0.0; for (int i = 0; i < n; i++) r += a[off + i]; return r; }
        if (n <= 128) {
            double[] r = new double[8];
            for (int j = 0; j < 8; j++) r[j] = a[off + j];
            int i;
            for (i = 8; i < n - (n % 8); i += 8) for (int j = 0; j < 8; j++) r[j] += a[off + i + j];
            double res = ((r[0] + r[1]) + (r[2] + r[3])) + ((r[4] + r[5]) + (r[6] + r[7]));
            for (; i < n; i++) res += a[off + i];
            return res;
        }
        int n2 = n / 2; n2 -= n2 % 8;
        return pairwise(a, off, n2) + pairwise(a, off + n2, n - n2);
    }
    static double sum(double[] a) { return pairwise(a, 0, a.length); }
    /** searchsorted(cdf, x, side='right'): the number of entries <= x. */
    static int searchRight(double[] cdf, double x) {
        int lo = 0, hi = cdf.length;
        while (lo < hi) { int mid = (lo + hi) >>> 1; if (cdf[mid] <= x) lo = mid + 1; else hi = mid; }
        return lo;
    }
    static double[] zipfCdf(int K, double s) {
        double[] w = new double[K];
        for (int k = 1; k <= K; k++) w[k - 1] = Math.pow(k, -s);
        double tot = sum(w), c = 0;
        double[] cdf = new double[K];
        for (int i = 0; i < K; i++) { c += w[i]; cdf[i] = c / tot; }
        return cdf;
    }
    static int mix32(int x) { x ^= x >>> 16; x *= 0x7FEB352D; x ^= x >>> 15; x *= 0x846CA68B; x ^= x >>> 16; return x; }

    // ================================================================ data.py's generators (seed 0)
    static int[] uniform(int n, long seed) {
        Rng r = new Rng(seed); int[] o = new int[n];
        for (int i = 0; i < n; i++) o[i] = (int) r.integer(0, 1L << 32);
        return o;
    }
    static int[] zipf(int n, double s, boolean raw, long seed) {
        double[] cdf = zipfCdf(1 << 20, s);
        int base = n / 64, rem = n % 64, pos = 0; int[] o = new int[n];
        for (int p = 0; p < 64; p++) {
            int m = base + (p < rem ? 1 : 0);
            Rng r = new Rng(seed, p);
            for (int i = 0; i < m; i++) { int k = searchRight(cdf, r.random()) + 1; o[pos++] = raw ? k : mix32(k); }
        }
        return o;
    }
    static int[] tracelike(int n, int hotSize, double hotFrac, long seed) {
        long space = 1L << 30; int seqLo = 100, seqHi = 10000; int[] strides = { 4, 8 };
        Rng r = new Rng(seed);
        double rest = 1.0 - hotFrac, seqFrac = rest * 4 / 7, uniFrac = rest * 3 / 7, meanSeq = (seqLo + seqHi) / 2.0;
        double[] w = { hotFrac, seqFrac / meanSeq, uniFrac };
        double ws = sum(w); for (int i = 0; i < 3; i++) w[i] /= ws;
        double meanEv = w[0] + w[1] * meanSeq + w[2];
        long[] hot = new long[hotSize];
        for (int i = 0; i < hotSize; i++) hot[i] = r.integer(0, space);
        double[] hotCdf = zipfCdf(hotSize, 1.0);
        double[] pc = new double[3]; double c = 0; for (int i = 0; i < 3; i++) { c += w[i]; pc[i] = c; }   // choice: p.cumsum()
        for (int i = 0; i < 3; i++) pc[i] /= pc[2];                                                    // cdf /= cdf[-1]
        long[] out = new long[n]; int total = 0;
        while (total < n) {
            int E = (int) ((n - total) / meanEv * 1.1) + 64;
            int[] cats = new int[E];
            for (int e = 0; e < E; e++) cats[e] = searchRight(pc, r.random());
            long[] lens = new long[E]; Arrays.fill(lens, 1);
            List<Integer> seqIdx = new ArrayList<>(), hotIdx = new ArrayList<>(), uniIdx = new ArrayList<>();
            for (int e = 0; e < E; e++) (cats[e] == 0 ? hotIdx : cats[e] == 1 ? seqIdx : uniIdx).add(e);
            for (int e : seqIdx) lens[e] = r.integer(seqLo, seqHi + 1);
            long[] offs = new long[E + 1]; for (int e = 0; e < E; e++) offs[e + 1] = offs[e] + lens[e];
            long[] buf = new long[(int) offs[E]];
            for (int e : hotIdx) buf[(int) offs[e]] = hot[searchRight(hotCdf, r.random())];
            for (int e : uniIdx) buf[(int) offs[e]] = r.integer(0, space);
            for (int e : seqIdx) {
                long L = lens[e]; int st = strides[(int) r.integer(0, 2)];
                long start = r.integer(0, space - L * st);
                for (int k = 0; k < L; k++) buf[(int) offs[e] + k] = start + (long) st * k;
            }
            int take = Math.min(buf.length, n - total);
            System.arraycopy(buf, 0, out, total, take);
            total += buf.length;
        }
        int[] o = new int[n]; for (int i = 0; i < n; i++) o[i] = (int) out[i];
        return o;
    }
    static int[] sortedU(int[] k) { long[] t = new long[k.length]; for (int i = 0; i < k.length; i++) t[i] = k[i] & 0xFFFFFFFFL; Arrays.sort(t); int[] o = new int[k.length]; for (int i = 0; i < k.length; i++) o[i] = (int) t[i]; return o; }
    static int[] mostlyOne(int n, long seed) {
        Rng r = new Rng(seed); int[] k = uniform(n, seed);
        for (int i = 0; i < n; i++) if (r.random() < 0.99) k[i] = 0xDEADBEEF;
        return k;
    }

    /** The catalogue in data.datasets() order: slug -> generator of N keys. */
    static Map<String, java.util.function.IntFunction<int[]>> catalogue() {
        Map<String, java.util.function.IntFunction<int[]>> m = new LinkedHashMap<>();
        m.put("uniform", N -> uniform(N, 0));
        for (double s : new double[] { 0.5, 0.8, 1.0 }) {
            String t = s == 0.5 ? "05" : s == 0.8 ? "08" : "10";
            m.put("zipf" + t, N -> zipf(N, s, false, 0));
            m.put("zipfraw" + t, N -> zipf(N, s, true, 0));
        }
        for (int h : new int[] { 10, 100, 10000 }) for (double f : new double[] { 0.10, 0.30, 0.60 })
            m.put("trace" + h + "-" + String.format("%02d", Math.round(f * 100)), N -> tracelike(N, h, f, 0));
        m.put("sorted", N -> sortedU(uniform(N, 0)));
        m.put("reverse", N -> { int[] s = sortedU(uniform(N, 0)); int[] o = new int[N]; for (int i = 0; i < N; i++) o[i] = s[N - 1 - i]; return o; });
        m.put("equal", N -> { int[] o = new int[N]; Arrays.fill(o, 12345); return o; });
        m.put("one99", N -> mostlyOne(N, 0));
        return m;
    }

    public static void main(String[] a) throws IOException {
        if (a.length == 0) { System.err.println("usage: KeyGen OUTDIR [--n PER_TILE] [--only slug,...]"); System.exit(1); }
        Path out = Path.of(a[0]); int n = 65536; Set<String> only = Set.of();
        for (int i = 1; i < a.length; i++) {
            if (a[i].equals("--n")) n = Integer.parseInt(a[++i]);
            else if (a[i].equals("--only")) only = Set.of(a[++i].split(","));
        }
        Files.createDirectories(out);
        for (var e : catalogue().entrySet()) {
            if (!only.isEmpty() && !only.contains(e.getKey())) continue;
            int[] k = e.getValue().apply(64 * n);
            Path f = out.resolve(e.getKey() + ".keys");
            try (DataOutputStream d = new DataOutputStream(new BufferedOutputStream(Files.newOutputStream(f), 1 << 20))) {
                d.writeInt(Integer.reverseBytes(0x4B455953));
                for (int p = 0; p < 64; p++) d.writeInt(Integer.reverseBytes(n));
                for (int v : k) d.writeInt(Integer.reverseBytes(v));
            }
            System.out.println(f + ": 64 x " + n);
        }
    }
}
