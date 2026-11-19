import java.io.BufferedOutputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Map;

/**
 * The split's 25 data sets without Python: what design_v2/sort/split-bisect/tools/export_sets.py
 * writes, up to and including the exact cuts, which is all SplitRun reads. The 20 sets of design v1's data.datasets()
 * come from {@link KeyGen}'s port (bit-exact with numpy 2.x); the five extras of run_multiseq.extra_sets() (one tile at
 * X, ragged at X, low keys) add numpy's Generator.choice without replacement (Floyd's algorithm and the tail shuffle).
 * Targets and exact cuts as run_multiseq.targets and check.global_ranks: records ordered by (key, tile, index).
 *
 * File (little-endian, export_sets.py's prefix): "VSD1", char[64] label "name (mode)", u32 mode (0 balanced,
 * 1 shaped), u32 ns[64], the keys (tile 0's sorted run, then tile 1's, ...), u32 R[65], u32 exp[63][64] (q = 1..63).
 * export_sets.py also appends the Varman model's round statistics, which SplitRun does not read; they are omitted.
 *   java -cp <classes> SplitSets OUTDIR [--n PER_TILE] [--only NN,NN,...]     (writes OUTDIR/NN.bin, NN = 00..24)
 */
final class SplitSets {
    static final int X = 102_680;

    record Set(String label, int mode, int[][] runs) {}

    /** split_tiles: n keys into 64 consecutive slices, each sorted (unsigned). */
    static int[][] splitTiles(int[] k) {
        int n = k.length, base = n / 64, rem = n % 64, s = 0;
        int[][] r = new int[64][];
        for (int p = 0; p < 64; p++) { int e = s + base + (p < rem ? 1 : 0); r[p] = KeyGen.sortedU(Arrays.copyOfRange(k, s, e)); s = e; }
        return r;
    }

    /** data.datasets() labels, in KeyGen.catalogue()'s order. */
    static String label(String slug) {
        if (slug.equals("uniform") || slug.equals("sorted")) return slug;
        if (slug.equals("reverse")) return "reverse sorted";
        if (slug.equals("equal")) return "all equal";
        if (slug.equals("one99")) return "99% one value";
        if (slug.startsWith("zipf")) {
            boolean raw = slug.startsWith("zipfraw"); String t = slug.substring(raw ? 7 : 4);
            return (raw ? "zipfraw" : "zipf") + " s=" + (t.equals("05") ? "0.5" : t.equals("08") ? "0.8" : "1.0");
        }
        if (slug.startsWith("trace")) {
            String[] hf = slug.substring(5).split("-");
            return "trace hot=" + hf[0] + " frac=0." + hf[1];
        }
        throw new IllegalArgumentException(slug);
    }

    /** numpy Generator.choice(pop, size, replace=False) for pop <= 10000 (Floyd's algorithm, then the tail shuffle). */
    static long[] choiceNoReplace(KeyGen.Rng r, int pop, int size) {
        long setSize = (long) (1.2 * size), mask = genMask(setSize);
        long[] hash = new long[(int) (mask + 1)]; Arrays.fill(hash, -1);
        long[] idx = new long[size];
        for (int j = pop - size; j < pop; j++) {
            long val = r.integer(0, (long) j + 1);                 // random_bounded_uint64(0, j): Lemire 32-bit
            int loc = (int) (val & mask);
            while (hash[loc] != -1 && hash[loc] != val) loc = (int) ((loc + 1) & mask);
            if (hash[loc] == -1) { hash[loc] = val; idx[j - pop + size] = val; }
            else {
                loc = (int) (j & mask);
                while (hash[loc] != -1) loc = (int) ((loc + 1) & mask);
                hash[loc] = j; idx[j - pop + size] = j;
            }
        }
        for (int i = size - 1; i >= 1; i--) {                      // _shuffle_int(n = size, first = 1): Generator's uses
            int j = (int) r.integer(0, (long) i + 1);              // random_bounded_uint64(0, i), Lemire, like integers()
            long t = idx[j]; idx[j] = idx[i]; idx[i] = t;
        }
        return idx;
    }
    static long genMask(long max) { long m = max; m |= m >>> 1; m |= m >>> 2; m |= m >>> 4; m |= m >>> 8; m |= m >>> 16; m |= m >>> 32; return m; }
    /** All 25 sets in export_sets.py's order (n keys per tile for the first 20). */
    static List<java.util.function.Supplier<Set>> all(int n) {
        List<java.util.function.Supplier<Set>> l = new ArrayList<>();
        for (Map.Entry<String, java.util.function.IntFunction<int[]>> e : KeyGen.catalogue().entrySet())
            l.add(() -> new Set(label(e.getKey()) + " (balanced)", 0, splitTiles(e.getValue().apply(64 * n))));
        // run_multiseq.extra_sets(n, seed 0): the generator default_rng(7) is shared by sizes, choice and the low keys
        int[][][] holder = new int[3][][];
        Runnable make = () -> {
            if (holder[0] != null) return;
            KeyGen.Rng r = new KeyGen.Rng(7);
            int[][] one = new int[64][]; one[0] = KeyGen.sortedU(KeyGen.uniform(X, 0)); for (int p = 1; p < 64; p++) one[p] = new int[0];
            long[] sizes = new long[64]; for (int p = 0; p < 64; p++) sizes[p] = r.integer(0, X + 1);
            for (long i : choiceNoReplace(r, 64, 6)) sizes[(int) i] = 0;
            int[][] rag = new int[64][]; for (int p = 0; p < 64; p++) rag[p] = KeyGen.sortedU(KeyGen.uniform((int) sizes[p], 1 + p));
            int[][] low = new int[64][];
            for (int p = 0; p < 64; p++) { int[] k = new int[3000]; for (int i = 0; i < 3000; i++) k[i] = (int) r.integer(0, 1 << 24); low[p] = KeyGen.sortedU(k); }
            holder[0] = one; holder[1] = rag; holder[2] = low;
        };
        l.add(() -> { make.run(); return new Set("one tile at X (balanced)", 0, holder[0]); });
        l.add(() -> { make.run(); return new Set("one tile at X (shaped)", 1, holder[0]); });
        l.add(() -> { make.run(); return new Set("ragged at X (balanced)", 0, holder[1]); });
        l.add(() -> { make.run(); return new Set("ragged at X (shaped)", 1, holder[1]); });
        l.add(() -> { make.run(); return new Set("lowkeys 3000 (balanced)", 0, holder[2]); });
        return l;
    }

