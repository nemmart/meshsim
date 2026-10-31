import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Random;
import java.util.TreeMap;

/**
 * Microbenchmark for the split session: search steps through the controller read port (spec 4.3), the loop as the spec
 * gives it (sw/tests/search.c), cycles per step against the number of active searches. Every tile runs its own
 * searches at once (the read port reads only its own big SRAM). Results are checked against Java.
 * Usage: {@code java SearchBench [shuffleSeed]}.
 */
public final class SearchBench {
    static final int[] BS = { 1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 63 };
    static final String[] VARIANT = { "state by index", "state packed" };

    public static void main(String[] args) {
        long shuffle = args.length > 0 ? Long.parseLong(args[0]) : -1;
        StringBuilder sb = new StringBuilder("search steps through the controller read port (spec 4.3's loop with early exit, sw/tests/search.c, -O2), every tile at once [meas: SearchBench]\n");
        for (int N : new int[] { 65536, 4096 }) {
            Mach m = new Mach(Mach.cfg(shuffle));
            Random r = new Random(N);
            int[][] keys = new int[64][N], targets = new int[64][64];
            for (Tile p : m.c.tiles) {
                long[] k = new long[N]; for (int i = 0; i < N; i++) k[i] = r.nextInt() & 0xFFFFFFFFL;
                Arrays.sort(k);
                for (int i = 0; i < N; i++) { keys[p.idx][i] = (int) k[i]; p.sram.poke(i, (int) k[i]); }
                p.sram.poke(0xF0000, N); p.sram.poke(0xF0001, 0);
                for (int i = 0; i < 64; i++) { targets[p.idx][i] = i == 0 ? 0 : i == 1 ? -1 : r.nextInt(); p.sram.poke(0xF0002 + i, targets[p.idx][i]); }
            }
            Mach.Image im = m.boot("search");
            List<List<Long>> marks = new ArrayList<>(); for (int i = 0; i < 64; i++) marks.add(new ArrayList<>());
            for (Tile p : m.c.tiles) { final int i = p.idx; p.ctl.traceHook = (t, v) -> marks.get(i).add(t); }   // search.c's mark(): a TRACE store (8.15)
            List<TreeMap<Integer, long[]>> byActive = List.of(new TreeMap<>(), new TreeMap<>());   // active -> {cycles, steps}
            long[][] total = new long[2][64];
            int bad = 0, k = 0;
            for (int v = 0; v < 2; v++) for (int B : BS) {
                long[] d0 = new long[64]; int[] base = new int[64];
                for (Tile p : m.c.tiles) { d0[p.idx] = p.ctl.dones; base[p.idx] = marks.get(p.idx).size(); }
                m.c.cc.command(LinkWord.BROADCAST, 1 << 24 | v << 8 | B).go();
                m.run(() -> !m.c.cc.busy(), 5_000_000);                         // the GO has gone out: the run starts
                for (Tile p : m.c.tiles) d0[p.idx] = p.ctl.dones;                // v1.2: the boot ROM pulses done too; count from the GO
                boolean ok = m.run(() -> { for (Tile p : m.c.tiles) if (p.ctl.dones <= d0[p.idx]) return false; return true; }, 5_000_000);
                T.check(ok, "N = " + N + ", " + VARIANT[v] + ", B = " + B + ": every tile finished");
                for (Tile p : m.c.tiles) {
                    List<Long> mk = marks.get(p.idx);
                    int steps = mk.size() - base[p.idx] - 1;
                    for (int s = 0; s < steps; s++) {
                        int na = m.word(p.idx, im, "nact", s);
                        long[] e = byActive.get(v).computeIfAbsent(na, x -> new long[2]); e[0] += mk.get(base[p.idx] + s + 1) - mk.get(base[p.idx] + s); e[1]++;
                    }
                    if (steps >= 0) total[v][B] = Math.max(total[v][B], mk.get(mk.size() - 1) - mk.get(base[p.idx]));
                    int c = 0; for (int i = 0; i < B; i++) c += lowerBound(keys[p.idx], targets[p.idx][i]) * (i + 1);
                    if (m.word(p.idx, im, "chk", k & 63) != c) bad++;
                }
                k++;
            }
            m.c.cc.command(LinkWord.BROADCAST, 2 << 24).go();
            T.check(m.run(m::allStopped, 100000) && m.allExited(), "N = " + N + ": every tile exited: " + m.haltSummary());
            T.eq(0, bad, "N = " + N + ": every run's lower bounds exact on every tile (checksums)");
            T.check(m.c.violations == 0, "N = " + N + ": no violations " + m.c.violationReport());
            long ovf = 0; for (Tile p : m.c.tiles) ovf += p.readPort.overflows;
            T.eq(0, ovf, "N = " + N + ": no read-port overflow");
            sb.append(String.format("  N = %,d (a full search takes %d steps):%n", N, 32 - Integer.numberOfLeadingZeros(N)));
            sb.append("    active searches   cycles per step: by index | packed    per search-step: by index | packed\n");
            for (int na : new int[] { 1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 63 }) {
                long[] a = byActive.get(0).get(na), b = byActive.get(1).get(na);
                if (a == null || b == null) continue;
                double ca = (double) a[0] / a[1], cb = (double) b[0] / b[1];
                sb.append(String.format("    %15d   %26.1f | %6.1f    %24.2f | %5.2f%n", na, ca, cb, ca / na, cb / na));
            }
            for (int v = 0; v < 2; v++) {
                double[] f = fit(byActive.get(v));
                sb.append(String.format("    %-15s a step costs %.1f + %.2f x active cycles (least squares over every step); 63 searches from first step to last: %,d cycles%n", VARIANT[v] + ":", f[0], f[1], total[v][63]));
            }
        }
        System.out.print(sb);
        System.exit(T.done("SearchBench"));
    }
    static double[] fit(TreeMap<Integer, long[]> m) {
        double n = 0, sx = 0, sy = 0, sxx = 0, sxy = 0;
        for (var e : m.entrySet()) { double x = e.getKey(), w = e.getValue()[1], y = (double) e.getValue()[0] / w; n += w; sx += w * x; sy += w * y; sxx += w * x * x; sxy += w * x * y; }
        double b = (n * sxy - sx * sy) / (n * sxx - sx * sx); return new double[] { (sy - b * sx) / n, b };
    }
    static int lowerBound(int[] k, int target) {
        int lo = 0, hi = k.length;
        while (lo < hi) { int mid = (lo + hi) >>> 1; if (Integer.compareUnsigned(k[mid], target) < 0) lo = mid + 1; else hi = mid; }
        return lo;
    }
}
