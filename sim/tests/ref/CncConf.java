import java.io.BufferedReader;
import java.io.BufferedWriter;
import java.io.FileReader;
import java.io.IOException;
import java.io.OutputStreamWriter;
import java.io.Writer;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Random;

/**
 * C&C's conformance, ref's side (WORKER9; the C++ side is tests/test_cnc.cpp). ref's CnC with its peripheral
 * (Cnc1Peripheral) and C&C 1's endpoint, in a real Chip whose tiles are not run: the driver plays them and tile 0's
 * router, as Chip.runCycle orders them. Compiled into ref's default package (D5); nothing in sim/ref is changed.
 * <pre>
 *   java -cp REF:. CncConf gen SEED CYCLES        a script on stdout
 *   java -cp REF:. CncConf run HOPS SCRIPT        ref's observations on stdout
 *   java -cp REF:. CncConf idle HOPS              StateFp's "ep" and "reqbus" of a chip at rest, hashed
 * </pre>
 * Script: "# cncconf v1", "# seed S cycles N", then lines in any order:
 * <pre>
 *   P load DST ADDR w0 w1 ...    CnC.loadWords (hex words)          P go            CnC.go
 *   P cmd DST WORD a0 a1 ...     CnC.command (hex)                  P run ID        CnC.go().call(ID)
 *   P wait N                     CnC.waitCycles                     P allready ID   SortTest's atAllReady(ID)
 *   P call ID                    CnC.call                           P release T     the P lines after it are queued
 *                                                                                   between cycles T and T + 1 (as
 *                                                                                   SortTest queues after its boot)
 *   R K E        tile K's READY rises at the end of cycle E
 *   E K E L      tile K's error line has level L after cycle E (a change)
 *   X T a,b,c    tile 0's router puts a packet on C&C's receive FIFO at T
 *   K T          tile 0's router does not take C&C's send holding register at T (a stall, so a send is refused)
 * </pre>
 * The tiles also answer every GO: tile k's READY rises at the end of goAt + d(k, n), goAt = the GO's store cycle +
 * go_latency + skew[k] (Chip's skews), n the GO's number from 1, d = 1 + splitmix64(SEED * 1000003 + 64 n + k) mod 40.
 *
 * Observation line for cycle t: {@code t [T:a,b,c] [G] [C:id]... H:cnc,cc1p}: the word tile 0's router took from the
 * send register at t, G if GO was stored at t, the calls run at t, then StateFp's "cnc" and "cc1p" streams hashed.
 */
final class CncConf {
    public static void main(String[] a) throws IOException {
        switch (a[0]) {
            case "gen" -> gen(Long.parseLong(a[1]), Integer.parseInt(a[2]));
            case "run" -> run(Hops.load(Path.of(a[1])), a[2]);
            case "idle" -> {                                   // StateFp "ep" and "reqbus" of a chip at rest, at cycle 5
                Config cfg = Config.machine(); cfg.hops = Hops.load(Path.of(a[1]));
                Chip ch = new Chip(cfg);
                StateFp.Out e = new StateFp.Out(); StateFp.chipUnit(e, ch, "ep", 5);
                StateFp.Out q = new StateFp.Out(); StateFp.chipUnit(q, ch, "reqbus", 5);
                System.out.println(Long.toHexString(e.done()) + " " + Long.toHexString(q.done()));
            }
            default -> throw new IllegalArgumentException(a[0]);
        }
    }

    static long splitmix(long x) { x += 0x9E3779B97F4A7C15L; x = (x ^ (x >>> 30)) * 0xBF58476D1CE4E5B9L; x = (x ^ (x >>> 27)) * 0x94D049BB133111EBL; return x ^ (x >>> 31); }
    static int delay(long seed, int k, int n) { return 1 + (int) Long.remainderUnsigned(splitmix(seed * 1000003L + 64L * n + k), 40); }

