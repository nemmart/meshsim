import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Random;
import java.util.function.Consumer;

/**
 * M6 step 3: sensitivity sweeps over the uncertain parameters, so phase totals come with an error band. One parameter
 * at a time around the defaults, plus an all-pessimistic corner. Workloads run from C on the machine as specified
 * (until worker 6's sort lands): the 16-group all-to-all (uniform, and one skewed set in full mode), gather 1, and a
 * one-command phase's round trip. Each reports its main phase (GO -> all READY, from PhaseLog), its peaks and any
 * invariant violation. The disjoint-buffer check is off (a program property, independent of these parameters).
 * Usage: {@code java Sweep [quick|full]}; writes results/sweep-<mode>.csv.
 */
public final class Sweep {
    record Point(String param, String value, Consumer<Config> set) {}
    record Res(long cycles, long violations, int link, int eject, int area, int weIn, String firstViolation) {}
    interface Workload { String name(); Res run(Config k); }

    static Config base() { Config k = Config.machine(); k.checkDisjoint = false; return k; }

    public static void main(String[] args) throws Exception {
        boolean full = args.length > 0 && args[0].equals("full");
        List<Workload> ws = new ArrayList<>();
        ws.add(a2a("a2a uniform " + (full ? 65536 : 4096), new A2A(7, full ? 65536 : 4096)));
        if (full) ws.add(a2a("a2a zipfraw (full)", new A2A(Path.of("data/zipfraw.txt"))));
        ws.add(gather(full ? 65536 : 4096));
        ws.add(search());
        ws.add(roundTrip());
        List<Point> pts = new ArrayList<>();
        pts.add(new Point("default", "", k -> {}));
        for (int v : new int[] { 1, 3, 4 }) pts.add(new Point("array read latency", "" + v, k -> k.sramReadLatency = v));
        for (int v : new int[] { 4, 16 }) pts.add(new Point("area queue depth", "" + v, k -> k.areaQueueDepth = v));
        for (int v : new int[] { 2, 3, 4 }) pts.add(new Point("ts_engine_latency", "" + v, k -> k.tsEngineLatency = v));
        for (int v : new int[] { 4, 8 }) pts.add(new Point("ready_latency", "" + v, k -> k.readyLatency = v));
        for (int v : new int[] { 4, 8 }) pts.add(new Point("go/run latency", "" + v, k -> { k.goLatency = v; k.runLatency = v; }));
        for (int v : new int[] { 2, 4 }) pts.add(new Point("link latency", "" + v, k -> k.linkLatency = v));
        pts.add(new Point("start skew", "off", k -> k.skew = false));
        for (long s : new long[] { 2, 3, 4, 5, 6 }) pts.add(new Point("start skew", "seed " + s, k -> k.skewSeed = s));
        pts.add(new Point("cm_latency", "4", k -> k.cmLatency = 4));
        pts.add(new Point("corner (all pessimistic)", "", k -> { k.sramReadLatency = 4; k.areaQueueDepth = 4; k.tsEngineLatency = 4; k.readyLatency = 8;
            k.goLatency = 8; k.runLatency = 8; k.linkLatency = 4; k.cmLatency = 4; }));

        StringBuilder sb = new StringBuilder("sensitivity sweep (" + (full ? "full" : "quick") + "), main-phase cycles GO -> all READY; delta vs default; peaks link/eject/area/weIn [meas: Sweep]\n");
        StringBuilder csv = new StringBuilder("param,value,workload,cycles,delta,violations,peak_link,peak_eject,peak_area,peak_weIn\n");
        long[] def = new long[ws.size()], lo = new long[ws.size()], hi = new long[ws.size()];
        java.util.Arrays.fill(lo, Long.MAX_VALUE);
        for (Point pt : pts) {
            sb.append(String.format("  %-26s %-8s", pt.param, pt.value));
            for (int w = 0; w < ws.size(); w++) {
                Config k = base(); pt.set.accept(k);
                Res r = ws.get(w).run(k);
                if (pt.param.equals("default")) def[w] = r.cycles;
                long d = r.cycles - def[w];
                if (r.violations == 0 && r.cycles > 0) { lo[w] = Math.min(lo[w], r.cycles); hi[w] = Math.max(hi[w], r.cycles); }
                sb.append(String.format(" | %-22s %9d %+6d  %2d/%2d/%d/%d%s", ws.get(w).name(), r.cycles, d, r.link, r.eject, r.area, r.weIn, r.violations > 0 ? " VIOLATION(" + r.violations + ")" : ""));
                csv.append(String.join(",", pt.param, pt.value, ws.get(w).name(), "" + r.cycles, "" + d, "" + r.violations, "" + r.link, "" + r.eject, "" + r.area, "" + r.weIn)).append('\n');
                if (r.violations > 0) System.err.println(pt.param + " " + pt.value + " " + ws.get(w).name() + ": " + r.firstViolation);
            }
            sb.append('\n');
            System.out.print(sb); sb.setLength(0);
        }
        StringBuilder band = new StringBuilder("  band over every point without a violation:\n");
        for (int w = 0; w < ws.size(); w++) band.append(String.format("    %-24s default %,9d; min %,9d (%+.2f%%), max %,9d (%+.2f%%)%n", ws.get(w).name(), def[w], lo[w], 100.0 * (lo[w] - def[w]) / def[w], hi[w], 100.0 * (hi[w] - def[w]) / def[w]));
        System.out.print(band);
        Files.writeString(Path.of("results/sweep-" + (full ? "full" : "quick") + ".csv"), csv.toString());
    }

