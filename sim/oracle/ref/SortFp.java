import java.nio.file.Path;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * WORKER24's fingerprint driver for the sort's paths (P6): SortTest's runs exactly as SortTest queues them (its own
 * Machine, boot, input-load stand-in, sequence and monitor, with SortTest's classes), plus WORKER7's between-cycle hooks
 * (FpHooks: they only read), so the C++ `sort` driver can be compared at every GO and all-READY, not only by its log.
 * M2Sort's generalisation: SortTest's run arguments (shaped, force=bt, ready as SortTest's default) and the p1 sequence.
 * Compiled into ref's default package (D5); sim/ref is not changed.
 *
 *   java -cp REF_CLASSES:THIS SortFp [seed=-1] sort  kind=<builtin> n=N | keys=FILE  [fmt=k128|kv32] [shaped]
 *                                    [ready=N] [force=bt] [label=..]  fp=FILE [fpevery=N] [fpdata=0|1]
 *   java -cp REF_CLASSES:THIS SortFp [seed=-1] p1    kind=<builtin> n=N  [fmt=k128|kv32]  fp=FILE ...
 *
 * fpevery defaults to 10^15 (only the GO and all-READY points, the boot's end and the run's end). The output: the
 * inputs line, then one line of the run's facts (TOTAL as SortTest computes it, the phases' GOs) and the verification.
 */
final class SortFp {
    static void out(String s) { System.out.println(s); }

