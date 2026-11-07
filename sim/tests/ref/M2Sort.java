import java.io.FileWriter;
import java.io.PrintWriter;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * WORKER7's sort driver (sim/tests/AUDIT-M2.md): the sort exactly as SortTest.sortRun runs it (the same Machine, boot,
 * input-load stand-in, command sequence, monitor and verification, with SortTest's own classes), plus hooks that run
 * between cycles and only read: the canonical state fingerprint (StateFp), the coverage counters per sort step, and the
 * wall-clock split (setup, boot, the counted run). Compiled into ref's default package (D5); sim/ref is not changed.
 *
 *   java -cp REF_CLASSES:THIS M2Sort keys=FILE | kind=uniform n=N   [fmt=k128|kv32] [ready=8] [seed=-1]
 *        [fp=FILE] [fpevery=10000] [fpwin=A-B] [fpdata=0|1] [fpdump=CYCLE:UNIT[,UNIT..]] [dumpto=FILE]
 *        [cover=FILE] [phases=FILE] [trace=TILE,FILE]   (trace: run with -Dmeshsim.observe=on)
 *
 * fp: a fingerprint every fpevery cycles from the boot's end (cycle 0 of the sort's sequence: the boot's last cycle),
 * at the sort's GO's and all-READY's, at the end; and every cycle in [A, B] (fpwin). fpdata=0 leaves out sram.data (the
 * big SRAM's contents, the costly part) except at the interval points; in a window it is kept incrementally (the rows
 * the area queues write, read before and after each cycle) and checked against a full scan at the window's end.
 * The first output lines are the run's facts (cycles, verification) in the form the golden files record.
 */
final class M2Sort {
    static void out(String s) { System.out.println(s); }

