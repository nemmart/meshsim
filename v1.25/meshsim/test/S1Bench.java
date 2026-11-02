import java.util.Random;

/**
 * Item S1, measured (DECISIONS-v1.2 S1; spec 4.8, 4.10, invariant 24): for every counted write into big SRAM, PTR and
 * HIST, acceptance -> complete, against K, across workloads and ts_engine_latency L = 1-4 (K = max(8, kMin(L)): 8, 8, 10,
 * 12, meshsim's formula max(2L + 4, L + 4, cm_latency, rp_latency), 4.7, D49), and the
 * table-window case (a controller load of HIST right after a counting pass's done). Workloads, from C: the v1.2 recipes'
 * radix sort (rcp_bucket: count-on-arrival into HIST, prefix sums read through the table window, bucket passes through
 * PTR), the all-to-all (rcp_a2a) and gather (rcp_gather). meshsim flags a write complete later than K + 4 (the
 * controller's least reaction); the margin reported is K minus the largest latency. meshsim's big-SRAM write path is
 * 2L + 4 (W4-08), which the formula now covers (D49): every L is clean, with margin 0 at L = 2, 3 and 4.
 * Usage: java S1Bench
 */
public final class S1Bench {
    public static void main(String[] args) {
        for (int L = 1; L <= 4; L++) {
            for (String w : new String[] { "rcp_bucket", "rcp_a2a", "rcp_gather" }) {
                Config k = Mach.cfg(-1).withTsLatency(L);
                Mach m = new Mach(k);
                Mach.Image im = m.boot(w);
                Random r = new Random(L);
                if (w.equals("rcp_bucket")) {
                    for (Tile p : m.c.tiles) for (int i = 0; i < 200; i++) p.sram.poke64(2 * i, (r.nextInt() & 0xFFFFFFFFL) | (long) i << 32);
                    m.c.cc.command(LinkWord.BROADCAST, 1 << 24 | 200).go();
                } else if (w.equals("rcp_a2a")) {
                    for (Tile p : m.c.tiles) for (int i = 0; i < 512; i++) p.sram.poke64(2 * i, (long) p.idx << 32 | i);
                    m.c.cc.command(LinkWord.BROADCAST, 1 << 24).go().command(LinkWord.BROADCAST, 2 << 24).go().command(LinkWord.BROADCAST, 3 << 24).go();
                } else {
                    for (Tile p : m.c.tiles) { int n = 50 + p.idx; for (int i = 0; i < n; i++) p.sram.poke64(2 * i, (long) ((i * 7) % n) << 32 | i); }
                    m.c.cc.command(LinkWord.BROADCAST, 1 << 24 | 2).go();
                }
                m.c.cc.command(LinkWord.BROADCAST, 0x7E << 24).go();
                boolean ok = m.run(m::allStopped, 2_000_000);
                T.check(ok && m.allExited(), "L = " + L + ", " + w + ": ran to its exit: " + m.haltSummary());
                T.eq(0L, m.c.s1.late, "L = " + L + ", " + w + ": no write complete later than K + 4");
                long worst = Math.max(m.c.s1.sramMax, Math.max(m.c.s1.ptrMax, m.c.s1.histMax));
                T.check(m.c.violations == 0, "L = " + L + ", " + w + ": no violations " + m.c.violationReport());
                T.check(worst <= k.K, "L = " + L + ", " + w + ": every write complete by its decrement (K = " + k.K + ")");
                T.eq(2L * L + 4, m.c.s1.sramMax, "L = " + L + ", " + w + ": the big-SRAM write path is 2L + 4 (W4-08, D49)");
                System.out.println(String.format("L = %d, K = %d, %-11s %s [meas: S1Bench]", L, k.K, w + ":", m.c.s1.report(k.K)));
            }
        }
        System.exit(T.done("S1Bench"));
    }
}