    public static void main(String[] a) throws Exception {
        Map<String, String> kv = new LinkedHashMap<>();
        for (String s : a) { int e = s.indexOf('='); if (e > 0) kv.put(s.substring(0, e), s.substring(e + 1)); else kv.put(s, ""); }
        kv.putIfAbsent("fpevery", "1000000000000000");
        SortTest.Opts op = new SortTest.Opts();
        op.shuffle = Long.parseLong(kv.getOrDefault("seed", "-1"));
        if (kv.containsKey("ready")) op.readyLatency = Integer.parseInt(kv.get("ready"));
        op.forceBt = "bt".equals(kv.get("force"));
        int fmt = kv.getOrDefault("fmt", "k128").equals("kv32") ? SortTest.FMT_KV32 : SortTest.FMT_K128;
        boolean shaped = kv.containsKey("shaped"), p1 = kv.containsKey("p1");
        int[][] k, k2 = null; String label;
        if (kv.containsKey("keys")) { k = SortTest.readKeys(Path.of(kv.get("keys"))); label = kv.getOrDefault("label", Path.of(kv.get("keys")).getFileName().toString().replace(".keys", "")); }
        else {
            String kind = kv.getOrDefault("kind", "uniform"); int n = Integer.parseInt(kv.getOrDefault("n", "2000"));
            k = SortTest.keys(kind, n, 11 + kind.hashCode()); label = kv.getOrDefault("label", kind + " n=" + n);
            if (p1) k2 = SortTest.keys(kind, n, 77 + kind.hashCode());
        }
        String inputs = Inputs.line(Inputs.of(SortTest.build, kv.containsKey("keys") ? Path.of(kv.get("keys")) : null));
        out(inputs);

        SortTest.Machine m = new SortTest.Machine(op);
        if (!p1) m.c.phases.namer = SortTest::phaseName;
        m.boot();
        long bootCycles = m.c.cycle;
        String what = (p1 ? "p1 " : "sort ") + label + (fmt == SortTest.FMT_K128 ? " k128" : " kv32") + (shaped ? " shaped" : "")
            + (op.forceBt ? " force=bt" : "") + " ready " + m.c.cfg.readyLatency + " seed " + op.shuffle;

        // ---- the input and the sequence, as SortTest queues them
        SortTest.Oracle o = null;
        long[][] l1Expect = new long[64][], l2Expect = new long[64][];
        if (p1) {                                                     // SortTest.p1
            for (Tile p : m.c.tiles) {
                int i = p.idx; long[] rec = new long[k[i].length];
                for (int j = 0; j < k[i].length; j++) {
                    if (fmt == SortTest.FMT_K128) { p.sram.poke(SortTest.BUF_IN + j, k[i][j]); rec[j] = (k[i][j] & 0xFFFFFFFFL) | (long) j << 32; }
                    else { rec[j] = (k[i][j] & 0xFFFFFFFFL) | SortTest.value32(i, j) << 32; p.sram.poke64(SortTest.BUF_IN + 2 * j, rec[j]); }
                }
                l1Expect[i] = SortTest.stableByKey(rec);
                m.pokeS(i, SortTest.S_NIN, k[i].length);
            }
            final int[][] kk2 = k2;
            m.cc.command(LinkWord.BROADCAST, SortTest.CMD_L1 << 24 | fmt).go().command(LinkWord.BROADCAST, SortTest.CMD_L2 << 24);
            m.atAllReady(() -> {
                for (Tile p : m.c.tiles) {
                    int i = p.idx;
                    long[] rec = new long[kk2[i].length]; int[] cnt = new int[256];
                    for (int j = 0; j < rec.length; j++) { rec[j] = (kk2[i][j] & 0xFFFFFFFFL) | (long) j << 32; p.sram.poke64(SortTest.BUF_Y + 2 * j, rec[j]); cnt[kk2[i][j] & 0xFF]++; }
                    for (int t = 0; t < 256; t++) p.hist.poke(t, cnt[t]);
                    l2Expect[i] = SortTest.stableByKey(rec);
                    m.pokeS(i, SortTest.S_NRECV, rec.length);
                }
            }).go().command(LinkWord.BROADCAST, SortTest.CMD_EXIT << 24);
            m.atAllReady(() -> {}).go();
        } else {                                                      // SortTest.sortRun
            o = new SortTest.Oracle(k, fmt, shaped);
            SortTest.loadInput(m, k, fmt);
            if (m.im.oracle()) throw new IllegalStateException("SortFp: an image with the oracle split is not supported");
            List<Integer> cmd = new ArrayList<>(List.of(SortTest.CMD_L1 << 24 | fmt, SortTest.CMD_SPLIT << 24 | (shaped ? 1 : 0),
                SortTest.CMD_XB << 24, SortTest.CMD_RED << 24, SortTest.CMD_XC << 24, SortTest.CMD_TH << 24 | (op.forceBt ? 1 : 0),
                SortTest.CMD_SWEEP << 24, SortTest.CMD_DESC << 24, SortTest.CMD_K << 24));
            for (int l = 0; l < 5; l++) cmd.add(SortTest.CMD_K1 << 24 | l);
            for (int l = 0; l < 5; l++) cmd.add(SortTest.CMD_K2 << 24 | l);
            if (fmt == SortTest.FMT_K128) {
                cmd.add(SortTest.CMD_V << 24);
                for (int l = 0; l < 5; l++) cmd.add(SortTest.CMD_V1 << 24 | l);
                for (int l = 0; l < 5; l++) cmd.add(SortTest.CMD_V2 << 24 | l);
            }
            cmd.add(SortTest.CMD_L2 << 24);
            int barriers = shaped ? 65 : 66;
            m.cc.command(LinkWord.BROADCAST, cmd.get(0)).go();
            for (int i = 1; i <= cmd.size(); i++) {
                final int prev = i - 1;
                if ((cmd.get(prev) >>> 24) == SortTest.CMD_SPLIT)
                    for (int b = 0; b < barriers; b++) { m.cc.command(LinkWord.BROADCAST, SortTest.CMD_BARRIER << 24); m.atAllReady(() -> {}).go(); }
                m.cc.command(LinkWord.BROADCAST, i < cmd.size() ? cmd.get(i) : SortTest.CMD_EXIT << 24);
                m.atAllReady(() -> {}).go();
            }
        }

        // ---- the run with the hooks (SortTest.Machine.finish's condition, the hooks after the monitor)
        FpHooks h = new FpHooks(m.c, kv, List.of("SortFp " + what, inputs.replaceAll("keys=\\S*/", "keys=")));
        boolean ok = m.c.runUntil(() -> { m.monitor(); h.cycle(); return !m.cc.busy() && m.allStopped(); },
            Long.getLong("sort.maxcycles", p1 ? 30_000_000L : 400_000_000L));
        h.end();
        if (!ok) { out("FAIL: the sequence did not finish by cycle " + m.c.cycle); m.dumpState(); System.exit(1); }
        m.c.phases.close();

        // ---- the run's facts and SortTest's checks
        boolean exited = m.allExited() && m.haltSummary().isEmpty() && m.c.violations == 0;
        int cond = 0; for (Tile p : m.c.tiles) cond |= p.ctl.conditions;
        int bad = 0, sizeBad = 0, vBad = 0;
        if (p1) {
            for (Tile p : m.c.tiles) bad += SortTest.mismatches(m.c, p.idx, SortTest.BUF_Y, l2Expect[p.idx]);
        } else {
            for (Tile p : m.c.tiles) {
                int q = p.idx;
                if (m.peekS(q, SortTest.S_NRECV) != o.expect[q].length) sizeBad++;
                bad += SortTest.mismatches(m.c, q, SortTest.BUF_Y, o.expect[q]);
                if (fmt == SortTest.FMT_K128)
                    for (int r = 0; r < o.source[q].length; r++) {
                        int sp = (int) (o.source[q][r] >>> 32), sj = (int) o.source[q][r];
                        if (p.sram.peek64(SortTest.BUF_VS + 4 * r) != SortTest.valueHalf(sp, sj, 0) || p.sram.peek64(SortTest.BUF_VS + 4 * r + 2) != SortTest.valueHalf(sp, sj, 1)) vBad++;
                    }
            }
        }
        List<PhaseLog.Phase> ph = m.c.phases.phases;
        StringBuilder gos = new StringBuilder();
        for (PhaseLog.Phase p : ph) gos.append(' ').append(p.name).append('@').append(p.goAt);
        String path = p1 ? "-" : SortTest.PATHS[m.peekS(0, SortTest.S_PATH)] + (m.peekS(0, SortTest.S_PLAN) != 0 ? " (stage 2 ran)" : "");
        boolean pass = bad == 0 && sizeBad == 0 && vBad == 0 && exited && cond == 0;
        out(String.format("%s: path %s; boot %d cycles; run end at cycle %d; %d phases", what, path, bootCycles, m.c.cycle, ph.size()));
        out("GO:" + gos);
        out(String.format("verify: keys misplaced %d, receive sizes wrong %d, values wrong %d, every core exited 0 %b, violations %d, ERRORS %x: %s",
            bad, sizeBad, vBad, exited, m.c.violations, cond, pass ? "PASS" : "FAIL"));
        System.exit(pass ? 0 : 1);
    }
}