    // ------------------------------------------------------------------------------------------------ the generator
    static void gen(long seed, int cycles) {
        Random r = new Random(seed);
        StringBuilder sb = new StringBuilder("# cncconf v1\n# seed " + seed + " cycles " + cycles + "\n");
        int ids = 0;
        sb.append("P load 7e ").append(Integer.toHexString(0x01000000 + 16 * r.nextInt(64))).append(words(r, 1 + r.nextInt(12))).append('\n');
        sb.append("P cmd 7e 4a000000 ").append(Integer.toHexString(r.nextInt(1 << 16))).append('\n');
        sb.append("P run ").append(ids++).append('\n');
        int steps = 70 + r.nextInt(30), rel = steps * 3 / 5;
        for (int i = 0; i < steps; i++) {
            if (i == rel) sb.append("P release ").append(800 + r.nextInt(1200)).append('\n');
            int k = r.nextInt(10);
            if (k < 2) sb.append("P load ").append(r.nextInt(4) == 0 ? Integer.toHexString(r.nextInt(64)) : "7e").append(' ')
                         .append(Integer.toHexString(16 * r.nextInt(1 << 10))).append(words(r, 1 + r.nextInt(30))).append('\n');
            else if (k < 4) {
                StringBuilder args = new StringBuilder(); int na = r.nextInt(4);
                for (int j = 0; j < na; j++) args.append(' ').append(Integer.toHexString(r.nextInt()));
                if (r.nextInt(5) == 0) {                                  // two commands to one tile: the parities stay aligned
                    String d = Integer.toHexString(r.nextInt(64));
                    sb.append("P cmd ").append(d).append(' ').append(Integer.toHexString(r.nextInt(256) << 24)).append(args).append('\n');
                    sb.append("P cmd ").append(d).append(' ').append(Integer.toHexString(r.nextInt(256) << 24)).append('\n');
                } else sb.append("P cmd 7e ").append(Integer.toHexString(r.nextInt(256) << 24)).append(args).append('\n');
            }
            else if (k < 6) sb.append("P go\n");
            else if (k < 7) sb.append("P wait ").append(r.nextInt(30)).append('\n');
            else if (k < 8) sb.append("P allready ").append(ids++).append("\nP go\n");
            else if (k < 9) sb.append("P call ").append(ids++).append('\n');
            else sb.append("P run ").append(ids++).append('\n');
        }
        for (int k = 0; k < 64; k++) sb.append("R ").append(k).append(' ').append(1 + r.nextInt(80)).append('\n');   // the boot's READY
        // No extra READY pulses: a second pulse while a tile's first is visible (not yet cleared by GO) makes ref's READY
        // bit read 0 until the second arrives (readyAt holds one cycle: max(old, edge + ready_latency)), which C&C's model
        // cannot know before that arrival; the spec's bit is sticky (W9 NOTES, stage 3: for the owner).
        for (int i = 0; i < 6; i++) {                                     // error lines: up and down again
            int k = r.nextInt(64), e = 1 + r.nextInt(cycles - 200);
            sb.append("E ").append(k).append(' ').append(e).append(" 1\n");
            sb.append("E ").append(k).append(' ').append(e + 1 + r.nextInt(150)).append(" 0\n");
        }
        java.util.Set<Integer> xs = new java.util.HashSet<>();
        for (int i = 0; i < 40; i++) {
            int xt = 1 + r.nextInt(cycles);
            if (!xs.add(xt)) continue;                                   // one packet a cycle from the router
            sb.append("X ").append(xt).append(' ').append(Long.toHexString(r.nextLong())).append(',')
              .append(Long.toHexString(LinkWord.hdr(r.nextInt(64), 64, 0, 0, r.nextInt(), r.nextInt(3)))).append(',').append(Long.toHexString(r.nextLong())).append('\n');
        }
        for (int t = 1; t <= cycles; t++) if (r.nextInt(6) == 0) sb.append("K ").append(t).append('\n');
        System.out.print(sb);
    }
    static String words(Random r, int n) { StringBuilder s = new StringBuilder(); for (int i = 0; i < n; i++) s.append(' ').append(Integer.toHexString(r.nextInt())); return s.toString(); }

    // ------------------------------------------------------------------------------------------------ the driver
    static long curT;