    static void write(Path f, Set s) throws IOException {
        int[][] runs = s.runs;
        long N = 0; for (int[] r : runs) N += r.length;
        long[] R = new long[65];
        if (s.mode == 1) { long a = 0; for (int q = 0; q < 64; q++) { R[q] = a; a += runs[q].length; } R[64] = a; }
        else for (int q = 0; q <= 64; q++) R[q] = (q * N) / 64;
        // global ranks under (key, tile, index): sort, then count each tile's records below each target
        long[] all = new long[(int) N]; int k = 0;
        for (int p = 0; p < 64; p++) for (int i = 0; i < runs[p].length; i++) all[k++] = (runs[p][i] & 0xFFFFFFFFL) << 23 | (long) p << 17 | i;
        Arrays.sort(all);
        int[][] exp = new int[64][64]; int[] cnt = new int[64]; int q = 1;
        for (int r = 0; r <= all.length && q < 64; r++) {
            while (q < 64 && R[q] == r) { exp[q] = cnt.clone(); q++; }
            if (r < all.length) cnt[(int) (all[r] >>> 17) & 63]++;
        }
        try (DataOutputStream d = new DataOutputStream(new BufferedOutputStream(Files.newOutputStream(f), 1 << 20))) {
            d.writeBytes("VSD1");
            byte[] lb = new byte[64]; byte[] b = s.label.getBytes(java.nio.charset.StandardCharsets.UTF_8);
            System.arraycopy(b, 0, lb, 0, Math.min(63, b.length)); d.write(lb);
            d.writeInt(Integer.reverseBytes(s.mode));
            for (int[] r : runs) d.writeInt(Integer.reverseBytes(r.length));
            for (int[] r : runs) for (int v : r) d.writeInt(Integer.reverseBytes(v));
            for (long v : R) d.writeInt(Integer.reverseBytes((int) v));
            for (int qq = 1; qq < 64; qq++) for (int p = 0; p < 64; p++) d.writeInt(Integer.reverseBytes(exp[qq][p]));
        }
    }

    public static void main(String[] a) throws IOException {
        if (a.length == 0) { System.err.println("usage: SplitSets OUTDIR [--n PER_TILE] [--only NN,NN,...]"); System.exit(1); }
        Path out = Path.of(a[0]); int n = 65536; java.util.Set<String> only = java.util.Set.of();
        for (int i = 1; i < a.length; i++) {
            if (a[i].equals("--n")) n = Integer.parseInt(a[++i]);
            else if (a[i].equals("--only")) only = java.util.Set.of(a[++i].split(","));
        }
        Files.createDirectories(out);
        List<java.util.function.Supplier<Set>> l = all(n);
        for (int i = 0; i < l.size(); i++) {
            String nn = String.format("%02d", i);
            if (!only.isEmpty() && !only.contains(nn)) continue;
            Set s = l.get(i).get();
            Path f = out.resolve(nn + ".bin");
            write(f, s);
            System.out.println(f + ": " + s.label);
        }
    }
}