    public static void main(String[] a) throws Exception {
        long tMain = System.nanoTime();
        long jvmStartMs = java.lang.management.ManagementFactory.getRuntimeMXBean().getStartTime();
        Map<String, String> kv = new LinkedHashMap<>();
        for (String s : a) { int e = s.indexOf('='); if (e > 0) kv.put(s.substring(0, e), s.substring(e + 1)); else kv.put(s, ""); }
        SortTest.Opts op = new SortTest.Opts();
        op.shuffle = Long.parseLong(kv.getOrDefault("seed", "-1"));
        op.readyLatency = Integer.parseInt(kv.getOrDefault("ready", "8"));
        int fmt = kv.getOrDefault("fmt", "k128").equals("kv32") ? SortTest.FMT_KV32 : SortTest.FMT_K128;
        int[][] k; String label;
        if (kv.containsKey("keys")) { k = SortTest.readKeys(Path.of(kv.get("keys"))); label = Path.of(kv.get("keys")).getFileName().toString(); }
        else { String kind = kv.getOrDefault("kind", "uniform"); int n = Integer.parseInt(kv.getOrDefault("n", "2000")); k = SortTest.keys(kind, n, 11 + kind.hashCode()); label = kind + " n=" + n; }
        String inputs = Inputs.line(Inputs.of(SortTest.build, kv.containsKey("keys") ? Path.of(kv.get("keys")) : null));
        out(inputs);
        long tKeys = System.nanoTime();
        SortTest.Oracle o = new SortTest.Oracle(k, fmt, false);
        long tOracle = System.nanoTime();

        // ---- setup: the chip, the boot (cycles and seconds of their own), the input
        SortTest.Machine m = new SortTest.Machine(op);
        m.c.phases.namer = SortTest::phaseName;
        long tChip = System.nanoTime();
        PrintWriter traceW = null;
        if (kv.containsKey("trace")) {
            String[] t = kv.get("trace").split(",", 2);
            traceW = new PrintWriter(new FileWriter(t[1]));
            Observer.start(traceW, Integer.parseInt(t[0]));
            TraceRun.header(m.c.cfg, m.c, Integer.parseInt(t[0]), "sort " + label, SortTest.build.resolve("sort.code.bin"), SortTest.build.resolve("sort.data.bin"),
                    m.im.sym("_data_start"), m.im.sym("_start"), List.of("M2Sort " + String.join(" ", a)));
        }
        m.boot();
        long bootCycles = m.c.cycle, tBoot = System.nanoTime();
        SortTest.loadInput(m, k, fmt);
        long tInput = System.nanoTime();

        // ---- the sequence, as SortTest.sortRun queues it (the image has no ORACLE: the real split; checked)
        if (m.im.oracle()) throw new IllegalStateException("M2Sort: an image with the oracle split is not supported");
        List<Integer> cmd = new ArrayList<>(List.of(SortTest.CMD_L1 << 24 | fmt, SortTest.CMD_SPLIT << 24, SortTest.CMD_XB << 24, SortTest.CMD_RED << 24,
                SortTest.CMD_XC << 24, SortTest.CMD_TH << 24, SortTest.CMD_SWEEP << 24, SortTest.CMD_DESC << 24, SortTest.CMD_K << 24));
        for (int l = 0; l < 5; l++) cmd.add(SortTest.CMD_K1 << 24 | l);
        for (int l = 0; l < 5; l++) cmd.add(SortTest.CMD_K2 << 24 | l);
        if (fmt == SortTest.FMT_K128) { cmd.add(SortTest.CMD_V << 24); for (int l = 0; l < 5; l++) cmd.add(SortTest.CMD_V1 << 24 | l); for (int l = 0; l < 5; l++) cmd.add(SortTest.CMD_V2 << 24 | l); }
        cmd.add(SortTest.CMD_L2 << 24);
        int barriers = 66;
        m.cc.command(LinkWord.BROADCAST, cmd.get(0)).go();
        for (int i = 1; i <= cmd.size(); i++) {
            final int prev = i - 1;
            if ((cmd.get(prev) >>> 24) == SortTest.CMD_SPLIT)
                for (int b = 0; b < barriers; b++) { m.cc.command(LinkWord.BROADCAST, SortTest.CMD_BARRIER << 24); m.atAllReady(() -> {}).go(); }
            m.cc.command(LinkWord.BROADCAST, i < cmd.size() ? cmd.get(i) : SortTest.CMD_EXIT << 24);
            m.atAllReady(() -> {}).go();
        }

        // ---- the hooks
        FpHooks h = new FpHooks(m.c, kv, List.of("M2Sort " + label + (fmt == SortTest.FMT_K128 ? " k128" : " kv32") + " ready " + op.readyLatency + " seed " + op.shuffle, inputs.replaceAll("keys=\\S*/", "keys=")));
        long tRun = System.nanoTime();
        boolean ok = m.c.runUntil(() -> { m.monitor(); h.cycle(); return !m.cc.busy() && m.allStopped(); }, Long.getLong("sort.maxcycles", 400_000_000L));
        long tEnd = System.nanoTime();
        h.end();
        if (traceW != null) { TraceRun.closing(m.c, m.c.tiles[Integer.parseInt(kv.get("trace").split(",")[0])].core, "run end"); Observer.stop(); }
        if (!ok) { out("FAIL: the sequence did not finish by cycle " + m.c.cycle); m.dumpState(); System.exit(1); }
        m.c.phases.close();
        if (kv.containsKey("phases")) m.c.phases.csv(Path.of(kv.get("phases")));

        // ---- verification (SortTest.sortRun's checks)
        int bad = 0, sizeBad = 0, vBad = 0;
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
        boolean exited = m.allExited() && m.haltSummary().isEmpty() && m.c.violations == 0;
        int cond = 0; for (Tile p : m.c.tiles) cond |= p.ctl.conditions;
        List<PhaseLog.Phase> ph = m.c.phases.phases;
        int last = ph.size() - 1; while (last >= 0 && !ph.get(last).name.equals("L2")) last--;
        int first = 0; while (first < last && !ph.get(first).name.equals("L1")) first++;
        long goL1 = ph.get(first).goAt, readyL2 = ph.get(last).endAt, total = readyL2 - goL1;
        boolean pass = bad == 0 && sizeBad == 0 && vBad == 0 && exited && cond == 0;
        out(String.format("sort %s %s: TOTAL %d cycles (L1 GO at %d -> L2 all READY at %d); boot %d cycles; run end at cycle %d; phases %d",
                label, fmt == SortTest.FMT_K128 ? "k128" : "kv32", total, goL1, readyL2, bootCycles, m.c.cycle, ph.size()));
        out(String.format("verify: keys misplaced %d, receive sizes wrong %d, values wrong %d, every core exited 0 %b, violations %d, ERRORS %x: %s",
                bad, sizeBad, vBad, exited, m.c.violations, cond, pass ? "PASS" : "FAIL"));
        StringBuilder steps = new StringBuilder("steps (GO -> next GO):");
        for (int i = first; i <= last; i++) steps.append(' ').append(ph.get(i).name).append(' ').append((i < last ? ph.get(i + 1).goAt : ph.get(i).endAt) - ph.get(i).goAt);
        out(steps.toString());
        out("digest " + m.c.digest());

        // ---- the wall clock (D21's machine is recorded by the task that runs this)
        long tVerify = System.nanoTime();
        double s = 1e-9;
        long counted = h.wallAt(readyL2) - h.wallAt(goL1);
        out(String.format("timing: jvm start -> main %.3f s; key file and oracle %.3f s; chip %.3f s; boot %.3f s (%d cycles); input load %.3f s; "
                + "setup total (jvm start -> run start) %.3f s",
                mainDelay(jvmStartMs, tMain), (tOracle - tMain) * s, (tChip - tOracle) * s, (tBoot - tChip) * s, bootCycles,
                (tInput - tBoot) * s, mainDelay(jvmStartMs, tMain) + (tRun - tMain) * s));
        out(String.format("timing: the run (runUntil) %.3f s for %d cycles (%.2f us/cycle); the counted span (L1 GO -> L2 all READY) %.3f s for %d cycles (%.2f us/cycle); "
                + "verification %.3f s; hooks %s",
                (tEnd - tRun) * s, m.c.cycle - bootCycles, (tEnd - tRun) / 1e3 / (m.c.cycle - bootCycles), counted * s, total, counted / 1e3 / total,
                (tVerify - tEnd) * s, h.describe()));
        System.exit(pass ? 0 : 1);
    }
    /** Seconds from the JVM's start to main (the RuntimeMXBean's start time, in ms). */
    static double mainDelay(long jvmStartMs, long tMainNanos) {
        long nowMs = System.currentTimeMillis(), nowNs = System.nanoTime();
        double mainMs = nowMs - (nowNs - tMainNanos) / 1e6;
        return (mainMs - jvmStartMs) / 1e3;
    }
}