    static void run(Hops hops, String script) throws IOException {
        Config cfg = Config.machine(); cfg.hops = hops;
        Chip ch = new Chip(cfg);
        CnC cc = ch.cc; Endpoint ep = ch.cnc;
        long seed = 0; int cycles = 0;
        Map<Long, List<Integer>> ready = new HashMap<>();
        Map<Long, List<int[]>> err = new HashMap<>();
        Map<Long, long[]> rx = new HashMap<>();
        java.util.Set<Long> stall = new java.util.HashSet<>();
        Map<Long, List<Integer>> calls = new HashMap<>();
        long releaseAt = -1; List<String[]> later = new ArrayList<>();
        try (BufferedReader in = new BufferedReader(new FileReader(script))) {
            for (String line; (line = in.readLine()) != null; ) {
                if (line.startsWith("# seed ")) { String[] f = line.split(" "); seed = Long.parseLong(f[2]); cycles = Integer.parseInt(f[4]); continue; }
                if (line.startsWith("#") || line.isEmpty()) continue;
                String[] f = line.split(" ");
                switch (f[0]) {
                    case "P" -> {
                        if (f[1].equals("release")) releaseAt = Long.parseLong(f[2]);
                        else if (releaseAt >= 0) later.add(f);
                        else program(cc, f, calls);
                    }
                    case "R" -> ready.computeIfAbsent(Long.parseLong(f[2]), x -> new ArrayList<>()).add(Integer.parseInt(f[1]));
                    case "E" -> err.computeIfAbsent(Long.parseLong(f[2]), x -> new ArrayList<>()).add(new int[] { Integer.parseInt(f[1]), Integer.parseInt(f[3]) });
                    case "X" -> { String[] w = f[2].split(","); rx.put(Long.parseLong(f[1]), new long[] { Long.parseUnsignedLong(w[0], 16), Long.parseUnsignedLong(w[1], 16), Long.parseUnsignedLong(w[2], 16) }); }
                    case "K" -> stall.add(Long.parseLong(f[1]));
                    default -> throw new IllegalArgumentException(line);
                }
            }
        }
        boolean[] lvl = new boolean[64];
        int gos = 0; long lastGo = -1;
        try (Writer out = new BufferedWriter(new OutputStreamWriter(System.out), 1 << 16)) {
            for (long t = 1; t <= cycles; t++) {
                curT = t;
                StringBuilder sb = new StringBuilder().append(t);
                if (!stall.contains(t) && ep.send.ready(t)) {                // tile 0's router (a child before C&C)
                    sb.append(" T:").append(Long.toHexString(ep.send.peekA(t))).append(',').append(Long.toHexString(ep.send.peekB(t))).append(',').append(Long.toHexString(ep.send.peekC(t)));
                    ep.send.take(t);
                }
                long[] x = rx.get(t); if (x != null) ep.rx.put(t, x[0], x[1], x[2]);
                cc.runCycle(t);
                if (cc.lastGoAt == t && lastGo != t) {                        // the tiles answer GO
                    lastGo = t; gos++;
                    for (int k = 0; k < 64; k++)
                        ready.computeIfAbsent(t + cfg.goLatency + ch.startSkew[k] + delay(seed, k, gos), z -> new ArrayList<>()).add(k);
                }
                List<Integer> rs = ready.get(t);                              // the tiles' lines at the end of t (Chip.runCycle)
                boolean[] up = new boolean[64];
                if (rs != null) for (int k : rs) up[k] = true;
                List<int[]> es = err.get(t);
                boolean[] chg = new boolean[64];
                if (es != null) for (int[] e : es) if (lvl[e[0]] != (e[1] == 1)) { lvl[e[0]] = e[1] == 1; chg[e[0]] = true; }   // Chip reports changes only
                boolean any = false;
                for (int k = 0; k < 64; k++) {
                    if (up[k]) cc.readyPulse(k, t);
                    if (chg[k]) ch.cc1p.errorLine(t, k, lvl[k]);
                    any |= lvl[k];
                }
                ch.cc1p.runCycle(t);
                cc.errorLine(t, any);
                if (cc.lastGoAt == t) sb.append(" G");
                List<Integer> cs = calls.get(t); if (cs != null) for (int id : cs) sb.append(" C:").append(id);
                StateFp.Out a = new StateFp.Out(); StateFp.chipUnit(a, ch, "cnc", t);
                StateFp.Out b = new StateFp.Out(); StateFp.chipUnit(b, ch, "cc1p", t);
                sb.append(" H:").append(Long.toHexString(a.done())).append(',').append(Long.toHexString(b.done())).append('\n');
                out.write(sb.toString());
                if (t == releaseAt) for (String[] f : later) program(cc, f, calls);   // between cycles t and t + 1
            }
        }
    }

    static int hexi(String s) { return (int) Long.parseLong(s, 16); }

    static void program(CnC cc, String[] f, Map<Long, List<Integer>> calls) {
        switch (f[1]) {
            case "load" -> { int[] w = new int[f.length - 4]; for (int i = 0; i < w.length; i++) w[i] = hexi(f[4 + i]); cc.loadWords(hexi(f[2]), hexi(f[3]), w); }
            case "cmd" -> { int[] w = new int[f.length - 4]; for (int i = 0; i < w.length; i++) w[i] = hexi(f[4 + i]); cc.command(hexi(f[2]), hexi(f[3]), w); }
            case "go" -> cc.go();
            case "run" -> { int id = Integer.parseInt(f[2]); cc.go().call(() -> calls.computeIfAbsent(curT, z -> new ArrayList<>()).add(id)); }
            case "wait" -> cc.waitCycles(Integer.parseInt(f[2]));
            case "allready" -> { int id = Integer.parseInt(f[2]); cc.waitUntil(() -> cc.allReady(curT)).call(() -> calls.computeIfAbsent(curT, z -> new ArrayList<>()).add(id)); }
            case "call" -> { int id = Integer.parseInt(f[2]); cc.call(() -> calls.computeIfAbsent(curT, z -> new ArrayList<>()).add(id)); }
            default -> throw new IllegalArgumentException(f[1]);
        }
    }
}