    static Res result(Mach m, PhaseLog.Phase p) {
        return new Res(p == null ? -1 : p.cycles(), m.c.violations, p == null ? 0 : p.peak.get("link"), p == null ? 0 : p.peak.get("eject"),
                p == null ? 0 : p.peak.get("area"), p == null ? 0 : p.peak.get("weIn"), m.c.violations > 0 ? m.c.violationMessages.get(0) : "");
    }

    /** The 16-group all-to-all from C (sw/tests/a2a.c): configure, start (measured), exit. */
    static Workload a2a(String name, A2A a) {
        return new Workload() {
            public String name() { return name; }
            public Res run(Config k) {
                Mach m = new Mach(k);
                for (Tile p : m.c.tiles) {
                    int s = p.idx;
                    for (int i = 0; i < a.rec[s].length; i++) p.sram.poke64(A2A.SRC + 2 * i, a.rec[s][i]);
                    for (int d = 0; d < 64; d++) p.sram.poke(0xF0000 + d, a.cnt[s][d]);
                    for (int src = 0; src < 64; src++) p.sram.poke(0xF0000 + 64 + src, a.cnt[src][s]);
                    for (int g = 0; g < 16; g++) p.sram.poke(0xF0000 + 128 + g, a.R[g]);
                    p.sram.poke(0xF0000 + 144, 0);
                }
                m.boot("a2a");
                m.c.cc.command(LinkWord.BROADCAST, 1 << 24).go().command(LinkWord.BROADCAST, 2 << 24).go().command(LinkWord.BROADCAST, 3 << 24).go();
                m.run(m::allStopped, 10_000_000);
                m.c.phases.close();
                PhaseLog.Phase p = m.c.phases.phases.size() > 1 ? m.c.phases.phases.get(1) : null;
                Res r = result(m, p);
                if (a.verify(m.c) != 0) return new Res(r.cycles, r.violations + 1, r.link, r.eject, r.area, r.weIn, "records did not all land");
                return r;
            }
        };
    }

    /** Gather 1 from C (sw/tests/gather.c), k = 2. */
    static Workload gather(int n) {
        return new Workload() {
            public String name() { return "gather " + n + " k=2"; }
            public Res run(Config k) {
                Mach m = new Mach(k);
                for (Tile p : m.c.tiles) {
                    Random r = new Random(100 + p.idx);
                    int[] pm = new int[n]; for (int i = 0; i < n; i++) pm[i] = i;
                    for (int i = n - 1; i > 0; i--) { int j = r.nextInt(i + 1); int x = pm[i]; pm[i] = pm[j]; pm[j] = x; }
                    for (int i = 0; i < n; i++) p.sram.poke64(0x60000 + 2 * i, (long) pm[i] << 32 | i);
                }
                m.boot("gather");
                m.c.cc.command(LinkWord.BROADCAST, 1 << 24 | n).go().command(LinkWord.BROADCAST, 2 << 24).go();
                m.run(m::allStopped, 10_000_000);
                m.c.phases.close();
                return result(m, m.c.phases.phases.isEmpty() ? null : m.c.phases.phases.get(0));
            }
        };
    }

    /** 63 searches through the read port (sw/tests/search.c, packed variant, N = 65,536): GO -> all READY. */
    static Workload search() {
        return new Workload() {
            public String name() { return "63 searches (read port)"; }
            public Res run(Config k) {
                Mach m = new Mach(k);
                Random r = new Random(65536);
                for (Tile p : m.c.tiles) {
                    long[] key = new long[65536]; for (int i = 0; i < key.length; i++) key[i] = r.nextInt() & 0xFFFFFFFFL;
                    java.util.Arrays.sort(key);
                    for (int i = 0; i < key.length; i++) p.sram.poke(i, (int) key[i]);
                    p.sram.poke(0xF0000, 65536);
                    for (int i = 0; i < 64; i++) p.sram.poke(0xF0002 + i, r.nextInt());
                }
                Mach.Image im = m.boot("search");
                final int exitEbreak = im.sym("_start") + 0x30;
                for (Tile p : m.c.tiles) p.core.debugHook = (c, t) -> c.pc != exitEbreak;
                m.c.cc.command(LinkWord.BROADCAST, 1 << 24 | 1 << 8 | 63).go().command(LinkWord.BROADCAST, 2 << 24).go();
                m.run(m::allStopped, 10_000_000);
                m.c.phases.close();
                return result(m, m.c.phases.phases.isEmpty() ? null : m.c.phases.phases.get(0));
            }
        };
    }

    /** A one-command phase's round trip (sw/tests/cmd.c, command 1): GO -> all READY. */
    static Workload roundTrip() {
        return new Workload() {
            public String name() { return "one-command round trip"; }
            public Res run(Config k) {
                Mach m = new Mach(k);
                m.boot("cmd");
                m.c.cc.command(LinkWord.BROADCAST, 1 << 24 | 1, 1).go().command(LinkWord.BROADCAST, 1 << 24 | 2, 2).go().command(LinkWord.BROADCAST, 4 << 24).go();
                m.run(m::allStopped, 100_000);
                m.c.phases.close();
                return result(m, m.c.phases.phases.size() > 1 ? m.c.phases.phases.get(1) : null);
            }
        };
    }
}
