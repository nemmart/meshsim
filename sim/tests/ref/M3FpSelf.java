import java.util.HashMap;
import java.util.Map;

/**
 * WORKER13: a local check of StateFpM3 (sim/tests/AUDIT-M3.md 6.6) on a live Tracer, no inputs needed: TracerTest's loop
 * program on TracerRig, run twice, the second time with every Tracer unit fingerprinted after every cycle. Passes when
 * the two runs' outputs and statistics are identical (the reading is neutral), the fingerprints change as the Tracer
 * works, and a third run gives the same fingerprint sequence (deterministic). Usage: java -cp build M3FpSelf [dump]
 */
public final class M3FpSelf {
    static final long CODE = 0x80000000L, DATA = 0x80001000L;

    record Run(String out, long chain, int points, int distinct) {}

    static Run run(boolean fp, boolean dump) throws Exception {
        TracerRig.Prog p = TracerTest.loopProg();
        long[] base = new TracerRig().bases(p);
        Map<Long, Long> targets = new HashMap<>(); targets.put(p.pcOf(2), base[2]);
        TracerRig g = TracerTest.rig(p, targets, null);
        long[] r = new long[128]; r[2] = DATA;
        long[] chain = { 0x6A09E667F3BCC908L }; int[] pts = { 0 };
        java.util.HashSet<Long> seen = new java.util.HashSet<>();
        if (fp) g.onStep = () -> {
            long c = StateFp.l(g, "t");                       // onStep runs after runCycle(t), before t++
            for (String u : StateFpM3.TRACER_UNITS) {
                StateFp.Out o = dump && pts[0] == 40 ? new StateFp.Dump(new java.io.PrintWriter(System.out, true), u) : new StateFp.Out();
                StateFpM3.unit(o, g.tr, u, c);
                long h = o.done(); chain[0] = StateFp.fmix(chain[0] ^ h); seen.add(h);
            }
            pts[0]++;
        };
        g.loadRecord(CODE, (int) base[0], 0, r, new long[0]);
        Tracer.WindowOut w = g.window(2 + 5 * 5 + 1 + 6, 100000);
        StringBuilder b = new StringBuilder();
        b.append(java.util.Arrays.toString(w.record)).append(java.util.Arrays.toString(w.code)).append(java.util.Arrays.toString(w.rlog)).append(java.util.Arrays.toString(w.wlog));
        b.append(" rows ").append(g.tr.rows).append(" runCycles ").append(g.tr.runCycles).append(" drain ").append(g.tr.drainCycles)
         .append(" stalls ").append(java.util.Arrays.toString(g.tr.stalls)).append(" mem ").append(g.ms.summary()).append(" t ").append(StateFp.l(g, "t"));
        return new Run(b.toString(), chain[0], pts[0], seen.size());
    }

    public static void main(String[] a) throws Exception {
        boolean dump = a.length > 0 && a[0].equals("dump");
        Run plain = run(false, false), withFp = run(true, dump), again = run(true, false);
        boolean neutral = plain.out().equals(withFp.out()), det = withFp.chain() == again.chain();
        System.out.printf("points %d, distinct unit hashes %d, chain %016x; neutral %s, deterministic %s%n",
                withFp.points(), withFp.distinct(), withFp.chain(), neutral, det);
        if (!neutral) { System.out.println("plain:   " + plain.out()); System.out.println("with fp: " + withFp.out()); }
        System.out.println(neutral && det && withFp.distinct() > 10 ? "M3FpSelf: PASS" : "M3FpSelf: FAIL");
        System.exit(neutral && det ? 0 : 1);
    }
}
