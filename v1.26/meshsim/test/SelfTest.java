import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.HashMap;
import java.util.Map;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * The Java side of sw/tests/selftest.h, the self-test's generator and hash. Every constant is read from the header
 * itself (its ST_ #defines), so the tile code and this check can't disagree on them; the algorithm mirrors the header's,
 * and RecipeTest compares the two on all 64 tiles.
 */
final class SelfTest {
    final int a, b, c, warmup, k0, k1, m0, m1, r, h0, h1;

    SelfTest(Path header) {
        Map<String, Long> d = new HashMap<>();
        try {
            Matcher x = Pattern.compile("#define\\s+(ST_\\w+)\\s+(0x[0-9A-Fa-f]+|\\d+)u?").matcher(Files.readString(header));
            while (x.find()) d.put(x.group(1), Long.decode(x.group(2)));
        } catch (IOException e) { throw new RuntimeException(e); }
        a = get(d, "ST_XS_A"); b = get(d, "ST_XS_B"); c = get(d, "ST_XS_C"); warmup = get(d, "ST_WARMUP");
        k0 = get(d, "ST_SEED_K0"); k1 = get(d, "ST_SEED_K1");
        m0 = get(d, "ST_HASH_M0"); m1 = get(d, "ST_HASH_M1"); r = get(d, "ST_HASH_R");
        h0 = get(d, "ST_HASH_H0"); h1 = get(d, "ST_HASH_H1");
    }
    static SelfTest load() { return new SelfTest(Path.of("sw/tests/selftest.h")); }
    private static int get(Map<String, Long> d, String k) {
        Long v = d.get(k); if (v == null) throw new IllegalStateException("selftest.h: " + k + " missing"); return (int) (long) v;
    }

    /** One xorshift64 step. */
    long next(long x) { x ^= x << a; x ^= x >>> b; x ^= x << c; return x; }
    /** st_seed(): never all zero, then the warm-up steps. */
    long seed(long s) { if (s == 0) s = 1; for (int i = 0; i < warmup; i++) s = next(s); return s; }
    /** st_tile_seed(): the base seed, each half XORed with (me + 1) times its constant. */
    long tileSeed(int me, long base) {
        int lo = (int) base ^ (me + 1) * k0, hi = (int) (base >>> 32) ^ (me + 1) * k1;
        return seed((lo & 0xFFFFFFFFL) | (long) hi << 32);
    }
    /** fill(): n records from a seeded state (the first record is one step after it). */
    long[] fill(long state, int n) { long[] v = new long[n]; for (int i = 0; i < n; i++) { state = next(state); v[i] = state; } return v; }
    /** hash_block(): lane 0 the low words, lane 1 the high, h = rotl((h ^ w) * M, R); returns (h1 << 32) | h0. */
    long hash(long[] recs, int from, int n) {
        int x0 = h0, x1 = h1;
        for (int i = from; i < from + n; i++) { x0 = mix(x0, (int) recs[i], m0); x1 = mix(x1, (int) (recs[i] >>> 32), m1); }
        return (x0 & 0xFFFFFFFFL) | (long) x1 << 32;
    }
    int mix(int h, int w, int m) { h = (h ^ w) * m; return Integer.rotateLeft(h, r); }
}
